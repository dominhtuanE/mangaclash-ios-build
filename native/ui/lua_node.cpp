#include "lua_node.h"

#include <cstdio>

using namespace cocos2d;

namespace mc {
namespace ui {
namespace {

struct NodeBox { Node* node; };

NodeBox* box(lua_State* L, int idx) { return static_cast<NodeBox*>(luaL_checkudata(L, idx, kNodeMeta)); }

Node* self(lua_State* L) {
    NodeBox* b = box(L, 1);
    if (!b->node) luaL_error(L, "node already released");
    return b->node;
}

void pushPoint(lua_State* L, float x, float y) {
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, y); lua_setfield(L, -2, "y");
}

int gc(lua_State* L) {
    NodeBox* b = box(L, 1);
    if (b->node) { b->node->release(); b->node = nullptr; }
    return 0;
}

// ---- transform / state (methods use ':' so index 1 is the node)
int SetPosition(lua_State* L) { self(L)->setPosition(static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3))); return 0; }
int GetPositionX(lua_State* L) { lua_pushnumber(L, self(L)->getPositionX()); return 1; }
int GetPositionY(lua_State* L) { lua_pushnumber(L, self(L)->getPositionY()); return 1; }
int GetPosition(lua_State* L) { Vec2 p = self(L)->getPosition(); pushPoint(L, p.x, p.y); return 1; }
int SetVisible(lua_State* L) { self(L)->setVisible(lua_toboolean(L, 2) != 0); return 0; }
int IsVisible(lua_State* L) { lua_pushboolean(L, self(L)->isVisible()); return 1; }
int SetScaleFactor(lua_State* L) { self(L)->setScale(static_cast<float>(luaL_checknumber(L, 2))); return 0; }
int SetScaleXFactor(lua_State* L) { self(L)->setScaleX(static_cast<float>(luaL_checknumber(L, 2))); return 0; }
int SetScaleYFactor(lua_State* L) { self(L)->setScaleY(static_cast<float>(luaL_checknumber(L, 2))); return 0; }
int GetScaleXFactor(lua_State* L) { lua_pushnumber(L, self(L)->getScaleX()); return 1; }
int GetScaleYFactor(lua_State* L) { lua_pushnumber(L, self(L)->getScaleY()); return 1; }
int SetRotation(lua_State* L) { self(L)->setRotation(static_cast<float>(luaL_checknumber(L, 2))); return 0; }
int GetRotation(lua_State* L) { lua_pushnumber(L, self(L)->getRotation()); return 1; }
int SetZOrder(lua_State* L) { self(L)->setLocalZOrder(static_cast<int>(luaL_checknumber(L, 2))); return 0; }
int GetZOrder(lua_State* L) { lua_pushnumber(L, self(L)->getLocalZOrder()); return 1; }
int SetAnchorPoint(lua_State* L) { self(L)->setAnchorPoint(Vec2(static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3)))); return 0; }
int SetOpacity(lua_State* L) { self(L)->setOpacity(static_cast<GLubyte>(luaL_checknumber(L, 2))); return 0; }
int setCascadeOpacityEnabled(lua_State* L) { self(L)->setCascadeOpacityEnabled(lua_toboolean(L, 2) != 0); return 0; }
int setCascadeColorEnabled(lua_State* L) { self(L)->setCascadeColorEnabled(lua_toboolean(L, 2) != 0); return 0; }
int GetContentSize(lua_State* L) {
    Size s = self(L)->getContentSize();
    lua_pushnumber(L, s.width);
    lua_pushnumber(L, s.height);
    return 2;
}
int GetSize(lua_State* L) {            // table form {width,height}
    Size s = self(L)->getContentSize();
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, s.width); lua_setfield(L, -2, "width");
    lua_pushnumber(L, s.height); lua_setfield(L, -2, "height");
    return 1;
}
int IsObjectActive(lua_State* L) { lua_pushboolean(L, box(L, 1)->node != nullptr); return 1; }

// ---- hierarchy
int AddChild(lua_State* L) {
    Node* child = toNode(L, 2);
    if (child && !child->getParent()) self(L)->addChild(child, static_cast<int>(luaL_optnumber(L, 3, 0)), static_cast<int>(luaL_optnumber(L, 4, 0)));
    return 0;
}
int AddNode(lua_State* L) { return AddChild(L); }
int RemoveChild(lua_State* L) {
    Node* child = toNode(L, 2);
    if (child && child->getParent() == self(L)) self(L)->removeChild(child, lua_toboolean(L, 3) != 0);
    return 0;
}
int RemoveSelf(lua_State* L) { self(L)->removeFromParentAndCleanup(true); return 0; }
int RemoveAllChild(lua_State* L) { self(L)->removeAllChildrenWithCleanup(true); return 0; }
int DeleteMe(lua_State* L) {
    NodeBox* b = box(L, 1);
    if (b->node) {
        b->node->removeFromParentAndCleanup(true);
        b->node->release();
        b->node = nullptr;
    }
    return 0;
}

