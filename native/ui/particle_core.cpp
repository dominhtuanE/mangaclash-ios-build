// See particle_core.h. Values the files do not state are chosen here (the original engine's code is not available).
#include "particle_core.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace mc {
namespace pu {
namespace {

const float kPi = 3.14159265358979f;
const float kForever = 1e9f;
const float kLongest = 20.0f;          // a system that does not cycle is over after this, whatever it says
const float kDefaultSize = 100.0f;

// ---- the small XML subset the files use: elements, attributes, text
struct Node {
    std::string tag, text;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<std::unique_ptr<Node>> children;
    std::string attr(const char* name) const {
        for (auto& a : attrs) if (a.first == name) return a.second;
        return "";
    }
    const Node* child(const char* name) const {
        for (auto& c : children) if (c->tag == name) return c.get();
        return nullptr;
    }
    std::string value(const char* name) const { const Node* c = child(name); return c ? c->text : ""; }
};

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

std::unique_ptr<Node> readXml(const std::string& s) {
    std::unique_ptr<Node> root(new Node());
    std::vector<Node*> stack{root.get()};
    size_t i = 0, n = s.size();
    while (i < n) {
        if (s[i] != '<') {
            size_t e = s.find('<', i);
            if (e == std::string::npos) e = n;
            stack.back()->text += trim(s.substr(i, e - i));
            i = e;
            continue;
        }
        if (s.compare(i, 4, "<!--") == 0) { size_t e = s.find("-->", i + 4); i = e == std::string::npos ? n : e + 3; continue; }
        size_t e = s.find('>', i);
        if (e == std::string::npos) break;
        std::string body = s.substr(i + 1, e - i - 1);
        i = e + 1;
        if (body.empty() || body[0] == '?' || body[0] == '!') continue;
        if (body[0] == '/') { if (stack.size() > 1) stack.pop_back(); continue; }
        bool closed = body.back() == '/';
        if (closed) body.pop_back();
        std::unique_ptr<Node> node(new Node());
        size_t p = body.find_first_of(" \t\r\n");
        node->tag = body.substr(0, p);
        while (p != std::string::npos) {
            size_t eq = body.find('=', p);
            if (eq == std::string::npos) break;
            size_t q1 = body.find_first_of("\"'", eq);
            if (q1 == std::string::npos) break;
            size_t q2 = body.find(body[q1], q1 + 1);
            if (q2 == std::string::npos) break;
            node->attrs.push_back({trim(body.substr(p, eq - p)), body.substr(q1 + 1, q2 - q1 - 1)});
            p = q2 + 1;
        }
        Node* raw = node.get();
        stack.back()->children.push_back(std::move(node));
        if (!closed) stack.push_back(raw);
    }
    return root;
}

std::vector<float> numbers(const std::string& s) {
    std::vector<float> out;
    const char* p = s.c_str();
    while (*p) {
        char* end = nullptr;
        double v = std::strtod(p, &end);
        if (end == p) { ++p; continue; }
        out.push_back(static_cast<float>(v));
        p = end;
    }
    return out;
}
float num(const Node& n, const char* name, float fallback) {
    std::vector<float> v = numbers(n.value(name));
    return v.empty() ? fallback : v[0];
}
bool flag(const Node& n, const char* name, bool fallback) {
    std::string v = n.value(name);
    return v.empty() ? fallback : (v == "true" || v == "1");
}
Vec vec(const Node& n, const char* name, Vec fallback) {
    std::vector<float> v = numbers(n.value(name));
    if (v.size() < 2) return fallback;
    Vec r;
    r.x = v[0]; r.y = v[1]; r.z = v.size() > 2 ? v[2] : 0;
    return r;
}
Colour colour(const std::string& s, size_t from) {
    std::vector<float> v = numbers(s);
    Colour c;
    if (v.size() >= from + 3) { c.r = v[from]; c.g = v[from + 1]; c.b = v[from + 2]; c.a = v.size() > from + 3 ? v[from + 3] : 1; }
    return c;
}

Dyn dyn(const Node& d) {
    Dyn r;
    std::string kind = d.attr("dyn_type");
    if (kind == "random") { r.kind = Dyn::RANDOM; r.low = num(d, "min", 0); r.high = num(d, "max", 0); }
    else if (kind.compare(0, 5, "curve") == 0) {
        for (auto& c : d.children) {
            if (c->tag != "points") continue;
            std::vector<float> v = numbers(c->text);
            if (v.size() >= 2) r.points.push_back({v[0], v[1]});
        }
        if (!r.points.empty()) r.kind = Dyn::CURVE;
    } else { r.kind = Dyn::FIXED; r.value = num(d, "value", 0); }
    return r;
}

EmitterDef emitter(const Node& n) {
    EmitterDef e;
    std::string type = n.attr("type");
    e.kind = type == "Box" ? EmitterDef::BOX : type == "Circle" ? EmitterDef::CIRCLE : type == "Line" ? EmitterDef::LINE
             : type == "Sphere" ? EmitterDef::SPHERE : EmitterDef::POINT;
    e.name = n.value("name");
    e.rate = num(n, "emission_rate", -1);
    std::vector<float> window = numbers(n.value("cycle_time"));
    if (window.size() >= 2) { e.start = window[0]; e.end = std::max(window[0], window[1]); }
    e.forceEmit = flag(n, "force_emit", false);
    e.liveForever = flag(n, "live_forever", false);
    e.autoDir = flag(n, "circle_auto_dir", false) || flag(n, "sphere_aotu_dir", false);
    e.randomized = flag(n, "is_randomized", true);
    e.position = vec(n, "position", Vec());
    Vec up; up.y = 1;
    e.direction = vec(n, "direction", up);
    e.size = vec(n, "size", Vec());
    e.lineStart = vec(n, "start_point", Vec());
    e.lineDir = vec(n, "line_direction", Vec());
    e.radiusX = num(n, "circle_x_radius", 0);
    e.radiusZ = num(n, "circle_z_radius", e.radiusX);
    e.widthX = num(n, "circle_x_width", 0);
    e.widthZ = num(n, "circle_z_width", 0);
    e.fanStart = num(n, "fan_start_angle", 0);
    e.fanEnd = num(n, "fan_end_angle", e.fanStart + num(n, "circle_angle", 360));
    e.sphereRadius = num(n, "sphere_radius", 0);
    e.increment = num(n, "increment", 0);
    if (n.child("start_color")) {
        e.hasStartColour = true;
        e.startColour = colour(n.value("start_color"), 0);
        e.endColour = n.child("end_color") ? colour(n.value("end_color"), 0) : e.startColour;
    }
    for (auto& c : n.children) {
        if (c->tag != "dyn") continue;
        std::string t = c->attr("type");
        Dyn d = dyn(*c);
        if (t == "live_time") e.liveTime = d;
        else if (t == "velocity") e.velocity = d;
        else if (t == "angle") e.angle = d;
        else if (t == "width") e.width = d;
        else if (t == "height") e.height = d;
        else if (t == "all_xyz") e.all = d;
    }
    return e;
}

AffectorDef affector(const Node& n) {
    AffectorDef a;
    std::string type = n.attr("type");
    a.kind = type == "Colour" ? AffectorDef::COLOUR : type == "Scale" ? AffectorDef::SCALE : type == "Rotation" ? AffectorDef::ROTATION
             : type == "TextureAnimator" ? AffectorDef::TEXTURE : type == "Elasticity" ? AffectorDef::ELASTICITY
             : type == "Deflector" ? AffectorDef::DEFLECTOR : type == "LinearForce" ? AffectorDef::FORCE
             : type == "Suction" ? AffectorDef::SUCTION : type == "Randomiser" ? AffectorDef::RANDOMISER : AffectorDef::OTHER;
    for (auto& c : n.children) {
        if (c->tag == "exclude_emitter") a.excluded.push_back(c->text);
        else if (c->tag == "time_colour") {
            std::vector<float> v = numbers(c->text);
            if (!v.empty()) a.colours.push_back({v[0], colour(c->text, 1)});
        } else if (c->tag == "dyn") {
            std::string t = c->attr("type");
            Dyn d = dyn(*c);
            if (t == "scale_xyz") a.scaleAll = d;
            else if (t == "scale_x") a.scaleX = d;
            else if (t == "scale_y") a.scaleY = d;
            else if (t == "rot_start_angle") a.startAngle = d;
            else if (t == "rot_speed") a.speed = d;
            else if (t == "dyn_force" || t == "suction_force" || t == "plane_bounce") a.force = d;
        }
    }
    a.from = num(n, "affect_start", 0);
    a.to = num(n, "affect_end", 1);
    a.scaleFixed = flag(n, "scale_fixed", false);
    a.timeStep = std::max(0.001f, num(n, "time_step", 0.1f));
    a.rows = std::max(1, static_cast<int>(num(n, "row_num", 1)));
    a.cols = std::max(1, static_cast<int>(num(n, "col_num", 1)));
    a.startFrame = std::max(0, static_cast<int>(num(n, "start_frame", 0)));
    a.randomStart = flag(n, "random_start", false);
    a.forward = flag(n, "play_order", true);
    a.reverseFactor = num(n, "reverse_factor", 1);
    a.timeStart = num(n, "time_start", 0.5f);
    if (a.kind == AffectorDef::DEFLECTOR) { Vec up; up.y = 1; a.vector = vec(n, "plane_normal", up); a.point = vec(n, "plane_point", Vec()); }
    if (a.kind == AffectorDef::FORCE) { Vec down; down.y = -1; a.vector = vec(n, "force_vector", down); }
    if (a.kind == AffectorDef::SUCTION) a.point = vec(n, "suction_pos", Vec());
    if (a.kind == AffectorDef::RANDOMISER) a.vector = vec(n, "max_deviation", Vec());
    return a;
}

TechniqueDef technique(const Node& n) {
    TechniqueDef t;
    t.quota = std::max(1, std::min(300, static_cast<int>(num(n, "particle_quota", 60))));
    t.position = vec(n, "tech_pos", Vec());
    Vec axis = vec(n, "tech_axis", Vec{0, 0, 1});
    if (std::fabs(axis.z) >= std::max(std::fabs(axis.x), std::fabs(axis.y))) t.angle = num(n, "tech_angle", 0) * (axis.z < 0 ? -1.0f : 1.0f);
    for (auto& c : n.children) {
        if (c->tag == "Emitter") t.emitters.push_back(emitter(*c));
        else if (c->tag == "Affector") t.affectors.push_back(affector(*c));
        else if (c->tag == "render") {
            t.supported = c->attr("type") == "Billboard";
            t.texture = c->value("texture_name");
            t.additive = static_cast<int>(num(*c, "mat_type", 0)) == 1;
            std::string origin = c->value("billboard_origin");
            if (origin.compare(0, 3, "top") == 0) t.originY = 1;
            else if (origin.compare(0, 6, "bottom") == 0) t.originY = 0;
            if (origin.find("left") != std::string::npos) t.originX = 0;
            else if (origin.find("right") != std::string::npos) t.originX = 1;
            std::string kind = c->value("billboard_type");
            t.alongVelocity = kind == "oriented_self";
            if (kind == "oriented_common") { t.commonDir = vec(*c, "billboard_common_dir", Vec{0, 1, 0});
                t.fixedDirection = std::fabs(t.commonDir.x) + std::fabs(t.commonDir.y) > 0.001f; }
        }
    }
    return t;
}

// growth stops at zero: a shrinking particle never turns inside out
float grown(float size, float change) {
    float v = size + change;
    return size * v < 0 ? 0 : v;
}

bool listed(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

void rotate(float& x, float& y, float degrees) {
    if (degrees == 0) return;
    float r = degrees * kPi / 180, c = std::cos(r), s = std::sin(r), nx = x * c - y * s;
    y = x * s + y * c;
    x = nx;
}

Colour keyed(const std::vector<std::pair<float, Colour>>& keys, float t) {
    if (t <= keys.front().first) return keys.front().second;
    for (size_t i = 1; i < keys.size(); ++i) {
        if (t > keys[i].first) continue;
        float span = keys[i].first - keys[i - 1].first, k = span > 0 ? (t - keys[i - 1].first) / span : 1;
        const Colour &a = keys[i - 1].second, &b = keys[i].second;
        Colour c;
        c.r = a.r + (b.r - a.r) * k; c.g = a.g + (b.g - a.g) * k; c.b = a.b + (b.b - a.b) * k; c.a = a.a + (b.a - a.a) * k;
        return c;
    }
    return keys.back().second;
}

}  // namespace

float Dyn::at(float t, float rnd, float fallback) const {
    switch (kind) {
        case FIXED: return value;
        case RANDOM: return low + (high - low) * rnd;
        case CURVE: {
            if (t <= points.front().first) return points.front().second;
            for (size_t i = 1; i < points.size(); ++i) {
                if (t > points[i].first) continue;
                float span = points[i].first - points[i - 1].first, k = span > 0 ? (t - points[i - 1].first) / span : 1;
                return points[i - 1].second + (points[i].second - points[i - 1].second) * k;
            }
            return points.back().second;
        }
        default: return fallback;
    }
}

std::shared_ptr<SystemDef> parse(const std::string& xml) {
    std::unique_ptr<Node> doc = readXml(xml);
    const Node* root = doc->child("ParticleSystem");
    if (!root) return nullptr;
    auto s = std::make_shared<SystemDef>();
    s->cycle = root->attr("is_cycle") == "true";
    s->totalTime = num(*root, "cycle_total_time", 0);
    s->preTime = std::max(0.0f, std::min(10.0f, num(*root, "pre_time", 0)));
    s->scale = num(*root, "ps_scale", 1);
    s->timeScale = num(*root, "scale_time", 1);
    for (auto& c : root->children)
        if (c->tag == "Technique") s->techniques.push_back(technique(*c));
    return s;
}

Instance::Instance(std::shared_ptr<const SystemDef> def, unsigned seed) : def_(std::move(def)), seed_(seed ? seed : 1) { restart(); }

float Instance::random() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return static_cast<float>(seed_ & 0xffffff) / 16777215.0f;
}

void Instance::restart() {
    time_ = age_ = 0;
    finished_ = false;
    tech_.assign(def_->techniques.size(), Technique());
    float last = 0;
    for (size_t i = 0; i < tech_.size(); ++i) {
        tech_[i].emitters.assign(def_->techniques[i].emitters.size(), EmitterState());
        for (const EmitterDef& e : def_->techniques[i].emitters) last = std::max(last, e.end >= 0 ? e.end : e.start + (e.rate < 0 ? 0.0f : 1.0f));
    }
    length_ = def_->totalTime > 0 ? def_->totalTime : last;
    for (float t = 0; t < def_->preTime; t += 0.033f) step(0.033f);
}

void Instance::emit(size_t ti, size_t ei) {
    const TechniqueDef& t = def_->techniques[ti];
    const EmitterDef& e = t.emitters[ei];
    if (tech_[ti].particles.size() >= static_cast<size_t>(t.quota)) return;
    EmitterState& state = tech_[ti].emitters[ei];
    float span = e.end > e.start ? (time_ - e.start) / (e.end - e.start) : 0;
    Particle p;
    for (float& r : p.rnd) r = random();
    p.emitter = static_cast<int>(ei);
    p.age = 0;
    p.bounced = 0;
    p.life = e.liveForever ? kForever : std::max(0.01f, e.liveTime.at(span, random(), 1.0f));
    float x = e.position.x, y = e.position.y, dx = e.direction.x, dy = e.direction.y;
    switch (e.kind) {
        case EmitterDef::BOX:
            x += (random() - 0.5f) * e.size.x;
            y += (random() - 0.5f) * e.size.y;
            break;
        case EmitterDef::CIRCLE: {
            float a = (e.fanStart + (e.fanEnd - e.fanStart) * random()) * kPi / 180;
            float rx = e.radiusX - e.widthX * random(), ry = e.radiusZ - e.widthZ * random();
            x += std::cos(a) * rx;
            y += std::sin(a) * ry;
            if (e.autoDir) { dx = std::cos(a); dy = std::sin(a); }
            break;
        }
        case EmitterDef::LINE: {
            float len = std::sqrt(e.lineDir.x * e.lineDir.x + e.lineDir.y * e.lineDir.y), k = random();
            if (!e.randomized && e.increment > 0 && len > 0) k = std::fmod(state.count * e.increment, len) / len;
            x += e.lineStart.x + e.lineDir.x * k;
            y += e.lineStart.y + e.lineDir.y * k;
            break;
        }
        case EmitterDef::SPHERE: {
            float a = random() * 2 * kPi, r = e.sphereRadius * std::sqrt(random());
            x += std::cos(a) * r;
            y += std::sin(a) * r;
            if (e.autoDir) { dx = std::cos(a); dy = std::sin(a); }
            break;
        }
        default: break;
    }
    ++state.count;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len > 0) { dx /= len; dy /= len; }                    // straight into the screen: no motion
    float spread = e.angle.at(span, random(), 0);
    rotate(dx, dy, spread * (random() * 2 - 1));
    float speed = e.velocity.at(span, random(), 0);
    p.vx = dx * speed;
    p.vy = dy * speed;
    rotate(x, y, t.angle);
    rotate(p.vx, p.vy, t.angle);
    p.x = x + t.position.x;
    p.y = y + t.position.y;
    float both = e.all.at(span, random(), kDefaultSize);
    p.w = e.width.at(span, random(), both);
    p.h = e.height.at(span, random(), both);
    p.baseRotation = t.angle;
    if (e.hasStartColour) {
        float k = random();
        p.base.r = e.startColour.r + (e.endColour.r - e.startColour.r) * k;
        p.base.g = e.startColour.g + (e.endColour.g - e.startColour.g) * k;
        p.base.b = e.startColour.b + (e.endColour.b - e.startColour.b) * k;
        p.base.a = e.startColour.a + (e.endColour.a - e.startColour.a) * k;
    }
    tech_[ti].particles.push_back(p);
}

