#include "host.h"

#include <cstdio>
#include <cstring>
#include "secret.h"   // CI writes this from the GitHub secret MC_SCRIPT_PASSWORD

namespace mc {
namespace {

const char* kPassword = MC_SCRIPT_PASSWORD;
Host* g_host = nullptr;

int bundleLoader(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    Host* h = hostOf(L);
    std::string path(name), src;
    path += ".lua";
    if (!h->bundles().read(path, src)) {
        lua_pushfstring(L, "\n\tno bundle file '%s'", path.c_str());
        return 1;
    }
    if (luaL_loadbuffer(L, src.data(), src.size(), path.c_str()) != 0) return lua_error(L);
    return 1;
}

// require("ui_res/account") -> <resourceDir>/ui_res/account.cnf (plain Lua); require("language/xx/config/y.zcnf") -> first entry of that zip.
int resourceLoader(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    Host* h = hostOf(L);
    std::string n(name), src;
    bool zc = n.size() > 5 && n.compare(n.size() - 5, 5, ".zcnf") == 0;
    std::string path = h->resourceDir + "/" + n + (zc ? "" : ".cnf");
    if (!h->writableDir.empty()) { FILE* t = std::fopen(path.c_str(), "rb"); if (t) std::fclose(t); else path = h->writableDir + "resource/" + n + (zc ? "" : ".cnf"); }
    if (zc) {
        BundleFs fs;
        if (!fs.addBundle(path, "") || !fs.readFirst(src)) { lua_pushfstring(L, "%sno resource file '%s'", "\n\t", path.c_str()); return 1; }
    } else if (!readFile(path, src)) {
        lua_pushfstring(L, "%sno resource file '%s'", "\n\t", path.c_str());
        return 1;
    }
    if (luaL_loadbuffer(L, src.data(), src.size(), n.c_str()) != 0) return lua_error(L);
    return 1;
}

int missingCall(lua_State* L) {          // unimplemented native called: log once, return nil
    Host* h = hostOf(L);
    const char* id = lua_tostring(L, lua_upvalueindex(1));
    bool seen = false;
    for (auto& m : h->missing()) if (m == id) { seen = true; break; }
    if (!seen) h->missing().push_back(id);
    lua_pushnil(L);
    return 1;
}

int missingIndex(lua_State* L) {       // __index on tables whose native members are not implemented yet: yields a logging stub
    const char* tbl = lua_tostring(L, lua_upvalueindex(1));
    const char* key = lua_tostring(L, 2);
    lua_pushfstring(L, "%s.%s", tbl, key ? key : "?");
    lua_pushcclosure(L, missingCall, 1);
    return 1;
}

}  // namespace

Host* hostOf(lua_State*) { return g_host; }

Host::Host() {
    g_host = this;
    L_ = luaL_newstate();
    luaL_openlibs(L_);
    lua_getglobal(L_, "package");
    lua_getfield(L_, -1, "loaders");
    int n = static_cast<int>(lua_objlen(L_, -1));
    lua_pushcfunction(L_, bundleLoader);
    lua_rawseti(L_, -2, n + 1);
    lua_pop(L_, 2);
}

Host::~Host() {
    for (auto& t : threads_) if (t.joinable()) t.join();
    lua_close(L_);
    g_host = nullptr;
}

void Host::post(std::function<void()> fn) {
    std::lock_guard<std::mutex> lk(mu_);
    posted_.push_back(std::move(fn));
}

bool Host::loadBundles(const std::string& dir) {
    resourceDir = dir;
    for (int i = 0; i <= 10; ++i)
        if (!fs_.addBundle(dir + "/script" + std::to_string(i) + ".ios", kPassword)) return false;
    return true;
}

bool callGlobal(lua_State* L, const char* fn, int nargs, std::string& err) {
    lua_getglobal(L, fn);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1 + nargs); err = std::string("global '") + fn + "' is not a function"; return false; }
    lua_insert(L, -(nargs + 1));
    if (lua_pcall(L, nargs, 0, 0) != 0) { err = lua_tostring(L, -1); lua_pop(L, 1); return false; }
    return true;
}

bool Host::loadConfig(const std::string& path, std::string& err) {
    std::string src;
    if (!readFile(path, src)) { err = "cannot read " + path; return false; }
    if (luaL_loadbuffer(L_, src.data(), src.size(), "config.cnf") != 0 || lua_pcall(L_, 0, 0, 0) != 0) {
        err = lua_tostring(L_, -1);
        return false;
    }
    return true;
}

bool Host::boot(std::string& err) {
    std::string src;
    if (!fs_.read("main.lua", src)) { err = "main.lua missing"; return false; }
    if (luaL_loadbuffer(L_, src.data(), src.size(), "main.lua") != 0 || lua_pcall(L_, 0, 0, 0) != 0) {
        err = lua_tostring(L_, -1);
        return false;
    }
    return callGlobal(L_, "main", 0, err);
}

bool Host::update(double dt, std::string& err) {
    std::deque<std::function<void()>> todo;
    {
        std::lock_guard<std::mutex> lk(mu_);
        todo.swap(posted_);
    }
    for (auto& f : todo) f();
    pumpNet(*this);
    lua_pushnumber(L_, dt);
    return callGlobal(L_, "Update", 1, err);
}

void markMissingTable(lua_State* L, const char* name) {   // table on top of stack gets the logging __index
    lua_newtable(L);
    lua_pushstring(L, name);
    lua_pushcclosure(L, missingIndex, 1);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
}

}  // namespace mc
