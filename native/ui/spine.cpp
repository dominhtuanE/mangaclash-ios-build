// SkeletonAnimation / SpineEffect on top of cocos2d-x' spine runtime (editor-support/spine).
// .ssdz = 04 0d 0e 0f + PNG (atlas page) + gzip(Spine JSON) + <u32 jsonLen, u32 gzLen> + gzip(atlas text)   (see server/platform.py _alias_spine_anims)
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "json/document.h"
#include "json/stringbuffer.h"
#include "json/writer.h"

#include "common.h"
#include "lua_node.h"
#include "../core/rlog.h"
#include "../third_party/miniz/miniz.h"
#include "spine/spine-cocos2dx.h"
#include "spine/Cocos2dAttachmentLoader.h"   // see loadSkel

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

// Skeletons exported by the old Spine (2.x) used by this game, adapted for the runtime in cocos2d-x 3.17:
//   boundingbox without "vertexCount" -> the new reader takes it for weighted vertices and reads out of bounds (crash)
//   "skinnedmesh" / "weightedmesh"    -> "mesh" (weighting is detected from the vertex count)
void upgradeSkeletonJson(std::string& js) {
    if (js.find("boundingbox") == std::string::npos && js.find("skinnedmesh") == std::string::npos && js.find("weightedmesh") == std::string::npos) return;
    rapidjson::Document doc;
    doc.Parse<0>(js.c_str());
    if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("skins") || !doc["skins"].IsObject()) return;
    auto& alloc = doc.GetAllocator();
    for (auto skin = doc["skins"].MemberBegin(); skin != doc["skins"].MemberEnd(); ++skin) {
        if (!skin->value.IsObject()) continue;
        for (auto slot = skin->value.MemberBegin(); slot != skin->value.MemberEnd(); ++slot) {
            if (!slot->value.IsObject()) continue;
            for (auto att = slot->value.MemberBegin(); att != slot->value.MemberEnd(); ++att) {
                rapidjson::Value& a = att->value;
                if (!a.IsObject() || !a.HasMember("type") || !a["type"].IsString()) continue;
                std::string type = a["type"].GetString();
                if (type == "skinnedmesh" || type == "weightedmesh") a["type"].SetString("mesh", alloc);
                else if (type == "boundingbox" && !a.HasMember("vertexCount") && a.HasMember("vertices") && a["vertices"].IsArray())
                    a.AddMember("vertexCount", static_cast<int>(a["vertices"].Size() / 2), alloc);
            }
        }
    }
    rapidjson::StringBuffer out;
    rapidjson::Writer<rapidjson::StringBuffer> writer(out);
    doc.Accept(writer);
    js.assign(out.GetString(), out.GetSize());
}

// users = nodes drawing the data; a skeleton nobody draws is freed after a while (it used to stay for the whole run,
// with its atlas texture, so memory only grew until iOS killed the app)
struct Skel {
    spAtlas* atlas = nullptr;
    spAttachmentLoader* loader = nullptr;
    spSkeletonData* data = nullptr;
    int users = 0;
    double lastUse = 0;
};
std::map<std::string, Skel*> g_skels;      // by resolved file (nodes share the data)
std::map<std::string, std::string> g_dirs; // file -> directory holding its extracted atlas page (reused after a reload)
int g_seq = 0;

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Skel* loadSkel(const std::string& file) {
    auto it = g_skels.find(file);
    if (it != g_skels.end()) {
        if (it->second) it->second->lastUse = nowSeconds();
        return it->second;
    }
    g_skels[file] = nullptr;
    rlogTrace("[spine] load %s", file.c_str());
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
    upgradeSkeletonJson(js);
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
    std::string& slot = g_dirs[file];
    if (slot.empty()) slot = std::to_string(++g_seq);
    dir += "/" + slot;
    FileUtils::getInstance()->createDirectory(dir);
    std::string pngPath = dir + "/" + page;
    if (FILE* f = std::fopen(pngPath.c_str(), "wb")) { std::fwrite(raw.data() + 4, 1, e - 4, f); std::fclose(f); }
    else { rlog("[spine] cannot write %s", pngPath.c_str()); return nullptr; }
    Skel* s = new Skel();
    s->atlas = spAtlas_create(atlasTxt.c_str(), static_cast<int>(atlasTxt.size()), dir.c_str(), nullptr);
    if (!s->atlas) { rlog("[spine] %s: atlas failed", file.c_str()); delete s; return nullptr; }
    // must be cocos' loader: it builds the AttachmentVertices that SkeletonRenderer::draw reads (the plain spine loader
    // leaves the atlas region there -> crash while drawing). Kept for as long as the skeleton data.
    s->loader = &Cocos2dAttachmentLoader_create(s->atlas)->super;
    spSkeletonJson* sj = spSkeletonJson_createWithLoader(s->loader);
    sj->scale = 1.0f;
    s->data = spSkeletonJson_readSkeletonData(sj, js.c_str());
    if (!s->data) rlog("[spine] %s: json error %s", file.c_str(), sj->error ? sj->error : "?");
    spSkeletonJson_dispose(sj);
    if (!s->data) { spAttachmentLoader_dispose(s->loader); spAtlas_dispose(s->atlas); delete s; return nullptr; }
    s->lastUse = nowSeconds();
    g_skels[file] = s;
    rlogTrace("[spine] ok %s", file.c_str());
    return s;
}

