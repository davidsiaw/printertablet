// Native::Log: the app log (logcat on Android, stdout elsewhere).
//
//   Native::Log.line(text)    C++: one line to the log
//   Native::Log.write(text)   Ruby: buffers text, logs each complete line
//   Native::Log.flush         Ruby: logs any partial line
//   Native::Log.error(e)      Ruby: logs an exception with its backtrace
//
// Also makes print/puts/p go to the log (stdout goes nowhere on Android).

#include "native/native.hpp"

#include "printlog.hpp"
#include "scripting.hpp"

namespace
{
    void Line(std::string line)
    {
        printlog("%s\n", line.c_str());
    }

    const char* RUBY = R"RUBY(
module Native
  module Log
    @buffer = ""

    def self.write(text)
      @buffer << text
      while (i = @buffer.index("\n"))
        line(@buffer[0, i])
        @buffer = @buffer[(i + 1)..-1]
      end
    end

    def self.flush
      return if @buffer.empty?
      line(@buffer)
      @buffer = ""
    end

    def self.error(e)
      flush
      line("Ruby error: #{e.inspect}")
      (e.backtrace || []).each { |l| line("    from #{l}") }
    end
  end
end

module Kernel
  # print/puts/p all go through print.
  def print(*args)
    args.each { |a| Native::Log.write(a.to_s) }
    nil
  end
end
)RUBY";
}

void Native::BindLog(Scripting& scripting)
{
    Module(scripting)->create_module("Log")->bind_method("line", Line);
    scripting.RunSource(RUBY, "native/log.cpp");
}
