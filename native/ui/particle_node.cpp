// ParticleSprite.CreateWithTemplate(name): runs particle_core and shows its quads with one pooled Sprite per particle
// (cocos batches sprites of the same texture and blending by itself).
#include <algorithm>
#include <map>
#include <memory>
#include <vector>

#include "base/CCRefPtr.h"
#include "common.h"
#include "lua_node.h"
#include "particle_core.h"
#include "register.h"
#include "../core/rlog.h"

using namespace cocos2d;

namespace mc {
namespace ui {
namespace {

std::shared_ptr<pu::SystemDef> definition(const std::string& name) {
    static std::map<std::string, std::shared_ptr<pu::SystemDef>> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    std::shared_ptr<pu::SystemDef> def;
    std::string file = resolveResFile("resource/effects/particle/psconfig/" + name + ".xml");
    if (!file.empty()) def = pu::parse(FileUtils::getInstance()->getStringFromFile(file));
    if (def) cache[name] = def;                              // a missing file may still be downloaded later
    else rlog("[particle] template missing: %s", name.c_str());
    return def;
}

class ParticleNode : public Node {
public:
    static ParticleNode* create(const std::string& name) {
        ParticleNode* n = new (std::nothrow) ParticleNode();
        if (n && n->init()) {
            n->autorelease();
            n->setTemplate(name);
            n->scheduleUpdate();
            return n;
        }
        delete n;
        return nullptr;
    }

    void setTemplate(const std::string& name) {
        static unsigned seed = 1;
        auto def = definition(name);
        sim_.reset(def ? new pu::Instance(def, seed += 7919) : nullptr);
        told_ = false;
        textures_.clear();
        if (def)
            for (const pu::TechniqueDef& t : def->techniques)
                textures_.emplace_back(t.supported ? textureFor("resource/effects/particle/texture/" + t.texture + ".sdz") : nullptr);
        show();
    }
    void setFlip(bool flip) { flip_ = flip; show(); }
    void setPaused(bool paused) { paused_ = paused; }
    void setSpeed(float speed) { speed_ = speed; }
    void setEndListener(int id) { listener_ = id; }

    void update(float dt) override {
        if (paused_ || !isVisible()) return;
        if (sim_ && !sim_->finished()) {
            sim_->update(dt * speed_);
            show();
        }
        if ((!sim_ || sim_->finished()) && !told_ && listener_ > 0) {
            told_ = true;
            retain();                                        // the listener usually removes this effect
            runCallback(listener_);
            release();
        }
    }

private:
    void show() {
        quads_.clear();
        if (sim_) sim_->quads(quads_);
        size_t used = 0;
        for (const pu::Quad& q : quads_) {
            Texture2D* tex = textures_[q.technique].get();
            if (!tex) continue;
            if (used == sprites_.size()) {
                Sprite* fresh = Sprite::createWithTexture(tex);
                if (!fresh) break;
                fresh->setOpacityModifyRGB(false);
                addChild(fresh);
                sprites_.push_back(fresh);
            }
            Sprite* s = sprites_[used];
            s->setLocalZOrder(static_cast<int>(used));       // later techniques stay on top
            ++used;
            if (s->getTexture() != tex) s->setTexture(tex);
            Size size = tex->getContentSize();
            Rect rect(q.u0 * size.width, q.v0 * size.height, (q.u1 - q.u0) * size.width, (q.v1 - q.v0) * size.height);
            s->setTextureRect(rect);
            bool additive = sim_->def().techniques[q.technique].additive;
            s->setBlendFunc(additive ? BlendFunc{GL_SRC_ALPHA, GL_ONE} : BlendFunc{GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
            s->setAnchorPoint(Vec2(q.originX, q.originY));
            float mirror = flip_ ? -1.0f : 1.0f;
            s->setPosition(q.x * mirror, q.y);
            s->setScaleX(q.w / rect.size.width * mirror);
            s->setScaleY(q.h / rect.size.height);
            s->setRotation(-q.rotation * mirror);            // cocos turns clockwise
            auto byte = [](float v) { return static_cast<GLubyte>(std::max(0.0f, std::min(1.0f, v)) * 255.0f); };
            s->setColor(Color3B(byte(q.colour.r), byte(q.colour.g), byte(q.colour.b)));
            s->setOpacity(byte(q.colour.a));
            s->setVisible(true);
        }
        for (size_t i = used; i < sprites_.size(); ++i) sprites_[i]->setVisible(false);
    }

    std::unique_ptr<pu::Instance> sim_;
    std::vector<RefPtr<Texture2D>> textures_;                // held so a cache purge cannot free one in use
    std::vector<Sprite*> sprites_;
    std::vector<pu::Quad> quads_;
    int listener_ = 0;
    bool flip_ = false, paused_ = false, told_ = false;
    float speed_ = 1;
};

ParticleNode* particle(lua_State* L) { return dynamic_cast<ParticleNode*>(toNode(L, 1)); }

int create(lua_State* L) {
    const char* name = lua_tostring(L, 1);
    pushNode(L, ParticleNode::create(name ? name : ""));
    return 1;
}
int setTemplate(lua_State* L) { const char* n = lua_tostring(L, 2); particle(L)->setTemplate(n ? n : ""); return 0; }
int setFlipX(lua_State* L) { particle(L)->setFlip(lua_toboolean(L, 2) != 0); return 0; }
int setManualUpdate(lua_State* L) { particle(L)->setPaused(lua_toboolean(L, 2) != 0); return 0; }
int setTimeScale(lua_State* L) { particle(L)->setSpeed(static_cast<float>(luaL_optnumber(L, 2, 1))); return 0; }
int setEndListener(lua_State* L) { particle(L)->setEndListener(static_cast<int>(luaL_optnumber(L, 2, 0))); return 0; }

// A method every node shares: particle effects get their own, other nodes keep what was registered before.
int dispatch(lua_State* L) {
    if (particle(L)) return lua_tocfunction(L, lua_upvalueindex(2))(L);
    if (lua_isnil(L, lua_upvalueindex(1))) return 0;
    int n = lua_gettop(L);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, n, LUA_MULTRET);
    return lua_gettop(L);
}

}  // namespace

void registerParticle(lua_State* L) {
    lua_getglobal(L, "ParticleSprite");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); lua_newtable(L); }
    lua_pushcfunction(L, create);
    lua_setfield(L, -2, "CreateWithTemplate");
    lua_setglobal(L, "ParticleSprite");

    luaL_getmetatable(L, kNodeMeta);
    struct R { const char* n; lua_CFunction f; };
    static const R regs[] = {
        {"SetTemplate", setTemplate}, {"SetFlipX", setFlipX}, {"SetMansualUpdate", setManualUpdate},
        {"SetTimeScale", setTimeScale}, {"SetPlayeEndListener", setEndListener},
    };
    for (const R& r : regs) {
        lua_pushstring(L, r.n);
        lua_rawget(L, -2);                                   // the earlier method, nil when there is none
        lua_pushcfunction(L, r.f);
        lua_pushcclosure(L, dispatch, 2);
        lua_setfield(L, -2, r.n);
    }
    lua_pop(L, 1);
}

}  // namespace ui
}  // namespace mc
