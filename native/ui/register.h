#pragma once
#include <string>

extern "C" {
#include "../third_party/lua/lua.h"
}

namespace mc {
namespace ui {
void registerNodes(lua_State* L);
void registerImages(lua_State* L);
void registerText(lua_State* L);
void registerScene(lua_State* L);
void setResourceRoot(const std::string& root);
void setFontFile(const std::string& path);
// Call after mc::registerCore so these real classes replace the logging stubs.
inline void registerAll(lua_State* L) { registerNodes(L); registerImages(L); registerText(L); registerScene(L); }
}  // namespace ui
}  // namespace mc
