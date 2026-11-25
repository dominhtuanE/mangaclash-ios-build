#pragma once
#include <memory>
#include <string>

#include "cocos2d.h"
#include "../core/host.h"

// Cocos2d-x application shell: creates the GL view, boots the Lua host and pumps Host::update every frame.
class AppDelegate : private cocos2d::Application {
public:
    AppDelegate() = default;
    bool applicationDidFinishLaunching() override;
    void applicationDidEnterBackground() override;
    void applicationWillEnterForeground() override;
    void setResourceDir(const std::string& dir) { resourceDir_ = dir; }
private:
    std::unique_ptr<mc::Host> host_;
    std::string resourceDir_;
};
