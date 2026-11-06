#ifndef SCRIPTING_HPP
#define SCRIPTING_HPP

#include <string>
#include <mruby.hpp>

// The app's mruby VM. Scripts are plain Ruby source, compiled at runtime.
class Scripting
{
public:
    Scripting();
    ~Scripting();

    // Runs a script file. `path` is relative to the assets folder (bundled in
    // the APK on Android), or absolute. Returns false and logs the Ruby error
    // (with backtrace) if the script raised.
    bool RunFile(const std::string& path);

    // Where main.rb is loaded from: on Android,
    // /sdcard/Android/data/<package>/files/main.rb if that file exists (so
    // `make push-script` can replace it without a rebuild), otherwise
    // scripts/main.rb from the assets.
    static std::string MainScriptPath();

    mruby::VM& GetVM() { return vm; }

private:
    mruby::VM vm;

    Scripting(const Scripting&);
    Scripting& operator=(const Scripting&);
};

#endif // SCRIPTING_HPP
