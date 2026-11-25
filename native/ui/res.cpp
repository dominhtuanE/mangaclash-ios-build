// Resource decoding + the atlas table (ResCompose): "Ui_Res_X" modules are Lua .cnf files that describe rectangles in one big image.
#include <map>
#include <set>
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
    std::string roots[2] = {g_root, g_dl};
    for (const std::string& root : roots) {
        if (root.empty()) continue;
        std::string a = root + rel;
        if (exists(a)) return a;
        for (const char* e : {".sdz", ".ssdz", ".png", ".jpg"}) { std::string c = withExt(a, e); if (exists(c)) return c; }
    }
    return "";
}

Texture2D* textureAs(const std::string& key, const std::string& path) {
    std::string file = resolveResFile(path);
    if (file.empty()) { logOnce("image missing: " + path); return nullptr; }
    Data raw = FileUtils::getInstance()->getDataFromFile(file);
    if (raw.isNull()) { logOnce("image unreadable: " + file); return nullptr; }
    Data img = decode(raw);
    Image* image = new (std::nothrow) Image();
    Texture2D* tex = nullptr;
    if (image && !img.isNull() && image->initWithImageData(img.getBytes(), img.getSize())) tex = Director::getInstance()->getTextureCache()->addImage(image, key);
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

void runCallback(int id, int nargs, double a, double b) {
    lua_State* L = g_L;
    if (!L || id <= 0) return;
    lua_getglobal(L, "RunCallBackFunc");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushnumber(L, id);
    if (nargs > 0) lua_pushnumber(L, a);
    if (nargs > 1) lua_pushnumber(L, b);
    if (lua_pcall(L, 1 + nargs, 0, 0) != 0) {
        rlog("[lua] callback %d: %s", id, lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

void registerRes(lua_State* L) { g_L = L; }

}  // namespace ui
}  // namespace mc