// ---- NodeUnity (static helpers, node is argument 1)
int NU_GetWorldPosition(lua_State* L) {
    Node* n = toNode(L, 1);
    if (!n) { pushPoint(L, 0, 0); return 1; }
    Vec2 p = n->convertToWorldSpace(Vec2::ZERO);
    pushPoint(L, p.x, p.y);
    return 1;
}
int NU_ConvertToNodePosition(lua_State* L) {          // (node, worldPoint) -> local point
    Node* n = toNode(L, 1);
    lua_getfield(L, 2, "x"); float x = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, 2, "y"); float y = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    Vec2 p = n ? n->convertToNodeSpace(Vec2(x, y)) : Vec2(x, y);
    pushPoint(L, p.x, p.y);
    return 1;
}
int NU_ConvertToWorldPosition(lua_State* L) {
    Node* n = toNode(L, 1);
    lua_getfield(L, 2, "x"); float x = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    lua_getfield(L, 2, "y"); float y = static_cast<float>(lua_tonumber(L, -1)); lua_pop(L, 1);
    Vec2 p = n ? n->convertToWorldSpace(Vec2(x, y)) : Vec2(x, y);
    pushPoint(L, p.x, p.y);
    return 1;
}
int NU_GetPosition(lua_State* L) { Node* n = toNode(L, 1); Vec2 p = n ? n->getPosition() : Vec2::ZERO; pushPoint(L, p.x, p.y); return 1; }
int NU_GetVisible(lua_State* L) { Node* n = toNode(L, 1); lua_pushboolean(L, n && n->isVisible()); return 1; }
int NU_SetVisible(lua_State* L) { if (Node* n = toNode(L, 1)) n->setVisible(lua_toboolean(L, 2) != 0); return 0; }
int NU_GetWidth(lua_State* L) { Node* n = toNode(L, 1); lua_pushnumber(L, n ? n->getContentSize().width : 0); return 1; }
int NU_GetHeight(lua_State* L) { Node* n = toNode(L, 1); lua_pushnumber(L, n ? n->getContentSize().height : 0); return 1; }
int NU_SetWidth(lua_State* L) { if (Node* n = toNode(L, 1)) n->setContentSize(Size(static_cast<float>(luaL_checknumber(L, 2)), n->getContentSize().height)); return 0; }
int NU_SetHeight(lua_State* L) { if (Node* n = toNode(L, 1)) n->setContentSize(Size(n->getContentSize().width, static_cast<float>(luaL_checknumber(L, 2)))); return 0; }
int NU_GetAnchorPoint(lua_State* L) { Node* n = toNode(L, 1); Vec2 a = n ? n->getAnchorPoint() : Vec2::ZERO; pushPoint(L, a.x, a.y); return 1; }
int NU_SetRotation(lua_State* L) { if (Node* n = toNode(L, 1)) n->setRotation(static_cast<float>(luaL_checknumber(L, 2))); return 0; }
int NU_GetRotation(lua_State* L) { Node* n = toNode(L, 1); lua_pushnumber(L, n ? n->getRotation() : 0); return 1; }
int NU_GetScaleFactor(lua_State* L) { Node* n = toNode(L, 1); lua_pushnumber(L, n ? n->getScale() : 1); return 1; }
int NU_GetScaleXFactor(lua_State* L) { Node* n = toNode(L, 1); lua_pushnumber(L, n ? n->getScaleX() : 1); return 1; }
int NU_GetScaleYFactor(lua_State* L) { Node* n = toNode(L, 1); lua_pushnumber(L, n ? n->getScaleY() : 1); return 1; }
int NU_SetScaleFactor(lua_State* L) { if (Node* n = toNode(L, 1)) n->setScale(static_cast<float>(luaL_checknumber(L, 2))); return 0; }
int NU_SetScaleXFactor(lua_State* L) { if (Node* n = toNode(L, 1)) n->setScaleX(static_cast<float>(luaL_checknumber(L, 2))); return 0; }
int NU_SetScaleYFactor(lua_State* L) { if (Node* n = toNode(L, 1)) n->setScaleY(static_cast<float>(luaL_checknumber(L, 2))); return 0; }
int NU_Pause(lua_State* L) { if (Node* n = toNode(L, 1)) n->pause(); return 0; }
int NU_Resume(lua_State* L) { if (Node* n = toNode(L, 1)) n->resume(); return 0; }
int NU_Retain(lua_State* L) { if (Node* n = toNode(L, 1)) n->retain(); return 0; }
int NU_Release(lua_State* L) { if (Node* n = toNode(L, 1)) n->release(); return 0; }
int NU_IsNodeEqual(lua_State* L) { lua_pushboolean(L, toNode(L, 1) == toNode(L, 2)); return 1; }
int NU_Noop(lua_State*) { return 0; }                 // SetGreyShader / SetBright*: visual-only effects, added with the shader work

