// GameLib.ResManager / FileVersion: resource availability + on-demand download from the game's CDN.
// Files shipped inside the app (resourceDir) count as present; everything else listed in the server's version.ios is fetched into the
// writable directory (writableDir/resource/...) when the game asks for it (UpdateFileList), like the original client did.
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <sys/stat.h>
#include <thread>

#ifdef _WIN32
#include <direct.h>
#define MC_MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define MC_MKDIR(p) mkdir(p, 0755)
#endif

#include "host.h"
#include "net.h"
#include "rlog.h"

namespace mc {
namespace {

std::mutex g_mu;
std::string g_cdn;                                        // "http://host:port/"
std::map<std::string, long> g_remote;                     // path -> size, from the server's version.ios
bool g_remoteLoaded = false;

struct Task { std::string file; int funcid; int httpId; int timeoutMs; };
std::deque<Task> g_queue;
std::condition_variable g_cv;
bool g_workers = false;
int g_nextHttpId = 1;

Host& H(lua_State* L) { return *hostOf(L); }

std::string stripRes(const std::string& rel) { return rel.compare(0, 9, "resource/") == 0 ? rel.substr(9) : rel; }

bool statFile(const std::string& p, long* size) {
    struct stat st;
    if (stat(p.c_str(), &st) != 0 || (st.st_mode & S_IFMT) == S_IFDIR) return false;
    if (size) *size = static_cast<long>(st.st_size);
    return true;
}

bool localFile(Host& h, const std::string& rel, long* size) {
    if (statFile(h.resourceDir + "/" + stripRes(rel), size)) return true;
    if (!h.writableDir.empty() && statFile(h.writableDir + "resource/" + stripRes(rel), size)) return true;
    return false;
}

void mkdirs(const std::string& file) {
    for (size_t i = 1; i < file.size(); ++i)
        if (file[i] == '/') MC_MKDIR(file.substr(0, i).c_str());
}

// ---- callbacks into Lua: RunCallBackFunc(funcid, http_id, funcid, is_abort, result, ret_code, file_name, file_len)
void callDone(Host* h, const Task& t, bool ok, int ret, long len) {
    lua_State* L = h->L();
    lua_getglobal(L, "RunCallBackFunc");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushnumber(L, t.funcid);
    lua_pushnumber(L, t.httpId);
    lua_pushnumber(L, t.funcid);
    lua_pushboolean(L, 0);
    lua_pushboolean(L, ok);
    lua_pushnumber(L, ret);
    lua_pushstring(L, t.file.c_str());
    lua_pushnumber(L, static_cast<double>(len));
    if (lua_pcall(L, 8, 0, 0) != 0) { rlog("[res] callback error: %s", lua_tostring(L, -1)); lua_pop(L, 1); }
}

void worker(Host* h) {
    for (;;) {
        Task t;
        {
            std::unique_lock<std::mutex> lk(g_mu);
            g_cv.wait(lk, [] { return !g_queue.empty(); });
            t = g_queue.front();
            g_queue.pop_front();
        }
        std::string base;
        for (int i = 0; i < 75; ++i) {                    // the CDN address arrives asynchronously; wait up to 15 s
            { std::lock_guard<std::mutex> lk(g_mu); base = g_cdn; }
            if (!base.empty()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (base.empty()) rlog("[dl] no cdn address yet, giving up %s", t.file.c_str());
        bool ok = false;
        long len = 0;
        int ret = 1;
        if (!base.empty()) {
            HttpResult r = httpRequest(base + t.file, {}, t.timeoutMs);
            if (r.code == 200 && !r.body.empty()) {
                std::string path = h->writableDir + "resource/" + stripRes(t.file);
                mkdirs(path);
                if (FILE* f = std::fopen(path.c_str(), "wb")) {
                    ok = std::fwrite(r.body.data(), 1, r.body.size(), f) == r.body.size();
                    std::fclose(f);
                    len = static_cast<long>(r.body.size());
                }
                ret = ok ? 0 : 2;
            } else {
                ret = r.code > 0 ? r.code : 3;
                rlog("[dl] %s -> %d %s", t.file.c_str(), r.code, r.error.c_str());
            }
        }
        Task done = t;
        h->post([h, done, ok, ret, len]() { callDone(h, done, ok, ret, len); });
    }
}

void ensureWorkers(Host* h) {
    if (g_workers) return;
    g_workers = true;
    for (int i = 0; i < 4; ++i) std::thread(worker, h).detach();
}

// version.ios (zip with version.xml): <res path="resource/..." s="size" ...>version</res>
void loadRemoteList(Host* h, const std::string& cdn) {
    HttpResult r = httpRequest(cdn + "version.ios", {}, 20000);
    if (r.code != 200) { rlog("[res] version.ios from %s failed: %d %s", cdn.c_str(), r.code, r.error.c_str()); return; }
    std::string tmp = h->writableDir + "version.ios";
    if (FILE* f = std::fopen(tmp.c_str(), "wb")) { std::fwrite(r.body.data(), 1, r.body.size(), f); std::fclose(f); }
    BundleFs fs;
    std::string xml;
    if (!fs.addBundle(tmp, "") || !fs.readFirst(xml)) { rlog("[res] cannot read version.ios"); return; }
    std::map<std::string, long> m;
    size_t p = 0;
    while ((p = xml.find("<res path=\"", p)) != std::string::npos) {
        p += 11;
        size_t e = xml.find('"', p);
        if (e == std::string::npos) break;
        std::string path = xml.substr(p, e - p);
        long size = 0;
        size_t tagEnd = xml.find('>', e);
        size_t s = xml.find(" s=\"", e);
        if (s != std::string::npos && tagEnd != std::string::npos && s < tagEnd) size = std::atol(xml.c_str() + s + 4);
        m[path] = size;
        p = e;
    }
    std::lock_guard<std::mutex> lk(g_mu);
    g_remote.swap(m);
    g_remoteLoaded = true;
    rlog("[res] server file list: %zu files", g_remote.size());
}

// ---- natives (':' methods: index 1 is self)
void useCdn(Host* h, std::string cdn) {
    if (cdn.compare(0, 7, "http://") != 0) cdn = "http://" + cdn;
    if (cdn.back() != '/') cdn += '/';
    {
        std::lock_guard<std::mutex> lk(g_mu);
        if (g_cdn == cdn) return;
        g_cdn = cdn;
        g_remoteLoaded = false;
    }
    rlog("[res] cdn = %s", cdn.c_str());
    g_cv.notify_all();
    loadRemoteList(h, cdn);
}

// SetPlatAndResURL(self, platUrl, cdn): cdn may be empty, then the platform server tells it: POST <plat>cdn/version.php -> {"msg":{"cdn":"host:port"}}
int setPlatAndResURL(lua_State* L) {
    std::string plat = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";
    std::string cdn = lua_isstring(L, 3) ? lua_tostring(L, 3) : "";
    if (cdn == "nil") cdn.clear();
    Host* h = &H(L);
    rlog("[res] SetPlatAndResURL plat=%s cdn=%s", plat.c_str(), cdn.c_str());
    if (!cdn.empty()) { h->spawn([h, cdn]() { useCdn(h, cdn); }); return 0; }
    if (plat.empty()) return 0;
    std::string lang = "vn";
    lua_getglobal(L, "Config");
    if (lua_istable(L, -1)) { lua_getfield(L, -1, "Language"); if (lua_isstring(L, -1)) lang = lua_tostring(L, -1); lua_pop(L, 1); }
    lua_pop(L, 1);
    h->spawn([h, plat, lang]() {
        for (int attempt = 0; attempt < 5; ++attempt) {
            HttpResult r = httpRequest(plat + "cdn/version.php", {{"lang", lang}, {"version", "2.20.141113"}}, 8000);
            size_t k = r.body.find("\"cdn\"");
            if (r.code == 200 && k != std::string::npos) {
                size_t q1 = r.body.find('"', r.body.find(':', k) + 1), q2 = q1 == std::string::npos ? q1 : r.body.find('"', q1 + 1);
                if (q2 != std::string::npos) { useCdn(h, r.body.substr(q1 + 1, q2 - q1 - 1)); return; }
            }
            rlog("[res] cdn/version.php failed (%d %s), retry", r.code, r.error.c_str());
        }
    });
    return 0;
}

int getFileVersionStatus(lua_State* L) {                 // 1 = shipped/downloaded, 3 = must be downloaded, 4 = unknown to the server
    std::string f = luaL_checkstring(L, 2);
    int status = 1;
    if (!localFile(H(L), f, nullptr)) {
        std::lock_guard<std::mutex> lk(g_mu);
        status = (!g_remoteLoaded || g_remote.count(f)) ? 3 : 4;
    }
    lua_pushnumber(L, status);
    return 1;
}

int getFileSize(lua_State* L) {
    std::string f = luaL_checkstring(L, 2);
    long size = 0;
    if (!localFile(H(L), f, &size)) {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_remote.find(f);
        if (it != g_remote.end()) size = it->second;
    }
    lua_pushnumber(L, static_cast<double>(size));
    return 1;
}

int getUpdateFileList(lua_State*) { return 0; }           // everything needed is shipped or fetched on demand

// UpdateFileList(self, list, count, funcid, timeout)  |  (self, list, funcid, timeout, httpIdTable, fileIdTable)
int updateFileList(lua_State* L) {
    Host* h = &H(L);
    bool variantB = lua_istable(L, 5);
    int funcid = static_cast<int>(luaL_checknumber(L, variantB ? 3 : 4));
    double to = lua_isnumber(L, variantB ? 4 : 5) ? lua_tonumber(L, variantB ? 4 : 5) : 15;
    int timeoutMs = static_cast<int>(to < 1000 ? to * 1000 : to);
    if (!lua_istable(L, 2)) { lua_pushboolean(L, 0); return 1; }
    ensureWorkers(h);
    int n = static_cast<int>(lua_objlen(L, 2));
    for (int i = 1; i <= n; ++i) {
        lua_rawgeti(L, 2, i);
        const char* name = lua_tostring(L, -1);
        std::string file = name ? name : "";
        lua_pop(L, 1);
        if (file.empty()) continue;
        Task t{file, funcid, 0, timeoutMs};
        { std::lock_guard<std::mutex> lk(g_mu); t.httpId = g_nextHttpId++; }
        if (variantB) {
            lua_pushnumber(L, t.httpId); lua_setfield(L, 5, file.c_str());
            if (lua_istable(L, 6)) { lua_pushnumber(L, t.httpId); lua_setfield(L, 6, file.c_str()); }
        }
        long size = 0;
        if (localFile(*h, file, &size)) {
            h->post([h, t, size]() { callDone(h, t, true, 0, size); });
        } else {
            std::lock_guard<std::mutex> lk(g_mu);
            g_queue.push_back(t);
            g_cv.notify_one();
        }
    }
    lua_pushboolean(L, 1);
    return 1;
}

int cancelUpdateFileList(lua_State*) { return 0; }

}  // namespace

void startCdn(Host* h, const std::string& cdn) {          // boot fallback: Lua may never call SetPlatAndResURL
    rlog("[res] startCdn %s", cdn.c_str());
    h->spawn([h, cdn]() { useCdn(h, cdn); });
}

void fillResManager(lua_State* L) {                       // GameLib on top of the stack
    lua_getfield(L, -1, "ResManager");
    struct R { const char* n; lua_CFunction f; };
    static const R regs[] = {
        {"SetPlatAndResURL", setPlatAndResURL}, {"GetFileVersionStatus", getFileVersionStatus}, {"GetFileSize", getFileSize},
        {"GetUpdateFileList", getUpdateFileList}, {"UpdateFileList", updateFileList}, {"CancelUpdateFileList", cancelUpdateFileList},
    };
    for (const R& r : regs) { lua_pushcfunction(L, r.f); lua_setfield(L, -2, r.n); }
    lua_pop(L, 1);

    // FileVersion.VS_* are plain numbers the Lua side compares GetFileVersionStatus() against
    lua_newtable(L);
    const char* names[] = {"VS_INVALID", "VS_INIT_NEW", "VS_DOWNLOAD_NEW", "VS_SERVER_NEW", "VS_NOT_EXIST", "VS_INIT_EX_NEW"};
    int vals[] = {0, 1, 2, 3, 4, 5};
    for (int i = 0; i < 6; ++i) { lua_pushnumber(L, vals[i]); lua_setfield(L, -2, names[i]); }
    lua_pushcfunction(L, [](lua_State* S) { lua_pushstring(S, "0"); return 1; });
    lua_setfield(L, -2, "GetRemoteSvnVersion");
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, "FileVersion");
    lua_setglobal(L, "FileVersion");
}

}  // namespace mc
