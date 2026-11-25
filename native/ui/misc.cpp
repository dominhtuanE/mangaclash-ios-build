// Smaller node classes: GraphicLabel (drawing), ProgressControl, StencilContainer, placeholders for Spine / particles, value types.
#include "common.h"
#include "lua_node.h"
#include "register.h"
#include "../core/rlog.h"

using namespace cocos2d;

namespace mc {
namespace ui {
namespace {

float numAt(lua_State* L, int i, float def) { return lua_isnumber(L, i) ? static_cast<float>(lua_tonumber(L, i)) : def; }
Color4F colorAt(lua_State* L, int i) {
    return Color4F(numAt(L, i, 0) / 255.f, numAt(L, i + 1, 0) / 255.f, numAt(L, i + 2, 0) / 255.f, numAt(L, i + 3, 255) / 255.f);
}

// ---- GraphicLabel: DrawNode
int graphicNode(lua_State* L) { pushNode(L, DrawNode::create()); return 1; }
DrawNode* dn(lua_State* L) { return dynamic_cast<DrawNode*>(toNode(L, 1)); }
float g_lineWidth = 1;
int drawSolidRect(lua_State* L) {
    if (DrawNode* d = dn(L)) {
        float x = numAt(L, 2, 0), y = numAt(L, 3, 0), w = numAt(L, 4, 0), h = numAt(L, 5, 0);
        d->drawSolidRect(Vec2(x, y), Vec2(x + w, y + h), colorAt(L, 6));
    }
    return 0;
}
int drawRect(lua_State* L) {
    if (DrawNode* d = dn(L)) {
        float x = numAt(L, 2, 0), y = numAt(L, 3, 0), w = numAt(L, 4, 0), h = numAt(L, 5, 0);
        d->drawRect(Vec2(x, y), Vec2(x + w, y + h), colorAt(L, 6));
    }
    return 0;
}
int drawLine(lua_State* L) {
    if (DrawNode* d = dn(L)) d->drawLine(Vec2(numAt(L, 2, 0), numAt(L, 3, 0)), Vec2(numAt(L, 4, 0), numAt(L, 5, 0)), colorAt(L, 6));
    return 0;
}
int setLineWidth(lua_State* L) { g_lineWidth = numAt(L, 2, 1); if (DrawNode* d = dn(L)) d->setLineWidth(g_lineWidth); return 0; }
int clearNodes(lua_State* L) { if (DrawNode* d = dn(L)) d->clear(); return 0; }

// ---- ProgressControl.CreateWithImage(bar, frame): a ProgressTimer with the frame behind it
Sprite* spriteArg(lua_State* L, int i) {
    ImageData d;
    if (lua_istable(L, i)) d = readImageData(L, i);
    else if (lua_isstring(L, i)) d.path = lua_tostring(L, i);
    else return nullptr;
    return makeSprite(d);
}
int progressCreate(lua_State* L) {
    Sprite* bar = spriteArg(L, 1);
    if (!bar) bar = Sprite::create();
    ProgressTimer* t = ProgressTimer::create(bar);
    t->setType(ProgressTimer::Type::BAR);
    t->setMidpoint(Vec2(0, 0));
    t->setBarChangeRate(Vec2(1, 0));
    t->setPercentage(0);
    Sprite* frame = spriteArg(L, 2);
    if (frame) {
        frame->setPosition(t->getContentSize() / 2);
        t->addChild(frame, -1);
    }
    pushNode(L, t);
    return 1;
}

// ---- StencilContainer
int stencilNode(lua_State* L) { ClippingNode* c = ClippingNode::create(); c->setAlphaThreshold(0.05f); pushNode(L, c); return 1; }
int setStencilMask(lua_State* L) {
    auto* c = dynamic_cast<ClippingNode*>(toNode(L, 1));
    Node* m = toNode(L, 2);
    if (c && m) { if (m->getParent()) m->removeFromParent(); c->setStencil(m); }
    return 0;
}
int setInverted(lua_State* L) { if (auto* c = dynamic_cast<ClippingNode*>(toNode(L, 1))) c->setInverted(lua_toboolean(L, 2) != 0); return 0; }
int setAlphaTest(lua_State* L) { if (auto* c = dynamic_cast<ClippingNode*>(toNode(L, 1))) c->setAlphaThreshold(numAt(L, 2, 0.05f)); return 0; }

// ---- placeholders (Spine skeletons, particles): an empty node, all methods fall back to logged no-ops
int emptyNode(lua_State* L) { Node* n = Node::create(); pushNode(L, n); return 1; }

// ---- per-frame callback: node:SetLuaUpdate(id) -> RunCallBackFunc(id, dt)
int setLuaUpdate(lua_State* L) {
    Node* n = toNode(L, 1);
    int id = lua_isnumber(L, 2) ? static_cast<int>(lua_tonumber(L, 2)) : 0;
    if (!n) return 0;
    n->unschedule("mc.luaupdate");
    if (id > 0) n->schedule([id](float dt) { runCallback(id, 1, dt); }, "mc.luaupdate");
    return 0;
}
int setScissor(lua_State* L) { (void)L; return 0; }

// ---- value types used as constructors from Lua
int newPoint(lua_State* L) { lua_createtable(L, 0, 2); lua_pushnumber(L, 0); lua_setfield(L, -2, "x"); lua_pushnumber(L, 0); lua_setfield(L, -2, "y"); return 1; }
int newRect(lua_State* L) {
    lua_createtable(L, 0, 4);
    for (const char* k : {"x", "y", "width", "height"}) { lua_pushnumber(L, 0); lua_setfield(L, -2, k); }
    return 1;
}
int newColor(lua_State* L) {
    lua_createtable(L, 0, 4);
    for (const char* k : {"r", "g", "b"}) { lua_pushnumber(L, 0); lua_setfield(L, -2, k); }
    lua_pushnumber(L, 255); lua_setfield(L, -2, "a");
    return 1;
}

}  // namespace

void registerMisc(lua_State* L) {
    auto cls = [&](const char* name, const char* fn, lua_CFunction f) {
        lua_getglobal(L, name);
        if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_newtable(L); }
        lua_pushcfunction(L, f);
        lua_setfield(L, -2, fn);
        lua_setglobal(L, name);
    };
    cls("GraphicLabel", "Node", graphicNode);
    cls("ProgressControl", "CreateWithImage", progressCreate);
    cls("StencilContainer", "Node", stencilNode);
    cls("SkeletonAnimation", "CreateWithFile", emptyNode);
    cls("SpineEffect", "CreateWithFile", emptyNode);
    cls("ParticleSprite", "CreateWithTemplate", emptyNode);
    cls("ListContainer", "Node", emptyNode);
    for (auto nf : {std::make_pair("COCOPoint", newPoint), std::make_pair("COCORect", newRect), std::make_pair("COCOColor", newColor)}) {
        lua_pushcfunction(L, nf.second);
        lua_setglobal(L, nf.first);
    }

    luaL_getmetatable(L, kNodeMeta);
    struct R { const char* n; lua_CFunction f; };
    static const R regs[] = {
        {"DrawSolidRect", drawSolidRect}, {"DrawRect", drawRect}, {"DrawLine", drawLine}, {"SetLineWidth", setLineWidth},
        {"RemoveAllNodes", clearNodes}, {"SetStencilMask", setStencilMask}, {"SetInverted", setInverted},
        {"SetAlphaTestValue", setAlphaTest}, {"SetLuaUpdate", setLuaUpdate}, {"SetScissorSection", setScissor},
    };
    for (const R& r : regs) { lua_pushcfunction(L, r.f); lua_setfield(L, -2, r.n); }
    lua_pop(L, 1);
}

}  // namespace ui
}  // namespace mc