// ---- GameScene / GameLayer constructors
int GS_Node(lua_State* L) { Node* n = Node::create(); n->setAnchorPoint(Vec2::ZERO); pushNode(L, n); return 1; }
int GL_Node(lua_State* L) { Node* n = Node::create(); n->setAnchorPoint(Vec2::ZERO); pushNode(L, n); return 1; }

struct Reg { const char* name; lua_CFunction fn; };

void setAll(lua_State* L, int tbl, const Reg* regs, size_t n) {
    for (size_t i = 0; i < n; ++i) { lua_pushcfunction(L, regs[i].fn); lua_setfield(L, tbl, regs[i].name); }
}

}  // namespace

void pushNode(lua_State* L, Node* node) {
    NodeBox* b = static_cast<NodeBox*>(lua_newuserdata(L, sizeof(NodeBox)));
    b->node = node;
    if (node) node->retain();
    luaL_getmetatable(L, kNodeMeta);
    lua_setmetatable(L, -2);
}

Node* toNode(lua_State* L, int idx) {
    if (lua_type(L, idx) != LUA_TUSERDATA) return nullptr;
    void* p = luaL_checkudata(L, idx, kNodeMeta);
    return static_cast<NodeBox*>(p)->node;
}

void registerNodes(lua_State* L) {
    static const Reg methods[] = {
        {"SetPosition", SetPosition}, {"GetPositionX", GetPositionX}, {"GetPositionY", GetPositionY}, {"GetPosition", GetPosition},
        {"SetVisible", SetVisible}, {"IsVisible", IsVisible}, {"SetScaleFactor", SetScaleFactor}, {"SetScaleXFactor", SetScaleXFactor},
        {"SetScaleYFactor", SetScaleYFactor}, {"GetScaleXFactor", GetScaleXFactor}, {"GetScaleYFactor", GetScaleYFactor},
        {"SetRotation", SetRotation}, {"GetRotation", GetRotation}, {"SetZOrder", SetZOrder}, {"GetZOrder", GetZOrder},
        {"SetAnchorPoint", SetAnchorPoint}, {"SetOpacity", SetOpacity}, {"setCascadeOpacityEnabled", setCascadeOpacityEnabled},
        {"setCascadeColorEnabled", setCascadeColorEnabled}, {"GetContentSize", GetContentSize}, {"GetSize", GetSize},
        {"IsObjectActive", IsObjectActive}, {"AddChild", AddChild}, {"AddNode", AddNode}, {"RemoveChild", RemoveChild},
        {"RemoveSelf", RemoveSelf}, {"RemoveAllChild", RemoveAllChild}, {"DeleteMe", DeleteMe},
    };
    luaL_newmetatable(L, kNodeMeta);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, gc);
    lua_setfield(L, -2, "__gc");
    setAll(L, lua_gettop(L), methods, sizeof(methods) / sizeof(methods[0]));
    lua_pop(L, 1);

    static const Reg nodeUnity[] = {
        {"GetWorldPosition", NU_GetWorldPosition}, {"ConvertToNodePosition", NU_ConvertToNodePosition},
        {"ConvertToWorldPosition", NU_ConvertToWorldPosition}, {"GetPosition", NU_GetPosition}, {"GetVisible", NU_GetVisible},
        {"SetVisible", NU_SetVisible}, {"GetWidth", NU_GetWidth}, {"GetHeight", NU_GetHeight}, {"SetWidth", NU_SetWidth},
        {"SetHeight", NU_SetHeight}, {"GetAnchorPoint", NU_GetAnchorPoint}, {"SetRotation", NU_SetRotation},
        {"GetRotation", NU_GetRotation}, {"GetScaleFactor", NU_GetScaleFactor}, {"GetScaleXFactor", NU_GetScaleXFactor},
        {"GetScaleYFactor", NU_GetScaleYFactor}, {"SetScaleFactor", NU_SetScaleFactor}, {"SetScaleXFactor", NU_SetScaleXFactor},
        {"SetScaleYFactor", NU_SetScaleYFactor}, {"Pause", NU_Pause}, {"Resume", NU_Resume}, {"Retain", NU_Retain},
        {"Release", NU_Release}, {"IsNodeEqual", NU_IsNodeEqual}, {"SetGreyShader", NU_Noop}, {"SetBrightShader", NU_Noop},
        {"SetBrightValue", NU_Noop},
    };
    lua_newtable(L);
    setAll(L, lua_gettop(L), nodeUnity, sizeof(nodeUnity) / sizeof(nodeUnity[0]));
    lua_setglobal(L, "NodeUnity");

    lua_newtable(L);
    lua_pushcfunction(L, GS_Node);
    lua_setfield(L, -2, "Node");
    lua_setglobal(L, "GameScene");

    lua_newtable(L);
    lua_pushcfunction(L, GL_Node);
    lua_setfield(L, -2, "Node");
    lua_pushcfunction(L, NU_Noop);
    lua_setfield(L, -2, "SetLuaCallBack");
    lua_setglobal(L, "GameLayer");
}

}  // namespace ui
}  // namespace mc
