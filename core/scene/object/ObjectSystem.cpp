#include "scene/object/ObjectSystem.hpp"

#include <algorithm>
#include <cmath>

#include "scene/movement/Body.hpp"

namespace geode::scene::object {

namespace {

using signal::ParamStore;

template <class E>
E intParam(const ParamStore& p, signal::ParamId id, int count) {
    return static_cast<E>(std::clamp(static_cast<int>(p.effective(id)), 0, count - 1));
}

Vec3 vec(const ParamStore& p, signal::ParamId x) { return {p.effective(x), p.effective(x + 1), p.effective(x + 2)}; }

// The orientation at `from` whose forward points at `to`, its up as near `upHint` as it can be (so a
// subject never rolls the view); `fallback` when the two points meet.
Quat lookAt(const Vec3& from, const Vec3& to, const Vec3& upHint, const Quat& fallback) {
    const Vec3 d = sub(to, from);
    if (length(d) < 1e-5f) return fallback;
    const Vec3 f = normalize(d);
    Vec3 r = cross(upHint, f);
    if (length(r) < 1e-4f) r = rightOf(fallback);
    r = normalize(r, {1.0f, 0.0f, 0.0f});
    const Vec3 u = cross(f, r);
    return fromBasis(r, u, f);
}

int originRank(Origin o) {
    switch (o) {
        case Origin::User:
            return 0;
        case Origin::Starting:
            return 1;
        case Origin::Music:
            return 2;
    }
    return 3;
}

}  // namespace

ObjectSystem::ObjectSystem(std::size_t capacity) : pool_(capacity) {}

void ObjectSystem::reset(std::uint64_t seed) {
    seed_ = seed;
    matched_.fill(0);
    fires_.fill(0);
    pool_.clear();
    placeStarting();
}

void ObjectSystem::declare(const Declaration& d) {
    ruleCount_ = std::min(d.spawners != nullptr ? d.spawnerCount : 0, kMaxSpawners);
    for (std::size_t i = 0; i < ruleCount_; ++i) rules_[i] = d.spawners[i];
    startingCount_ = std::min(d.starting != nullptr ? d.startingCount : 0, kMaxStarting);
    for (std::size_t i = 0; i < startingCount_; ++i) starting_[i] = d.starting[i];
    matched_.fill(0);
    fires_.fill(0);
    for (std::size_t i = 0; i < pool_.capacity(); ++i) {
        const ObjectPool::Slot& s = pool_.slot(i);
        if (s.used && s.origin != Origin::User) pool_.remove(s.instance);
    }
    placeStarting();
}

void ObjectSystem::placeStarting() {
    for (std::size_t i = 0; i < startingCount_; ++i) pool_.spawn(starting_[i], Origin::Starting);
}

std::uint32_t ObjectSystem::place(const ObjectSpec& spec) { return pool_.spawn(spec, Origin::User).instance; }

bool ObjectSystem::kill(std::uint32_t instance) {
    ObjectPool::Slot* s = pool_.find(instance);
    if (s == nullptr) return false;
    if (s->life.phase == Phase::Waiting) return pool_.remove(instance);
    if (s->life.phase != Phase::Exiting) {
        s->life.phase = Phase::Exiting;
        s->life.exitAge = 0.0f;
    }
    return true;
}

void ObjectSystem::beginFrame() {
    for (std::size_t i = 0; i < pool_.capacity(); ++i) {
        ObjectPool::Slot& s = pool_.slot(i);
        if (s.used) s.params.clearModulation();
    }
}

void ObjectSystem::step(const FrameIn& in) {
    const float dt = movement::clampDt(in.dt);
    camera_ = in.camera;
    for (std::size_t i = 0; i < pool_.capacity(); ++i) {
        ObjectPool::Slot& s = pool_.slot(i);
        if (s.used) stepObject(s, dt, in.events);
    }
    if (in.events == nullptr) return;
    for (const signal::Event& e : *in.events) {
        for (std::size_t r = 0; r < ruleCount_; ++r) {
            const SpawnRule& rule = rules_[r];
            if (e.kind != rule.trigger || !(e.strength >= rule.minStrength)) continue;
            const std::uint32_t n = matched_[r]++;
            if (n % static_cast<std::uint32_t>(std::max(1, rule.everyNth)) == 0) fire(r);
        }
    }
}

void ObjectSystem::stepObject(ObjectPool::Slot& s, float dt, const signal::EventQueue* events) {
    ParamStore& p = s.params;
    p.advance(dt);

    // Age, and the part of this frame (in seconds) after the object's birth.
    const float rate = std::max(0.0f, p.effective(kAgeRate));
    const float da = dt * rate;
    const float birth = p.effective(kBirth);
    const float before = s.body.age;
    const float after = before + da;
    s.body.age = after;
    float moveDt = 0.0f;
    if (after >= birth) moveDt = (before >= birth || !(da > 0.0f)) ? dt : dt * (after - birth) / da;

    // Kinematics: the rates integrated, the offsets held. Exact for constant rates at any frame rate.
    const Vec3 vel = vec(p, kVelX);
    const Vec3 spin = vec(p, kSpinX);
    s.travel = add(s.travel, scale(vel, moveDt));
    s.body.vel = vel;
    s.body.spin = spin;
    s.body.scale = p.effective(kScale);
    const float w = length(spin);
    if (w > 1e-7f && moveDt > 0.0f) s.body.orient = qnormalize(mul(axisAngle(scale(spin, 1.0f / w), w * moveDt), s.body.orient));
    s.body.pos = add(vec(p, kPosX), s.travel);
    s.orbitPhase = wrapAngle(s.orbitPhase + p.effective(kTargetOrbitRate) * moveDt);

    // Life.
    Life& life = s.life;
    if (life.phase == Phase::Exiting) life.exitAge += da;
    if (life.phase == Phase::Waiting && after >= birth) life.phase = Phase::Entering;
    const float since = after - birth;
    if (life.phase == Phase::Entering && since >= p.effective(kFadeIn)) life.phase = Phase::Living;
    if (life.phase == Phase::Entering || life.phase == Phase::Living) {
        const int deathEvent = static_cast<int>(p.effective(kDeathEvent));
        bool eventHit = false;
        if (events != nullptr) {
            for (const signal::Event& e : *events) {
                if (e.kind == signal::EventKind::Beat) ++life.beats;
                if (static_cast<int>(e.kind) == deathEvent) eventHit = true;
            }
        }
        switch (intParam<Death>(p, kDeath, kDeathCount)) {
            case Death::Time: {
                const float lifetime = p.effective(kLifetime);
                if (since >= lifetime) {
                    // The exit starts at the moment the life ran out, not at this frame, so its progress
                    // is the same at any frame rate.
                    life.phase = Phase::Exiting;
                    life.exitAge = since - lifetime;
                }
                break;
            }
            case Death::Beats:
                if (life.beats >= static_cast<int>(p.effective(kDeathBeats))) {
                    life.phase = Phase::Exiting;
                    life.exitAge = 0.0f;
                }
                break;
            case Death::Event:
                if (eventHit) {
                    life.phase = Phase::Exiting;
                    life.exitAge = 0.0f;
                }
                break;
            case Death::Never:
                break;
        }
    }
    if (life.phase == Phase::Exiting && life.exitAge >= p.effective(kFadeOut)) pool_.remove(s.instance);
}

void ObjectSystem::fire(std::size_t r) {
    const SpawnRule& rule = rules_[r];
    const std::uint32_t seed = firingSeed(seed_, r, fires_[r]);
    ++fires_[r];
    const int n = firingCount(rule, seed);
    for (int i = 0; i < n; ++i) {
        if (pool_.aliveFrom(static_cast<int>(r)) >= rule.maxAlive) break;
        pool_.spawn(emitOne(rule, seed, i, n, camera_), Origin::Music, static_cast<int>(r));
    }
}

void ObjectSystem::worldOf(const ObjectPool::Slot& s, Vec3& pos, Quat& orient) const {
    const ParamStore& p = s.params;
    const Quat local = qnormalize(mul(s.body.orient, camera::fromLook({p.effective(kRotYaw), p.effective(kRotPitch), p.effective(kRotRoll)})));
    if (intParam<Anchor>(p, kAnchor, kAnchorCount) == Anchor::Camera) {
        pos = add(camera_.pos, rotate(camera_.orient, s.body.pos));
        orient = qnormalize(mul(camera_.orient, local));
    } else {
        pos = s.body.pos;
        orient = local;
    }
}

bool ObjectSystem::worldPose(std::uint32_t instance, Vec3& pos, Quat& orient) const {
    const ObjectPool::Slot* s = pool_.find(instance);
    if (s == nullptr) return false;
    worldOf(*s, pos, orient);
    return true;
}

LifeLook ObjectSystem::look(std::uint32_t instance) const {
    const ObjectPool::Slot* s = pool_.find(instance);
    return s == nullptr ? LifeLook{} : lifeLook(s->params, s->life, s->body.age);
}

std::size_t ObjectSystem::packShapes(ShapeGpu* out, std::size_t max) const {
    if (out == nullptr) return 0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < pool_.capacity() && n < max; ++i) {
        const ObjectPool::Slot& s = pool_.slot(i);
        if (!s.used || s.kind != Kind::Shape) continue;
        const ParamStore& p = s.params;
        const LifeLook lk = lifeLook(p, s.life, s.body.age);
        const float opacity = p.effective(kOpacity) * lk.opacity;
        const float drawnScale = s.body.scale * lk.scale;
        if (!(opacity > 1e-4f) || !(drawnScale > 1e-5f)) continue;
        Vec3 pos;
        Quat q;
        worldOf(s, pos, q);
        ShapeGpu& g = out[n++];
        g.posScale[0] = pos[0];
        g.posScale[1] = pos[1];
        g.posScale[2] = pos[2];
        g.posScale[3] = drawnScale;
        g.orient[0] = q.x;
        g.orient[1] = q.y;
        g.orient[2] = q.z;
        g.orient[3] = q.w;
        g.size[0] = p.effective(kShapeSizeX);
        g.size[1] = p.effective(kShapeSizeY);
        g.size[2] = p.effective(kShapeSizeZ);
        g.size[3] = p.effective(kShapeRound);
        g.material[0] = p.effective(kPalette);
        g.material[1] = p.effective(kGlow);
        g.material[2] = opacity;
        g.material[3] = p.effective(kMaterial);
        g.life[0] = p.effective(kShapeType);
        g.life[1] = std::max(0.0f, s.body.age - p.effective(kBirth));
        g.life[2] = lk.burst;
        g.life[3] = lk.collapse;
    }
    return n;
}

