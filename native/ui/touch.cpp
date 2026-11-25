// Touch input: nodes get SetEventListener(down, move, up) / SetTouchRect / SetLuaCallBack, ids refer to Lua callbacks (RunCallBackFunc).
#include <memory>

#include "ui/UIWidget.h"

#include "common.h"
#include "lua_node.h"
#include "register.h"
#include "../core/rlog.h"

using namespace cocos2d;

namespace mc {
namespace ui {
namespace {

struct TouchState {
    int down = 0, move = 0, up = 0;
    bool hasRect = false;
    Rect rect;
    bool enabled = true;
    bool pressed = false;
    Color3B restore = Color3B::WHITE;
    bool tint = false;                          // dim the node while it is pressed (buttons)
};

bool visibleInTree(Node* n) {
    for (; n; n = n->getParent()) if (!n->isVisible()) return false;
    return true;
}

// Plain nodes: one EventListenerTouchOneByOne per node, state lives in the listener closures and in node->userData.
TouchState* stateFor(Node* node, bool swallow) {
    if (node->getUserData()) return static_cast<TouchState*>(node->getUserData());
    auto st = std::make_shared<TouchState>();
    node->setUserData(st.get());
    auto* l = EventListenerTouchOneByOne::create();
    l->setSwallowTouches(swallow);
    l->onTouchBegan = [node, st](Touch* t, Event*) -> bool {
        if (!st->enabled || !node->isRunning() || !visibleInTree(node)) return false;
        if (!st->down && !st->move && !st->up) return false;
        Vec2 p = node->convertToNodeSpace(t->getLocation());
        Rect r = st->hasRect ? st->rect : Rect(0, 0, node->getContentSize().width, node->getContentSize().height);
        if (!r.containsPoint(p)) return false;
        st->pressed = true;
        if (st->tint) { st->restore = node->getColor(); node->setColor(Color3B(170, 170, 170)); }
        runCallback(st->down, 2, p.x, p.y);
        return true;
    };
    l->onTouchMoved = [node, st](Touch* t, Event*) {
        if (!st->pressed) return;
        Vec2 p = node->convertToNodeSpace(t->getLocation());
        runCallback(st->move, 2, p.x, p.y);
    };
    auto finish = [node, st](Touch* t, bool ok) {
        if (!st->pressed) return;
        st->pressed = false;
        if (st->tint) node->setColor(st->restore);
        Vec2 p = node->convertToNodeSpace(t->getLocation());
        if (ok) runCallback(st->up, 2, p.x, p.y);
    };
    l->onTouchEnded = [finish](Touch* t, Event*) { finish(t, true); };
    l->onTouchCancelled = [finish](Touch* t, Event*) { finish(t, false); };
    node->getEventDispatcher()->addEventListenerWithSceneGraphPriority(l, node);
    return st.get();
}

int num(lua_State* L, int i) { return lua_isnumber(L, i) ? static_cast<int>(lua_tonumber(L, i)) : 0; }

// SetEventListener(down, move, up): 0 / nil keeps the previous value (the game sets up and down separately)
int setEventListener(lua_State* L) {
    Node* n = toNode(L, 1);
    if (!n) return 0;
    int d = num(L, 2), m = num(L, 3), u = num(L, 4);
    if (auto* w = dynamic_cast<cocos2d::ui::Widget*>(n)) {
        // CocoStudio widgets handle touches themselves; keep ids in a side state and forward widget events
        TouchState* st = static_cast<TouchState*>(w->getUserData());
        if (!st) {
            st = new TouchState();                       // lives as long as the process; widgets are few and reused
            w->setUserData(st);
            w->setTouchEnabled(true);
            w->addTouchEventListener([w, st](Ref*, cocos2d::ui::Widget::TouchEventType t) {
                Vec2 p = w->getTouchBeganPosition();
                switch (t) {
                    case cocos2d::ui::Widget::TouchEventType::BEGAN: runCallback(st->down, 2, p.x, p.y); break;
                    case cocos2d::ui::Widget::TouchEventType::MOVED: runCallback(st->move, 2, w->getTouchMovePosition().x, w->getTouchMovePosition().y); break;
                    case cocos2d::ui::Widget::TouchEventType::ENDED: runCallback(st->up, 2, w->getTouchEndPosition().x, w->getTouchEndPosition().y); break;
                    default: break;
                }
            });
        }
        if (d) st->down = d;
        if (m) st->move = m;
        if (u) st->up = u;
        return 0;
    }
    TouchState* st = stateFor(n, true);
    if (d) st->down = d;
    if (m) st->move = m;
    if (u) st->up = u;
    if (dynamic_cast<Sprite*>(n)) st->tint = true;      // image buttons dim while pressed
    return 0;
}

int setTouchRect(lua_State* L) {
    Node* n = toNode(L, 1);
    if (!n) return 0;
    if (dynamic_cast<cocos2d::ui::Widget*>(n)) return 0;
    TouchState* st = stateFor(n, true);
    st->hasRect = true;
    st->rect = Rect(static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3)),
                    static_cast<float>(luaL_checknumber(L, 4)), static_cast<float>(luaL_checknumber(L, 5)));
    return 0;
}

int setTouchEnabled(lua_State* L) {
    Node* n = toNode(L, 1);
    if (!n) return 0;
    bool on = lua_toboolean(L, 2) != 0;
    if (auto* w = dynamic_cast<cocos2d::ui::Widget*>(n)) { w->setTouchEnabled(on); return 0; }
    if (n->getUserData()) static_cast<TouchState*>(n->getUserData())->enabled = on;
    return 0;
}

// per-callback setters
int setDownId(lua_State* L) { if (Node* n = toNode(L, 1)) { int v = num(L, 2); if (!dynamic_cast<cocos2d::ui::Widget*>(n)) stateFor(n, true)->down = v; } return 0; }
int setMoveId(lua_State* L) { if (Node* n = toNode(L, 1)) { int v = num(L, 2); if (!dynamic_cast<cocos2d::ui::Widget*>(n)) stateFor(n, true)->move = v; } return 0; }
int setUpId(lua_State* L) { if (Node* n = toNode(L, 1)) { int v = num(L, 2); if (!dynamic_cast<cocos2d::ui::Widget*>(n)) stateFor(n, true)->up = v; } return 0; }
int noop(lua_State*) { return 0; }

}  // namespace

void registerTouch(lua_State* L) {
    luaL_getmetatable(L, kNodeMeta);
    struct R { const char* n; lua_CFunction f; };
    static const R regs[] = {
        {"SetEventListener", setEventListener}, {"SetLuaCallBack", setEventListener}, {"SetTouchRect", setTouchRect},
        {"SetTouchEnabled", setTouchEnabled}, {"SetLuaTouchDown", setDownId}, {"SetLuaTouchMove", setMoveId},
        {"SetLuaTouchUp", setUpId}, {"SetLuaTouchEnd", setUpId}, {"SetLuaTouchLink", noop}, {"SetLuaTouchCancel", noop}, {"SetSwallow", noop}, {"SetScissorSectionByLayer", noop}, {"SetTouchPriority", noop},
    };
    for (const R& r : regs) { lua_pushcfunction(L, r.f); lua_setfield(L, -2, r.n); }
    lua_pop(L, 1);
}

}  // namespace ui
}  // namespace mc
