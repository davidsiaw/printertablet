// Native::Worker: Ruby scripts running on their own threads, each in its own
// VM, talking to the main VM only through message queues. Messages are JSON
// strings; the Worker class in assets/scripts/lib/worker.rb wraps all this.
//
// In the main VM (bound by BindWorkers):
//   start(script, args_json) -> id    starts scripts/<script> on a new thread
//   post(id, json)                    queues a message for the worker
//   stop(id)                          asks it to stop (see stopping?)
//   C++ calls Native::Worker.deliver(id, json) and .finished(id, error) (Ruby,
//   in lib/worker.rb) from the main loop: see DeliverWorkerMessages.
//
// In a worker's VM (bound on its thread):
//   receive(timeout) -> json or nil   next message; waits up to timeout
//                                     seconds (< 0: until one comes); nil on
//                                     timeout or when stopping
//   reply(json)                       queues a message for the main VM
//   stopping?                         the app is quitting or stop was called
//   args -> json                      what start was given
//   Workers also get Native::Http (native/http.cpp). They can't start
//   workers themselves (nothing would deliver their messages).

#include "native/native.hpp"

#include <deque>
#include <map>
#include <string>

#include <SDL.h>

#include "printlog.hpp"
#include "scripting.hpp"

namespace
{
    // Shared between the main thread and one worker thread.
    struct Channel
    {
        int id;
        std::string script, args;
        SDL_mutex* mutex;
        SDL_cond* wake;                     // signalled on a message for the worker, or stop
        std::deque<std::string> toWorker;   // guarded by mutex
        std::deque<std::string> toMain;     // guarded by mutex
        bool finished;                      // guarded by mutex: the script has ended
        std::string error;                  // guarded by mutex: why, if it raised
        SDL_atomic_t stop;
        SDL_Thread* thread;

        Channel() : id(0), mutex(SDL_CreateMutex()), wake(SDL_CreateCond()), finished(false), thread(nullptr)
        {
            SDL_AtomicSet(&stop, 0);
        }
        ~Channel()
        {
            SDL_DestroyCond(wake);
            SDL_DestroyMutex(mutex);
        }
    };

    // Main thread only.
    std::map<int, std::shared_ptr<Channel> > workers;
    int nextId = 1;

    // Bumped by worker threads when they reply or finish, so the main loop can
    // skip DeliverWorkerMessages' work (and Ruby) on frames with nothing new.
    SDL_atomic_t pending;

    // ---- worker thread ----

    Channel* ChannelOf(mrb_state* mrb)
    {
        return static_cast<Channel*>(mrb->ud);
    }

    mrb_value Receive(mrb_state* mrb, mrb_value self)
    {
        mrb_float timeout;
        mrb_get_args(mrb, "f", &timeout);
        Channel* ch = ChannelOf(mrb);

        bool got = false;
        std::string message;
        {
            Uint32 deadline = SDL_GetTicks() + (Uint32)(timeout * 1000);
            SDL_LockMutex(ch->mutex);
            while (ch->toWorker.empty() && !SDL_AtomicGet(&ch->stop))
            {
                if (timeout < 0)
                {
                    SDL_CondWait(ch->wake, ch->mutex);
                    continue;
                }
                Uint32 now = SDL_GetTicks();
                if ((Sint32)(deadline - now) <= 0) break;
                SDL_CondWaitTimeout(ch->wake, ch->mutex, deadline - now);
            }
            if (!ch->toWorker.empty() && !SDL_AtomicGet(&ch->stop))
            {
                message = ch->toWorker.front();
                ch->toWorker.pop_front();
                got = true;
            }
            SDL_UnlockMutex(ch->mutex);
        }
        return got ? mrb_str_new(mrb, message.data(), message.size()) : mrb_nil_value();
    }

    mrb_value Reply(mrb_state* mrb, mrb_value self)
    {
        char* json;
        mrb_int length;
        mrb_get_args(mrb, "s", &json, &length);
        Channel* ch = ChannelOf(mrb);
        SDL_LockMutex(ch->mutex);
        ch->toMain.push_back(std::string(json, length));
        SDL_UnlockMutex(ch->mutex);
        SDL_AtomicIncRef(&pending);
        return mrb_nil_value();
    }

    mrb_value Stopping(mrb_state* mrb, mrb_value self)
    {
        return mrb_bool_value(SDL_AtomicGet(&ChannelOf(mrb)->stop) != 0);
    }

    mrb_value Args(mrb_state* mrb, mrb_value self)
    {
        const std::string& args = ChannelOf(mrb)->args;
        return mrb_str_new(mrb, args.data(), args.size());
    }