CameraTarget ObjectSystem::cameraTarget(const camera::Pose& current) const {
    CameraTarget t;
    const ObjectPool::Slot* best = nullptr;
    for (std::size_t i = 0; i < pool_.capacity(); ++i) {
        const ObjectPool::Slot& s = pool_.slot(i);
        if (!s.used || s.life.phase == Phase::Waiting) continue;
        if (intParam<Anchor>(s.params, kAnchor, kAnchorCount) != Anchor::Target) continue;
        if (intParam<TargetMode>(s.params, kTargetMode, kTargetModeCount) == TargetMode::None) continue;
        if (best == nullptr || originRank(s.origin) < originRank(best->origin) ||
            (originRank(s.origin) == originRank(best->origin) && s.serial < best->serial)) {
            best = &s;
        }
    }
    if (best == nullptr) return t;

    const ParamStore& p = best->params;
    Vec3 subject;
    Quat orient;
    worldOf(*best, subject, orient);
    const LifeLook lk = lifeLook(p, best->life, best->body.age);
    const float size = std::max({p.effective(kShapeSizeX), p.effective(kShapeSizeY), p.effective(kShapeSizeZ)});
    const float dist = p.effective(kTargetDistance);
    const float height = p.effective(kTargetHeight);
    const Vec3 up = upOf(current.orient);

    t.valid = true;
    t.instance = best->instance;
    t.mode = intParam<TargetMode>(p, kTargetMode, kTargetModeCount);
    t.subject = subject;
    t.radius = (size + p.effective(kShapeRound)) * best->body.scale * lk.scale;
    t.pose.focal = current.focal;
    switch (t.mode) {
        case TargetMode::None:
        case TargetMode::LookAt:
            t.pose.pos = current.pos;
            break;
        case TargetMode::LockOn: {
            const Vec3 dir = normalize(sub(subject, current.pos), fwdOf(current.orient));
            t.pose.pos = sub(subject, scale(dir, dist));
            break;
        }
        case TargetMode::Orbit:
            t.pose.pos = add(subject, Vec3{std::cos(best->orbitPhase) * dist, height, std::sin(best->orbitPhase) * dist});
            break;
        case TargetMode::Follow: {
            const Vec3 back = normalize(best->body.vel, fwdOf(orient));
            t.pose.pos = add(sub(subject, scale(back, dist)), Vec3{0.0f, height, 0.0f});
            break;
        }
    }
    t.pose.orient = lookAt(t.pose.pos, subject, up, current.orient);
    t.weight = std::clamp(lk.opacity, 0.0f, 1.0f);
    return t;
}

}  // namespace geode::scene::object
