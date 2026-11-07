#ifndef SCRIPTING_HPP
#define SCRIPTING_HPP

#include <string>
#include <mruby.hpp>

// The app's mruby VM. Scripts are plain Ruby source, compiled at runtime.
// What Ruby can call in C++ lives in native/ (see native/native.hpp); every
// VM gets Native::Log and Native::Script.
class Scripting
{
public:
    Scripting();
    ~Scripting();

    // Runs a script file. `path` is relative to the assets folder (bundled in
    // the APK on Android), or absolute. Returns false and logs the Ruby error
    // (with backtrace) if the script raised.
    bool RunFile(const std::string& path);

    // Runs Ruby source at the top level as the file `name`. Returns false and
    // logs the error if it raised.
    bool RunSource(const std::string& source, const std::string& name);

    // Where script `name` (e.g. "main.rb", "lib/ui.rb") is loaded from: on
    // Android, /sdcard/Android/data/<package>/files/scripts/<name> if that file
    // exists (so `make push-script` can replace scripts without a rebuild),
    // otherwise scripts/<name> from the assets.
    static std::string ScriptPath(const std::string& name);
    static std::string MainScriptPath() { return ScriptPath("main.rb"); }

    // Calls a method on a Ruby module, e.g. Call("Native::UI", "frame", ...)
    // from the main loop. If it raises, the error is logged and cleared, and
    // it returns false.
    bool Call(const char* module, const char* method, int argc, const mrb_value* argv);

    mruby::VM& GetVM() { return vm; }
    mrb_state* GetState() { return mrb; }

    // ---- helpers for native/ ----

    // Reads a file from the assets (or an absolute path).
    static bool ReadFile(const std::string& path, std::string& out);

    // Compiles and runs Ruby source at the top level, as the file `name`
    // (like Ruby's `load`): classes it defines are top-level classes. Safe to
    // call while Ruby is running (e.g. from a native method). Returns the
    // exception it raised, or nil; never raises.
    static mrb_value Load(mrb_state* mrb, const std::string& source, const std::string& name);

    // Logs an exception with its backtrace (via Native::Log.error).
    static void Report(mrb_state* mrb, mrb_value exc);

private:
    mrb_state* mrb;     // ours; vm only wraps it (declared first: built first)
    mruby::VM vm;

    Scripting(const Scripting&);
    Scripting& operator=(const Scripting&);
};

#endif // SCRIPTING_HPP
