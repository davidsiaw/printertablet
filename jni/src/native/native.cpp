// Native: the module that holds everything Ruby can call in C++. Its
// submodules each have their own file; see native.hpp for the list.

#include "native/native.hpp"

#include "scripting.hpp"

std::shared_ptr<mruby::Module> Native::Module(Scripting& scripting)
{
    // Returns the existing module if there is one, like reopening
    // `module Native` in Ruby.
    return scripting.GetVM().create_module("Native");
}
