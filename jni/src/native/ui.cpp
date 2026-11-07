// Native::UI: the screen's node tree (ui/uitree.hpp), for the classes in
// assets/scripts/lib/ui/. Deliberately flat: ids and plain values; the Ruby
// classes wrap them.
//
//   create(kind) -> id     kind: "group", "rect" or "text"
//   destroy(id)            and its children
//   add(parent, child)     remove(child)     show(id)
//   set_num(id, name, value)   x y w h visible touchable color text_color size padding
//   set_str(id, name, value)   text align
//   contains(id, x, y)     screen_width     screen_height
//   now                    seconds, on the main loop's clock
//   wake_at(t)             when Ruby next needs Native::UI.frame called (-1: never)
//
// The other way, C++ calls Native::UI.frame(now) and .touch(id, kind, x, y):
// those are Ruby, in assets/scripts/lib/ui.rb, as they drive the Ruby classes.
//
// Main thread only: there is one UiTree, and it belongs to the main loop.

#include "native/native.hpp"

#include <SDL.h>

#include "scripting.hpp"
#include "ui/uitree.hpp"

namespace
{
    UiTree* tree = nullptr;
    int screenW = 0, screenH = 0;

    UiTree::Node& NodeOrRaise(int id)
    {
        UiTree::Node* n = tree->Get(id);
        if (!n) throw mruby::RubyRuntimeError("no such UI node (destroyed?)");
        return *n;
    }

    int UiNew(std::string kind)
    {
        if (kind == "group") return tree->Create(UiTree::GROUP);
        if (kind == "rect") return tree->Create(UiTree::RECT);
        if (kind == "text") return tree->Create(UiTree::TEXT);
        throw mruby::RubyRuntimeError("unknown UI node kind: " + kind);
    }

    void UiDestroy(int id) { tree->Destroy(id); }
    void UiAdd(int parent, int child) { NodeOrRaise(parent); NodeOrRaise(child); tree->Add(parent, child); }
    void UiRemove(int child) { tree->Remove(child); }
    void UiShow(int id) { tree->Show(id); }
    bool UiContains(int id, double x, double y) { return tree->Contains(id, x, y); }
    void UiWakeAt(double t) { tree->SetWakeAt(t); }
    double UiNow() { return SDL_GetTicks() / 1000.0; }   // the clock C++ passes to Native::UI.frame
    int UiScreenWidth() { return screenW; }
    int UiScreenHeight() { return screenH; }

    // Numbers and flags. Colours are 0xRRGGBB, or -1 for none.
    void UiSetNum(int id, std::string name, double value)
    {
        UiTree::Node& n = NodeOrRaise(id);
        if      (name == "x") n.x = value;
        else if (name == "y") n.y = value;
        else if (name == "w") n.w = value;
        else if (name == "h") n.h = value;
        else if (name == "visible") n.visible = value != 0;
        else if (name == "touchable") n.touchable = value != 0;
        else if (name == "color")
        {
            n.hasColor = value >= 0;
            n.color = n.hasColor ? (Uint32)value & 0xffffff : 0;
        }
        else if (name == "text_color") n.textColor = (Uint32)value & 0xffffff;
        else if (name == "size") n.fontSize = value < 1 ? 1 : (int)value;
        else if (name == "padding") n.padding = value;
        else throw mruby::RubyRuntimeError("unknown UI property: " + name);
    }

    void UiSetStr(int id, std::string name, std::string value)
    {
        UiTree::Node& n = NodeOrRaise(id);
        if (name == "text") n.text = value;
        else if (name == "align")
        {
            if      (value == "left") n.align = UiTree::LEFT;
            else if (value == "center") n.align = UiTree::CENTER;
            else if (value == "right") n.align = UiTree::RIGHT;
            else throw mruby::RubyRuntimeError("align must be left, center or right");
        }
        else throw mruby::RubyRuntimeError("unknown UI property: " + name);
    }
}

void Native::BindUI(Scripting& scripting, UiTree& t, int screenWidth, int screenHeight)
{
    tree = &t;
    screenW = screenWidth;
    screenH = screenHeight;
    std::shared_ptr<mruby::Module> ui = Module(scripting)->create_module("UI");
    ui->bind_method("create", UiNew);
    ui->bind_method("destroy", UiDestroy);
    ui->bind_method("add", UiAdd);
    ui->bind_method("remove", UiRemove);
    ui->bind_method("show", UiShow);
    ui->bind_method("contains", UiContains);
    ui->bind_method("set_num", UiSetNum);
    ui->bind_method("set_str", UiSetStr);
    ui->bind_method("wake_at", UiWakeAt);
    ui->bind_method("now", UiNow);
    ui->bind_method("screen_width", UiScreenWidth);
    ui->bind_method("screen_height", UiScreenHeight);
}
