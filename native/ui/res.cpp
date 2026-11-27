// Resource decoding + the atlas table (ResCompose): "Ui_Res_X" modules are Lua .cnf files that describe rectangles in one big image.
#include <map>
#include <set>
#include <memory>
#include <thread>
#include <vector>

#include "common.h"
#include "register.h"
#include "../core/rlog.h"

using namespace cocos2d;

namespace mc {
namespace ui {

lua_State* g_L = nullptr;

namespace {

std::string g_root;
std::string g_dl;                                          // downloaded files: <Documents>/resource/                                       // ".../MangaClash.app/resource/"
std::map<std::string, Texture2D*> g_tex;
std::set<std::string> g_loggedMissing;
std::set<std::string> g_loadedModules;

void logOnce(const std::string& what) {
    if (g_loggedMissing.insert(what).second) rlog("[res] %s", what.c_str());
}

std::string absPath(const std::string& p) {
    if (!p.empty() && p[0] == '/') return p;
    if (p.compare(0, 9, "resource/") == 0) return g_root + p.substr(9);
    return g_root + p;
}

std::string withExt(const std::string& p, const char* ext) {
    size_t dot = p.rfind('.');
    size_t slash = p.rfind('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return p + ext;
    return p.substr(0, dot) + ext;
}

bool exists(const std::string& p) { return FileUtils::getInstance()->isFileExist(p); }

// Strips the optional 4-byte container tag (0N 0d 0e 0f) from .sdz/.ssdz data.
Data decode(const Data& in) {
    Data out;
    const unsigned char* p = in.getBytes();
    ssize_t n = in.getSize();
    if (n > 4 && p[1] == 0x0d && p[2] == 0x0e && p[3] == 0x0f && p[0] >= 1 && p[0] <= 4) { p += 4; n -= 4; }
    unsigned char* copy = static_cast<unsigned char*>(malloc(static_cast<size_t>(n)));
    if (!copy) return out;
    memcpy(copy, p, static_cast<size_t>(n));
    out.fastSet(copy, n);
    return out;
}

std::string langDir() {
    std::string lang = "vn";
    lua_State* L = g_L;
    if (L) {
        lua_getglobal(L, "Config");
        if (lua_istable(L, -1)) {
            lua_getfield(L, -1, "Language");
            if (lua_isstring(L, -1)) lang = lua_tostring(L, -1);
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    return lang;
}

std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c))); return s; }
std::string noUnderscore(const std::string& s) { std::string o; for (char c : s) if (c != '_') o += c; return o; }

std::string moduleFile(const std::string& module) {
    std::string tail = module.compare(0, 7, "Ui_Res_") == 0 ? module.substr(7) : module;
    std::vector<std::string> cands;
    if (tail.compare(0, 9, "Language_") == 0) {
        std::string t = lower(tail.substr(9));
        std::vector<std::string> langs = {langDir(), "vn", "en"};
        for (const std::string& lg : langs) {
            cands.push_back("ui_res/language/" + lg + "/" + t + ".cnf");
            cands.push_back("ui_res/language/" + lg + "/" + noUnderscore(t) + ".cnf");
        }
    } else {
        std::string t = lower(tail);
        std::vector<std::string> vs = {t, noUnderscore(t)};
        for (const std::string& v : vs) {
            cands.push_back("ui_res/" + v + ".cnf");
            cands.push_back("ui_res/singlepic/" + v + ".cnf");
        }
    }
    for (auto& c : cands) if (exists(g_root + c)) return g_root + c;
    return "";
}

}  // namespace

void setResourceRoot(const std::string& root) { g_root = root; }
void setDownloadRoot(const std::string& root) { g_dl = root; }

std::string resolveResFile(const std::string& path) {
    std::string rel = path.compare(0, 9, "resource/") == 0 ? path.substr(9) : path;
    std::string roots[2] = {g_dl, g_root};                // a downloaded copy replaces the shipped one
    for (const std::string& root : roots) {
        if (root.empty()) continue;
        std::string a = root + rel;
        if (exists(a)) return a;
        for (const char* e : {".sdz", ".ssdz", ".png", ".jpg"}) { std::string c = withExt(a, e); if (exists(c)) return c; }
    }
    return "";
}

// .sdz tag 2 = JPEG + gzip(one alpha byte per pixel) (+ 4 bytes). Without the alpha every icon gets a white/black box.
bool initJpegAlpha(Image* out, const Data& raw) {
    const unsigned char* p = raw.getBytes();
    size_t n = static_cast<size_t>(raw.getSize());
    if (n < 16 || p[0] != 2 || p[1] != 0x0d || p[2] != 0x0e || p[3] != 0x0f) return false;
    size_t gz = 0;
    for (size_t i = 6; i + 3 < n; ++i)
        if (p[i - 2] == 0xff && p[i - 1] == 0xd9 && p[i] == 0x1f && p[i + 1] == 0x8b && p[i + 2] == 0x08) { gz = i; break; }
    if (!gz) return false;
    std::string all(reinterpret_cast<const char*>(p), n), alpha;
    size_t end = 0;
    if (!gzipPart(all, gz, alpha, end)) return false;
    Image* jpg = new (std::nothrow) Image();
    bool ok = false;
    if (jpg && jpg->initWithImageData(p + 4, static_cast<ssize_t>(gz - 4))) {
        int w = jpg->getWidth(), h = jpg->getHeight();
        size_t pixels = static_cast<size_t>(w) * static_cast<size_t>(h);
        size_t bpp = pixels ? static_cast<size_t>(jpg->getDataLen()) / pixels : 0;
        if (pixels && alpha.size() == pixels && (bpp == 1 || bpp >= 3)) {
            std::string rgba(pixels * 4, static_cast<char>(0));
            const unsigned char* src = jpg->getData();
            for (size_t k = 0; k < pixels; ++k) {
                const unsigned char* s = src + k * bpp;
                rgba[k * 4] = static_cast<char>(s[0]);
                rgba[k * 4 + 1] = static_cast<char>(bpp >= 3 ? s[1] : s[0]);
                rgba[k * 4 + 2] = static_cast<char>(bpp >= 3 ? s[2] : s[0]);
                rgba[k * 4 + 3] = alpha[k];
            }
            ok = out->initWithRawData(reinterpret_cast<const unsigned char*>(rgba.data()), static_cast<ssize_t>(rgba.size()), w, h, 8, false);
        }
    }
    if (jpg) jpg->release();
    return ok;
}

bool loadImage(Image* out, const Data& raw) {
    if (initJpegAlpha(out, raw)) return true;
    Data plain = decode(raw);
    return !plain.isNull() && out->initWithImageData(plain.getBytes(), plain.getSize());
}

Texture2D* textureAs(const std::string& key, const std::string& path) {
    std::string file = resolveResFile(path);
    if (file.empty()) { logOnce("image missing: " + path); return nullptr; }
    Data raw = FileUtils::getInstance()->getDataFromFile(file);
    if (raw.isNull()) { logOnce("image unreadable: " + file); return nullptr; }
    Image* image = new (std::nothrow) Image();
    Texture2D* tex = nullptr;
    if (image && loadImage(image, raw)) tex = Director::getInstance()->getTextureCache()->addImage(image, key);
    else logOnce("image decode failed: " + path);
    if (image) image->release();
    return tex;
}

Texture2D* textureFor(const std::string& path) {
    auto it = g_tex.find(path);
    if (it != g_tex.end()) return it->second;
    Texture2D* tex = textureAs(path, path);
    g_tex[path] = tex;
    return tex;
}

void loadTextureAsync(const std::string& path, std::function<void()> done) {
    auto* director = Director::getInstance();
    std::string file = g_tex.count(path) ? "" : resolveResFile(path);
    Data raw = file.empty() ? Data() : FileUtils::getInstance()->getDataFromFile(file);
    if (raw.isNull()) {                                      // cached already, or missing: nothing to decode
        textureFor(path);
        director->getScheduler()->performFunctionInCocosThread(done);
        return;
    }
    auto bytes = std::make_shared<Data>(std::move(raw));
    std::thread([path, bytes, done, director]() {
        Image* image = new (std::nothrow) Image();
        bool ok = image && loadImage(image, *bytes);
        director->getScheduler()->performFunctionInCocosThread([path, image, ok, done]() {
            if (ok && !g_tex.count(path)) g_tex[path] = Director::getInstance()->getTextureCache()->addImage(image, path);
            else if (!ok) logOnce("image decode failed: " + path);
            if (image) image->release();
            done();
        });
    }).detach();
}

float fieldNum(lua_State* L, int idx, const char* key, float def) {
    lua_getfield(L, idx, key);
    float v = lua_isnumber(L, -1) ? static_cast<float>(lua_tonumber(L, -1)) : def;
    lua_pop(L, 1);
    return v;
}
std::string fieldStr(lua_State* L, int idx, const char* key) {
    lua_getfield(L, idx, key);
    std::string s = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    return s;
}

ImageData readImageData(lua_State* L, int idx) {
    ImageData d;
    if (idx < 0) idx = lua_gettop(L) + idx + 1;
    if (!lua_istable(L, idx)) return d;
    d.path = fieldStr(L, idx, "image_path");
    d.x = fieldNum(L, idx, "x", 0); d.y = fieldNum(L, idx, "y", 0);
    d.w = fieldNum(L, idx, "width", 0); d.h = fieldNum(L, idx, "height", 0);
    d.scale = fieldNum(L, idx, "scale_size", 1); d.ox = fieldNum(L, idx, "offset_x", 0); d.oy = fieldNum(L, idx, "offset_y", 0);
    return d;
}

void applyImage(Sprite* s, const ImageData& d) {
    Texture2D* tex = d.path.empty() ? nullptr : textureFor(d.path);
    if (!tex) return;
    Rect r = d.w > 0 && d.h > 0 ? Rect(d.x, d.y, d.w, d.h) : Rect(0, 0, tex->getContentSize().width, tex->getContentSize().height);
    s->setTexture(tex);
    s->setTextureRect(r);
}

Sprite* makeSprite(const ImageData& d) {
    Sprite* s = Sprite::create();
    applyImage(s, d);
    s->setAnchorPoint(Vec2(0.5f, 0.5f));
    return s;
}

void runCallback(int id, int nargs, double a, double b, double c) {
    lua_State* L = g_L;
    if (!L || id <= 0) return;
    lua_getglobal(L, "RunCallBackFunc");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushnumber(L, id);
    if (nargs > 0) lua_pushnumber(L, a);
    if (nargs > 1) lua_pushnumber(L, b);
    if (nargs > 2) lua_pushnumber(L, c);
    if (lua_pcall(L, 1 + nargs, 0, 0) != 0) {
        rlog("[lua] callback %d: %s", id, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

void runAnimCallback(int id, const std::string& anim, int type, const std::string& event, int i, double f, const std::string& s) {
    lua_State* L = g_L;
    if (!L || id <= 0) return;
    lua_getglobal(L, "RunCallBackFunc");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushnumber(L, id);
    lua_pushstring(L, anim.c_str());
    lua_pushnumber(L, type);
    lua_pushstring(L, event.c_str());
    lua_pushnumber(L, i);
    lua_pushnumber(L, f);
    lua_pushstring(L, s.c_str());
    if (lua_pcall(L, 7, 0, 0) != 0) {
        rlog("[lua] animation callback %d: %s", id, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

void registerRes(lua_State* L) { g_L = L; }

}  // namespace ui
}  // namespace mc
