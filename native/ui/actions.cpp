// ActionManager.*(node, time, ..., callbackId): thin wrappers over cocos actions; the optional trailing id is a Lua callback.
#include "common.h"
#include "lua_node.h"
#include "register.h"
#include "../core/rlog.h"

using namespace cocos2d;

namespace mc {
namespace ui {
namespace {

float numAt(lua_State* L, int i, float def) { return lua_isnumber(L, i) ? static_cast<float>(lua_tonumber(L, i)) : def; }

// Runs `action` on the node at arg 1, followed by the Lua callback found at index cbIdx (if it is a number).
void run(lua_State* L, Node* n, FiniteTimeAction* action, int cbIdx) {
    if (!n || !action) return;
    int id = lua_isnumber(L, cbIdx) ? static_cast<int>(lua_tonumber(L, cbIdx)) : 0;
    if (id > 0) n->runAction(Sequence::create(action, CallFunc::create([id]() { runCallback(id); }), nullptr));
    else n->runAction(action);
}

#define NODE Node* n = toNode(L, 1)

int moveTo(lua_State* L) { NODE; run(L, n, MoveTo::create(numAt(L, 2, 0), Vec2(numAt(L, 3, 0), numAt(L, 4, 0))), 5); return 0; }
int moveBy(lua_State* L) { NODE; run(L, n, MoveBy::create(numAt(L, 2, 0), Vec2(numAt(L, 3, 0), numAt(L, 4, 0))), 5); return 0; }
int scaleTo(lua_State* L) { NODE; run(L, n, ScaleTo::create(numAt(L, 2, 0), numAt(L, 3, 1)), 4); return 0; }
int scaleToXY(lua_State* L) { NODE; run(L, n, ScaleTo::create(numAt(L, 2, 0), numAt(L, 3, 1), numAt(L, 4, 1)), 5); return 0; }
int fadeIn(lua_State* L) { NODE; run(L, n, FadeIn::create(numAt(L, 2, 0)), 3); return 0; }
int fadeOut(lua_State* L) { NODE; run(L, n, FadeOut::create(numAt(L, 2, 0)), 3); return 0; }
int fadeTo(lua_State* L) { NODE; run(L, n, FadeTo::create(numAt(L, 2, 0), static_cast<GLubyte>(255 * numAt(L, 3, 1))), 4); return 0; }
int rotateBy(lua_State* L) { NODE; run(L, n, RotateBy::create(numAt(L, 2, 0), numAt(L, 3, 0)), 4); return 0; }
int rotateTo(lua_State* L) { NODE; run(L, n, RotateTo::create(numAt(L, 2, 0), numAt(L, 3, 0)), 4); return 0; }
int delayTo(lua_State* L) { NODE; run(L, n, DelayTime::create(numAt(L, 2, 0)), 3); return 0; }
int jumpTo(lua_State* L) { NODE; run(L, n, JumpTo::create(numAt(L, 2, 0), Vec2(numAt(L, 3, 0), numAt(L, 4, 0)), numAt(L, 5, 0), static_cast<int>(numAt(L, 6, 1))), 7); return 0; }
int jumpBy(lua_State* L) { NODE; run(L, n, JumpBy::create(numAt(L, 2, 0), Vec2(numAt(L, 3, 0), numAt(L, 4, 0)), numAt(L, 5, 0), static_cast<int>(numAt(L, 6, 1))), 7); return 0; }
int jump(lua_State* L) {                                // Jump(node, time, height, ?, jumps)
    NODE;
    run(L, n, JumpBy::create(numAt(L, 2, 0), Vec2(0, 0), numAt(L, 3, 0), static_cast<int>(numAt(L, 5, 1))), 6);
    return 0;
}
int shakeLoop(lua_State* L) {                           // ShakeLoop(node, period, amplitude, delay)
    NODE;
    if (!n) return 0;
    float t = numAt(L, 2, 0.1f), a = numAt(L, 3, 0.1f), delay = numAt(L, 4, 0);
    auto seq = Sequence::create(DelayTime::create(delay), RotateBy::create(t, a * 20), RotateBy::create(t, -a * 40), RotateBy::create(t, a * 20), nullptr);
    n->runAction(RepeatForever::create(seq));
    return 0;
}
int rollover(lua_State* L) { (void)L; return 0; }
int stopAll(lua_State* L) { if (Node* n = toNode(L, 1)) n->stopAllActions(); return 0; }
int pauseAction(lua_State* L) { if (Node* n = toNode(L, 1)) n->pause(); return 0; }
int resumeAction(lua_State* L) { if (Node* n = toNode(L, 1)) n->resume(); return 0; }

}  // namespace

void registerActions(lua_State* L) {
    lua_newtable(L);
    struct R { const char* n; lua_CFunction f; };
    static const R regs[] = {
        {"MoveTo", moveTo}, {"MoveBy", moveBy}, {"ScaleTo", scaleTo}, {"ScaleToXY", scaleToXY}, {"FadeIn", fadeIn}, {"FadeOut", fadeOut},
        {"FadeTo", fadeTo}, {"RotateBy", rotateBy}, {"RotateTo", rotateTo}, {"DelayTo", delayTo}, {"JumpTo", jumpTo}, {"JumpBy", jumpBy},
        {"Jump", jump}, {"ShakeLoop", shakeLoop}, {"Rollover", rollover}, {"StopAllActions", stopAll}, {"PauseAction", pauseAction},
        {"ResumeAction", resumeAction},
    };
    for (const R& r : regs) { lua_pushcfunction(L, r.f); lua_setfield(L, -2, r.n); }
    lua_pushvalue(L, -1);
    lua_setglobal(L, "ActionManager");
    lua_getglobal(L, "GameLib");
    if (lua_istable(L, -1)) { lua_pushvalue(L, -2); lua_setfield(L, -2, "ActionManager"); }
    lua_pop(L, 2);
}

}  // namespace ui
}  // namespace mc
