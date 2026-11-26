// CocoStudio layouts (.ExportJson): UITouchGroupEx hosts the widget tree, WidgetUnity finds widgets by name,
// ResHelper.CreateWidgetFromJsonFile builds the tree with cocostudio::GUIReader.
#include "ui/CocosGUI.h"
#include "cocostudio/CocoStudio.h"

#include "common.h"
#include "lua_node.h"
#include "register.h"
#include "../core/rlog.h"

using namespace cocos2d;
using cocos2d::ui::Widget;

namespace mc {
namespace ui {
namespace {

int touchGroupCreate(lua_State* L) {                     // UITouchGroupEx.Create()
    Node* n = Node::create();
    n->setAnchorPoint(Vec2::ZERO);
    pushNode(L, n);
    return 1;
}

int createWidgetFromJson(lua_State* L) {                 // ResHelper.CreateWidgetFromJsonFile(path)
    std::string path = luaL_checkstring(L, 1);
    Widget* w = nullptr;
    rlogTrace("[widget] load %s", path.c_str());
    if (FileUtils::getInstance()->isFileExist(path)) {
        w = cocostudio::GUIReader::getInstance()->widgetFromJsonFile(path.c_str());
    } else {
        rlog("[widget] layout missing: %s", path.c_str());
    }
    if (!w) { w = cocos2d::ui::Layout::create(); }
    pushNode(L, w);
    return 1;
}

int getWidgetByName(lua_State* L) {                      // WidgetUnity.GetWidgetByName(root, name)
    Node* root = toNode(L, 1);
    const char* name = luaL_checkstring(L, 2);
    Widget* w = nullptr;
    if (auto* rw = dynamic_cast<Widget*>(root)) w = cocos2d::ui::Helper::seekWidgetByName(rw, name);
    if (!w) {
        static std::set<std::string> seen;
        if (seen.insert(name).second) rlog("[widget] not found: %s", name);
        lua_pushnil(L);
        return 1;
    }
    pushNode(L, w);
    return 1;
}

int addWidget(lua_State* L) {                            // layer:AddWidget(widget) / widget:AddWidget(child)
    Node* parent = toNode(L, 1);
    Node* child = toNode(L, 2);
    if (parent && child && !child->getParent()) parent->addChild(child);
    return 0;
}
int removeWidget(lua_State* L) {
    Node* parent = toNode(L, 1);
    Node* child = toNode(L, 2);
    if (parent && child && child->getParent() == parent) parent->removeChild(child);
    return 0;
}

// ---- widget instance methods
int setPercent(lua_State* L) {
    float p = static_cast<float>(luaL_checknumber(L, 2));
    Node* n = toNode(L, 1);
    if (auto* b = dynamic_cast<cocos2d::ui::LoadingBar*>(n)) b->setPercent(p);
    else if (auto* s = dynamic_cast<cocos2d::ui::Slider*>(n)) s->setPercent(static_cast<int>(p));
    else if (auto* t = dynamic_cast<ProgressTimer*>(n)) t->setPercentage(p);
    return 0;
}
int getPercent(lua_State* L) {
    Node* n = toNode(L, 1);
    float p = 0;
    if (auto* b = dynamic_cast<cocos2d::ui::LoadingBar*>(n)) p = b->getPercent();
    else if (auto* s = dynamic_cast<cocos2d::ui::Slider*>(n)) p = static_cast<float>(s->getPercent());
    else if (auto* t = dynamic_cast<ProgressTimer*>(n)) p = t->getPercentage();
    lua_pushnumber(L, p);
    return 1;
}
int setSelectedState(lua_State* L) { if (auto* c = dynamic_cast<cocos2d::ui::CheckBox*>(toNode(L, 1))) c->setSelected(lua_toboolean(L, 2) != 0); return 0; }
int getSelectedState(lua_State* L) {
    auto* c = dynamic_cast<cocos2d::ui::CheckBox*>(toNode(L, 1));
    lua_pushboolean(L, c && c->isSelected());
    return 1;
}
int setWidgetImage(lua_State* L) {                       // ImageView / Button: SetImage(path)
    Node* n = toNode(L, 1);
    const char* p = lua_isstring(L, 2) ? lua_tostring(L, 2) : nullptr;
    if (!p) return 0;
    if (auto* iv = dynamic_cast<cocos2d::ui::ImageView*>(n)) { iv->loadTexture(p); return 1; }
    return 0;
}
int setInnerContainerSize(lua_State* L) {
    if (auto* sv = dynamic_cast<cocos2d::ui::ScrollView*>(toNode(L, 1)))
        sv->setInnerContainerSize(Size(static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3))));
    return 0;
}
int scrollToBottom(lua_State* L) { if (auto* sv = dynamic_cast<cocos2d::ui::ScrollView*>(toNode(L, 1))) sv->jumpToBottom(); return 0; }
int scrollContentToTop(lua_State* L) { if (auto* sv = dynamic_cast<cocos2d::ui::ScrollView*>(toNode(L, 1))) sv->jumpToTop(); return 0; }
int setClipping(lua_State* L) { if (auto* l = dynamic_cast<cocos2d::ui::Layout*>(toNode(L, 1))) l->setClippingEnabled(lua_toboolean(L, 2) != 0); return 0; }
int noop(lua_State*) { return 0; }

}  // namespace

void registerWidgets(lua_State* L) {
    auto cls = [&](const char* name, const char* fn, lua_CFunction f) {
        lua_getglobal(L, name);
        if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_newtable(L); }
        lua_pushcfunction(L, f);
        lua_setfield(L, -2, fn);
        lua_setglobal(L, name);
    };
    cls("UITouchGroupEx", "Create", touchGroupCreate);
    cls("WidgetUnity", "GetWidgetByName", getWidgetByName);
    lua_getglobal(L, "GameLib"); lua_getfield(L, -1, "ResHelper"); lua_remove(L, -2);   // global ResHelper only exists after main.lua runs
    if (lua_istable(L, -1)) { lua_pushcfunction(L, createWidgetFromJson); lua_setfield(L, -2, "CreateWidgetFromJsonFile"); }
    lua_pop(L, 1);

    luaL_getmetatable(L, kNodeMeta);
    struct R { const char* n; lua_CFunction f; };
    static const R regs[] = {
        {"AddWidget", addWidget}, {"RemoveWidget", removeWidget}, {"SetPercent", setPercent}, {"GetPercent", getPercent},
        {"SetSelectedState", setSelectedState}, {"GetSelectedState", getSelectedState}, {"SetInnerContainerSize", setInnerContainerSize},
        {"ScrollToBottom", scrollToBottom}, {"ScrollContentToTop", scrollContentToTop}, {"SetClippingEnable", setClipping},
        {"SetBrightStyle", noop}, {"SetScrollDirection", noop},
    };
    for (const R& r : regs) { lua_pushcfunction(L, r.f); lua_setfield(L, -2, r.n); }
    lua_pop(L, 1);
    (void)setWidgetImage;
}

}  // namespace ui
}  // namespace mc
