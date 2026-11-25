// FontLabel / RichText / EditText on top of cocos2d::Label and ui::TextField. Font: zhunyuan.ttf shipped beside the resources.
#include "ui/UITextField.h"

#include "lua_node.h"

using namespace cocos2d;

namespace mc {
namespace ui {
namespace {

std::string g_font = "zhunyuan.ttf";

struct Meta {                                   // extra state kept per text node
    bool stroke = false;
    Color3B color = Color3B::WHITE;
};

Label* labelOf(lua_State* L) {
    Node* n = toNode(L, 1);
    return dynamic_cast<Label*>(n);
}

Label* makeLabel(const std::string& text, float size) {
    Label* l = Label::createWithTTF(text, g_font, size);
    if (!l) l = Label::createWithSystemFont(text, "Arial", size);
    l->setAnchorPoint(Vec2(0.5f, 0.5f));
    return l;
}

void applyStroke(Label* l, bool on) {
    if (on) l->enableOutline(Color4B::BLACK, 2);
    else l->disableEffect(LabelEffect::OUTLINE);
}

// FontLabel.CreateWithString(text, fontName, fontSize)
int fontLabelCreate(lua_State* L) {
    const char* text = lua_tostring(L, 1);
    float size = static_cast<float>(luaL_optnumber(L, 3, 20));
    pushNode(L, makeLabel(text ? text : "", size));
    return 1;
}

// RichText.CreateRichText(width, height, fontName, fontSize); width/height -1 = unconstrained
int richTextCreate(lua_State* L) {
    float w = static_cast<float>(luaL_optnumber(L, 1, -1));
    float size = static_cast<float>(luaL_optnumber(L, 4, 20));
    Label* l = makeLabel("", size);
    if (w > 0) l->setDimensions(w, 0);
    pushNode(L, l);
    return 1;
}

// EditText.CreateEditText(placeholder, width, height, fontName, fontSize)
int editTextCreate(lua_State* L) {
    const char* hint = lua_tostring(L, 1);
    float w = static_cast<float>(luaL_optnumber(L, 2, 200)), h = static_cast<float>(luaL_optnumber(L, 3, 30));
    float size = static_cast<float>(luaL_optnumber(L, 5, 20));
    cocos2d::ui::TextField* f = cocos2d::ui::TextField::create(hint ? hint : "", g_font, size);
    f->setContentSize(Size(w, h));
    pushNode(L, f);
    return 1;
}

int setString(lua_State* L) {
    const char* t = lua_tostring(L, 2);
    if (Label* l = labelOf(L)) l->setString(t ? t : "");
    else if (auto* f = dynamic_cast<cocos2d::ui::TextField*>(toNode(L, 1))) f->setString(t ? t : "");
    return 0;
}
int getText(lua_State* L) {
    if (Label* l = labelOf(L)) lua_pushstring(L, l->getString().c_str());
    else if (auto* f = dynamic_cast<cocos2d::ui::TextField*>(toNode(L, 1))) lua_pushstring(L, f->getString().c_str());
    else lua_pushstring(L, "");
    return 1;
}
int setStroke(lua_State* L) { if (Label* l = labelOf(L)) applyStroke(l, lua_toboolean(L, 2) != 0); return 0; }
int setTextColor(lua_State* L) {
    Color3B c(static_cast<GLubyte>(luaL_checknumber(L, 2)), static_cast<GLubyte>(luaL_checknumber(L, 3)), static_cast<GLubyte>(luaL_checknumber(L, 4)));
    if (Label* l = labelOf(L)) l->setTextColor(Color4B(c));
    else if (auto* f = dynamic_cast<cocos2d::ui::TextField*>(toNode(L, 1))) f->setTextColor(Color4B(c));
    return 0;
}
int setFontSize(lua_State* L) {
    if (Label* l = labelOf(L)) { TTFConfig cfg = l->getTTFConfig(); cfg.fontSize = static_cast<float>(luaL_checknumber(L, 2)); l->setTTFConfig(cfg); }
    return 0;
}
int noop(lua_State*) { return 0; }
int setAlign(lua_State* L) {                    // Label.ALIGIN_* : 1 left, 2 center, 3 right (values assumed; reconciled with Lua constants)
    if (Label* l = labelOf(L)) {
        int v = static_cast<int>(luaL_checknumber(L, 2));
        l->setHorizontalAlignment(v == 2 ? TextHAlignment::CENTER : v == 3 ? TextHAlignment::RIGHT : TextHAlignment::LEFT);
    }
    return 0;
}

void addMethods(lua_State* L) {
    luaL_getmetatable(L, kNodeMeta);
    lua_getfield(L, -1, "__index");                // the methods table is the metatable itself (__index = self)
    lua_pop(L, 1);
    struct R { const char* n; lua_CFunction f; };
    static const R regs[] = {
        {"SetString", setString}, {"SetText", setString}, {"GetText", getText}, {"GetString", getText},
        {"SetStroke", setStroke}, {"SetTextColor", setTextColor}, {"SetFontSize", setFontSize}, {"SetFontName", noop},
        {"SetTextVertiacalAlign", setAlign}, {"SetPlaceHolderColor", noop}, {"SetMaxLength", noop},
        {"SetKeyBoardDidHideCallBack", noop}, {"SetKeyBoardDidShowCallBack", noop}, {"SetKeyBoardWillShowCallBack", noop},
        {"DetachWithIME", noop}, {"SetFilterWords", noop},
    };
    for (const R& r : regs) { lua_pushcfunction(L, r.f); lua_setfield(L, -2, r.n); }
    lua_pop(L, 1);
}

}  // namespace

void setFontFile(const std::string& path) { g_font = path; }

void registerText(lua_State* L) {
    addMethods(L);
    auto cls = [&](const char* name, const char* fn, lua_CFunction f) {
        lua_getglobal(L, name);
        if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_newtable(L); }
        lua_pushcfunction(L, f);
        lua_setfield(L, -2, fn);
        lua_setglobal(L, name);
    };
    cls("FontLabel", "CreateWithString", fontLabelCreate);
    cls("RichText", "CreateRichText", richTextCreate);
    cls("EditText", "CreateEditText", editTextCreate);
    lua_getglobal(L, "FontLabel");
    const char* names[] = {"ALIGIN_TOP", "ALIGIN_MIDDLE", "ALIGIN_BOTTOM", "ALIGIN_LEFT", "ALIGIN_CENTER", "ALIGIN_RIGHT"};
    int vals[] = {1, 2, 3, 1, 2, 3};
    for (int i = 0; i < 6; ++i) { lua_pushnumber(L, vals[i]); lua_setfield(L, -2, names[i]); }
    lua_pop(L, 1);
}

}  // namespace ui
}  // namespace mc
