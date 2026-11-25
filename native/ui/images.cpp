// ImageData / ImageLabel / Image9Label / ImageButton: sprites cut from the game's atlas images.
// Resource files (.sdz) are a 4-byte tag + JPEG/PNG (see native-spec/resdecode.py); atlas rectangles come from Lua (ImageData).
#include <map>
#include <string>

#include "ui/UIScale9Sprite.h"

#include "lua_node.h"
#include "../core/host.h"

using namespace cocos2d;

namespace mc {
namespace ui {
namespace {

std::string g_resRoot;                         // directory that holds resource/... (set by the platform layer)

Data readResource(const std::string& path) {
    Data d = FileUtils::getInstance()->getDataFromFile(g_resRoot + path);
    return d;
}

// Strips the optional 4-byte container tag (0N 0d 0e 0f, N = 1..4) from .sdz/.ssdz data.
bool decodeImageData(const Data& in, Data& out) {
    const unsigned char* p = in.getBytes();
    ssize_t n = in.getSize();
    if (n > 4 && p[1] == 0x0d && p[2] == 0x0e && p[3] == 0x0f && p[0] >= 1 && p[0] <= 4) { p += 4; n -= 4; }
    unsigned char* copy = static_cast<unsigned char*>(malloc(static_cast<size_t>(n)));
    if (!copy) return false;
    memcpy(copy, p, static_cast<size_t>(n));
    out.fastSet(copy, n);
    return true;
}

std::map<std::string, Texture2D*> g_textures;

Texture2D* textureFor(const std::string& path) {
    auto it = g_textures.find(path);
    if (it != g_textures.end()) return it->second;
    Texture2D* tex = nullptr;
    Data raw = readResource(path), img;
    if (!raw.isNull() && decodeImageData(raw, img)) {
        Image* image = new (std::nothrow) Image();
        if (image && image->initWithImageData(img.getBytes(), img.getSize())) {
            tex = Director::getInstance()->getTextureCache()->addImage(image, path);
        }
        if (image) image->release();
    }
    g_textures[path] = tex;
    return tex;
}

struct ImageData {
    std::string path;
    float x = 0, y = 0, w = 0, h = 0, scale = 1, ox = 0, oy = 0;
};

ImageData readImageData(lua_State* L, int idx) {
    ImageData d;
    auto num = [&](const char* k, float def) { lua_getfield(L, idx, k); float v = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : def; lua_pop(L, 1); return v; };
    lua_getfield(L, idx, "image_path");
    if (lua_isstring(L, -1)) d.path = lua_tostring(L, -1);
    lua_pop(L, 1);
    d.x = num("x", 0); d.y = num("y", 0); d.w = num("width", 0); d.h = num("height", 0);
    d.scale = num("scale_size", 1); d.ox = num("offset_x", 0); d.oy = num("offset_y", 0);
    return d;
}

Sprite* makeSprite(const ImageData& d) {
    Texture2D* tex = textureFor(d.path);
    if (!tex) return Sprite::create();
    Rect r = d.w > 0 && d.h > 0 ? Rect(d.x, d.y, d.w, d.h) : Rect(0, 0, tex->getContentSize().width, tex->getContentSize().height);
    Sprite* s = Sprite::createWithTexture(tex, r);
    s->setAnchorPoint(Vec2(0.5f, 0.5f));
    if (d.scale != 1.0f) s->setScale(d.scale);
    return s;
}

// ImageData() constructor: plain table with the fields Lua fills in.
int newImageData(lua_State* L) { lua_newtable(L); return 1; }

int imageLabelCreate(lua_State* L) {           // ImageLabel.CreateWithImageFile(imageData | filePath)
    Sprite* s;
    if (lua_istable(L, 1)) s = makeSprite(readImageData(L, 1));
    else {
        ImageData d;
        d.path = luaL_checkstring(L, 1);
        s = makeSprite(d);
    }
    pushNode(L, s);
    return 1;
}

int image9Create(lua_State* L) {               // Image9Label.CreateWithImageFile(imageData)
    ImageData d = readImageData(L, 1);
    Texture2D* tex = textureFor(d.path);
    cocos2d::ui::Scale9Sprite* s = cocos2d::ui::Scale9Sprite::create();
    if (tex) {
        Rect r = d.w > 0 && d.h > 0 ? Rect(d.x, d.y, d.w, d.h) : Rect(0, 0, tex->getContentSize().width, tex->getContentSize().height);
        s->initWithSpriteFrame(SpriteFrame::createWithTexture(tex, r));
    }
    pushNode(L, s);
    return 1;
}

int imageButtonCreate(lua_State* L) {          // ImageButton.CreateWithImages(normal [, pressed [, disabled]])
    ImageData n = readImageData(L, 1);
    Sprite* s = makeSprite(n);
    pushNode(L, s);                            // visual only for now; touch handling is added with the input layer
    return 1;
}

int loadWidgetImageResource(lua_State* L) { lua_pushboolean(L, textureFor(luaL_checkstring(L, 1)) != nullptr); return 1; }
int releaseWidgetImageResource(lua_State* L) {
    std::string p = luaL_checkstring(L, 1);
    auto it = g_textures.find(p);
    if (it != g_textures.end()) { Director::getInstance()->getTextureCache()->removeTextureForKey(p); g_textures.erase(it); }
    lua_pushboolean(L, 1);
    return 1;
}

}  // namespace

void setResourceRoot(const std::string& root) { g_resRoot = root; }

void registerImages(lua_State* L) {
    lua_pushcfunction(L, newImageData);
    lua_setglobal(L, "ImageData");

    lua_getglobal(L, "ResHelper");
    if (lua_istable(L, -1)) {
        lua_pushcfunction(L, loadWidgetImageResource);
        lua_setfield(L, -2, "LoadWidgetImageResource");
        lua_pushcfunction(L, releaseWidgetImageResource);
        lua_setfield(L, -2, "ReleaseWidgetImageResource");
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
    cls("Image9Label", "CreateWithImageFile", image9Create);
    cls("ImageButton", "CreateWithImages", imageButtonCreate);
}

}  // namespace ui
}  // namespace mc
