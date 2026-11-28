// Natives of the original engine (class list extracted from the APK's LuaRegister::Regist) that the game's Lua calls:
// image effects, image buttons, text helpers, list / page / scroll widgets, sound, and a few SDK calls.
// Registered last, so these replace earlier placeholders.
#include <cstdio>
#include <functional>
#include <map>
#include <string>

#include "audio/include/AudioEngine.h"
#include "ui/CocosGUI.h"
#include "spine/spine-cocos2dx.h"

#include "common.h"
#include "lua_node.h"
#include "register.h"
#include "../core/rlog.h"

using namespace cocos2d;
using cocos2d::ui::Widget;
using cocos2d::experimental::AudioEngine;

namespace mc {
namespace ui {
namespace {

float num(lua_State* L, int i, float def = 0) { return static_cast<float>(luaL_optnumber(L, i, def)); }
bool flag(lua_State* L, int i) { return lua_toboolean(L, i) != 0; }
GLubyte byteArg(lua_State* L, int i, int def = 0) { return static_cast<GLubyte>(luaL_optnumber(L, i, def)); }
int noop(lua_State*) { return 0; }
int retZero(lua_State* L) { lua_pushnumber(L, 0); return 1; }
int retOne(lua_State* L) { lua_pushnumber(L, 1); return 1; }
int retFalse(lua_State* L) { lua_pushboolean(L, 0); return 1; }
int retEmpty(lua_State* L) { lua_pushstring(L, ""); return 1; }

// calls self:<method>(args 2..n) through the node metatable
void callSelf(lua_State* L, const char* method, int argIdx) {
    lua_getfield(L, 1, method);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushvalue(L, 1);
    lua_pushvalue(L, argIdx);
    if (lua_pcall(L, 2, 0, 0) != 0) { rlog("[compat] %s: %s", method, lua_tostring(L, -1)); lua_pop(L, 1); }
}
void setTouch(lua_State* L, bool on) {
    lua_getfield(L, 1, "SetTouchEnabled");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushvalue(L, 1);
    lua_pushboolean(L, on);
    if (lua_pcall(L, 2, 0, 0) != 0) lua_pop(L, 1);
}

// ---------------------------------------------------------------- grey / highlight
GLProgramState* greyState() {
    static GLProgram* prog = nullptr;
    if (!prog) {
        const char* vs =
            "attribute vec4 a_position;\nattribute vec2 a_texCoord;\nattribute vec4 a_color;\n"
            "#ifdef GL_ES\nvarying lowp vec4 v_c;\nvarying mediump vec2 v_t;\n#else\nvarying vec4 v_c;\nvarying vec2 v_t;\n#endif\n"
            "void main() { gl_Position = CC_PMatrix * a_position; v_c = a_color; v_t = a_texCoord; }\n";
        const char* fs =
            "#ifdef GL_ES\nprecision lowp float;\n#endif\nvarying vec4 v_c;\nvarying vec2 v_t;\n"
            "void main() { vec4 c = v_c * texture2D(CC_Texture0, v_t); float g = dot(c.rgb, vec3(0.299, 0.587, 0.114)); gl_FragColor = vec4(g, g, g, c.a); }\n";
        prog = GLProgram::createWithByteArrays(vs, fs);
        if (!prog) return nullptr;
        prog->retain();
    }
    return GLProgramState::getOrCreateWithGLProgram(prog);
}
// The Android engine's "high light": the picture's colours times 1.6 (Image9Label::SetBright), alpha untouched.
GLProgramState* brightStateImpl(float value) {
    static GLProgram* prog = nullptr;
    static std::map<float, GLProgramState*> states;
    if (!prog) {
        const char* vs =
            "attribute vec4 a_position;
attribute vec2 a_texCoord;
attribute vec4 a_color;
"
            "#ifdef GL_ES
varying lowp vec4 v_c;
varying mediump vec2 v_t;
#else
varying vec4 v_c;
varying vec2 v_t;
#endif
"
            "void main() { gl_Position = CC_PMatrix * a_position; v_c = a_color; v_t = a_texCoord; }
";
        const char* fs =
            "#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_c;
varying vec2 v_t;
uniform float u_bright;
"
            "void main() { vec4 c = v_c * texture2D(CC_Texture0, v_t); gl_FragColor = vec4(c.rgb * u_bright, c.a); }
";
        prog = GLProgram::createWithByteArrays(vs, fs);
        if (!prog) return nullptr;
        prog->retain();
    }
    auto it = states.find(value);
    if (it != states.end()) return it->second;
    GLProgramState* st = GLProgramState::create(prog);
    st->setUniformFloat("u_bright", value);
    st->retain();
    states[value] = st;
    return st;
}
void applyShader(Node* n, GLProgramState* st) {          // a node and the pictures under it; nullptr: back to normal
    if (!n) return;
    Sprite* s = dynamic_cast<Sprite*>(n);
    if (!s) if (auto* w = dynamic_cast<Widget*>(n)) s = dynamic_cast<Sprite*>(w->getVirtualRenderer());
    if (s) s->setGLProgramState(st ? st : GLProgramState::getOrCreateWithGLProgramName(GLProgram::SHADER_NAME_POSITION_TEXTURE_COLOR_NO_MVP));
    for (Node* c : n->getChildren()) applyShader(c, st);
}
void applyGrey(Node* n, bool grey) {
    if (!n) return;
    if (dynamic_cast<spine::SkeletonAnimation*>(n)) { n->setColor(grey ? Color3B(110, 110, 110) : Color3B::WHITE); return; }
    Sprite* s = dynamic_cast<Sprite*>(n);
    if (!s) if (auto* w = dynamic_cast<Widget*>(n)) s = dynamic_cast<Sprite*>(w->getVirtualRenderer());
    if (auto* s9 = dynamic_cast<cocos2d::ui::Scale9Sprite*>(s ? static_cast<Node*>(s) : n)) s9->setState(grey ? cocos2d::ui::Scale9Sprite::State::GRAY : cocos2d::ui::Scale9Sprite::State::NORMAL);
    else if (s) {
        if (grey) { if (GLProgramState* st = greyState()) s->setGLProgramState(st); }
        else s->setGLProgramState(GLProgramState::getOrCreateWithGLProgramName(GLProgram::SHADER_NAME_POSITION_TEXTURE_COLOR_NO_MVP));
    }
    for (Node* c : n->getChildren()) applyGrey(c, grey);
}
int setGrey(lua_State* L) { applyGrey(toNode(L, 1), flag(L, 2)); return 0; }
int setHighLight(lua_State* L) { applyShader(toNode(L, 1), flag(L, 2) ? brightStateImpl(1.6f) : nullptr); return 0; }
int setBrightValue(lua_State* L) { applyShader(toNode(L, 1), brightStateImpl(num(L, 2, 1))); return 0; }

// ---------------------------------------------------------------- buttons
int setEnabledM(lua_State* L) {
    bool on = flag(L, 2);
    if (auto* w = dynamic_cast<Widget*>(toNode(L, 1))) { w->setEnabled(on); w->setBright(on); }
    else { applyGrey(toNode(L, 1), !on); setTouch(L, on); }
    return 0;
}
int isEnable(lua_State* L) { auto* w = dynamic_cast<Widget*>(toNode(L, 1)); lua_pushboolean(L, !w || w->isEnabled()); return 1; }
int setBrightM(lua_State* L) { if (auto* w = dynamic_cast<Widget*>(toNode(L, 1))) w->setBright(flag(L, 2)); return 0; }
int setNormalImage(lua_State* L) {
    if (auto* b = dynamic_cast<cocos2d::ui::Button*>(toNode(L, 1))) { if (lua_isstring(L, 2)) b->loadTextureNormal(lua_tostring(L, 2)); return 0; }
    callSelf(L, "SetImage", 2);
    return 0;
}
int setSelectedImage(lua_State* L) {
    if (auto* b = dynamic_cast<cocos2d::ui::Button*>(toNode(L, 1))) if (lua_isstring(L, 2)) b->loadTexturePressed(lua_tostring(L, 2));
    return 0;                                            // plain image buttons show the pressed state by brightening (touch.cpp)
}
int pushSelf(lua_State* L) { lua_pushvalue(L, 1); return 1; }   // GetNormalImageNode & co: our image button is one sprite
int setPressedAction(lua_State* L) { if (auto* b = dynamic_cast<cocos2d::ui::Button*>(toNode(L, 1))) b->setPressedActionEnabled(flag(L, 2)); return 0; }
int setOutline(lua_State* L) {
    Color4B c(byteArg(L, 2), byteArg(L, 3), byteArg(L, 4), byteArg(L, 5, 255));
    int size = static_cast<int>(num(L, 6, 1));
    if (auto* b = dynamic_cast<cocos2d::ui::Button*>(toNode(L, 1))) { if (Label* l = b->getTitleRenderer()) l->enableOutline(c, size); }
    else if (auto* l = dynamic_cast<Label*>(toNode(L, 1))) l->enableOutline(c, size);
    return 0;
}

// ---------------------------------------------------------------- text
Label* labelNode(Node* n) {
    if (auto* l = dynamic_cast<Label*>(n)) return l;
    if (auto* t = dynamic_cast<cocos2d::ui::Text*>(n)) return dynamic_cast<Label*>(t->getVirtualRenderer());
    if (auto* b = dynamic_cast<cocos2d::ui::Button*>(n)) return b->getTitleRenderer();
    return nullptr;
}
int setShadow(lua_State* L) {
    if (Label* l = labelNode(toNode(L, 1))) {
        if (flag(L, 2)) l->enableShadow(Color4B(0, 0, 0, 200), Size(2, -2));
        else l->disableEffect(LabelEffect::SHADOW);
    }
    return 0;
}
int setShadowColor(lua_State* L) {                     // SetShadowColor(r, g, b, a)
    if (Label* l = labelNode(toNode(L, 1)))
        l->enableShadow(Color4B(byteArg(L, 2), byteArg(L, 3), byteArg(L, 4), byteArg(L, 5)), Size(2, -2));
    return 0;
}
int disableEffect(lua_State* L) { if (Label* l = labelNode(toNode(L, 1))) l->disableEffect(); return 0; }
int setHolderText(lua_State* L) {
    if (auto* f = dynamic_cast<cocos2d::ui::TextField*>(toNode(L, 1))) f->setPlaceHolder(luaL_optstring(L, 2, ""));
    return 0;
}
int setTextAreaSize(lua_State* L) { if (auto* t = dynamic_cast<cocos2d::ui::Text*>(toNode(L, 1))) t->setTextAreaSize(Size(num(L, 2), num(L, 3))); return 0; }
int setTextHAlign(lua_State* L) {
    int v = static_cast<int>(num(L, 2, 1));
    if (Label* l = labelNode(toNode(L, 1)))
        l->setHorizontalAlignment(v == 2 ? TextHAlignment::CENTER : v == 3 ? TextHAlignment::RIGHT : TextHAlignment::LEFT);
    return 0;
}
int labelNodeCreate(lua_State* L) { Label* l = Label::createWithSystemFont("", "Helvetica", 20); pushNode(L, l); return 1; }

// ---------------------------------------------------------------- scroll / list / page / layout
cocos2d::ui::ListView* listSelf(lua_State* L) { return dynamic_cast<cocos2d::ui::ListView*>(toNode(L, 1)); }
void pushOrNil(lua_State* L, Node* n) { if (n) pushNode(L, n); else lua_pushnil(L); }
int getItem(lua_State* L) {
    auto* lv = listSelf(L);
    pushOrNil(L, lv ? lv->getItem(static_cast<ssize_t>(num(L, 2))) : nullptr);
    return 1;
}
int getIndex(lua_State* L) {                             // list:GetIndex(item) or item:GetIndex()
    long idx = -1;
    if (auto* lv = listSelf(L)) { if (auto* w = dynamic_cast<Widget*>(toNode(L, 2))) idx = static_cast<long>(lv->getIndex(w)); }
    else if (auto* w = dynamic_cast<Widget*>(toNode(L, 1))) {
        Node* p = w->getParent();
        auto* owner = p ? dynamic_cast<cocos2d::ui::ListView*>(p->getParent()) : nullptr;
        if (owner) idx = static_cast<long>(owner->getIndex(w));
    }
    lua_pushnumber(L, idx);
    return 1;
}
int refreshView(lua_State* L) { if (auto* lv = listSelf(L)) lv->requestDoLayout(); return 0; }
int pushBackItem(lua_State* L) { auto* lv = listSelf(L); auto* w = dynamic_cast<Widget*>(toNode(L, 2)); if (lv && w) lv->pushBackCustomItem(w); return 0; }
int removeAllItems(lua_State* L) { if (auto* lv = listSelf(L)) lv->removeAllItems(); return 0; }
int createPage(lua_State* L) {
    auto* page = cocos2d::ui::Layout::create();
    if (Node* n = toNode(L, 1)) page->setContentSize(n->getContentSize());
    pushNode(L, page);
    return 1;
}
int addPage(lua_State* L) {
    auto* pv = dynamic_cast<cocos2d::ui::PageView*>(toNode(L, 1));
    auto* w = dynamic_cast<cocos2d::ui::Layout*>(toNode(L, 2));
    if (pv && w) pv->pushBackCustomItem(w);
    return 0;
}
int removePage(lua_State* L) {
    auto* pv = dynamic_cast<cocos2d::ui::PageView*>(toNode(L, 1));
    if (!pv) return 0;
    if (lua_isnumber(L, 2)) { ssize_t i = static_cast<ssize_t>(num(L, 2)); if (pv->getItem(i)) pv->removeItem(i); }
    else if (auto* w = dynamic_cast<Widget*>(toNode(L, 2))) pv->removeItem(pv->getIndex(w));
    return 0;
}
int getCurPage(lua_State* L) {
    auto* pv = dynamic_cast<cocos2d::ui::PageView*>(toNode(L, 1));
    pushOrNil(L, pv ? pv->getItem(pv->getCurrentPageIndex()) : nullptr);
    return 1;
}
int scrollViewCreate(lua_State* L) { auto* s = cocos2d::ui::ScrollView::create(); s->setAnchorPoint(Vec2::ZERO); pushNode(L, s); return 1; }
// SetSize(w, h): scroll and list views have no size until the script gives them one (the mailbox stayed empty without it)
int setSize(lua_State* L) {
    Node* n = toNode(L, 1);
    if (!n) return 0;
    if (auto* w = dynamic_cast<Widget*>(n)) w->ignoreContentAdaptWithSize(false);
    n->setContentSize(Size(num(L, 2), num(L, 3)));
    return 0;
}
int setScrollDirection(lua_State* L) {
    if (auto* s = dynamic_cast<cocos2d::ui::ScrollView*>(toNode(L, 1))) s->setDirection(static_cast<cocos2d::ui::ScrollView::Direction>(static_cast<int>(num(L, 2, 1))));
    return 0;
}
int setBounce(lua_State* L) { if (auto* s = dynamic_cast<cocos2d::ui::ScrollView*>(toNode(L, 1))) s->setBounceEnabled(flag(L, 2)); return 0; }
int scrollToTop(lua_State* L) { if (auto* s = dynamic_cast<cocos2d::ui::ScrollView*>(toNode(L, 1))) s->jumpToTop(); return 0; }
int scrollToLeft(lua_State* L) { if (auto* s = dynamic_cast<cocos2d::ui::ScrollView*>(toNode(L, 1))) s->jumpToLeft(); return 0; }
int scrollToRight(lua_State* L) { if (auto* s = dynamic_cast<cocos2d::ui::ScrollView*>(toNode(L, 1))) s->jumpToRight(); return 0; }
cocos2d::ui::Layout* layoutSelf(lua_State* L) { return dynamic_cast<cocos2d::ui::Layout*>(toNode(L, 1)); }
int setBgColor(lua_State* L) {
    auto* l = layoutSelf(L);
    if (l && lua_isnumber(L, 2) && lua_isnumber(L, 3)) {
        if (l->getBackGroundColorType() == cocos2d::ui::Layout::BackGroundColorType::NONE) l->setBackGroundColorType(cocos2d::ui::Layout::BackGroundColorType::SOLID);
        l->setBackGroundColor(Color3B(byteArg(L, 2), byteArg(L, 3), byteArg(L, 4)));
    }
    return 0;
}
int setBgColorType(lua_State* L) { if (auto* l = layoutSelf(L)) l->setBackGroundColorType(static_cast<cocos2d::ui::Layout::BackGroundColorType>(static_cast<int>(num(L, 2)))); return 0; }
int setBgColorOpacity(lua_State* L) { if (auto* l = layoutSelf(L)) l->setBackGroundColorOpacity(byteArg(L, 2, 255)); return 0; }
int setBgImage(lua_State* L) { if (auto* l = layoutSelf(L)) if (lua_isstring(L, 2)) l->setBackGroundImage(lua_tostring(L, 2)); return 0; }
int setBgImageOpacity(lua_State* L) { if (auto* l = layoutSelf(L)) l->setBackGroundImageOpacity(byteArg(L, 2, 255)); return 0; }
int setLayoutType(lua_State* L) { if (auto* l = layoutSelf(L)) l->setLayoutType(static_cast<cocos2d::ui::Layout::Type>(static_cast<int>(num(L, 2)))); return 0; }

// ---------------------------------------------------------------- misc nodes
int getTimerType(lua_State* L) { auto* t = dynamic_cast<ProgressTimer*>(toNode(L, 1)); lua_pushnumber(L, t ? static_cast<int>(t->getType()) : 0); return 1; }
int clearNode(lua_State* L) {
    Node* n = toNode(L, 1);
    if (auto* d = dynamic_cast<DrawNode*>(n)) d->clear();
    else if (n) n->removeAllChildren();
    return 0;
}
int removeAllLayer(lua_State* L) { if (Node* n = toNode(L, 1)) n->removeAllChildren(); return 0; }
int setTouchEnableAlias(lua_State* L) { setTouch(L, flag(L, 2)); return 0; }
int originX(lua_State* L) { Node* n = toNode(L, 1); lua_pushnumber(L, n ? n->getBoundingBox().origin.x - n->getPositionX() : 0); return 1; }
int originY(lua_State* L) { Node* n = toNode(L, 1); lua_pushnumber(L, n ? n->getBoundingBox().origin.y - n->getPositionY() : 0); return 1; }

// ---------------------------------------------------------------- sound (GameLib.SoundManager:...)
float g_musicVol = 1.0f, g_effectVol = 1.0f;
int g_music = AudioEngine::INVALID_AUDIO_ID;
std::string g_musicPath;

// the game's .sdzs / .sdzm files are plain MP3; the iOS decoder wants the extension
std::string soundFile(const std::string& path) {
    static std::map<std::string, std::string> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    std::string src = resolveResFile(path);
    if (src.empty()) for (const char* e : {".sdzs", ".sdzm", ".mp3"}) { size_t d = path.rfind('.'); src = resolveResFile(path.substr(0, d) + e); if (!src.empty()) break; }
    if (src.empty()) return "";                          // not downloaded yet: try again next time
    Data data = FileUtils::getInstance()->getDataFromFile(src);
    if (data.isNull()) return "";
    std::string dir = FileUtils::getInstance()->getWritablePath() + "snd_cache/";
    FileUtils::getInstance()->createDirectory(dir);
    std::string out = dir + std::to_string(std::hash<std::string>()(src)) + ".mp3";
    FILE* f = std::fopen(out.c_str(), "wb");
    if (!f) return "";
    std::fwrite(data.getBytes(), 1, static_cast<size_t>(data.getSize()), f);
    std::fclose(f);
    cache[path] = out;
    return out;
}
int playMusic(lua_State* L) {                            // PlayBGMusic(path, loop)
    const char* p = lua_tostring(L, 2);
    if (!p) return 0;
    bool loop = lua_isnone(L, 3) || flag(L, 3);
    if (g_music != AudioEngine::INVALID_AUDIO_ID && g_musicPath == p) return 0;
    if (g_music != AudioEngine::INVALID_AUDIO_ID) AudioEngine::stop(g_music);
    g_music = AudioEngine::INVALID_AUDIO_ID;
    std::string f = soundFile(p);
    if (!f.empty()) { g_music = AudioEngine::play2d(f, loop, g_musicVol); g_musicPath = p; }
    return 0;
}
int stopMusic(lua_State*) { if (g_music != AudioEngine::INVALID_AUDIO_ID) AudioEngine::stop(g_music); g_music = AudioEngine::INVALID_AUDIO_ID; return 0; }
int pauseMusic(lua_State*) { if (g_music != AudioEngine::INVALID_AUDIO_ID) AudioEngine::pause(g_music); return 0; }
int resumeMusic(lua_State*) { if (g_music != AudioEngine::INVALID_AUDIO_ID) AudioEngine::resume(g_music); return 0; }
int setMusicVol(lua_State* L) { g_musicVol = num(L, 2, 1); if (g_music != AudioEngine::INVALID_AUDIO_ID) AudioEngine::setVolume(g_music, g_musicVol); return 0; }
int getMusicVol(lua_State* L) { lua_pushnumber(L, g_musicVol); return 1; }
int playEffect(lua_State* L) {                           // PlayEffect(path, loop) -> id
    const char* p = lua_tostring(L, 2);
    std::string f = p ? soundFile(p) : "";
    int id = f.empty() ? 0 : AudioEngine::play2d(f, flag(L, 3), g_effectVol) + 1;
    lua_pushnumber(L, id < 0 ? 0 : id);
    return 1;
}
int stopEffect(lua_State* L) { int id = static_cast<int>(num(L, 2)); if (id > 0) AudioEngine::stop(id - 1); return 0; }
int pauseEffect(lua_State* L) { int id = static_cast<int>(num(L, 2)); if (id > 0) AudioEngine::pause(id - 1); return 0; }
int resumeEffect(lua_State* L) { int id = static_cast<int>(num(L, 2)); if (id > 0) AudioEngine::resume(id - 1); return 0; }
int setEffectVol(lua_State* L) { g_effectVol = num(L, 2, 1); return 0; }
int getEffectVol(lua_State* L) { lua_pushnumber(L, g_effectVol); return 1; }

// ---------------------------------------------------------------- SDK
int gotoBrowser(lua_State* L) {
    const char* u = lua_isstring(L, -1) ? lua_tostring(L, -1) : nullptr;
    if (u) Application::getInstance()->openURL(u);
    return 0;
}

struct R { const char* n; lua_CFunction f; };
template <size_t N> void setFields(lua_State* L, const R (&regs)[N]) {
    for (const R& r : regs) { lua_pushcfunction(L, r.f); lua_setfield(L, -2, r.n); }
}
// pushes GameLib.<name> (created when absent)
void gameLibTable(lua_State* L, const char* name) {
    lua_getglobal(L, "GameLib");
    lua_getfield(L, -1, name);
    if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_newtable(L); lua_pushvalue(L, -1); lua_setfield(L, -3, name); }
    lua_remove(L, -2);
}
void globalTable(lua_State* L, const char* name) {
    lua_getglobal(L, name);
    if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_newtable(L); lua_pushvalue(L, -1); lua_setglobal(L, name); }
}
void constant(lua_State* L, const char* k, int v) { lua_pushnumber(L, v); lua_setfield(L, -2, k); }

}  // namespace