    // Runs the worker's script, returns why it ended ("" if it just finished).
    std::string RunWorker(Channel* ch)
    {
        Scripting scripting;   // Native::Log and Native::Script, like every VM
        mrb_state* mrb = scripting.GetState();
        mrb->ud = ch;

        Native::Module(scripting)->create_module("Worker");
        RClass* worker = mrb_module_get_under(mrb, mrb_module_get(mrb, "Native"), "Worker");
        mrb_define_module_function(mrb, worker, "receive", Receive, MRB_ARGS_REQ(1));
        mrb_define_module_function(mrb, worker, "reply", Reply, MRB_ARGS_REQ(1));
        mrb_define_module_function(mrb, worker, "stopping?", Stopping, MRB_ARGS_NONE());
        mrb_define_module_function(mrb, worker, "args", Args, MRB_ARGS_NONE());
        Native::BindHttp(scripting, &ch->stop);

        std::string path = Scripting::ScriptPath(ch->script), source;
        if (!Scripting::ReadFile(path, source))
        {
            return "cannot load script " + ch->script + " (" + path + ")";
        }
        printlog("Worker %d: running %s\n", ch->id, path.c_str());
        mrb_value exc = Scripting::Load(mrb, source, ch->script);
        if (mrb_nil_p(exc))
        {
            return "";
        }
        Scripting::Report(mrb, exc);
        mrb_value message = mrb_inspect(mrb, exc);
        return mrb_string_value_cstr(mrb, &message);
    }

    int WorkerThread(void* data)
    {
        Channel* ch = static_cast<Channel*>(data);
        std::string error = RunWorker(ch);
        printlog("Worker %d (%s) ended%s%s\n", ch->id, ch->script.c_str(), error.empty() ? "" : ": ", error.c_str());
        SDL_LockMutex(ch->mutex);
        ch->finished = true;
        ch->error = error;
        SDL_UnlockMutex(ch->mutex);
        SDL_AtomicIncRef(&pending);
        return 0;
    }

    // ---- main thread ----

    std::shared_ptr<Channel> Find(int id)
    {
        std::map<int, std::shared_ptr<Channel> >::iterator it = workers.find(id);
        return it == workers.end() ? std::shared_ptr<Channel>() : it->second;
    }

    void Stop(Channel& ch)
    {
        SDL_AtomicSet(&ch.stop, 1);
        SDL_LockMutex(ch.mutex);
        SDL_CondBroadcast(ch.wake);
        SDL_UnlockMutex(ch.mutex);
    }

    int Start(std::string script, std::string args)
    {
        std::shared_ptr<Channel> ch(new Channel());
        ch->id = nextId++;
        ch->script = script;
        ch->args = args;
        workers[ch->id] = ch;
        ch->thread = SDL_CreateThread(WorkerThread, "worker", ch.get());
        if (!ch->thread)
        {
            workers.erase(ch->id);
            throw mruby::RubyRuntimeError(std::string("cannot start worker: ") + SDL_GetError());
        }
        return ch->id;
    }

    void Post(int id, std::string json)
    {
        std::shared_ptr<Channel> ch = Find(id);
        if (!ch) throw mruby::RubyRuntimeError("no such worker (it has ended)");
        SDL_LockMutex(ch->mutex);
        ch->toWorker.push_back(json);
        SDL_CondBroadcast(ch->wake);
        SDL_UnlockMutex(ch->mutex);
    }

    void StopWorker(int id)
    {
        std::shared_ptr<Channel> ch = Find(id);
        if (ch) Stop(*ch);
    }
}

void Native::BindWorkers(Scripting& scripting)
{
    std::shared_ptr<mruby::Module> worker = Module(scripting)->create_module("Worker");
    worker->bind_method("start", Start);
    worker->bind_method("post", Post);
    worker->bind_method("stop", StopWorker);
}

void Native::DeliverWorkerMessages(Scripting& scripting)
{
    if (SDL_AtomicGet(&pending) == 0) return;
    SDL_AtomicSet(&pending, 0);

    mrb_state* mrb = scripting.GetState();
    // Copy: Ruby handlers may start or stop workers while we deliver.
    std::map<int, std::shared_ptr<Channel> > current = workers;
    for (std::map<int, std::shared_ptr<Channel> >::iterator it = current.begin(); it != current.end(); ++it)
    {
        Channel& ch = *it->second;
        std::deque<std::string> messages;
        bool finished;
        std::string error;
        SDL_LockMutex(ch.mutex);
        messages.swap(ch.toMain);
        finished = ch.finished;
        error = ch.error;
        SDL_UnlockMutex(ch.mutex);

        for (size_t i = 0; i < messages.size(); i++)
        {
            int ai = mrb_gc_arena_save(mrb);
            mrb_value args[2] = { mrb_fixnum_value(ch.id), mrb_str_new(mrb, messages[i].data(), messages[i].size()) };
            scripting.Call("Native::Worker", "deliver", 2, args);
            mrb_gc_arena_restore(mrb, ai);
        }

        if (finished)
        {
            SDL_WaitThread(ch.thread, nullptr);   // it has returned already
            workers.erase(ch.id);
            int ai = mrb_gc_arena_save(mrb);
            mrb_value args[2] = { mrb_fixnum_value(ch.id),
                                  error.empty() ? mrb_nil_value() : mrb_str_new(mrb, error.data(), error.size()) };
            scripting.Call("Native::Worker", "finished", 2, args);
            mrb_gc_arena_restore(mrb, ai);
        }
    }
}

void Native::StopWorkers()
{
    for (std::map<int, std::shared_ptr<Channel> >::iterator it = workers.begin(); it != workers.end(); ++it)
    {
        Stop(*it->second);
    }
    for (std::map<int, std::shared_ptr<Channel> >::iterator it = workers.begin(); it != workers.end(); ++it)
    {
        SDL_WaitThread(it->second->thread, nullptr);   // Http notices `stop` within 200ms
    }
    workers.clear();
}
