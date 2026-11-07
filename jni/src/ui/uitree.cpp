#include "uitree.hpp"

#include <algorithm>

UiTree::UiTree() : nextId(1), root(0), wakeAt(-1)
{
}

int UiTree::Create(Kind kind)
{
    int id = nextId++;
    nodes.insert(std::make_pair(id, Node(kind)));
    return id;
}

UiTree::Node* UiTree::Get(int id)
{
    std::map<int, Node>::iterator it = nodes.find(id);
    return it == nodes.end() ? nullptr : &it->second;
}

void UiTree::Remove(int child)
{
    Node* c = Get(child);
    if (!c || !c->parent) return;
    Node* p = Get(c->parent);
    if (p)
    {
        p->children.erase(std::remove(p->children.begin(), p->children.end(), child), p->children.end());
    }
    c->parent = 0;
}

void UiTree::Add(int parent, int child)
{
    Node* p = Get(parent);
    Node* c = Get(child);
    if (!p || !c || parent == child) return;
    Remove(child);
    p->children.push_back(child);
    c->parent = parent;
}

void UiTree::Destroy(int id)
{
    Node* n = Get(id);
    if (!n) return;
    Remove(id);
    std::vector<int> children = n->children;
    for (size_t i = 0; i < children.size(); i++)
    {
        Node* c = Get(children[i]);
        if (c) c->parent = 0;   // so Destroy doesn't edit our list while we walk it
        Destroy(children[i]);
    }
    if (root == id) root = 0;
    nodes.erase(id);
}

void UiTree::Show(int id)
{
    root = Get(id) ? id : 0;
}

void UiTree::ScreenPosition(int id, double& sx, double& sy)
{
    sx = sy = 0;
    for (Node* n = Get(id); n; n = Get(n->parent))
    {
        sx += n->x;
        sy += n->y;
    }
}

bool UiTree::Contains(int id, double sx, double sy)
{
    Node* n = Get(id);
    if (!n) return false;
    double x, y;
    ScreenPosition(id, x, y);
    return sx >= x && sx < x + n->w && sy >= y && sy < y + n->h;
}

int UiTree::HitTest(double sx, double sy)
{
    return root ? HitTest(root, 0, 0, sx, sy) : 0;
}

int UiTree::HitTest(int id, double ox, double oy, double sx, double sy)
{
    Node* n = Get(id);
    if (!n || !n->visible) return 0;
    double x = ox + n->x, y = oy + n->y;
    // Children are drawn over their parent, the last one on top: check them first.
    for (size_t i = n->children.size(); i-- > 0; )
    {
        int hit = HitTest(n->children[i], x, y, sx, sy);
        if (hit) return hit;
    }
    if (n->touchable && sx >= x && sx < x + n->w && sy >= y && sy < y + n->h)
    {
        return id;
    }
    return 0;
}