bool Instance::affects(const AffectorDef& a, const TechniqueDef& t, const Particle& p) const {
    float k = p.life >= kForever ? 0 : p.age / p.life;
    return k >= a.from && k <= a.to && !listed(a.excluded, t.emitters[p.emitter].name);
}

void Instance::step(float dt) {
    time_ += dt;
    age_ += dt;
    if (def_->cycle && length_ > 0 && time_ >= length_) {
        time_ = std::fmod(time_, length_);
        for (auto& t : tech_) for (auto& e : t.emitters) e = EmitterState();
    }
    bool emitting = false, alive = false;
    for (size_t ti = 0; ti < tech_.size(); ++ti) {
        const TechniqueDef& t = def_->techniques[ti];
        auto& list = tech_[ti].particles;
        for (Particle& p : list) {
            p.age += dt;
            for (size_t i = 0; i < t.affectors.size(); ++i) {
                const AffectorDef& a = t.affectors[i];
                if (!affects(a, t, p)) continue;
                float k = p.life >= kForever ? 0 : p.age / p.life, rnd = p.rnd[i % 4];
                if (a.kind == AffectorDef::FORCE) {
                    float f = a.force.at(k, rnd, 1);
                    p.vx += a.vector.x * f * dt;
                    p.vy += a.vector.y * f * dt;
                } else if (a.kind == AffectorDef::SUCTION) {
                    float dx = a.point.x + t.position.x - p.x, dy = a.point.y + t.position.y - p.y, d = std::sqrt(dx * dx + dy * dy);
                    float f = a.force.at(k, rnd, 0);
                    if (d > 1) { p.vx += dx / d * f * dt; p.vy += dy / d * f * dt; }
                } else if (a.kind == AffectorDef::RANDOMISER) {
                    p.x += (random() * 2 - 1) * a.vector.x * dt * 60;
                    p.y += (random() * 2 - 1) * a.vector.y * dt * 60;
                } else if (a.kind == AffectorDef::ELASTICITY) {
                    unsigned bit = 1u << (i % 32);
                    if (k >= a.timeStart && !(p.bounced & bit)) { p.bounced |= bit; p.vx *= -a.reverseFactor; p.vy *= -a.reverseFactor; }
                } else if (a.kind == AffectorDef::DEFLECTOR) {
                    float nx = a.vector.x, ny = a.vector.y, n = std::sqrt(nx * nx + ny * ny);
                    if (n <= 0) continue;
                    nx /= n; ny /= n;
                    float side = (p.x - t.position.x - a.point.x) * nx + (p.y - t.position.y - a.point.y) * ny, into = p.vx * nx + p.vy * ny;
                    if (side < 0 && into < 0) {
                        float bounce = a.force.at(k, rnd, 1);
                        p.vx = (p.vx - 2 * into * nx) * bounce;
                        p.vy = (p.vy - 2 * into * ny) * bounce;
                    }
                } else if (a.kind == AffectorDef::SCALE && !a.scaleFixed) {      // growth per second
                    float all = a.scaleAll.at(k, rnd, 0);
                    p.w = grown(p.w, a.scaleX.at(k, rnd, all) * dt);
                    p.h = grown(p.h, a.scaleY.at(k, rnd, all) * dt);
                }
            }
            p.x += p.vx * dt;
            p.y += p.vy * dt;
        }
        list.erase(std::remove_if(list.begin(), list.end(), [](const Particle& p) { return p.age >= p.life; }), list.end());
        for (size_t ei = 0; ei < t.emitters.size(); ++ei) {
            const EmitterDef& e = t.emitters[ei];
            EmitterState& state = tech_[ti].emitters[ei];
            // no window: one burst for a single particle, otherwise as long as the system lasts
            float end = e.end >= 0 ? e.end : (e.rate < 0 ? e.start : (def_->cycle ? kForever : std::max(length_, e.start + 1.0f)));
            if (time_ < e.start) { emitting = true; continue; }
            bool first = !state.started;
            if (time_ > end && !first) continue;
            if (time_ > end && first && !(e.forceEmit || e.rate < 0)) { state.started = true; continue; }
            state.started = true;
            if (e.rate < 0) {                                  // a single particle per cycle
                if (first) emit(ti, ei);
                continue;
            }
            emitting = true;
            state.carry += e.rate * dt;
            if (first && e.forceEmit) state.carry = std::max(state.carry, 1.0f);
            for (; state.carry >= 1; state.carry -= 1) emit(ti, ei);
        }
        if (t.supported && !list.empty()) alive = true;
    }
    if (!def_->cycle && ((!emitting && !alive && age_ >= std::min(length_, kLongest)) || age_ >= kLongest)) {
        finished_ = true;
        for (auto& t : tech_) t.particles.clear();
    }
}

