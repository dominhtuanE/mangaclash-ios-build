#pragma once
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "bundle.h"

extern "C" {
#include "../third_party/lua/lua.h"
#include "../third_party/lua/lauxlib.h"
#include "../third_party/lua/lualib.h"
}

namespace mc {

// Owns the Lua state, the script bundles and the settings store. Platform layers (iOS/test) register natives on top.
class Host {
public:
    Host();
    ~Host();
    bool loadBundles(const std::string& resourceDir);
    bool loadConfig(const std::string& path, std::string& err);   // config.cnf: Lua file defining global Config
    bool boot(std::string& err);                    // runs main()
    bool update(double dt, std::string& err);       // Update(interval)
    lua_State* L() const { return L_; }
    BundleFs& bundles() { return fs_; }
    std::map<std::string, std::string>& settings() { return settings_; }
    std::vector<std::string>& missing() { return missing_; }
    std::string platform = "apple";
    std::string writableDir;                        // app Documents/ (downloaded resources go to writableDir/resource/...), ends with '/'
    std::string resourceDir;                        // loose game data (.cnf / .zcnf) next to the script bundles
    void post(std::function<void()> fn);            // thread-safe: runs fn on the next update() (main thread)
    void spawn(std::function<void()> fn) { threads_.emplace_back(std::move(fn)); }

private:
    lua_State* L_;
    BundleFs fs_;
    std::map<std::string, std::string> settings_;
    std::vector<std::string> missing_;
    std::mutex mu_;
    std::deque<std::function<void()>> posted_;
    std::vector<std::thread> threads_;
};

Host* hostOf(lua_State* L);
void startCdn(Host* h, const std::string& cdn);     // resmgr.cpp: set the resource server at boot
void setScreenSize(float w, float h);                // natives.cpp: logic size reported to SceneManager:GetSize
void fillResManager(lua_State* L);                 // resmgr.cpp: GameLib.ResManager + FileVersion
void registerCore(Host& h);                         // GameLib stubs/natives shared by every platform
void registerNet(Host& h);                          // MessageAdapter + HTTP bindings
void pumpNet(Host& h);                              // called every update(): drains sockets into Lua
void markMissingTable(lua_State* L, const char* name);
bool callGlobal(lua_State* L, const char* fn, int nargs, std::string& err);

}  // namespace mc
