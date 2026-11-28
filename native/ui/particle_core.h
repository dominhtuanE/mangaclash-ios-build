#pragma once
// Particle effects of the game (resource/effects/particle/psconfig/*.xml, a "Particle Universe" style description):
// parsing and simulation only, no engine types, so it can be built and tried on any PC (tests/particle_test.cpp).
// The cocos2d-x node that draws the result is particle_node.cpp.
#include <memory>
#include <string>
#include <vector>

namespace mc {
namespace pu {

struct Vec { float x = 0, y = 0, z = 0; };
struct Colour { float r = 1, g = 1, b = 1, a = 1; };

// <dyn dyn_type="fixed|random|curve_spline">
struct Dyn {
    enum Kind { NONE, FIXED, RANDOM, CURVE };
    Kind kind = NONE;
    float value = 0, low = 0, high = 0;
    std::vector<std::pair<float, float>> points;
    float at(float t, float rnd, float fallback) const;      // t: curve position, rnd: 0..1 for random values
};

struct EmitterDef {
    enum Kind { POINT, BOX, CIRCLE, LINE, SPHERE };
    Kind kind = POINT;
    std::string name;
    float rate = -1;                                          // particles per second, -1: a single particle
    float start = 0, end = -1;                                // active window in system time, end < 0: not given
    bool forceEmit = false, liveForever = false, autoDir = false, randomized = true;
    Vec position, direction{0, 1, 0}, size, lineStart, lineDir;
    float radiusX = 0, radiusZ = 0, widthX = 0, widthZ = 0, fanStart = 0, fanEnd = 360, sphereRadius = 0, increment = 0;
    bool hasStartColour = false;
    Colour startColour, endColour;
    Dyn liveTime, velocity, angle, width, height, all;
};

struct AffectorDef {
    enum Kind { COLOUR, SCALE, ROTATION, TEXTURE, ELASTICITY, DEFLECTOR, FORCE, SUCTION, RANDOMISER, OTHER };
    Kind kind = OTHER;
    std::vector<std::string> excluded;                        // emitter names this affector leaves alone
    float from = 0, to = 1;                                   // part of the particle's life it acts on
    std::vector<std::pair<float, Colour>> colours;            // COLOUR
    bool scaleFixed = false;
    Dyn scaleAll, scaleX, scaleY;                             // SCALE
    Dyn startAngle, speed;                                    // ROTATION (degrees)
    float timeStep = 0.1f;                                    // TEXTURE
    int rows = 1, cols = 1, startFrame = 0;
    bool randomStart = false, forward = true;
    float reverseFactor = 1, timeStart = 0;                   // ELASTICITY
    Vec vector, point;                                        // DEFLECTOR (normal, point), FORCE, SUCTION (point), RANDOMISER
    Dyn force;                                                // FORCE, SUCTION, DEFLECTOR bounce
};

struct TechniqueDef {
    bool supported = false;                                   // billboards; meshes / trails / nested systems are skipped
    std::string texture;
    bool additive = false;
    float originX = 0.5f, originY = 0.5f;                     // the point of the quad placed on the particle
    bool alongVelocity = false;
    bool fixedDirection = false;
    Vec commonDir;
    int quota = 60;
    Vec position;
    float angle = 0;                                          // rotation of the whole technique in the screen plane
    std::vector<EmitterDef> emitters;
    std::vector<AffectorDef> affectors;
};

struct SystemDef {
    bool cycle = false;
    float totalTime = 0, preTime = 0, scale = 1, timeScale = 1;
    std::vector<TechniqueDef> techniques;
};

std::shared_ptr<SystemDef> parse(const std::string& xml);     // nullptr when the text is not a particle system

// One textured quad, in the node's space (pixels, y up). uv: the part of the texture, v grows downwards.
struct Quad {
    int technique = 0;
    float x = 0, y = 0, w = 0, h = 0, originX = 0.5f, originY = 0.5f, rotation = 0;   // degrees, counter clockwise; w, h < 0: mirrored
    Colour colour;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
};

class Instance {
public:
    explicit Instance(std::shared_ptr<const SystemDef> def, unsigned seed = 1);
    void restart();
    void update(float dt);
    bool finished() const { return finished_; }               // a system that does not cycle has nothing left to show
    void quads(std::vector<Quad>& out) const;
    const SystemDef& def() const { return *def_; }

private:
    struct Particle {
        float x, y, vx, vy, age, life, w, h, baseRotation;
        Colour base;
        float rnd[4];
        int emitter;
        unsigned bounced;                                     // one bit per elasticity affector
    };
    struct EmitterState { float carry = 0; bool started = false; int count = 0; };
    struct Technique { std::vector<Particle> particles; std::vector<EmitterState> emitters; };

    float random();                                           // 0..1
    void step(float dt);
    void emit(size_t t, size_t e);
    bool affects(const AffectorDef& a, const TechniqueDef& t, const Particle& p) const;

    std::shared_ptr<const SystemDef> def_;
    std::vector<Technique> tech_;
    unsigned seed_;
    float time_ = 0, age_ = 0, length_ = 0;
    bool finished_ = false;
};

}  // namespace pu
}  // namespace mc
