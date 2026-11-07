#include "scripting.hpp"

#include <cstring>

#include <SDL.h>
#include <mruby/compile.h>
#include <mruby/error.h>

#include "native/native.hpp"
#include "printlog.hpp"

namespace
{
    struct LoadJob
    {
        const char* source;
        size_t size;
        mrbc_context* context;
    };

    // Inside mrb_protect_error: no C++ objects here, as an exception may
    // longjmp out of this function.
    mrb_value DoLoad(mrb_state* mrb, void* userdata)
    {
        LoadJob* job = static_cast<LoadJob*>(userdata);
        return mrb_load_nstring_cxt(mrb, job->source, job->size, job->context);
    }
}

Scripting::Scripting()
: mrb(mrb_open()), vm(mrb)
{
    // Every VM gets these: logging (and print/puts), and load_script.
    Native::BindLog(*this);
    Native::BindScript(*this);
}

Scripting::~Scripting()
{
    vm.run("Native::Log.flush");
    mrb_close(mrb);   // vm doesn't own it
}

bool Scripting::ReadFile(const std::string& path, std::string& out)
{
    SDL_RWops* rw = SDL_RWFromFile(path.c_str(), "rb");
    if (!rw)
    {
        return false;
    }
    Sint64 size = SDL_RWsize(rw);
    out.resize(size > 0 ? (size_t)size : 0);
    size_t got = size > 0 ? SDL_RWread(rw, &out[0], 1, (size_t)size) : 0;
    SDL_RWclose(rw);
    out.resize(got);
    return true;
}

mrb_value Scripting::Load(mrb_state* mrb, const std::string& source, const std::string& name)
{
    LoadJob job = { source.data(), source.size(), mrbc_context_new(mrb) };
    mrbc_filename(mrb, job.context, name.c_str());
    job.context->capture_errors = TRUE;   // syntax errors become the exception, not stderr
    mrb_bool raised = FALSE;
    mrb_value result = mrb_protect_error(mrb, DoLoad, &job, &raised);
    mrbc_context_free(mrb, job.context);

    mrb_value exc = mrb_nil_value();
    if (raised)
    {
        exc = result;                       // raised while running a nested load
    }
    else if (mrb->exc)
    {
        exc = mrb_obj_value(mrb->exc);      // syntax error, or raised at the top
        mrb->exc = nullptr;
    }
    return exc;
}

void Scripting::Report(mrb_state* mrb, mrb_value exc)
{
    mrb_value log = mrb_obj_value(mrb_module_get_under(mrb, mrb_module_get(mrb, "Native"), "Log"));
    mrb_funcall_argv(mrb, log, mrb_intern_lit(mrb, "error"), 1, &exc);
    if (mrb->exc)
    {
        mrb->exc = nullptr;   // the logger itself failed: say something at least
        mrb_value message = mrb_inspect(mrb, exc);
        printlog("Ruby error: %s\n", mrb_string_value_cstr(mrb, &message));
    }
}

std::string Scripting::ScriptPath(const std::string& name)
{
#ifdef __ANDROID__
    // A fixed path rather than SDL_AndroidGetExternalStoragePath(): some
    // devices report an empty SD card slot as "external storage", while
    // /sdcard (where adb pushes to) is the built-in storage. Must match
    // ANDROID_SCRIPT_DIR in the Makefile.
    std::string pushed = "/sdcard/Android/data/net.astrobunny.printertablet/files/scripts/" + name;
    SDL_RWops* rw = SDL_RWFromFile(pushed.c_str(), "rb");
    if (rw)
    {
        SDL_RWclose(rw);
        return pushed;
    }
#endif
    return "scripts/" + name;
}

bool Scripting::Call(const char* module, const char* method, int argc, const mrb_value* argv)
{
    int ai = mrb_gc_arena_save(mrb);

    // Find the module, e.g. "Native::UI", one name at a time.
    RClass* receiver = mrb->object_class;
    for (const char* name = module; *name; )
    {
        const char* end = strstr(name, "::");
        size_t length = end ? (size_t)(end - name) : strlen(name);
        mrb_sym sym = mrb_intern(mrb, name, length);
        if (!mrb_const_defined(mrb, mrb_obj_value(receiver), sym))
        {
            printlog("Ruby: cannot call %s.%s: no such module\n", module, method);
            mrb_gc_arena_restore(mrb, ai);
            return false;
        }
        receiver = mrb_class_ptr(mrb_const_get(mrb, mrb_obj_value(receiver), sym));
        name = end ? end + 2 : name + length;
    }

    mrb_funcall_argv(mrb, mrb_obj_value(receiver), mrb_intern_cstr(mrb, method), argc, argv);
    bool ok = !mrb->exc;
    if (!ok)
    {
        mrb_value exc = mrb_obj_value(mrb->exc);
        mrb->exc = nullptr;
        printlog("Ruby error in %s.%s:\n", module, method);
        Report(mrb, exc);
    }
    mrb_gc_arena_restore(mrb, ai);
    return ok;
}

bool Scripting::RunSource(const std::string& source, const std::string& name)
{
    int ai = mrb_gc_arena_save(mrb);
    mrb_value exc = Load(mrb, source, name);
    bool ok = mrb_nil_p(exc);
    if (!ok) Report(mrb, exc);
    vm.run("Native::Log.flush");
    mrb_gc_arena_restore(mrb, ai);
    return ok;
}

bool Scripting::RunFile(const std::string& path)
{
    std::string source;
    if (!ReadFile(path, source))
    {
        printlog("Script %s: cannot open: %s\n", path.c_str(), SDL_GetError());
        return false;
    }
    printlog("Running script %s\n", path.c_str());
    return RunSource(source, path);
}
