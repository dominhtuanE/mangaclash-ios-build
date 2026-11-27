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
    if (!h->writableDir.empty()) {                          // a downloaded (newer) copy replaces the shipped one
        std::string dl = h->writableDir + "resource/" + n + (zc ? "" : ".cnf");
        if (FILE* t = std::fopen(dl.c_str(), "rb")) { std::fclose(t); path = dl; }
    }
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

// error handler: append the Lua stack, so a server log line says who called the failing function
int withTraceback(lua_State* L) {
    lua_getglobal(L, "debug");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return 1; }
    lua_getfield(L, -1, "traceback");
    lua_remove(L, -2);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return 1; }
    lua_pushvalue(L, 1);
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);
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
    lua_pushcfunction(L_, resourceLoader);
    lua_rawseti(L_, -2, n + 2);
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

// Settings file: repeated "<keyLength> <valueLength>\n<key><value>", replaced atomically. It lives in the app's private
// folder (it holds what the Android client keeps there too: account name and login token).
void Host::loadSettings(const std::string& path) {
    settingsPath_ = path;
    std::string data;
    if (!readFile(path, data)) return;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t eol = data.find('\n', pos);
        unsigned long k = 0, v = 0;
        if (eol == std::string::npos || std::sscanf(data.c_str() + pos, "%lu %lu", &k, &v) != 2) break;
        pos = eol + 1;
        if (k > data.size() - pos || v > data.size() - pos - k) break;
        settings_[data.substr(pos, k)] = data.substr(pos + k, v);
        pos += k + v;
    }
}

void Host::saveSettings() const {
    if (settingsPath_.empty()) return;
    std::string tmp = settingsPath_ + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    bool ok = true;
    for (const auto& s : settings_) {
        ok = ok && std::fprintf(f, "%lu %lu\n", static_cast<unsigned long>(s.first.size()), static_cast<unsigned long>(s.second.size())) > 0
             && std::fwrite(s.first.data(), 1, s.first.size(), f) == s.first.size()
             && std::fwrite(s.second.data(), 1, s.second.size(), f) == s.second.size();
    }
    ok = std::fclose(f) == 0 && ok;
    if (ok) std::rename(tmp.c_str(), settingsPath_.c_str());
    else std::remove(tmp.c_str());
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
    int handler = lua_gettop(L) - nargs;
    lua_pushcfunction(L, withTraceback);
    lua_insert(L, handler);
    bool ok = lua_pcall(L, nargs, 0, handler) == 0;
    if (!ok) { const char* m = lua_tostring(L, -1); err = m ? m : "(error without message)"; lua_pop(L, 1); }
    lua_remove(L, handler);
    return ok;
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
