#include <chrono>
#include <cstdio>

#include "host.h"
#include "rlog.h"

namespace mc {
namespace {

Host& H(lua_State* L) { return *hostOf(L); }

int ret0(lua_State*) { return 0; }
int retTrue(lua_State* L) { lua_pushboolean(L, 1); return 1; }
int retFalse(lua_State* L) { lua_pushboolean(L, 0); return 1; }
int retEmptyStr(lua_State* L) { lua_pushstring(L, ""); return 1; }

// ---- GameLib.ResManager (methods use ':' so arg 1 is self)
int resGetSystemType(lua_State* L) { lua_pushstring(L, H(L).platform.c_str()); return 1; }
int resGetSetting(lua_State* L) {
    auto& s = H(L).settings();
    auto it = s.find(luaL_checkstring(L, 2));
    lua_pushstring(L, it == s.end() ? "" : it->second.c_str());
    return 1;
}
int resSetSetting(lua_State* L) {
    H(L).settings()[luaL_checkstring(L, 2)] = luaL_checkstring(L, 3);
    return 0;
}

// ---- GameLib.SceneManager
int sceneGetSize(lua_State* L) {          // fills a COCOSize table {width,height} passed as arg 2
    if (lua_istable(L, 2)) {
        lua_pushnumber(L, 640); lua_setfield(L, 2, "width");
        lua_pushnumber(L, 960); lua_setfield(L, 2, "height");
    }
    return 0;
}
int sceneGetScale(lua_State* L) { lua_pushnumber(L, 1.0); return 1; }
int sceneGetRunningScene(lua_State* L) { lua_newtable(L); markMissingTable(L, "Scene"); return 1; }

int cocosSize(lua_State* L) { lua_newtable(L); lua_pushnumber(L, 0); lua_setfield(L, -2, "width"); lua_pushnumber(L, 0); lua_setfield(L, -2, "height"); return 1; }

int getCocosVersion(lua_State* L) { lua_pushnumber(L, 3.2); return 1; }
int nowMs(lua_State* L) {
    using namespace std::chrono;
    lua_pushnumber(L, duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    return 1;
}

int sufCompress(lua_State* L) { lua_pushstring(L, ".sdz"); return 1; }
int sufMusic(lua_State* L) { lua_pushstring(L, ".sdzm"); return 1; }
int sufSound(lua_State* L) { lua_pushstring(L, ".sdzs"); return 1; }
int sufSkeleton(lua_State* L) { lua_pushstring(L, ".ssdz"); return 1; }

uint32_t g_seed = 12345;
int gameRand(lua_State* L) {              // Rand(n) -> [1,n]; Rand(a,b) -> [a,b]; Rand() -> [0,1)
    g_seed = g_seed * 1664525u + 1013904223u;
    double r = (g_seed >> 8) / 16777216.0;
    int n = lua_gettop(L);
    if (n == 0) lua_pushnumber(L, r);
    else if (n == 1) lua_pushnumber(L, 1 + static_cast<int>(r * luaL_checknumber(L, 1)));
    else { double a = luaL_checknumber(L, 1), b = luaL_checknumber(L, 2); lua_pushnumber(L, a + static_cast<int>(r * (b - a + 1))); }
    return 1;
}

int cocosLog(lua_State* L) {
    const char* m = lua_tostring(L, 1);
    std::printf("[lua] %s\n", m ? m : "");
    return 0;
}

void fn(lua_State* L, const char* name, lua_CFunction f) { lua_pushcfunction(L, f); lua_setfield(L, -2, name); }

void sub(lua_State* L, const char* name, std::initializer_list<std::pair<const char*, lua_CFunction>> fns) {
    lua_newtable(L);
    for (auto& p : fns) fn(L, p.first, p.second);
    markMissingTable(L, name);
    lua_setfield(L, -2, name);
}

}  // namespace

void registerCore(Host& h) {
    lua_State* L = h.L();
    lua_newtable(L);                                     // GameLib
    sub(L, "ResManager", {{"GetSystemType", resGetSystemType}, {"GetSetting", resGetSetting}, {"SetSetting", resSetSetting},
                          {"SetPlatAndResURL", ret0}, {"SetGameURL", ret0}});
    sub(L, "SceneManager", {{"GetSize", sceneGetSize}, {"GetScaleSize", sceneGetScale}, {"GetRunningScene", sceneGetRunningScene},
                            {"SetGameFrame", ret0}, {"SetKeypadEnabled", ret0}, {"SetLuaBackKey", ret0}, {"SetLuaTouchDown", ret0}});
    sub(L, "FontManager", {{"registerFilterChar", ret0}, {"registerFont", ret0}});
    sub(L, "SoundManager", {});
    lua_getfield(L, -1, "ResHelper");
    lua_pop(L, 1);
    for (const char* t : {"SystemTools", "SdkTools", "NodeUnity", "ActionManager", "UIListViewEx", "UIPageViewEx",
                          "UIScrollViewEx", "ExtraComponet", "MessageMem"})
        sub(L, t, {});
    sub(L, "ResHelper", {{"GetCompressSuffix", sufCompress}, {"GetMusicSuffix", sufMusic}, {"GetSoundSuffix", sufSound},
                         {"GetSkeletonSuffix", sufSkeleton}});
    for (const char* cls : {
#include "gamelib_classes.inc"
         }) {
        lua_getfield(L, -1, cls);
        bool have = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (have) continue;
        lua_newtable(L);
        markMissingTable(L, cls);
        lua_setfield(L, -2, cls);
    }
    fn(L, "GetCocosVersion", getCocosVersion);
    fn(L, "Rand", gameRand);
    fn(L, "COCOLog", cocosLog);
    lua_setglobal(L, "GameLib");
    for (const char* cls : {
#include "native_classes.inc"
         }) {
        lua_getglobal(L, cls);
        bool have = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (have) continue;
        lua_newtable(L);
        markMissingTable(L, cls);
        lua_setglobal(L, cls);
    }
    lua_newtable(L);
    lua_setglobal(L, "GameConfig");
    lua_pushcfunction(L, cocosSize);
    lua_setglobal(L, "COCOSize");
}

}  // namespace mc
