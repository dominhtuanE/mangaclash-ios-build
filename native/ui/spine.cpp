// SkeletonAnimation / SpineEffect on top of cocos2d-x' spine runtime (editor-support/spine).
// .ssdz = 04 0d 0e 0f + PNG (atlas page) + gzip(Spine JSON) + <u32 jsonLen, u32 gzLen> + gzip(atlas text)   (see server/platform.py _alias_spine_anims)
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "common.h"
#include "lua_node.h"
#include "../core/rlog.h"
#include "../third_party/miniz/miniz.h"
#include "spine/spine-cocos2dx.h"

using namespace cocos2d;

namespace mc {
namespace ui {
namespace {

// gzip member at in[pos..] -> out; end = offset just after its 8-byte trailer
bool gunzip(const std::string& in, size_t pos, std::string& out, size_t& end) {
    if (pos + 18 > in.size() || static_cast<unsigned char>(in[pos]) != 0x1f || static_cast<unsigned char>(in[pos + 1]) != 0x8b) return false;
    size_t p = pos + 10;
    unsigned char flg = static_cast<unsigned char>(in[pos + 3]);
    if (flg & 4) { if (p + 2 > in.size()) return false; p += 2 + (static_cast<unsigned char>(in[p]) | (static_cast<unsigned char>(in[p + 1]) << 8)); }
    if (flg & 8) { while (p < in.size() && in[p]) ++p; ++p; }
    if (flg & 16) { while (p < in.size() && in[p]) ++p; ++p; }
    if (flg & 2) p += 2;
    if (p >= in.size()) return false;
    mz_stream z;
    std::memset(&z, 0, sizeof z);
    if (mz_inflateInit2(&z, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) return false;
    z.next_in = reinterpret_cast<const unsigned char*>(in.data()) + p;
    z.avail_in = static_cast<unsigned>(in.size() - p);
    unsigned char buf[16384];
    int r;
    do {
        z.next_out = buf;
        z.avail_out = sizeof buf;
        r = mz_inflate(&z, MZ_NO_FLUSH);
        out.append(reinterpret_cast<char*>(buf), sizeof buf - z.avail_out);
    } while (r == MZ_OK);
    size_t used = in.size() - p - z.avail_in;
    mz_inflateEnd(&z);
    if (r != MZ_STREAM_END) return false;
    end = p + used + 8;
    return true;
}

struct Skel { spAtlas* atlas = nullptr; spSkeletonData* data = nullptr; };
std::map<std::string, Skel*> g_skels;      // by resolved file; kept for the whole run (nodes share the data)
int g_seq = 0;

Skel* loadSkel(const std::string& file) {
    auto it = g_skels.find(file);
    if (it != g_skels.end()) return it->second;
    g_skels[file] = nullptr;
    Data d = FileUtils::getInstance()->getDataFromFile(file);
    if (d.isNull()) return nullptr;
    std::string raw(reinterpret_cast<const char*>(d.getBytes()), static_cast<size_t>(d.getSize()));
    if (raw.size() < 16 || static_cast<unsigned char>(raw[0]) != 4 || raw[1] != 13 || raw[2] != 14 || raw[3] != 15) { rlog("[spine] %s: not a png-tagged ssdz", file.c_str()); return nullptr; }
    size_t e = raw.find("IEND");
    if (e == std::string::npos) return nullptr;
    e += 8;
    std::string js, atlasTxt;
    size_t end = 0, end2 = 0;
    if (!gunzip(raw, e, js, end) || !gunzip(raw, end + 8, atlasTxt, end2)) { rlog("[spine] %s: bad gzip parts", file.c_str()); return nullptr; }
    // atlas page file name = first non-empty line
    std::string page;
    size_t a = 0;
    while (a < atlasTxt.size() && page.empty()) {
        size_t b = atlasTxt.find('\n', a);
        std::string line = atlasTxt.substr(a, b == std::string::npos ? std::string::npos : b - a);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        page = line;
        a = b == std::string::npos ? atlasTxt.size() : b + 1;
    }
    if (page.empty()) page = "page.png";
    std::string dir = FileUtils::getInstance()->getWritablePath() + "spine_cache";
    FileUtils::getInstance()->createDirectory(dir);
    dir += "/" + std::to_string(++g_seq);
    FileUtils::getInstance()->createDirectory(dir);
    std::string pngPath = dir + "/" + page;
    if (FILE* f = std::fopen(pngPath.c_str(), "wb")) { std::fwrite(raw.data() + 4, 1, e - 4, f); std::fclose(f); }
    else { rlog("[spine] cannot write %s", pngPath.c_str()); return nullptr; }
    Skel* s = new Skel();
    s->atlas = spAtlas_create(atlasTxt.c_str(), static_cast<int>(atlasTxt.size()), dir.c_str(), nullptr);
    if (!s->atlas) { rlog("[spine] %s: atlas failed", file.c_str()); delete s; return nullptr; }
    spSkeletonJson* sj = spSkeletonJson_create(s->atlas);
    sj->scale = 1.0f;
    s->data = spSkeletonJson_readSkeletonData(sj, js.c_str());
    if (!s->data) rlog("[spine] %s: json error %s", file.c_str(), sj->error ? sj->error : "?");
    spSkeletonJson_dispose(sj);
    if (!s->data) { delete s; return nullptr; }
    g_skels[file] = s;
    return s;
}

spine::SkeletonAnimation* sk(lua_State* L) { return dynamic_cast<spine::SkeletonAnimation*>(toNode(L, 1)); }

// SkeletonAnimation.CreateWithFile(path) -> node (an empty node when the skeleton cannot be read, so later calls stay harmless)
int spineCreate(lua_State* L) {
    const char* p = lua_tostring(L, 1);
    std::string file = p ? resolveResFile(p) : "";
    Skel* s = file.empty() ? nullptr : loadSkel(file);
    if (!s) {
        static std::map<std::string, bool> warned;
        if (p && !warned[p]) { warned[p] = true; rlog("[spine] no skeleton for %s", p); }
        pushNode(L, Node::create());
        return 1;
    }
    spine::SkeletonAnimation* n = spine::SkeletonAnimation::createWithData(s->data, false);
    pushNode(L, n);
    return 1;
}

int setAnimation(lua_State* L) {                         // SetAnimation(track, name, loop)
    if (auto* n = sk(L)) {
        const char* name = lua_tostring(L, 3);
        if (name) n->setAnimation(static_cast<int>(luaL_optnumber(L, 2, 0)), name, lua_toboolean(L, 4) != 0);
    }
    return 0;
}
int setAnimationListener(lua_State* L) {                 // SetAnimationListener(callbackId): fires when an animation completes
    if (auto* n = sk(L)) {
        int id = static_cast<int>(luaL_checknumber(L, 2));
        n->setCompleteListener([id](spTrackEntry*) {
            Director::getInstance()->getScheduler()->performFunctionInCocosThread([id]() { runCallback(id); });   // the callback may delete the node
        });
    }
    return 0;
}
int clearAnimationListener(lua_State* L) { if (auto* n = sk(L)) n->setCompleteListener(nullptr); return 0; }
int setToSetupPose(lua_State* L) { if (auto* n = sk(L)) spSkeleton_setToSetupPose(n->getSkeleton()); return 0; }
int setTimeScale(lua_State* L) { if (auto* n = sk(L)) n->getState()->timeScale = static_cast<float>(luaL_checknumber(L, 2)); return 0; }
int resumeAnimation(lua_State* L) { if (auto* n = sk(L)) n->getState()->timeScale = 1.0f; return 0; }
int stopAnimation(lua_State* L) { if (auto* n = sk(L)) n->getState()->timeScale = 0.0f; return 0; }
int setToPlayTime(lua_State* L) {
    if (auto* n = sk(L)) if (spTrackEntry* t = n->getCurrent(0)) t->trackTime = static_cast<float>(luaL_checknumber(L, 2));
    return 0;
}
int isAnimationExist(lua_State* L) {
    auto* n = sk(L);
    const char* name = lua_tostring(L, 2);
    lua_pushboolean(L, n && name && spSkeletonData_findAnimation(n->getSkeleton()->data, name) != nullptr);
    return 1;
}
int getPlayAnimationName(lua_State* L) {
    auto* n = sk(L);
    spTrackEntry* t = n ? n->getCurrent(0) : nullptr;
    lua_pushstring(L, t && t->animation ? t->animation->name : "");
    return 1;
}
int getPlayAnimationTime(lua_State* L) {
    auto* n = sk(L);
    spTrackEntry* t = n ? n->getCurrent(0) : nullptr;
    lua_pushnumber(L, t ? t->trackTime : 0);
    return 1;
}

int setAnimationPlayTime(lua_State* L) {                 // SetAnimationPlayTime(name, totalSeconds): speed so the animation lasts that long
    auto* n = sk(L);
    const char* name = lua_tostring(L, 2);
    double total = luaL_optnumber(L, 3, 0);
    if (n && name && total > 0) {
        spTrackEntry* t = n->getCurrent(0);
        if (t && t->animation && std::strcmp(t->animation->name, name) == 0 && t->animation->duration > 0)
            n->getState()->timeScale = static_cast<float>(t->animation->duration / total);
    }
    return 0;
}

// GameLib.SkeletonDataCahce:AddSsdzSkeletonDataAsync(path, callbackId): load, then tell Lua (loading waits for it)
void notifyLater(int id) {
    Director::getInstance()->getScheduler()->performFunctionInCocosThread([id]() { runCallback(id); });
}
int sdcAddSsdz(lua_State* L) {
    const char* p = lua_tostring(L, 2);
    int id = static_cast<int>(luaL_optnumber(L, 3, 0));
    if (p) { std::string f = resolveResFile(p); if (!f.empty()) loadSkel(f); }
    notifyLater(id);
    return 0;
}
int sdcAddJson(lua_State* L) { notifyLater(static_cast<int>(luaL_optnumber(L, 5, 0))); return 0; }   // (json, atlas, image, callbackId): only .ssdz skeletons are drawn
int sdcSsdzInCache(lua_State* L) {
    const char* p = lua_tostring(L, 2);
    std::string f = p ? resolveResFile(p) : "";
    auto it = g_skels.find(f);
    lua_pushboolean(L, !f.empty() && it != g_skels.end());
    return 1;
}
int sdcTrue(lua_State* L) { lua_pushboolean(L, 1); return 1; }

}  // namespace

void registerSpine(lua_State* L) {
    for (const char* cls : {"SkeletonAnimation", "SpineEffect"}) {
        lua_getglobal(L, cls);
        if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_newtable(L); }
        lua_pushcfunction(L, spineCreate);
        lua_setfield(L, -2, "CreateWithFile");
        lua_pushcfunction(L, spineCreate);
        lua_setfield(L, -2, "New");
        lua_setglobal(L, cls);
    }
    luaL_getmetatable(L, kNodeMeta);
    struct R { const char* n; lua_CFunction f; };
    static const R regs[] = {
        {"SetAnimation", setAnimation}, {"SetAnimationListener", setAnimationListener}, {"ClearAnimationListener", clearAnimationListener},
        {"SetToSetupPose", setToSetupPose}, {"SetTimeScale", setTimeScale}, {"ResumeAnimation", resumeAnimation}, {"StopAnimation", stopAnimation},
        {"SetToPlayTime", setToPlayTime}, {"IsAnimationExist", isAnimationExist}, {"GetPlayAnimationName", getPlayAnimationName},
        {"GetPlayAnimationTime", getPlayAnimationTime},
        {"SetAnimationPlayTime", setAnimationPlayTime},
    };
    for (const R& r : regs) { lua_pushcfunction(L, r.f); lua_setfield(L, -2, r.n); }
    lua_pop(L, 1);

    lua_getglobal(L, "GameLib");                             // GameLib.SkeletonDataCahce:...
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, "SkeletonDataCahce");
        if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_newtable(L); lua_pushvalue(L, -1); lua_setfield(L, -3, "SkeletonDataCahce"); }
        struct F { const char* n; lua_CFunction f; };
        static const F fs[] = {{"AddSsdzSkeletonDataAsync", sdcAddSsdz}, {"AddSkeletonDataAsync", sdcAddJson}, {"IsSSdzDataInCache", sdcSsdzInCache},
                               {"IsSkeletonDataInCache", sdcTrue}, {"LoadSsdzSkeletonData", sdcAddSsdz}};
        for (const F& f : fs) { lua_pushcfunction(L, f.f); lua_setfield(L, -2, f.n); }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

}  // namespace ui
}  // namespace mc
