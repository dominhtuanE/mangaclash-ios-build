// ImageLabel / Image9Label / ImageButton and the widget-image cache (ResHelper.*):
// sprites are cut from the game's atlas images (.sdz = 4-byte tag + JPEG/PNG), rectangles come from the Lua ImageData tables.
#include <map>
#include <string>
#include <vector>

#include "ui/UIScale9Sprite.h"
#include "ui/UIImageView.h"

#include "common.h"
#include "lua_node.h"
#include "register.h"
#include "../core/rlog.h"

using namespace cocos2d;

namespace mc {
namespace ui {
namespace {

Sprite* spriteFromArg(lua_State* L, int idx) {          // ImageData table | file path
    if (lua_istable(L, idx)) return makeSprite(readImageData(L, idx));
    ImageData d;
    if (lua_isstring(L, idx)) d.path = lua_tostring(L, idx);
    return makeSprite(d);
}

int newImageData(lua_State* L) { lua_newtable(L); return 1; }

int imageLabelCreate(lua_State* L) { pushNode(L, spriteFromArg(L, 1)); return 1; }

int image9Create(lua_State* L) {                        // Image9Label.CreateWithImageFile(imageData)
    ImageData d = lua_istable(L, 1) ? readImageData(L, 1) : ImageData();
    if (!lua_istable(L, 1) && lua_isstring(L, 1)) d.path = lua_tostring(L, 1);
    Texture2D* tex = d.path.empty() ? nullptr : textureFor(d.path);
    cocos2d::ui::Scale9Sprite* s = cocos2d::ui::Scale9Sprite::create();
    if (tex) {
        Rect r = d.w > 0 && d.h > 0 ? Rect(d.x, d.y, d.w, d.h) : Rect(0, 0, tex->getContentSize().width, tex->getContentSize().height);
        s->initWithSpriteFrame(SpriteFrame::createWithTexture(tex, r));
    }
    s->setAnchorPoint(Vec2(0.5f, 0.5f));
    pushNode(L, s);
    return 1;
}

int imageButtonCreate(lua_State* L) {                   // ImageButton.CreateWithImages(normal [, pressed [, disabled]])
    pushNode(L, spriteFromArg(L, 1));
    return 1;
}

// ---- instance methods (nodes created above; Scale9Sprite derives from Sprite)
Sprite* spriteSelf(lua_State* L) { return dynamic_cast<Sprite*>(toNode(L, 1)); }

int setImage(lua_State* L) {                            // SetImage(imageData | path)
    if (auto* iv = dynamic_cast<cocos2d::ui::ImageView*>(toNode(L, 1))) { if (lua_isstring(L, 2)) iv->loadTexture(lua_tostring(L, 2)); return 0; }
    Sprite* s = spriteSelf(L);
    if (!s) return 0;
    ImageData d;
    if (lua_istable(L, 2)) d = readImageData(L, 2);
    else if (lua_isstring(L, 2)) d.path = lua_tostring(L, 2);
    Texture2D* tex = d.path.empty() ? nullptr : textureFor(d.path);
    if (!tex) return 0;
    Rect r = d.w > 0 && d.h > 0 ? Rect(d.x, d.y, d.w, d.h) : Rect(0, 0, tex->getContentSize().width, tex->getContentSize().height);
    if (auto* s9 = dynamic_cast<cocos2d::ui::Scale9Sprite*>(s)) s9->setSpriteFrame(SpriteFrame::createWithTexture(tex, r));
    else { s->setTexture(tex); s->setTextureRect(r); }
    return 0;
}

int stretchTo(lua_State* L) {                           // StretchTo(w, h): fit the image to this size
    Sprite* s = spriteSelf(L);
    if (!s) return 0;
    float w = static_cast<float>(luaL_checknumber(L, 2)), h = static_cast<float>(luaL_checknumber(L, 3));
    if (auto* s9 = dynamic_cast<cocos2d::ui::Scale9Sprite*>(s)) { s9->setContentSize(Size(w, h)); return 0; }
    Size cs = s->getContentSize();
    if (cs.width > 0 && cs.height > 0) s->setScale(w / cs.width, h / cs.height);
    return 0;
}

int setInnerRect(lua_State* L) {                        // 9-slice centre rectangle
    if (auto* s9 = dynamic_cast<cocos2d::ui::Scale9Sprite*>(toNode(L, 1)))
        s9->setCapInsets(Rect(static_cast<float>(luaL_checknumber(L, 2)), static_cast<float>(luaL_checknumber(L, 3)),
                              static_cast<float>(luaL_checknumber(L, 4)), static_cast<float>(luaL_checknumber(L, 5))));
    return 0;
}

int setFlipX(lua_State* L) { if (Sprite* s = spriteSelf(L)) s->setFlippedX(lua_toboolean(L, 2) != 0); return 0; }
int setNodeColor(lua_State* L) {
    if (Node* n = toNode(L, 1)) {
        Color3B c(static_cast<GLubyte>(luaL_checknumber(L, 2)), static_cast<GLubyte>(luaL_checknumber(L, 3)), static_cast<GLubyte>(luaL_checknumber(L, 4)));
        n->setColor(c);
    }
    return 0;
}
int getWidth(lua_State* L) { Node* n = toNode(L, 1); lua_pushnumber(L, n ? n->getContentSize().width * n->getScaleX() : 0); return 1; }
int getHeight(lua_State* L) { Node* n = toNode(L, 1); lua_pushnumber(L, n ? n->getContentSize().height * n->getScaleY() : 0); return 1; }

// ---- ResHelper: preload the sub-images a widget layout refers to by name ("resource/ui_res/x/name.png")
std::map<std::string, std::vector<std::string>> g_clipKeys;

// ResHelper.IsImageExistInCache(path) / LoadAsyncImageFile(path, callbackId): decode on demand, then report back on the next frame
int noopRes(lua_State*) { return 0; }
int isImageInCache(lua_State* L) {
    std::string p = luaL_checkstring(L, 1);
    lua_pushboolean(L, Director::getInstance()->getTextureCache()->getTextureForKey(p) != nullptr);
    return 1;
}
int loadAsyncImageFile(lua_State* L) {
    std::string p = luaL_checkstring(L, 1);
    int id = lua_isnumber(L, 2) ? static_cast<int>(lua_tonumber(L, 2)) : 0;
    textureFor(p);
    if (id > 0) Director::getInstance()->getScheduler()->performFunctionInCocosThread([id]() { runCallback(id); });
    return 0;
}

int loadImageResource(lua_State* L) { lua_pushboolean(L, textureFor(luaL_checkstring(L, 1)) != nullptr); return 1; }

// LoadWidgetImageResource(image_path, {ImageData{x,y,width,height,clip_image_name}...}): crops each rectangle into its own texture
// registered under clip_image_name, because the layout loader (CocoStudio) looks the pieces up by that file name.
int loadWidgetImageResource(lua_State* L) {
    std::string path = luaL_checkstring(L, 1);
    if (!lua_istable(L, 2)) { lua_pushboolean(L, textureFor(path) != nullptr); return 1; }
    std::string file = resolveResFile(path);
    if (file.empty()) { rlog("[res] widget atlas missing: %s", path.c_str()); lua_pushboolean(L, 0); return 1; }
    Data raw = FileUtils::getInstance()->getDataFromFile(file);
    if (raw.isNull()) { lua_pushboolean(L, 0); return 1; }
    const unsigned char* p = raw.getBytes();
    ssize_t n = raw.getSize();
    if (n > 4 && p[1] == 0x0d && p[2] == 0x0e && p[3] == 0x0f && p[0] >= 1 && p[0] <= 4) { p += 4; n -= 4; }
    Image* src = new (std::nothrow) Image();
    if (!src || !src->initWithImageData(p, n)) { if (src) src->release(); rlog("[res] cannot decode %s", path.c_str()); lua_pushboolean(L, 0); return 1; }
    const unsigned char* px = src->getData();
    int sw = src->getWidth(), sh = src->getHeight();
    int bpp = src->hasAlpha() ? 4 : 3;
    std::vector<std::string>& keys = g_clipKeys[path];
    int made = 0;
    int cnt = static_cast<int>(lua_objlen(L, 2));
    for (int i = 1; i <= cnt; ++i) {
        lua_rawgeti(L, 2, i);
        if (lua_istable(L, -1)) {
            int x = static_cast<int>(fieldNum(L, -1, "x", 0)), y = static_cast<int>(fieldNum(L, -1, "y", 0));
            int w = static_cast<int>(fieldNum(L, -1, "width", 0)), h = static_cast<int>(fieldNum(L, -1, "height", 0));
            std::string clip = fieldStr(L, -1, "clip_image_name");
            if (w > 0 && h > 0 && x >= 0 && y >= 0 && x + w <= sw && y + h <= sh && !clip.empty()) {
                std::vector<unsigned char> buf(static_cast<size_t>(w) * h * 4);
                for (int r = 0; r < h; ++r) {
                    const unsigned char* line = px + (static_cast<size_t>(y + r) * sw + x) * bpp;
                    unsigned char* out = &buf[static_cast<size_t>(r) * w * 4];
                    for (int c = 0; c < w; ++c) {
                        out[c * 4] = line[c * bpp]; out[c * 4 + 1] = line[c * bpp + 1]; out[c * 4 + 2] = line[c * bpp + 2];
                        out[c * 4 + 3] = bpp == 4 ? line[c * bpp + 3] : 255;
                    }
                }
                Image* img = new (std::nothrow) Image();
                if (img && img->initWithRawData(buf.data(), static_cast<ssize_t>(buf.size()), w, h, 8)) {
                    std::string key = FileUtils::getInstance()->fullPathForFilename(clip);
                    Director::getInstance()->getTextureCache()->addImage(img, key);
                    keys.push_back(key);
                    ++made;
                }
                if (img) img->release();
            }
        }
        lua_pop(L, 1);
    }
    src->release();
    rlog("[res] widget atlas %s: %d pieces", path.c_str(), made);
    lua_pushboolean(L, 1);
    return 1;
}

int releaseWidgetImageResource(lua_State* L) {
    std::string path = luaL_checkstring(L, 1);
    auto it = g_clipKeys.find(path);
    if (it != g_clipKeys.end()) {
        for (auto& k : it->second) Director::getInstance()->getTextureCache()->removeTextureForKey(k);
        g_clipKeys.erase(it);
    }
    lua_pushboolean(L, 1);
    return 1;
}

}  // namespace

void registerImages(lua_State* L) {
    lua_pushcfunction(L, newImageData);
    lua_setglobal(L, "ImageData");

    lua_getglobal(L, "GameLib"); lua_getfield(L, -1, "ResHelper"); lua_remove(L, -2);   // global ResHelper only exists after main.lua runs
    if (lua_istable(L, -1)) {
        lua_pushcfunction(L, loadImageResource); lua_setfield(L, -2, "LoadImageResource");
        for (const char* n : {"ReleaseUnusedImageExcept", "ReleaseImageResource", "PrintAllTextureCache"}) { lua_pushcfunction(L, noopRes); lua_setfield(L, -2, n); }
        lua_pushcfunction(L, isImageInCache); lua_setfield(L, -2, "IsImageExistInCache");
        lua_pushcfunction(L, loadAsyncImageFile); lua_setfield(L, -2, "LoadAsyncImageFile");
        lua_pushcfunction(L, loadWidgetImageResource); lua_setfield(L, -2, "LoadWidgetImageResource");
        lua_pushcfunction(L, releaseWidgetImageResource); lua_setfield(L, -2, "ReleaseWidgetImageResource");
    }
    lua_pop(L, 1);

    auto cls = [&](const char* name, const char* fn, lua_CFunction f) {
        lua_getglobal(L, name);
        if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_newtable(L); }
        lua_pushcfunction(L, f);
        lua_setfield(L, -2, fn);
        lua_setglobal(L, name);
    };
    cls("ImageLabel", "CreateWithImageFile", imageLabelCreate);
    cls("Label", "CreateWithImageFile", imageLabelCreate);
    cls("Image9Label", "CreateWithImageFile", image9Create);
    cls("ImageButton", "CreateWithImages", imageButtonCreate);

    luaL_getmetatable(L, kNodeMeta);
    struct R { const char* n; lua_CFunction f; };
    static const R regs[] = {
        {"SetImage", setImage}, {"SetImageData", setImage}, {"StretchTo", stretchTo}, {"SetInnerRect", setInnerRect},
        {"SetFlipX", setFlipX}, {"SetColor", setNodeColor}, {"GetWidth", getWidth}, {"GetHeight", getHeight},
    };
    for (const R& r : regs) { lua_pushcfunction(L, r.f); lua_setfield(L, -2, r.n); }
    lua_pop(L, 1);
}

}  // namespace ui
}  // namespace mc
