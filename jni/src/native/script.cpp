// Native::Script: loading more script files.
//
//   Native::Script.load(name)   C++: runs scripts/<name> at the top level
//                               (see Scripting::ScriptPath); errors raise
//   load_script(name)           Ruby: the same, for app code

#include "native/native.hpp"

#include "scripting.hpp"

namespace
{
    // Returns the exception, or nil.
    mrb_value LoadScript(mrb_state* mrb, const std::string& name)
    {
        std::string path = Scripting::ScriptPath(name), source;
        if (!Scripting::ReadFile(path, source))
        {
            std::string message = "cannot load script " + name + " (" + path + ")";
            return mrb_exc_new(mrb, E_RUNTIME_ERROR, message.data(), message.size());
        }
        return Scripting::Load(mrb, source, name);
    }

    // Plain mruby C API rather than mruby-cpp's bind_method, because it needs
    // the mrb_state.
    mrb_value Load(mrb_state* mrb, mrb_value self)
    {
        mrb_value name;
        mrb_get_args(mrb, "S", &name);
        mrb_value exc = LoadScript(mrb, std::string(RSTRING_PTR(name), RSTRING_LEN(name)));
        // Raise only now, with no C++ objects alive in this frame.
        if (!mrb_nil_p(exc)) mrb_exc_raise(mrb, exc);
        return mrb_true_value();
    }

    const char* RUBY = R"RUBY(
module Kernel
  # Runs another script file at the top level, e.g. load_script "lib/ui.rb".
  # Errors propagate.
  def load_script(name)
    Native::Script.load(name)
  end
end
)RUBY";
}

void Native::BindScript(Scripting& scripting)
{
    Module(scripting)->create_module("Script");
    mrb_state* mrb = scripting.GetState();
    RClass* script = mrb_module_get_under(mrb, mrb_module_get(mrb, "Native"), "Script");
    mrb_define_module_function(mrb, script, "load", Load, MRB_ARGS_REQ(1));
    scripting.RunSource(RUBY, "native/script.cpp");
}
