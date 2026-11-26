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
void registerTouch(lua_State* L);
void registerActions(lua_State* L);
void registerWidgets(lua_State* L);
void registerMisc(lua_State* L);
void registerSpine(lua_State* L);
void registerCompat(lua_State* L);
void registerRes(lua_State* L);
void setResourceRoot(const std::string& root);
void setDownloadRoot(const std::string& root);
void setFontFile(const std::string& path);
// Call after mc::registerCore so these real classes replace the logging stubs.
inline void registerAll(lua_State* L) {
    registerRes(L); registerNodes(L); registerImages(L); registerText(L); registerScene(L);
    registerTouch(L); registerActions(L); registerWidgets(L); registerMisc(L); registerSpine(L); registerCompat(L);
}
}  // namespace ui
}  // namespace mc
