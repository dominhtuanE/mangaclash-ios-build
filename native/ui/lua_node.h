#pragma once
// Binds cocos2d::Node objects into Lua as userdata with a shared metatable ("mc.Node").
// Every UI class the Lua code calls (GameScene, GameLayer, ImageLabel, ...) creates a Node and pushes it through here,
// so methods like SetPosition/SetVisible/AddChild work on all of them.
#include "cocos2d.h"

extern "C" {
#include "../third_party/lua/lua.h"
#include "../third_party/lua/lauxlib.h"
}

namespace mc {
namespace ui {

constexpr const char* kNodeMeta = "mc.Node";

// Pushes a retained reference to the node. The Lua userdata releases it on garbage collection or DeleteMe().
void pushNode(lua_State* L, cocos2d::Node* node);
// Returns the node behind argument idx, or nullptr when it is not a node (or already released).
cocos2d::Node* toNode(lua_State* L, int idx);
// Registers the metatable and the node-level global tables (GameScene, GameLayer, NodeUnity, WidgetUnity).
void registerNodes(lua_State* L);

}  // namespace ui
}  // namespace mc
