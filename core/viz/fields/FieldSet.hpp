#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// Fields: up to eight force points that pull, push or swirl whatever is near them, for as long as they live.
// This is the state and the maths; three things read it: Drift Things (ThingPool::step), the fluid (catch points
// and splats, FluidSceneBase / Emitters) and FieldPass (viz/passes), which bends the whole picture. Pure and
// header-only, so it runs on the host (core/tests/fields_test.cpp). Render thread only.
//
// Nothing here spawns by itself: a Field exists only because something called spawn(). No tempo is known or
// used, and a Field carries no colour.
//
// For the Event Responder. On the render thread, between frames:
//   spawn(Spawn)   one Field; returns its slot, or -1 when the request is unusable (a non-finite number, no life,
//                  no radius, an unknown kind). All eight slots taken: the oldest is reused, so a call is never
//                  unanswered.
// Where it goes is the responder's choice, mapped from whatever the event carries: the hit point, the middle of
// the picture, the pitch as a height, the stereo pan as a place across. Space is the frame's uv (0..1, y up),
// a radius is in frame heights, time is seconds. Strength is a pull in uv per second squared: for Attract and
// Repel it is a size (0 and up, the kind gives the sign); for Swirl its sign is the turning, positive
// counter-clockwise. A Field may drift (vx, vy, uv per second).
//
// Time is the frame's real dt, summed in double, so a Field's age and place do not depend on the frame rate.
namespace geode::viz::fields {

inline constexpr int kMaxFields = 8;
// Floats a Field takes in the packed uniform array: two vec4.
inline constexpr int kFloatsPerField = 8;
inline constexpr float kSoftening = 0.05f;  // frame heights; keeps the force finite up close
inline constexpr float kMaxStrength = 2.0f;
// The most the summed force of every Field gives at one point, uv per second squared. The same as the most
// gravity gives a Thing (things::kMaxAccel), so a Field alone can use all of it.
inline constexpr float kMaxForce = 2.0f;
inline constexpr float kMinRadius = 0.02f;
inline constexpr float kMaxRadius = 2.0f;

enum class Kind : int { Attract = 0, Repel = 1, Swirl = 2 };

struct Spawn {
    Kind kind = Kind::Attract;
    float x = 0.5f;            // uv
    float y = 0.5f;
    float strength = 1.0f;
    float radius = 0.3f;       // frame heights; no force at or past it
    float life_s = 2.0f;
    float attack_s = 0.1f;     // fade in
    float release_s = 0.4f;    // fade out, ending at the Field's end
    float vx = 0.0f;           // drift, uv per second
    float vy = 0.0f;
};

struct Force {
    float x = 0.0f;  // uv per second squared
    float y = 0.0f;
};

// The live Fields as the passes read them.
struct Packed {
    int count = 0;
    std::array<float, kMaxFields * kFloatsPerField> data{};
};

// A look at one slot; all zero when it is free.
struct View {
    bool alive = false;
    Kind kind = Kind::Attract;
    float x = 0.0f, y = 0.0f;
    float strength = 0.0f;  // signed, the envelope already applied
    float radius = 0.0f;
};

// How much of its strength a Field has at `age`, fading in over `attack` and out over `release` before `end`.
inline float envelope(double age, double end, float attack, float release) {
    if (!(age >= 0.0) || age >= end) return 0.0f;
    float in = 1.0f;
    float out = 1.0f;
    if (attack > 0.0f) in = static_cast<float>(std::min(1.0, age / static_cast<double>(attack)));
    if (release > 0.0f) out = static_cast<float>(std::min(1.0, (end - age) / static_cast<double>(release)));
    const float k = std::min(in, out);
    return k * k * (3.0f - 2.0f * k);
}

class FieldSet {
public:
    // Starts a Field and returns its slot, or -1 when the request is unusable. Every slot taken: the oldest is reused.
    int spawn(const Spawn& in) {
        if (!usable(in)) return -1;
        int slot = -1;
        for (int i = 0; i < kMaxFields; ++i) {
            if (!slots_[static_cast<size_t>(i)].alive) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            slot = 0;
            for (int i = 1; i < kMaxFields; ++i) {
                if (slots_[static_cast<size_t>(i)].age > slots_[static_cast<size_t>(slot)].age) slot = i;
            }
        }
        Slot& s = slots_[static_cast<size_t>(slot)];
        s = Slot{};
        s.alive = true;
        s.sp = in;
        Spawn& q = s.sp;
        q.x = std::clamp(in.x, -1.0f, 2.0f);
        q.y = std::clamp(in.y, -1.0f, 2.0f);
        q.strength = in.kind == Kind::Swirl ? std::clamp(in.strength, -kMaxStrength, kMaxStrength)
                                            : std::clamp(in.strength, 0.0f, kMaxStrength);
        q.radius = std::clamp(in.radius, kMinRadius, kMaxRadius);
        q.attack_s = std::clamp(in.attack_s, 0.0f, in.life_s);
        q.release_s = std::clamp(in.release_s, 0.0f, in.life_s);
        q.vx = std::clamp(in.vx, -4.0f, 4.0f);
        q.vy = std::clamp(in.vy, -4.0f, 4.0f);
        s.x = static_cast<double>(q.x);
        s.y = static_cast<double>(q.y);
        return slot;
    }

