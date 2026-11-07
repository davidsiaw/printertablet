#ifndef UITREE_HPP
#define UITREE_HPP

#include <map>
#include <string>
#include <vector>

#include <SDL.h>

// The screen as Ruby built it: a tree of nodes, each with a position relative
// to its parent. Ruby creates nodes and sets their properties through
// Native::UI (native/ui.cpp; the Ruby classes are in assets/scripts/lib/ui/);
// UiScene draws the tree every frame. Nothing here touches OpenGL, so the tree
// survives the GL context being replaced.
//
// Main thread only.
class UiTree
{
public:
    enum Kind { GROUP, RECT, TEXT };
    enum Align { LEFT, CENTER, RIGHT };

    struct Node
    {
        Kind kind;
        int parent;                 // 0: not attached
        std::vector<int> children;  // drawn in order, so later ones are on top
        double x, y, w, h;
        bool visible;
        bool touchable;             // has a touch handler in Ruby
        bool hasColor;              // fill (RECT) or background (TEXT)
        Uint32 color;               // 0xRRGGBB
        float alpha;                // 0..1, for everything this node draws (not its children)
        // TEXT only
        std::string text;
        Uint32 textColor;
        int fontSize;
        Align align;
        double padding;             // left/right inset of the text

        Node(Kind kind)
        : kind(kind), parent(0), x(0), y(0), w(0), h(0), visible(true), touchable(false),
          hasColor(false), color(0), alpha(1), textColor(0xffffff), fontSize(20), align(CENTER), padding(0)
        {}
    };

    UiTree();

    int Create(Kind kind);
    void Destroy(int id);               // and its children; detaches it first
    void Add(int parent, int child);    // moves child if already attached
    void Remove(int child);             // detach, keep
    void Show(int id);                  // what the screen shows; 0 for nothing

    Node* Get(int id);
    int GetRoot() const { return root; }

    // Screen position of a node's top-left corner.
    void ScreenPosition(int id, double& sx, double& sy);
    bool Contains(int id, double sx, double sy);

    // Topmost visible touchable node under the point, or 0.
    int HitTest(double sx, double sy);

    // When Ruby next needs to run (its earliest timer), in seconds; Ruby sets
    // it with Native::UI.wake_at. The main loop only calls into Ruby once this time
    // has come, so frames where nothing is due cost no Ruby at all.
    bool WakeDue(double now) const { return wakeAt >= 0 && now >= wakeAt; }
    void SetWakeAt(double t) { wakeAt = t; }

private:
    std::map<int, Node> nodes;
    int nextId;
    int root;
    double wakeAt;      // -1: nothing scheduled

    int HitTest(int id, double ox, double oy, double sx, double sy);
};

#endif // UITREE_HPP
