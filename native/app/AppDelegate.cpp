#include "AppDelegate.h"

#include <cstdio>

#include "../ui/register.h"

using namespace cocos2d;

bool AppDelegate::applicationDidFinishLaunching() {
    auto director = Director::getInstance();
    auto glview = director->getOpenGLView();
    if (!glview) {
        glview = GLViewImpl::create("MangaClash");
        director->setOpenGLView(glview);
    }
    glview->setDesignResolutionSize(640, 960, ResolutionPolicy::SHOW_ALL);
    director->setAnimationInterval(1.0 / 60);

    std::string dir = resourceDir_.empty() ? FileUtils::getInstance()->getWritablePath() + "resource" : resourceDir_;
    FileUtils::getInstance()->addSearchPath(dir);
    mc::ui::setResourceRoot(dir + "/");
    mc::ui::setFontFile(dir + "/../zhunyuan.ttf");

    host_.reset(new mc::Host());
    mc::registerCore(*host_);
    mc::registerNet(*host_);
    mc::ui::registerAll(host_->L());

    std::string err;
    if (!host_->loadBundles(dir) || !host_->loadConfig(dir + "/../config.cnf", err) || !host_->boot(err)) {
        std::printf("[boot] %s\n", err.c_str());
        return false;
    }
    director->getScheduler()->schedule([this](float dt) {
        std::string e;
        if (!host_->update(dt, e)) std::printf("[lua] %s\n", e.c_str());
    }, this, 0, false, "mc.lua.update");
    return true;
}

void AppDelegate::applicationDidEnterBackground() {
    Director::getInstance()->stopAnimation();
    Director::getInstance()->pause();
}

void AppDelegate::applicationWillEnterForeground() {
    Director::getInstance()->resume();
    Director::getInstance()->startAnimation();
}