void Instance::update(float dt) {
    if (finished_) return;
    dt *= def_->timeScale > 0 ? def_->timeScale : 1;
    dt = std::min(dt, 0.25f);                                  // a hitch must not skip whole effects
    while (dt > 0 && !finished_) {
        float d = std::min(dt, 0.034f);
        step(d);
        dt -= d;
    }
}

void Instance::quads(std::vector<Quad>& out) const {
    out.clear();
    float scale = def_->scale;
    for (size_t ti = 0; ti < tech_.size(); ++ti) {
        const TechniqueDef& t = def_->techniques[ti];
        if (!t.supported) continue;
        for (const Particle& p : tech_[ti].particles) {
            Quad q;
            q.technique = static_cast<int>(ti);
            q.w = p.w; q.h = p.h;
            q.colour = p.base;
            q.rotation = p.baseRotation;
            float k = p.life >= kForever ? 0 : std::min(1.0f, p.age / p.life);
            for (size_t i = 0; i < t.affectors.size(); ++i) {
                const AffectorDef& a = t.affectors[i];
                if (listed(a.excluded, t.emitters[p.emitter].name)) continue;
                float rnd = p.rnd[i % 4];
                if (a.kind == AffectorDef::COLOUR && !a.colours.empty()) {
                    Colour c = keyed(a.colours, k);
                    q.colour.r = p.base.r * c.r; q.colour.g = p.base.g * c.g; q.colour.b = p.base.b * c.b; q.colour.a = p.base.a * c.a;
                } else if (a.kind == AffectorDef::SCALE && a.scaleFixed) {       // the size itself
                    const Dyn& dx = a.scaleX.kind != Dyn::NONE ? a.scaleX : a.scaleAll;
                    const Dyn& dy = a.scaleY.kind != Dyn::NONE ? a.scaleY : a.scaleAll;
                    float mirrorX = p.w < 0 ? -1.0f : 1.0f, mirrorY = p.h < 0 ? -1.0f : 1.0f;
                    if (dx.kind != Dyn::NONE) q.w = dx.at(k, rnd, 0) * mirrorX;
                    if (dy.kind != Dyn::NONE) q.h = dy.at(k, rnd, 0) * mirrorY;
                } else if (a.kind == AffectorDef::ROTATION) {
                    q.rotation += a.startAngle.at(0, rnd, 0) + a.speed.at(k, p.rnd[(i + 1) % 4], 0) * p.age;
                } else if (a.kind == AffectorDef::TEXTURE) {
                    int frames = a.rows * a.cols, step = static_cast<int>(p.age / a.timeStep);
                    int first = a.randomStart ? static_cast<int>(rnd * frames) : a.startFrame;
                    int frame = ((a.forward ? first + step : first + frames - 1 - step % frames) % frames + frames) % frames;
                    q.u0 = static_cast<float>(frame % a.cols) / a.cols;
                    q.v0 = static_cast<float>(frame / a.cols) / a.rows;
                    q.u1 = q.u0 + 1.0f / a.cols;
                    q.v1 = q.v0 + 1.0f / a.rows;
                }
            }
            if (t.alongVelocity && (std::fabs(p.vx) + std::fabs(p.vy) > 0.001f)) q.rotation += std::atan2(p.vy, p.vx) * 180 / kPi - 90;
            else if (t.fixedDirection) q.rotation += std::atan2(t.commonDir.y, t.commonDir.x) * 180 / kPi - 90;
            q.x = p.x * scale; q.y = p.y * scale; q.w *= scale; q.h *= scale;
            q.originX = t.originX; q.originY = t.originY;
            if (q.w != 0 && q.h != 0 && q.colour.a > 0.003f) out.push_back(q);
        }
    }
}

}  // namespace pu
}  // namespace mc
