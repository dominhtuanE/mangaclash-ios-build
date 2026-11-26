// Touch input, following the Android engine (libzxmod: GameLayer, ImageButton, DefaultTouchDelegate, UI*Ex):
//   layers   receive touches only inside SetTouchRect (or their content size), always claim them, swallow unless
//            SetSwallow(false); move/up callbacks get (x, y, inside) with inside = 1 or 0
//   images   (ImageButton) hit-test their content, report "up" only when released inside, otherwise "cancel"
//   widgets  (CocoStudio) detect touches themselves; the Lua callback ids hang beside them
//   SceneManager:SetLuaTouchDown sees every touch first and blocks it when the callback returns non-zero (guide)
// Callback ids refer to Lua functions run through RunCallBackFunc.
#include <memory>

#include "ui/UIWidget.h"

#include "common.h"
#include "layer.h"
#include "lua_node.h"
#include "register.h"
#include "../core/rlog.h"

using namespace cocos2d;
using cocos2d::ui::Widget;

namespace mc {
namespace ui {
namespace {

struct TouchState {
    int down = 0, move = 0, up = 0, cancel = 0;
    bool hasRect = false;
    Rect rect;
    bool enabled = true;
    bool pressed = false;
    Color3B restore = Color3B::WHITE;
    EventListenerTouchOneByOne* listener = nullptr;     // owned by the event dispatcher; null for widgets
};

int g_downId = 0;                                       // SceneManager:SetLuaTouchDown

// Diagnostics sent to the server log (no personal data: screen positions and node geometry only), capped per run.
bool traceTouch() {
    static int left = 150;
    if (left <= 0) return false;
    --left;
    return true;
}

// visible all the way up, and not touching a part that an enclosing layer has clipped away
bool reachable(Node* n, const Vec2& world) {
    for (; n; n = n->getParent()) {
        if (!n->isVisible()) return false;
        auto* layer = dynamic_cast<LayerNode*>(n);
        if (layer && layer->clipsOut(world)) return false;
    }
    return true;
}

bool isImage(Node* n) { return dynamic_cast<Sprite*>(n) != nullptr; }

Rect hitRect(Node* n, const TouchState& st) {
    return st.hasRect ? st.rect : Rect(0, 0, n->getContentSize().width, n->getContentSize().height);
}

bool hits(Node* n, const TouchState& st, const Vec2& local) {
    Rect r = hitRect(n, st);
    return r.size.width > 0 && r.size.height > 0 && r.containsPoint(local);
}

// Plain nodes: one EventListenerTouchOneByOne per node, state lives in the listener closures and in node->userData.
TouchState* stateForNode(Node* node) {
    if (node->getUserData()) return static_cast<TouchState*>(node->getUserData());
    auto st = std::make_shared<TouchState>();
    node->setUserData(st.get());
    auto* l = EventListenerTouchOneByOne::create();
    st->listener = l;
    l->setSwallowTouches(true);
    l->onTouchBegan = [node, st](Touch* t, Event*) -> bool {
        if (!st->enabled || !node->isRunning() || !reachable(node, t->getLocation())) return false;
        bool image = isImage(node);
        if (image && !st->down && !st->move && !st->up) return false;
        Vec2 p = node->convertToNodeSpace(t->getLocation());
        if (!hits(node, *st, p)) return false;
        st->pressed = true;
        if (image) { st->restore = node->getColor(); node->setColor(Color3B(170, 170, 170)); }    // pressed look
        if (traceTouch()) {
            Rect r = hitRect(node, *st);
            rlog("[touch]  -> %s rect=%.0f,%.0f,%.0fx%.0f swallow=%d cb=%d/%d/%d", image ? "image" : "layer",
                 r.origin.x, r.origin.y, r.size.width, r.size.height, st->listener->isSwallowTouches() ? 1 : 0, st->down, st->move, st->up);
        }
        runCallback(st->down, 2, p.x, p.y);
        return true;
    };
    l->onTouchMoved = [node, st](Touch* t, Event*) {
        if (!st->pressed) return;
        Vec2 p = node->convertToNodeSpace(t->getLocation());
        runCallback(st->move, 3, p.x, p.y, hits(node, *st, p) ? 1 : 0);
    };
    auto finish = [node, st](Touch* t, bool cancelled) {
        if (!st->pressed) return;
        st->pressed = false;
        bool image = isImage(node);
        if (image) node->setColor(st->restore);
        Vec2 p = node->convertToNodeSpace(t->getLocation());
        bool inside = hits(node, *st, p);
        if (cancelled || (image && !inside)) runCallback(st->cancel, 2, p.x, p.y);
        else runCallback(st->up, 3, p.x, p.y, inside ? 1 : 0);       // last: the callback may remove the node
    };
    l->onTouchEnded = [finish](Touch* t, Event*) { finish(t, false); };
    l->onTouchCancelled = [finish](Touch* t, Event*) { finish(t, true); };
    node->getEventDispatcher()->addEventListenerWithSceneGraphPriority(l, node);
    return st.get();
}

// CocoStudio widgets: keep the Lua callback ids beside them and forward the widget events.
TouchState* stateForWidget(Widget* w) {
    if (w->getUserData()) return static_cast<TouchState*>(w->getUserData());
    auto st = std::make_shared<TouchState>();                // owned by the listener closure, freed with the widget
    w->setUserData(st.get());
    w->setTouchEnabled(true);
    w->addTouchEventListener([w, st](Ref*, Widget::TouchEventType t) {
        auto call = [](int id, const Vec2& p) { runCallback(id, 2, p.x, p.y); };
        switch (t) {
            case Widget::TouchEventType::BEGAN:
                if (traceTouch()) rlog("[touch]  -> widget %s cb=%d/%d/%d", w->getName().c_str(), st->down, st->move, st->up);
                call(st->down, w->getTouchBeganPosition());
                break;
            case Widget::TouchEventType::MOVED: call(st->move, w->getTouchMovePosition()); break;
            case Widget::TouchEventType::ENDED: call(st->up, w->getTouchEndPosition()); break;
            case Widget::TouchEventType::CANCELED: call(st->cancel, w->getTouchEndPosition()); break;
        }
    });
    return st.get();
}

TouchState* touchState(Node* n) {
    if (auto* w = dynamic_cast<Widget*>(n)) return stateForWidget(w);
    return stateForNode(n);
}

int num(lua_State* L, int i) { return lua_isnumber(L, i) ? static_cast<int>(lua_tonumber(L, i)) : 0; }

// SetEventListener / SetLuaCallBack(down, move, up [, cancel]): 0 / nil keeps the previous value
int setEventListener(lua_State* L) {
    Node* n = toNode(L, 1);
    if (!n) return 0;
    TouchState* st = touchState(n);
    if (int d = num(L, 2)) st->down = d;
    if (int m = num(L, 3)) st->move = m;
    if (int u = num(L, 4)) st->up = u;
    if (int c = num(L, 5)) st->cancel = c;
    return 0;
}

int setTouchRect(lua_State* L) {
    Node* n = toNode(L, 1);
    if (!n || dynamic_cast<Widget*>(n)) return 0;
    TouchState* st = stateForNode(n);
    st->hasRect = true;
    st->rect = Rect(static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3)),
                    static_cast<float>(luaL_checknumber(L, 4)), static_cast<float>(luaL_checknumber(L, 5)));
    return 0;
}

int setTouchEnabled(lua_State* L) {
    Node* n = toNode(L, 1);
    if (!n) return 0;
    bool on = lua_toboolean(L, 2) != 0;
    if (auto* w = dynamic_cast<Widget*>(n)) { w->setTouchEnabled(on); return 0; }
    stateForNode(n)->enabled = on;
    return 0;
}

int setSwallow(lua_State* L) {
    Node* n = toNode(L, 1);
    if (!n) return 0;
    bool on = lua_toboolean(L, 2) != 0;
    if (auto* w = dynamic_cast<Widget*>(n)) { w->setSwallowTouches(on); return 0; }
    stateForNode(n)->listener->setSwallowTouches(on);
    return 0;
}

// per-callback setters (buttons from layout files and plain nodes alike)
int setDownId(lua_State* L) { if (Node* n = toNode(L, 1)) touchState(n)->down = num(L, 2); return 0; }
int setMoveId(lua_State* L) { if (Node* n = toNode(L, 1)) touchState(n)->move = num(L, 2); return 0; }
int setUpId(lua_State* L) { if (Node* n = toNode(L, 1)) touchState(n)->up = num(L, 2); return 0; }
int setCancelId(lua_State* L) { if (Node* n = toNode(L, 1)) touchState(n)->cancel = num(L, 2); return 0; }
int noop(lua_State*) { return 0; }

// ---- SceneManager:SetLuaTouchDown(id): runs before every other listener
int setSceneTouchDown(lua_State* L) { g_downId = num(L, 2); return 0; }

bool sceneTouchBlocks(double x, double y) {
    lua_State* L = g_L;
    if (!L || g_downId <= 0) return false;
    lua_getglobal(L, "RunCallBackFunc");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return false; }
    lua_pushnumber(L, g_downId);
    lua_pushnumber(L, x);
    lua_pushnumber(L, y);
    if (lua_pcall(L, 3, 1, 0) != 0) {
        rlog("[lua] scene touch: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    bool block = lua_isnumber(L, -1) ? lua_tonumber(L, -1) != 0 : lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return block;
}

void installSceneTouch() {
    static bool done = false;
    if (done) return;
    done = true;
    auto* l = EventListenerTouchOneByOne::create();
    l->setSwallowTouches(true);
    l->onTouchBegan = [](Touch* t, Event*) -> bool {
        Vec2 p = t->getLocation();
        bool block = sceneTouchBlocks(p.x, p.y);
        if (traceTouch()) rlog("[touch] %.0f,%.0f%s", p.x, p.y, block ? " blocked by guide" : "");
        return block;                                    // claimed (and swallowed) only when blocking
    };
    Director::getInstance()->getEventDispatcher()->addEventListenerWithFixedPriority(l, -1024);
}

}  // namespace

void registerTouch(lua_State* L) {
    installSceneTouch();
    lua_getglobal(L, "GameLib");
    lua_getfield(L, -1, "SceneManager");
    if (lua_istable(L, -1)) { lua_pushcfunction(L, setSceneTouchDown); lua_setfield(L, -2, "SetLuaTouchDown"); }
    lua_pop(L, 2);

    luaL_getmetatable(L, kNodeMeta);
    struct R { const char* n; lua_CFunction f; };
    static const R regs[] = {
        {"SetEventListener", setEventListener}, {"SetLuaCallBack", setEventListener}, {"SetTouchRect", setTouchRect},
        {"SetTouchEnabled", setTouchEnabled}, {"SetSwallow", setSwallow}, {"SetLuaTouchDown", setDownId},
        {"SetLuaTouchMove", setMoveId}, {"SetLuaTouchUp", setUpId}, {"SetLuaTouchEnd", setUpId},
        {"SetLuaTouchCancel", setCancelId}, {"SetLuaTouchLink", noop}, {"SetTouchPriority", noop},
    };
    for (const R& r : regs) { lua_pushcfunction(L, r.f); lua_setfield(L, -2, r.n); }
    lua_pop(L, 1);
}

}  // namespace ui
}  // namespace mc
