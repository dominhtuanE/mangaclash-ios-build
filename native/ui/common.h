#pragma once
// Shared helpers for the UI layer.
#include <string>

#include "cocos2d.h"

extern "C" {
#include "../third_party/lua/lua.h"
#include "../third_party/lua/lauxlib.h"
}

namespace mc {
namespace ui {

extern lua_State* g_L;                                   // the game's Lua state (set by registerRes)

// Calls the Lua global RunCallBackFunc(id, ...) (see callbackmanager.lua). id <= 0 does nothing.
void runCallback(int id, int nargs = 0, double a = 0, double b = 0);
float fieldNum(lua_State* L, int idx, const char* key, float def);
std::string fieldStr(lua_State* L, int idx, const char* key);

// Resource layer (res.cpp)
std::string resolveResFile(const std::string& path);     // real file for "resource/x/y.png" (tries .sdz/.ssdz), "" when missing
cocos2d::Texture2D* textureFor(const std::string& path);
cocos2d::Texture2D* textureAs(const std::string& key, const std::string& path);   // decode path, register in the cache under key
void registerRes(lua_State* L);

struct ImageData {
    std::string path;
    float x = 0, y = 0, w = 0, h = 0, scale = 1, ox = 0, oy = 0;
};
ImageData readImageData(lua_State* L, int idx);
cocos2d::Sprite* makeSprite(const ImageData& d);
void applyImage(cocos2d::Sprite* s, const ImageData& d);

// Other layers
void registerTouch(lua_State* L);
void registerActions(lua_State* L);
void registerWidgets(lua_State* L);
void registerMisc(lua_State* L);

}  // namespace ui
}  // namespace mc
