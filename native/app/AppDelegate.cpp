#include "AppDelegate.h"
#include "CrashReport.h"

#include <cstdio>

#include "../core/rlog.h"
#include "../ui/register.h"

using namespace cocos2d;

namespace {
// First uncommented  Plat = "http://host:port/"  line of config.cnf -> where remote log lines are POSTed.
std::string platUrl(const std::string& cfg) {
    size_t pos = 0;
    while (pos < cfg.size()) {
        size_t e = cfg.find('\n', pos);
        if (e == std::string::npos) e = cfg.size();
        std::string line = cfg.substr(pos, e - pos);
        pos = e + 1;
        size_t a = line.find_first_not_of(" \t\r");
        if (a == std::string::npos || line.compare(a, 4, "Plat") != 0) continue;
        size_t q1 = line.find('"', a), q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
        if (q2 != std::string::npos && line.compare(q1 + 1, 7, "http://") == 0) return line.substr(q1 + 1, q2 - q1 - 1);
    }
    return "";
}
}  // namespace

bool AppDelegate::applicationDidFinishLaunching() {
    auto director = Director::getInstance();
    auto glview = director->getOpenGLView();
    if (!glview) {
        glview = GLViewImpl::create("MangaClash");
        director->setOpenGLView(glview);
    }
    // like the Android build: fill the whole screen, the Lua side lays itself out from SceneManager:GetSize()
    // (logic height 640 on wide screens, logic width 960 on 4:3-like screens; its UI scale is min(w/960, h/640))
    Size frame = glview->getFrameSize();
    glview->setDesignResolutionSize(960, 640, frame.width / frame.height >= 1.5f ? ResolutionPolicy::FIXED_HEIGHT : ResolutionPolicy::FIXED_WIDTH);
    director->setAnimationInterval(1.0 / 60);

    std::string dir = resourceDir_.empty() ? FileUtils::getInstance()->getWritablePath() + "resource" : resourceDir_;
    // downloaded files first: they are newer than the ones shipped in the app
    FileUtils::getInstance()->addSearchPath(FileUtils::getInstance()->getWritablePath());
    FileUtils::getInstance()->addSearchPath(FileUtils::getInstance()->getWritablePath() + "resource");
    FileUtils::getInstance()->addSearchPath(dir);
    mc::ui::setDownloadRoot(FileUtils::getInstance()->getWritablePath() + "resource/");
    std::string cfg = FileUtils::getInstance()->getStringFromFile(dir + "/../config.cnf");
    std::string url = platUrl(cfg);
    mc::installCrashReport(FileUtils::getInstance()->getWritablePath());   // reports and clears the previous run
    mc::rlogSetFile(FileUtils::getInstance()->getWritablePath() + "mc.log");
    {
        auto* lbl = Label::createWithSystemFont("", "Menlo", 13);
        lbl->setAnchorPoint(Vec2(0, 1));
        lbl->setPosition(Vec2(4, director->getWinSize().height - 4));
        lbl->setTextColor(Color4B(0, 255, 0, 255));
        lbl->setDimensions(950, 0);
        director->setNotificationNode(lbl);
        director->getScheduler()->schedule([lbl](float) { lbl->setString(mc::rlogTail(30)); }, lbl, 0.5f, false, "mc.dbg.overlay");
    }
    mc::rlog("[boot] start dir=%s config=%zu bytes plat=%s", dir.c_str(), cfg.size(), url.c_str());
    if (!url.empty()) mc::rlogSetUrl(url);
    Size fs = glview->getFrameSize();
    mc::rlog("[boot] frame %.0fx%.0f", fs.width, fs.height);
    mc::ui::setResourceRoot(dir + "/");
    mc::ui::setFontFile(dir + "/../zhunyuan.ttf");

    host_.reset(new mc::Host());
    host_->writableDir = FileUtils::getInstance()->getWritablePath();
    mc::setScreenSize(director->getWinSize().width, director->getWinSize().height);
    mc::registerCore(*host_);
    mc::registerNet(*host_);
    mc::ui::registerAll(host_->L());

    std::string err;
    if (!host_->loadBundles(dir)) { mc::rlog("[boot] FAIL loadBundles (script*.ios missing or wrong password)"); return false; }
    if (!host_->loadConfig(dir + "/../config.cnf", err)) { mc::rlog("[boot] FAIL config: %s", err.c_str()); return false; }
    if (!host_->boot(err)) { mc::rlog("[boot] FAIL main(): %s", err.c_str()); return false; }
    mc::rlog("[boot] main() ok");
    if (auto* overlay = director->getNotificationNode()) overlay->setVisible(false);   // boot worked: hide the on-screen log (still sent to the server)
    if (!url.empty()) mc::startCdn(host_.get(), url);
    director->getScheduler()->schedule([this](float dt) {
        std::string e;
        static int frame = 0;
        static size_t seen = 0;
        static int errs = 0;
        ++frame;
        if (!host_->update(dt, e) && errs++ < 20) mc::rlog("[lua] frame %d: %s", frame, e.c_str());
        auto& miss = host_->missing();
        for (; seen < miss.size(); ++seen) mc::rlog("[missing] %s", miss[seen].c_str());
        if (frame == 1 || frame == 60 || frame == 300) {
            auto* run = Director::getInstance()->getRunningScene();
            mc::rlog("[frame %d] scene=%s children=%zu", frame, run ? "yes" : "NONE", run ? run->getChildrenCount() : 0);
        }
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