// The node Lua holds. The game creates a skeleton from a placeholder and later swaps the real one in (ResetSkeleton),
// so the drawn spine node is a replaceable child; animation, listener and tint survive the swap.
class SkeletonHolder : public Node {
public:
    static SkeletonHolder* create() {
        auto* n = new SkeletonHolder();
        n->init();
        n->setCascadeColorEnabled(true);
        n->setCascadeOpacityEnabled(true);
        n->autorelease();
        return n;
    }
    spine::SkeletonAnimation* anim() const { return anim_; }

    bool load(const char* path) {
        std::string file = path ? resolveResFile(path) : "";
        Skel* s = file.empty() ? nullptr : loadSkel(file);
        if (!s) {
            static std::map<std::string, bool> warned;
            if (path && !warned[path]) { warned[path] = true; rlog("[spine] no skeleton for %s", path); }
            return false;                                    // keep whatever is shown now
        }
        if (s == skel_) return true;
        auto* next = spine::SkeletonAnimation::createWithData(s->data, false);
        release_();
        skel_ = s;
        ++s->users;
        if (anim_) { next->setColor(anim_->getColor()); anim_->removeFromParent(); }
        anim_ = next;
        data_ = s->data;
        addChild(anim_);
        listen();
        play();
        anim_->update(0);                                    // pose the bones so the size below is right
        setContentSize(anim_->getBoundingBox().size);
        return true;
    }
    void setClip(int track, const std::string& name, bool loop) {
        if (track == 0) { clip_ = name; loop_ = loop; }
        if (anim_ && spSkeletonData_findAnimation(data_, name.c_str())) anim_->setAnimation(track, name, loop);
    }
    void setListener(int id) { listener_ = id; listen(); }
    void setSpeed(float v) { speed_ = v; setRate(v); }
    void setRate(float v) { rate_ = v; applyRate(); }               // current time scale (speed_ survives a skeleton swap)
    // SetUpdate(false) holds the pose (skill cut-ins, battle pause); kept apart from pause(), which entering a scene undoes
    void setFrozen(bool frozen) { frozen_ = frozen; applyRate(); }

    // the callbacks are posted to the next frame: they may delete the node. 2 = ANIMATION_COMPLETE, 3 = ANIMATION_EVENT
    void listen() {
        if (!anim_) return;
        int id = listener_;
        if (id <= 0) { anim_->setCompleteListener(nullptr); anim_->setEventListener(nullptr); return; }
        anim_->setCompleteListener([id](spTrackEntry* t) {
            std::string anim = t && t->animation ? t->animation->name : "";
            Director::getInstance()->getScheduler()->performFunctionInCocosThread([id, anim]() { runAnimCallback(id, anim, 2, "", 0, 0, ""); });
        });
        anim_->setEventListener([id](spTrackEntry* t, spEvent* ev) {
            std::string anim = t && t->animation ? t->animation->name : "";
            std::string name = ev && ev->data && ev->data->name ? ev->data->name : "";
            std::string text = ev && ev->stringValue ? ev->stringValue : "";
            int i = ev ? ev->intValue : 0;
            double f = ev ? ev->floatValue : 0;
            Director::getInstance()->getScheduler()->performFunctionInCocosThread([id, anim, name, i, f, text]() { runAnimCallback(id, anim, 3, name, i, f, text); });
        });
    }

    Rect getBoundingBox() const override {                   // used by GetOriginX / GetOriginY
        Rect r = anim_ ? anim_->getBoundingBox() : Rect::ZERO;
        return Rect(getPositionX() + r.origin.x * getScaleX(), getPositionY() + r.origin.y * getScaleY(),
                    r.size.width * getScaleX(), r.size.height * getScaleY());
    }
    ~SkeletonHolder() override { release_(); }
    void pause() override { Node::pause(); if (anim_) anim_->pause(); }
    void resume() override { Node::resume(); if (anim_) anim_->resume(); }

private:
    void release_() {
        if (!skel_) return;
        --skel_->users;
        skel_->lastUse = nowSeconds();
        skel_ = nullptr;
    }
    Skel* skel_ = nullptr;
    void applyRate() { if (anim_) anim_->getState()->timeScale = frozen_ ? 0.0f : rate_; }
    void play() {
        if (!clip_.empty()) setClip(0, clip_, loop_);
        rate_ = speed_;
        applyRate();
    }
    float rate_ = 1.0f;
    bool frozen_ = false;
    spine::SkeletonAnimation* anim_ = nullptr;               // a child of this node
    spSkeletonData* data_ = nullptr;
    std::string clip_;
    bool loop_ = false;
    int listener_ = 0;
    float speed_ = 1.0f;
};

