#include "scripting.hpp"

#include <SDL.h>

#include "printlog.hpp"

namespace
{
    // The one native function the Ruby side needs for output.
    void LogLine(std::string line)
    {
        printlog("%s\n", line.c_str());
    }

    // Runs once in every VM.
    const char* PRELUDE = R"RUBY(
module Kernel
  # print/puts/p all go through print. stdout goes nowhere on Android, so
  # collect output into lines and send each line to the app log instead.
  def print(*args)
    $__log_buffer ||= ""
    args.each { |a| $__log_buffer << a.to_s }
    while (i = $__log_buffer.index("\n"))
      __log_line($__log_buffer[0, i])
      $__log_buffer = $__log_buffer[(i + 1)..-1]
    end
    nil
  end

  def __log_flush
    if $__log_buffer && !$__log_buffer.empty?
      __log_line($__log_buffer)
      $__log_buffer = ""
    end
  end

  # Runs Ruby source as if it were the file `filename` (for error locations).
  # Logs any error with its backtrace; returns true if the script ran cleanly.
  def __run_script(source, filename)
    eval(source, nil, filename, 1)
    true
  rescue Exception => e
    __log_flush
    __log_line("Ruby error: #{e.inspect}")
    # "(unknown)" entries come from this prelude, not the script; skip them.
    (e.backtrace || []).reject { |line| line.start_with?("(unknown)") }
                       .each { |line| __log_line("    from #{line}") }
    false
  ensure
    __log_flush
  end
end
)RUBY";

    bool ReadFile(const std::string& path, std::string& out)
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
}

Scripting::Scripting()
{
    vm.bind_method("__log_line", LogLine);
    vm.run(PRELUDE);
}

Scripting::~Scripting()
{
    vm.run("__log_flush");
}

std::string Scripting::MainScriptPath()
{
#ifdef __ANDROID__
    // A fixed path rather than SDL_AndroidGetExternalStoragePath(): some
    // devices report an empty SD card slot as "external storage", while
    // /sdcard (where adb pushes to) is the built-in storage. Must match
    // ANDROID_SCRIPT_DIR in the Makefile.
    const char* pushed = "/sdcard/Android/data/net.astrobunny.printertablet/files/main.rb";
    SDL_RWops* rw = SDL_RWFromFile(pushed, "rb");
    if (rw)
    {
        SDL_RWclose(rw);
        return pushed;
    }
#endif
    return "scripts/main.rb";
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

    vm.set_global_variable("$__script_source", source);
    vm.set_global_variable("$__script_name", path);
    vm.run("$__script_ok = __run_script($__script_source, $__script_name)");
    bool ok = vm.get_global_variable<bool>("$__script_ok");
    vm.run("$__script_source = nil");
    return ok;
}