void registerCompat(lua_State* L) {
    luaL_getmetatable(L, kNodeMeta);
    static const R nodes[] = {
        {"SetGrey", setGrey}, {"SetGray", setGrey}, {"SetGraying", setGrey}, {"SetHighLight", setHighLight},
        {"SetEnabled", setEnabledM}, {"SetIsEnabled", setEnabledM}, {"IsEnable", isEnable}, {"SetBright", setBrightM},
        {"SetNormalImage", setNormalImage}, {"SetSelectedImage", setSelectedImage}, {"GetNormalImageNode", pushSelf},
        {"GetSelectedImageNode", pushSelf}, {"GetDisableImageNode", pushSelf}, {"SetDisableButtonTextColor", noop},
        {"SetPressedActionEnabled", setPressedAction}, {"SetOutline", setOutline}, {"SetTouchDownOverLap", noop},
        {"SetShadow", setShadow}, {"SetShadowColor", setShadowColor}, {"DisableEffect", disableEffect}, {"SetHolderText", setHolderText}, {"SetExpand", noop},
        {"SetTextAreaSize", setTextAreaSize}, {"SetTextHorizontalAlign", setTextHAlign},
        {"GetItem", getItem}, {"GetIndex", getIndex}, {"RefreshView", refreshView}, {"PushBackCustomItem", pushBackItem},
        {"RemoveAllItems", removeAllItems}, {"AddEventListener", noop},
        {"CreatePage", createPage}, {"AddPage", addPage}, {"RemovePage", removePage}, {"GetCurPage", getCurPage},
        {"SetScrollDirection", setScrollDirection}, {"SetDirection", setScrollDirection}, {"SetBounceEnabled", setBounce},
        {"ScrollToTop", scrollToTop}, {"ScrollToLeft", scrollToLeft}, {"ScrollToRight", scrollToRight},
        {"SetBackGroundColor", setBgColor}, {"SetBackGroundColorType", setBgColorType}, {"SetBackGroundColorOpacity", setBgColorOpacity},
        {"SetBackGroundImage", setBgImage}, {"SetBackGroundImageOpacity", setBgImageOpacity}, {"SetLayoutType", setLayoutType},
        {"SetAliasTexParameters", noop}, {"GetVirtualRenderer", pushSelf},
        {"GetType", getTimerType}, {"Clear", clearNode}, {"RemoveAllLayer", removeAllLayer}, {"removeAllChildren", removeAllLayer}, {"SetSize", setSize}, {"SetTouchEnable", setTouchEnableAlias},
        {"GetOriginX", originX}, {"GetOriginY", originY}, {"SetCycle", noop}, {"setDirty", noop},
    };
    setFields(L, nodes);
    lua_pop(L, 1);

    globalTable(L, "NodeUnity");
    { static const R f[] = {{"SetGreyShader", setGrey}, {"SetBrightShader", setHighLight}, {"SetBrightValue", setBrightValue}}; setFields(L, f); }       // static form: (node, bool / value)
    lua_pop(L, 1);
    gameLibTable(L, "MsgPush");
    { static const R f[] = {{"SetTag", noop}}; setFields(L, f); }                 // push notifications: none on this build
    lua_pop(L, 1);
    globalTable(L, "UIScrollViewEx");
    { static const R f[] = {{"Create", scrollViewCreate}}; setFields(L, f); }
    constant(L, "ScrollDirNone", 0); constant(L, "ScrollDirVertical", 1); constant(L, "ScrollDirHorizontal", 2); constant(L, "ScrollDirBoth", 3);
    lua_pop(L, 1);
    globalTable(L, "UIListViewEx");
    constant(L, "ScrollDirNone", 0); constant(L, "ScrollDirVertical", 1); constant(L, "ScrollDirHorizontal", 2); constant(L, "ScrollDirBoth", 3);
    lua_pop(L, 1);
    globalTable(L, "Label");
    { static const R f[] = {{"Node", labelNodeCreate}}; setFields(L, f); }
    lua_pop(L, 1);

    gameLibTable(L, "SoundManager");
    {
        static const R f[] = {
            {"PreloadBGMusic", noop}, {"PlayBGMusic", playMusic}, {"StopBGMusic", stopMusic}, {"PauseBGMusic", pauseMusic},
            {"ResumeBGMusic", resumeMusic}, {"RewindBGMusic", noop}, {"IsBGMusicPlaying", retFalse}, {"GetBGMusicVolume", getMusicVol},
            {"SetBGMusicVolume", setMusicVol}, {"PlayEffect", playEffect}, {"PauseEffect", pauseEffect}, {"PauseAllEffects", noop},
            {"ResumeEffect", resumeEffect}, {"ResumeAllEffects", noop}, {"StopEffect", stopEffect}, {"StopAllEffects", noop},
            {"PreloadEffect", noop}, {"UnloadEffect", noop}, {"GetEffectsVolume", getEffectVol}, {"SetEffectsVolume", setEffectVol},
            {"Release", noop}, {"Stop", stopMusic},
        };
        setFields(L, f);
    }
    lua_pop(L, 1);

    gameLibTable(L, "SdkTools");
    {
        static const R f[] = {
            {"GotoBrowser", gotoBrowser}, {"ShowDialog", noop}, {"OpenFile", noop}, {"LoginPanel", noop}, {"LoginPannelWithParam", noop},
            {"Pay", noop}, {"ExitSdk", noop}, {"ToUserCenter", noop}, {"CheckUpdate", noop}, {"Install", noop},
            {"GetAppId", retEmpty}, {"GetAppKey", retEmpty}, {"GetPhpOrderUrl", retEmpty}, {"GetPhpNotifyUrl", retEmpty},
            {"GetPhpUserLoginUrl", retEmpty}, {"GetStartImagePath", retEmpty}, {"GetUserCenterImagePath", retEmpty},
            {"IsShowUserCenter", retFalse}, {"GetUpdateUrlParams", retEmpty},
        };
        setFields(L, f);
    }
    lua_pop(L, 1);

    gameLibTable(L, "SystemTools");
    {
        static const R f[] = {
            {"GetNetWorkState", retOne}, {"IsAppInstalled", retFalse}, {"GetImsi", retEmpty}, {"IsOpenLocationService", retFalse},
            {"GetAppName", retEmpty}, {"GetAppPackPath", retEmpty}, {"GetMemorySize", retZero}, {"GetIdentifierForVendor", retEmpty},
        };
        setFields(L, f);
        constant(L, "NETWORK_NONE", 0); constant(L, "NETWORK_WIFI", 1); constant(L, "NETWORK_MOBILE", 2); constant(L, "NETWORK_UNKNOW", 3);
    }
    lua_pop(L, 1);
}

cocos2d::GLProgramState* brightState(float value) { return brightStateImpl(value); }

}  // namespace ui
}  // namespace mc