SkeletonHolder* holder(lua_State* L) { return dynamic_cast<SkeletonHolder*>(toNode(L, 1)); }
spine::SkeletonAnimation* sk(lua_State* L) { auto* h = holder(L); return h ? h->anim() : nullptr; }

// SkeletonAnimation.CreateWithFile(path)
int spineCreate(lua_State* L) {
    auto* h = SkeletonHolder::create();
    h->load(lua_tostring(L, 1));
    pushNode(L, h);
    return 1;
}
int resetSkeleton(lua_State* L) { if (auto* h = holder(L)) h->load(lua_tostring(L, 2)); return 0; }

int setAnimation(lua_State* L) {                         // SetAnimation(track, name, loop)
    auto* h = holder(L);
    const char* name = lua_tostring(L, 3);
    if (h && name) h->setClip(static_cast<int>(luaL_optnumber(L, 2, 0)), name, lua_toboolean(L, 4) != 0);
    return 0;
}
int setAnimationListener(lua_State* L) { if (auto* h = holder(L)) h->setListener(static_cast<int>(luaL_checknumber(L, 2))); return 0; }
int clearAnimationListener(lua_State* L) { if (auto* h = holder(L)) h->setListener(0); return 0; }
int setToSetupPose(lua_State* L) { if (auto* n = sk(L)) spSkeleton_setToSetupPose(n->getSkeleton()); return 0; }
int setTimeScale(lua_State* L) { if (auto* h = holder(L)) h->setSpeed(static_cast<float>(luaL_checknumber(L, 2))); return 0; }
int resumeAnimation(lua_State* L) { if (auto* h = holder(L)) h->setSpeed(1.0f); return 0; }
int stopAnimation(lua_State* L) { if (auto* h = holder(L)) h->setSpeed(0.0f); return 0; }
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
// How long the current animation lasts (not how far it has played): footprints are spaced by half of the run cycle,
// timed attacks stretch the animation over it.
int getPlayAnimationTime(lua_State* L) {
    auto* n = sk(L);
    spTrackEntry* t = n ? n->getCurrent(0) : nullptr;
    lua_pushnumber(L, t && t->animation ? t->animation->duration : 0);
    return 1;
}

int setUpdate(lua_State* L) { if (auto* h = holder(L)) h->setFrozen(lua_toboolean(L, 2) == 0); return 0; }

int setAnimationPlayTime(lua_State* L) {                 // SetAnimationPlayTime(name, totalSeconds): speed so the animation lasts that long
    auto* n = sk(L);
    const char* name = lua_tostring(L, 2);
    double total = luaL_optnumber(L, 3, 0);
    if (n && name && total > 0) {
        spTrackEntry* t = n->getCurrent(0);
        if (t && t->animation && std::strcmp(t->animation->name, name) == 0 && t->animation->duration > 0)
            holder(L)->setRate(static_cast<float>(t->animation->duration / total));
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

// Frees the skeletons no node has drawn for idleSeconds. Call from the main thread between frames only.
void purgeIdleSkeletons(double idleSeconds) {
    double now = nowSeconds();
    int freed = 0;
    for (auto it = g_skels.begin(); it != g_skels.end();) {
        Skel* s = it->second;
        if (!s || s->users > 0 || now - s->lastUse < idleSeconds) { ++it; continue; }
        spSkeletonData_dispose(s->data);            // its attachments call back into the loader: loader goes after
        spAttachmentLoader_dispose(s->loader);
        spAtlas_dispose(s->atlas);                  // releases the atlas texture
        delete s;
        it = g_skels.erase(it);
        ++freed;
    }
    if (freed) {
        Director::getInstance()->getTextureCache()->removeUnusedTextures();
        rlogTrace("[spine] freed %d idle skeletons", freed);
    }
}

void registerSpine(lua_State* L) {
    static int owner = 0;
    auto* scheduler = Director::getInstance()->getScheduler();
    if (!scheduler->isScheduled("mc.skelpurge", &owner))
        scheduler->schedule([](float) { purgeIdleSkeletons(90); }, &owner, 30.0f, false, "mc.skelpurge");
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
        {"SetToSetupPose", setToSetupPose}, {"ResetSkeleton", resetSkeleton}, {"SetTimeScale", setTimeScale}, {"ResumeAnimation", resumeAnimation}, {"StopAnimation", stopAnimation},
        {"SetToPlayTime", setToPlayTime}, {"IsAnimationExist", isAnimationExist}, {"GetPlayAnimationName", getPlayAnimationName},
        {"GetPlayAnimationTime", getPlayAnimationTime},
        {"SetAnimationPlayTime", setAnimationPlayTime}, {"SetUpdate", setUpdate},
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

bool gzipPart(const std::string& in, size_t pos, std::string& out, size_t& end) { return gunzip(in, pos, out, end); }

}  // namespace ui
}  // namespace mc
