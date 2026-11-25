// GameLib.SceneManager: the Lua side builds a "view" node per game scene and asks us to show it.
#include "lua_node.h"
#include "register.h"
#include "../core/rlog.h"

using namespace cocos2d;

namespace mc {
namespace ui {
namespace {

// SceneManager:ReplaceScene(view) / PushScene(view): wrap the node in a cocos Scene and run it.
int replaceScene(lua_State* L) {
    Node* view = toNode(L, 2);
    if (!view) { rlog("[scene] ReplaceScene without a node"); return 0; }
    Scene* sc = Scene::create();
    if (view->getParent()) view->removeFromParent();
    sc->addChild(view);
    auto* d = Director::getInstance();
    if (d->getRunningScene()) d->replaceScene(sc); else d->runWithScene(sc);
    rlog("[scene] ReplaceScene view children=%zu size=%.0fx%.0f", view->getChildrenCount(), view->getContentSize().width, view->getContentSize().height);
    return 0;
}
int pushScene(lua_State* L) {
    Node* view = toNode(L, 2);
    if (!view) return 0;
    Scene* sc = Scene::create();
    if (view->getParent()) view->removeFromParent();
    sc->addChild(view);
    auto* d = Director::getInstance();
    if (d->getRunningScene()) d->pushScene(sc); else d->runWithScene(sc);
    return 0;
}
int popScene(lua_State*) { Director::getInstance()->popScene(); return 0; }
int getRunningScene(lua_State* L) {
    Scene* s = Director::getInstance()->getRunningScene();
    if (!s) { lua_pushnil(L); return 1; }
    pushNode(L, s);
    return 1;
}
int getStartTime(lua_State* L) { lua_pushnumber(L, 0); return 1; }

}  // namespace

void registerScene(lua_State* L) {
    lua_getglobal(L, "GameLib");
    lua_getfield(L, -1, "SceneManager");
    if (lua_istable(L, -1)) {
        lua_pushcfunction(L, replaceScene); lua_setfield(L, -2, "ReplaceScene");
        lua_pushcfunction(L, pushScene); lua_setfield(L, -2, "PushScene");
        lua_pushcfunction(L, popScene); lua_setfield(L, -2, "PopScene");
        lua_pushcfunction(L, getRunningScene); lua_setfield(L, -2, "GetRunningScene");
        lua_pushcfunction(L, getStartTime); lua_setfield(L, -2, "GetStartTime");
    }
    lua_pop(L, 2);
}

}  // namespace ui
}  // namespace mc
