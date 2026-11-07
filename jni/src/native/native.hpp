#ifndef NATIVE_HPP
#define NATIVE_HPP

#include <memory>
#include <SDL.h>
#include <mruby.hpp>

class Scripting;
class UiTree;

// Native: what Ruby can call in C++. One file per Ruby module; each file holds
// everything for its module (its C++ functions and any Ruby methods on it).
//
//   Ruby                 File                   Bound by                Which VMs
//   Native               native/native.cpp      (the module itself)     all
//   Native::Log          native/log.cpp         Scripting()             all
//   Native::Script       native/script.cpp      Scripting()             all
//   Native::UI           native/ui.cpp          game() in main.cpp      main (owns GL)
//   Native::Worker       native/worker.cpp      game() / the worker     main: start, post, stop
//                                                                       workers: receive, reply, ...
//   Native::Http         native/http.cpp        the worker thread       workers (it blocks)
namespace Native
{
    // The Native module in this VM (created on first use).
    std::shared_ptr<mruby::Module> Module(Scripting& scripting);

    // Native::Log: Log.line (C++); Log.write, .flush, .error(e) (Ruby); and
    // print/puts/p, which go to the log.
    void BindLog(Scripting& scripting);

    // Native::Script: Script.load(name); and load_script(name).
    void BindScript(Scripting& scripting);

    // Native::UI: the screen's node tree, for the classes in lib/ui/.
    void BindUI(Scripting& scripting, UiTree& tree, int screenWidth, int screenHeight);

    // Native::Worker, main VM side: start/post/stop. Workers get their side on
    // their own thread.
    void BindWorkers(Scripting& scripting);
    // Main loop, once per frame: hands workers' replies (and their ends) to
    // Ruby. Does nothing, and no Ruby, when there are none.
    void DeliverWorkerMessages(Scripting& scripting);
    // Asks every worker to stop and waits for them. Before the main VM goes.
    void StopWorkers();

    // Native::Http, for a worker VM. Requests give up within 200ms of `cancel`
    // becoming non-zero.
    void BindHttp(Scripting& scripting, SDL_atomic_t* cancel);
}

#endif // NATIVE_HPP