    // Ages and drifts every Field by `dt` seconds; one past its life is gone. A non-finite or non-positive dt
    // changes nothing.
    void step(float dt) {
        if (!std::isfinite(dt) || dt <= 0.0f) return;
        const double d = static_cast<double>(dt);
        for (Slot& s : slots_) {
            if (!s.alive) continue;
            s.age += d;
            if (s.age >= static_cast<double>(s.sp.life_s)) {
                s.alive = false;
                continue;
            }
            s.x = std::clamp(s.x + static_cast<double>(s.sp.vx) * d, -1.0, 2.0);
            s.y = std::clamp(s.y + static_cast<double>(s.sp.vy) * d, -1.0, 2.0);
        }
    }

    // The summed force at (x, y) in uv, uv per second squared. Each Field gives a softened push along or around
    // the line to it, fading to nothing at its radius; the sum is clamped to kMaxForce. Distances are taken in
    // frame heights (x scaled by `aspect`, width / height), so a Field is round at any aspect, and the result is
    // turned back to uv. Finite for every input; zero when the inputs are not.
    Force forceAt(float x, float y, float aspect) const {
        Force out;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(aspect) || !(aspect > 0.0f)) return out;
        float fx = 0.0f;
        float fy = 0.0f;
        for (const Slot& s : slots_) {
            if (!s.alive) continue;
            const float amount = s.sp.strength * envelope(s.age, static_cast<double>(s.sp.life_s), s.sp.attack_s, s.sp.release_s);
            if (amount == 0.0f) continue;
            const float dx = (static_cast<float>(s.x) - x) * aspect;
            const float dy = static_cast<float>(s.y) - y;
            const float d2 = dx * dx + dy * dy;
            const float t = std::sqrt(d2) / s.sp.radius;
            if (!(t < 1.0f)) continue;
            const float w = (1.0f - t) * (1.0f - t);
            const float k = amount * w / std::sqrt(d2 + kSoftening * kSoftening);
            switch (s.sp.kind) {
                case Kind::Attract: fx += dx * k; fy += dy * k; break;
                case Kind::Repel: fx -= dx * k; fy -= dy * k; break;
                case Kind::Swirl: fx += dy * k; fy -= dx * k; break;  // counter-clockwise for a positive strength
            }
        }
        const float mag = std::hypot(fx, fy);
        if (mag > kMaxForce) {
            fx *= kMaxForce / mag;
            fy *= kMaxForce / mag;
        }
        out.x = fx / aspect;
        out.y = fy;
        return out;
    }

    void clear() { slots_ = {}; }

    int count() const {
        int n = 0;
        for (const Slot& s : slots_) n += s.alive ? 1 : 0;
        return n;
    }
    // False: nothing to draw or feel, and the frame skips the pass.
    bool live() const { return count() > 0; }
    bool alive(int slot) const { return slot >= 0 && slot < kMaxFields && slots_[static_cast<size_t>(slot)].alive; }
    // Seconds since the Field in `slot` was spawned; 0 for a free slot.
    float age(int slot) const { return alive(slot) ? static_cast<float>(slots_[static_cast<size_t>(slot)].age) : 0.0f; }

    View view(int slot) const {
        View v;
        if (!alive(slot)) return v;
        const Slot& s = slots_[static_cast<size_t>(slot)];
        v.alive = true;
        v.kind = s.sp.kind;
        v.x = static_cast<float>(s.x);
        v.y = static_cast<float>(s.y);
        v.strength = s.sp.strength * envelope(s.age, static_cast<double>(s.sp.life_s), s.sp.attack_s, s.sp.release_s);
        v.radius = s.sp.radius;
        return v;
    }

    // The live Fields, in slot order, as the pass's uniform array:
    //   [0] x, y (uv), radius (frame heights), kind (0 attract, 1 repel, 2 swirl)
    //   [1] amount (signed strength x envelope), 0, 0, 0
    Packed pack() const {
        Packed out;
        for (int i = 0; i < kMaxFields; ++i) {
            const View v = view(i);
            if (!v.alive) continue;
            float* f = out.data.data() + static_cast<size_t>(out.count) * kFloatsPerField;
            f[0] = v.x;
            f[1] = v.y;
            f[2] = v.radius;
            f[3] = static_cast<float>(static_cast<int>(v.kind));
            f[4] = v.strength;
            ++out.count;
        }
        return out;
    }

private:
    struct Slot {
        bool alive = false;
        double age = 0.0;
        double x = 0.0;
        double y = 0.0;
        Spawn sp;
    };

    static bool usable(const Spawn& s) {
        for (float v : {s.x, s.y, s.strength, s.radius, s.life_s, s.attack_s, s.release_s, s.vx, s.vy}) {
            if (!std::isfinite(v)) return false;
        }
        const int k = static_cast<int>(s.kind);
        return k >= 0 && k <= static_cast<int>(Kind::Swirl) && s.life_s > 0.0f && s.radius > 0.0f;
    }

    std::array<Slot, kMaxFields> slots_{};
};

}  // namespace geode::viz::fields
