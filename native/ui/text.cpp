// FontLabel / RichText / EditText on top of cocos2d::Label and ui::TextField. Font: zhunyuan.ttf shipped beside the resources.
#include "ui/UITextField.h"
#include "ui/UIText.h"
#include "ui/UIButton.h"

#include <cstdlib>

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
    if (auto* t = dynamic_cast<cocos2d::ui::Text*>(n)) return dynamic_cast<Label*>(t->getVirtualRenderer());
    return dynamic_cast<Label*>(n);
}

// RichText markup: <COLOR VALUE={AARRGGBB,...} OUTLINE=AARRGGBB>text</COLOR>, <BR>, other tags dropped. Only the first colour is used.
bool hexColor(const std::string& s, size_t pos, Color4B& out) {
    if (pos + 8 > s.size()) return false;
    unsigned v = static_cast<unsigned>(std::strtoul(s.substr(pos, 8).c_str(), nullptr, 16));
    out = Color4B((v >> 16) & 255, (v >> 8) & 255, v & 255, (v >> 24) & 255);
    return true;
}
std::string stripMarkup(const std::string& in, bool& hasColor, Color4B& color, bool& hasOutline, Color4B& outline) {
    std::string out;
    hasColor = hasOutline = false;
    for (size_t i = 0; i < in.size();) {
        if (in[i] != '<') { out += in[i++]; continue; }
        size_t e = in.find('>', i);
        if (e == std::string::npos) { out += in.substr(i); break; }
        std::string tag = in.substr(i + 1, e - i - 1);
        if (tag.compare(0, 5, "COLOR") == 0) {
            size_t v = tag.find("VALUE={");
            if (v != std::string::npos && !hasColor) hasColor = hexColor(tag, v + 7, color);
            size_t o = tag.find("OUTLINE=");
            if (o != std::string::npos && !hasOutline) hasOutline = hexColor(tag, o + 8, outline);
        } else if (tag == "BR" || tag == "BR/") out += '\n';
        i = e + 1;
    }
    return out;
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
    if (auto* tx = dynamic_cast<cocos2d::ui::Text*>(toNode(L, 1))) { tx->setString(t ? t : ""); return 0; }
    if (auto* bt = dynamic_cast<cocos2d::ui::Button*>(toNode(L, 1))) { bt->setTitleText(t ? t : ""); return 0; }
    if (Label* l = labelOf(L)) {
        std::string raw = t ? t : "";
        if (raw.find('<') != std::string::npos) {
            bool hc, ho; Color4B c, o;
            std::string plain = stripMarkup(raw, hc, c, ho, o);
            l->setString(plain);
            if (hc) l->setTextColor(Color4B(c.r, c.g, c.b, 255));
            if (ho) l->enableOutline(Color4B(o.r, o.g, o.b, 255), 2);
        } else l->setString(raw);
    }
    else if (auto* f = dynamic_cast<cocos2d::ui::TextField*>(toNode(L, 1))) f->setString(t ? t : "");
    return 0;
}
int getText(lua_State* L) {
    if (Label* l = labelOf(L)) lua_pushstring(L, l->getString().c_str());
    else if (auto* f = dynamic_cast<cocos2d::ui::TextField*>(toNode(L, 1))) lua_pushstring(L, f->getString().c_str());
    else lua_pushstring(L, "");
    return 1;
}
int setPasswordMode(lua_State* L) {                      // EditText:SetPasswordMode(true) -> show dots
    if (auto* f = dynamic_cast<cocos2d::ui::TextField*>(toNode(L, 1))) { f->setPasswordEnabled(lua_toboolean(L, 2) != 0); f->setPasswordStyleText("*"); }
    return 0;
}
int setTextWH(lua_State* L) {                            // SetTextWidthAndHeight(w, h) / SetTextWidth(w) / SetTextHeight(h)
    if (Label* l = labelOf(L)) {
        float w = static_cast<float>(luaL_optnumber(L, 2, 0)), h = static_cast<float>(luaL_optnumber(L, 3, 0));
        l->setDimensions(w, h);
    }
    return 0;
}
int setTextW(lua_State* L) { if (Label* l = labelOf(L)) l->setDimensions(static_cast<float>(luaL_optnumber(L, 2, 0)), l->getDimensions().height); return 0; }
int setTextH(lua_State* L) { if (Label* l = labelOf(L)) l->setDimensions(l->getDimensions().width, static_cast<float>(luaL_optnumber(L, 2, 0))); return 0; }
int setFontSpacing(lua_State* L) { if (Label* l = labelOf(L)) l->setAdditionalKerning(static_cast<float>(luaL_optnumber(L, 2, 0))); return 0; }
int setButtonTextColor(lua_State* L) {
    if (auto* bt = dynamic_cast<cocos2d::ui::Button*>(toNode(L, 1)))
        bt->setTitleColor(Color3B(static_cast<GLubyte>(luaL_checknumber(L, 2)), static_cast<GLubyte>(luaL_checknumber(L, 3)), static_cast<GLubyte>(luaL_checknumber(L, 4))));
    return 0;
}
int removeNodeChild(lua_State* L) { Node* n = toNode(L, 1); Node* c = toNode(L, 2); if (n && c) n->removeChild(c, true); return 0; }
int setStroke(lua_State* L) { if (Label* l = labelOf(L)) applyStroke(l, lua_toboolean(L, 2) != 0); return 0; }
int setStrokeColor(lua_State* L) {
    if (Label* l = labelOf(L))
        l->enableOutline(Color4B(static_cast<GLubyte>(luaL_checknumber(L, 2)), static_cast<GLubyte>(luaL_checknumber(L, 3)),
                                 static_cast<GLubyte>(luaL_checknumber(L, 4)), 255), 2);
    return 0;
}
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
        {"SetStroke", setStroke}, {"SetStrokeColor", setStrokeColor}, {"SetTextColor", setTextColor}, {"SetFontSize", setFontSize}, {"SetFontName", noop},
        {"SetTextVertiacalAlign", setAlign}, {"SetPlaceHolderColor", noop}, {"SetMaxLength", noop},
        {"SetKeyBoardDidHideCallBack", noop}, {"SetKeyBoardDidShowCallBack", noop}, {"SetKeyBoardWillShowCallBack", noop},
        {"DetachWithIME", noop}, {"SetTextWidthAndHeight", setTextWH}, {"SetTextWidth", setTextW}, {"SetTextHeight", setTextH}, {"SetFontSpacing", setFontSpacing},
        {"SetButtonTextColor", setButtonTextColor}, {"RemoveNode", removeNodeChild}, {"SetShadowColor", noop}, {"SetUpdate", noop},
        {"SetMansualUpdate", noop}, {"Destroy", noop}, {"SetTemplate", noop}, {"SetDisabledImage", noop}, {"ResetSize", noop},
        {"DrawSolidTriangle", noop}, {"SetPlayeEndListener", noop}, {"SetPasswordMode", setPasswordMode}, {"SetFilterWords", noop},
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
