#include <cstdio>
#include <map>
#include <memory>

#include "host.h"
#include "net.h"
#include "rlog.h"

namespace mc {
namespace {

struct NetState {
    std::map<int, std::shared_ptr<Tcp>> conns;
    int nextId = 1;
    int nextHttp = 1;
    PacketWriter writer;
    PacketReader reader;
    std::string platUrl, gameUrl;
};
NetState g;

std::string joinUrl(const std::string& base, const std::string& page) {
    if (!base.empty() && base.back() == '/' && !page.empty() && page[0] == '/') return base + page.substr(1);
    if (!base.empty() && base.back() != '/' && !page.empty() && page[0] != '/') return base + "/" + page;
    return base + page;
}

// ---- MessageAdapter (all functions are plain '.' calls)
int maWriteBegin(lua_State*) { g.writer.begin(); return 0; }
int maWriteHeader(lua_State* L) {
    lua_pushboolean(L, g.writer.header(static_cast<uint16_t>(luaL_checknumber(L, 1)), static_cast<uint16_t>(luaL_optnumber(L, 2, 0))));
    return 1;
}
int maWriteInt(lua_State* L) { lua_pushboolean(L, g.writer.u32(static_cast<uint32_t>(static_cast<int64_t>(luaL_checknumber(L, 1))))); return 1; }
int maWriteStr(lua_State* L) {
    size_t n = static_cast<size_t>(luaL_checknumber(L, 2));
    lua_pushboolean(L, g.writer.bytes(luaL_checkstring(L, 1), n));
    return 1;
}
int maWriteFMT(lua_State* L) {
    std::string fmt = luaL_checkstring(L, 1);
    std::vector<double> nums;
    std::vector<std::string> strs;
    int top = lua_gettop(L), arg = 2;
    for (size_t i = 0; i < fmt.size();) {
        char k = fmt[i++];
        while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') ++i;
        if (arg > top) break;
        if (k == 's') { const char* s = lua_tostring(L, arg++); strs.push_back(s ? s : ""); }
        else nums.push_back(lua_isboolean(L, arg) ? lua_toboolean(L, arg++) : lua_tonumber(L, arg++));
    }
    lua_pushboolean(L, g.writer.fmt(fmt, nums, strs));
    return 1;
}
int maSend(lua_State* L) {
    auto it = g.conns.find(static_cast<int>(luaL_checknumber(L, 1)));
    lua_pushboolean(L, it != g.conns.end() && it->second->send(g.writer.frame()));
    return 1;
}
int maDisconnect(lua_State* L) {
    auto it = g.conns.find(static_cast<int>(luaL_checknumber(L, 1)));
    if (it != g.conns.end()) { it->second->close(); g.conns.erase(it); }
    return 0;
}
int maReadHeader(lua_State* L) {
    uint16_t code = 0, gs = 0;
    g.reader.header(code, gs);
    lua_pushnumber(L, code);
    lua_pushnumber(L, gs);
    return 2;
}
int maReadInt(lua_State* L) {
    uint32_t v = 0;
    g.reader.u32(v);
    lua_pushnumber(L, static_cast<int32_t>(v));
    return 1;
}
int maReadStr(lua_State* L) {
    std::string s;
    g.reader.str(static_cast<size_t>(luaL_checknumber(L, 1)), s);
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}
int maReadFMT(lua_State* L) {
    std::string fmt = luaL_checkstring(L, 1);
    int pushed = 0;
    for (size_t i = 0; i < fmt.size();) {
        char k = fmt[i++];
        size_t n = 0;
        while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9') n = n * 10 + (fmt[i++] - '0');
        if (k == 's') {
            std::string s;
            g.reader.str(n, s);
            lua_pushlstring(L, s.data(), s.size());
        } else {
            double v = 0;
            g.reader.num(k, v);
            lua_pushnumber(L, v);
        }
        ++pushed;
    }
    return pushed;
}
int maReadResult(lua_State* L) { lua_pushboolean(L, g.reader.ok()); return 1; }

int maConnect(lua_State* L) {
    std::string host = luaL_checkstring(L, 1);
    int port = static_cast<int>(luaL_checknumber(L, 2));
    int timeoutMs = static_cast<int>(luaL_optnumber(L, 3, 10)) * 1000;
    Host* h = hostOf(L);
    int handle = g.nextId++;
    auto tcp = std::make_shared<Tcp>();
    h->spawn([h, tcp, host, port, timeoutMs, handle]() {
        bool ok = tcp->connect(host, port, timeoutMs);
        h->post([h, tcp, ok, host, port, handle]() {
            int netid = handle;
            if (ok) g.conns[netid] = tcp;
            lua_State* L = h->L();
            std::string err;
            lua_pushboolean(L, ok);
            lua_pushnumber(L, handle);
            lua_pushnumber(L, netid);
            lua_pushstring(L, host.c_str());
            lua_pushnumber(L, port);
            lua_pushnumber(L, netid);
            callGlobal(L, "OnConnect", 6, err);
            if (!err.empty()) rlog("[net] OnConnect error: %s", err.c_str());
        });
    });
    lua_pushboolean(L, 1);
    lua_pushnumber(L, handle);
    return 2;
}

// ---- ResManager HTTP (methods use ':' so arg 1 is self)
void httpCall(lua_State* L, const std::string& url) {
    std::vector<std::pair<std::string, std::string>> form;
    if (lua_istable(L, 3)) {
        lua_pushnil(L);
        while (lua_next(L, 3)) {
            form.emplace_back(lua_tostring(L, -2), lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
            lua_pop(L, 1);
        }
    }
    int funcId = static_cast<int>(luaL_checknumber(L, 4));
    int timeoutMs = static_cast<int>(luaL_optnumber(L, 5, 10000));
    Host* h = hostOf(L);
    int httpId = g.nextHttp++;
    h->spawn([h, url, form, funcId, timeoutMs, httpId]() {
        HttpResult r = httpRequest(url, form, timeoutMs);
        h->post([h, url, r, funcId, httpId]() {
            lua_State* L = h->L();
            std::string err;
            bool ok = r.code == 200;
            lua_pushnumber(L, funcId);
            lua_pushnumber(L, httpId);
            lua_pushnumber(L, funcId);
            lua_pushboolean(L, 0);
            lua_pushboolean(L, ok);
            lua_pushnumber(L, ok ? 0 : (r.code > 0 ? r.code : -1));
            lua_pushstring(L, url.c_str());
            lua_pushlstring(L, r.body.data(), r.body.size());
            lua_pushnumber(L, static_cast<double>(r.body.size()));
            callGlobal(L, "RunCallBackFunc", 9, err);
            if (!err.empty()) rlog("[net] http callback error: %s", err.c_str());
        });
    });
}
int resCallPlatApi(lua_State* L) { httpCall(L, joinUrl(g.platUrl, luaL_checkstring(L, 2))); lua_pushboolean(L, 1); return 1; }
int resCallDirectApi(lua_State* L) { httpCall(L, luaL_checkstring(L, 2)); lua_pushboolean(L, 1); return 1; }
int resSetPlatAndResURL(lua_State* L) { g.platUrl = luaL_checkstring(L, 2); return 0; }
int resSetGameURL(lua_State* L) { g.gameUrl = luaL_checkstring(L, 2); return 0; }

void setFn(lua_State* L, int tbl, const char* name, lua_CFunction f) {
    lua_pushcfunction(L, f);
    lua_setfield(L, tbl, name);
}

}  // namespace

void registerNet(Host& h) {
    lua_State* L = h.L();
    lua_getglobal(L, "MessageAdapter");
    int t = lua_gettop(L);
    setFn(L, t, "WriteBegin", maWriteBegin);
    setFn(L, t, "WriteHeader", maWriteHeader);
    setFn(L, t, "WriteInt", maWriteInt);
    setFn(L, t, "WriteStr", maWriteStr);
    setFn(L, t, "WriteFMT", maWriteFMT);
    setFn(L, t, "Send", maSend);
    setFn(L, t, "Disconnect", maDisconnect);
    setFn(L, t, "ReadHeader", maReadHeader);
    setFn(L, t, "ReadInt", maReadInt);
    setFn(L, t, "ReadStr", maReadStr);
    setFn(L, t, "ReadFMT", maReadFMT);
    setFn(L, t, "ReadResult", maReadResult);
    setFn(L, t, "Connect", maConnect);
    lua_pop(L, 1);

    lua_getglobal(L, "GameLib");
    lua_getfield(L, -1, "ResManager");
    int r = lua_gettop(L);
    setFn(L, r, "CallPlatApi", resCallPlatApi);
    setFn(L, r, "CallDirectApi", resCallDirectApi);
    setFn(L, r, "SetPlatAndResURL", resSetPlatAndResURL);
    setFn(L, r, "SetGameURL", resSetGameURL);
    lua_pop(L, 2);
}

void pumpNet(Host& h) {
    std::vector<int> dead;
    for (auto& kv : g.conns) {
        std::vector<std::string> frames;
        bool closed = false;
        kv.second->poll(frames, closed);
        for (auto& f : frames) {
            g.reader.load(f);
            lua_pushnumber(h.L(), kv.first);
            std::string err;
            if (!callGlobal(h.L(), "OnRecv", 1, err)) rlog("[net] OnRecv error: %s", err.c_str());
        }
        if (closed) dead.push_back(kv.first);
    }
    for (int id : dead) {
        g.conns.erase(id);
        lua_pushnumber(h.L(), id);
        std::string err;
        if (!callGlobal(h.L(), "OnDisconnect", 1, err)) rlog("[net] OnDisconnect error: %s", err.c_str());
    }
}

}  // namespace mc
