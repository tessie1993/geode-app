#pragma once
#include <algorithm>
#include <array>
#include <bitset>
#include <cmath>
#include <cstdint>

#include "viz/fields/FieldSet.hpp"
#include "viz/fx/Envelope.hpp"

// Things: up to 32 short-lived shapes (dots, rings, shards, blobs) that spawn, live and disappear on the
// picture. This is the state and the maths; ThingPass (viz/passes) draws it. Pure and header-only, so it runs
// on the host (core/tests/things_test.cpp).
//
// Nothing here spawns by itself: a Thing exists only because something called spawn(), burst() or, for a chain
// reaction, because a Thing with a chain depth died. No tempo is known or used, and a Thing carries no colour,
// only a palette position the renderer resolves on the current palette.
//
// For the Event Responder. All calls are on the render thread, between frames:
//   spawn(Spawn)            one Thing; returns its slot, or -1 when the request is unusable. All 32 slots taken:
//                           the oldest is reused, so a call is never unanswered. Spawn::fromEdge(edge, ...)
//                           makes one that enters at an edge and crosses to the other.
//   burst(cx, cy, n, speed) n Things in a ring around (cx, cy), moving outward at `speed` uv/s; returns how
//                           many were made. An optional Spawn is the template for each.
//   morph(slot, shape, s)   blends the Thing's shape into `shape` over `s` seconds. False when refused.
//   melt(slot, s)           softens it and drips it downward; it is gone `s` seconds from now. False when refused.
//   events() / drainEvents()  Collide events (two Things touched, once per contact, not once per frame), oldest
//                           first. drainEvents() also clears them. They are meant to come back to the responder
//                           as a song-event source: read them once a frame with drainEvents() and route them
//                           like any other event. At most kEventCapacity are kept; the oldest are dropped.
// Time is seconds from the frame's real dt. Space is the frame's uv (0..1, y up) for a place and a velocity
// (uv per second), and frame heights for a size. setAspect(width / height) keeps circles round.
namespace geode::viz::things {

inline constexpr int kMaxThings = 32;
// Floats a Thing takes in the packed uniform array: three vec4.
inline constexpr int kFloatsPerThing = 12;
inline constexpr int kEventCapacity = 16;
// Children one step's deaths may spawn between them, so a chain cannot run away.
inline constexpr int kMaxChainPerStep = 8;
inline constexpr float kMaxSpeed = 4.0f;   // uv per second
inline constexpr float kMaxAccel = 2.0f;   // uv per second squared, the most gravity gives in one moment
using fx::kSoftening;  // frame heights; keeps the pull finite up close (shared with Fields)
// The longest a Thing may live; a longer ask is cut to it.
inline constexpr float kMaxLifeSeconds = 60.0f;
inline constexpr float kMinSize = 0.004f;
inline constexpr float kMaxSize = 0.5f;
// Motion runs in fixed substeps of this many seconds, so a place does not depend on the frame rate.
inline constexpr double kSubstep = 1.0 / 240.0;
// The most motion one step catches up on (a stall does not turn into a burst).
inline constexpr double kMaxCatchUp = 0.25;

enum class Shape : int { Dot = 0, Ring = 1, Shard = 2, Blob = 3 };
enum class Edge : int { Left, Right, Bottom, Top };
enum Behaviour : std::uint32_t {
    Drift = 1u,    // moves by its velocity; without it a Thing stays where it is and nothing pushes it
    Orbit = 2u,    // its velocity turns at `orbit` radians a second, so it curves
    Gravity = 4u,  // pulls every drifting Thing toward it by inverse square (clamped, softened)
    Bounce = 8u,   // turns back at the picture's edges, and bounces off another Bounce Thing it touches
};
enum class EventType : int { Collide = 0 };

struct Spawn {
    Shape shape = Shape::Dot;
    float x = 0.5f;            // uv
    float y = 0.5f;
    float vx = 0.0f;           // uv per second
    float vy = 0.0f;
    float size = 0.05f;        // radius, frame heights
    float spin = 0.0f;         // radians a second
    float life_s = 2.0f;
    float palettePos = 0.0f;   // 0..1 on the current palette
    float attack_s = 0.05f;    // fade in
    float release_s = 0.3f;    // fade out, ending at the Thing's end
    std::uint32_t behaviour = Drift;
    float orbit = 0.0f;        // radians a second (Orbit)
    float pull = 0.05f;        // strength of the pull (Gravity)
    int chain = 0;             // above 0: on death it spawns `chainCount` smaller children, each with chain - 1
    int chainCount = 3;

    // A Thing that enters off `edge` and crosses to the opposite edge, leaving at the end of its life. Its
    // margin keeps it off the picture at birth for an aspect of 0.5 or wider.
    static Spawn fromEdge(Edge edge, Shape shape = Shape::Dot, float size = 0.05f, float life_s = 2.0f) {
        Spawn s;
        s.shape = shape;
        s.size = size;
        s.life_s = life_s;
        const float m = 2.0f * size;
        const float v = life_s > 0.0f ? (1.0f + 2.0f * m) / life_s : 0.0f;
        switch (edge) {
            case Edge::Left: s.x = -m; s.y = 0.5f; s.vx = v; break;
            case Edge::Right: s.x = 1.0f + m; s.y = 0.5f; s.vx = -v; break;
            case Edge::Bottom: s.x = 0.5f; s.y = -m; s.vy = v; break;
            case Edge::Top: s.x = 0.5f; s.y = 1.0f + m; s.vy = -v; break;
        }
        return s;
    }
};

struct ThingEvent {
    EventType type = EventType::Collide;
    int a = -1;        // slots
    int b = -1;
    float x = 0.0f;    // uv, midway between the two
    float y = 0.0f;
};

struct EventList {
    std::array<ThingEvent, kEventCapacity> items{};
    int count = 0;
};

// The live Things as the pass reads them.
struct Packed {
    int count = 0;
    std::array<float, kMaxThings * kFloatsPerThing> data{};
    // Per Thing, the palette position the renderer turns into a colour.
    std::array<float, kMaxThings> palettePos{};
};

// A look at one slot; all zero when it is free.
struct View {
    bool alive = false;
    Shape shape = Shape::Dot;
    float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f, size = 0.0f;
    int chain = 0;
};

// How much of its strength a Thing has at `age`, fading in over `attack` and out over `release` before `end`.
using fx::envelope;

class ThingPool {
public:
    // Circles are round on screen when x is scaled by this (width / height). Ignored when unusable.
    void setAspect(float aspect) {
        if (std::isfinite(aspect) && aspect > 0.0f) aspect_ = std::clamp(aspect, 0.2f, 5.0f);
    }

    // Starts a Thing and returns its slot, or -1 when the request is unusable (a non-finite number, no life,
    // no size, an unknown shape). Every slot taken: the oldest is reused. A life over kMaxLifeSeconds is cut to it.
    int spawn(const Spawn& in) {
        if (!usable(in)) return -1;
        ++revision_;
        return place(in);
    }

    // `n` Things in a ring around (cx, cy) moving outward at `speed` (uv per second), each from `base`
    // (its place and velocity are replaced). Returns how many were made.
    int burst(float cx, float cy, int n, float speed, const Spawn& base = Spawn{}) {
        if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(speed) || n <= 0) return 0;
        n = std::min(n, kMaxThings);
        int made = 0;
        for (int i = 0; i < n; ++i) {
            Spawn s = base;
            ring(s, cx, cy, i, n, speed, 0.0f);
            if (spawn(s) >= 0) ++made;
        }
        return made;
    }

    // Blends the Thing's shape into `to` over `seconds` (0: at once). A blend still running is cut where it is
    // (past halfway, to its target). False when the slot is free, the shape unknown or the time unusable.
    bool morph(int slot, Shape to, float seconds) {
        if (!alive(slot) || !validShape(to) || !std::isfinite(seconds) || seconds < 0.0f) return false;
        Slot& s = slots_[static_cast<size_t>(slot)];
        if (morphProgress(slot) >= 0.5f) s.from = s.to;
        s.to = to;
        if (seconds == 0.0f) s.from = to;
        s.morphStart = s.age;
        s.morphDur = static_cast<double>(seconds);
        ++revision_;
        return true;
    }

    // Softens the Thing's edges and drips it downward as it fades; it ends `seconds` from now (never later
    // than it would have). Its fade-out shortens with it, so a melt shorter than the release does not make the
    // picture jump. False when the slot is free or the time is not above zero.
    bool melt(int slot, float seconds) {
        if (!alive(slot) || !std::isfinite(seconds) || !(seconds > 0.0f)) return false;
        Slot& s = slots_[static_cast<size_t>(slot)];
        if (!s.melting) {
            s.melting = true;
            s.meltStart = s.age;
        }
        const double left = s.end - s.age;
        const double within = static_cast<double>(seconds);
        if (within < left) {
            // A release not yet begun fits inside the melt; one under way keeps the strength it has.
            const double release = static_cast<double>(s.sp.release_s);
            s.sp.release_s = static_cast<float>(left >= release ? std::min(release, within) : release * within / left);
            s.end = s.age + within;
        }
        ++revision_;
        return true;
    }

    // Moves every Thing by `dt` seconds, runs the interactions, ages them and ends the ones past their life.
    // A non-finite or non-positive dt changes nothing. `pulls` (optional, read only, not stepped here): its force
    // is added to every Drift Thing, from a copy that ages with each substep, so the pull does not depend on the
    // frame rate. The caller steps the real set after this call.
    void step(float dt, const fields::FieldSet* pulls = nullptr) {
        if (!std::isfinite(dt) || dt <= 0.0f) return;
        const double d = static_cast<double>(dt);
        if (count() > 0) {
            fields::FieldSet local;
            const bool pulled = pulls != nullptr && pulls->live();
            if (pulled) local = *pulls;
            carry_ += std::min(d, kMaxCatchUp);
            while (carry_ >= kSubstep - 1e-6) {
                substep(static_cast<float>(kSubstep), pulled ? &local : nullptr);
                if (pulled) local.step(static_cast<float>(kSubstep));
                carry_ -= kSubstep;
            }
            if (carry_ < 0.0) carry_ = 0.0;
        } else {
            carry_ = 0.0;
        }
        ageAll(d);
    }

    void clear() {
        slots_ = {};
        touch_ = {};
        events_ = {};
        eventCount_ = 0;
        carry_ = 0.0;
        owed_ = {};
        owedCount_ = 0;
        owedCursor_ = 0;
        ++revision_;
    }

    int count() const {
        int n = 0;
        for (const Slot& s : slots_) n += s.alive ? 1 : 0;
        return n;
    }
    // False: nothing to draw and no child still owed, and the frame skips the pass.
    bool live() const { return count() > 0 || owedCount_ > 0; }
    // Counts the calls that changed what pack() would return (a spawn, a morph, a melt, a clear), so a frame
    // that is held while the clock is stopped can see it owes a redraw.
    unsigned revision() const { return revision_; }
    bool alive(int slot) const { return slot >= 0 && slot < kMaxThings && slots_[static_cast<size_t>(slot)].alive; }

    // Seconds since the Thing in `slot` was spawned; 0 for a free slot.
    float age(int slot) const { return alive(slot) ? static_cast<float>(slots_[static_cast<size_t>(slot)].age) : 0.0f; }
    // Seconds until it ends; 0 for a free slot.
    float remaining(int slot) const {
        if (!alive(slot)) return 0.0f;
        const Slot& s = slots_[static_cast<size_t>(slot)];
        return static_cast<float>(s.end - s.age);
    }
    // 0..1: how far the shape has blended to its target; 1 when it is not blending.
    float morphProgress(int slot) const {
        if (!alive(slot)) return 0.0f;
        const Slot& s = slots_[static_cast<size_t>(slot)];
        if (!(s.morphDur > 0.0)) return 1.0f;
        return static_cast<float>(std::clamp((s.age - s.morphStart) / s.morphDur, 0.0, 1.0));
    }
    // 0..1: how far it has melted; 0 when it is not melting, 1 at its end.
    float meltProgress(int slot) const {
        if (!alive(slot)) return 0.0f;
        const Slot& s = slots_[static_cast<size_t>(slot)];
        if (!s.melting) return 0.0f;
        const double span = s.end - s.meltStart;
        if (!(span > 0.0)) return 1.0f;
        return static_cast<float>(std::clamp((s.age - s.meltStart) / span, 0.0, 1.0));
    }
    View view(int slot) const {
        View v;
        if (!alive(slot)) return v;
        const Slot& s = slots_[static_cast<size_t>(slot)];
        v.alive = true;
        v.shape = s.to;
        v.x = s.x;
        v.y = s.y;
        v.vx = s.vx;
        v.vy = s.vy;
        v.size = s.size;
        v.chain = s.sp.chain;
        return v;
    }

    // The Collide events kept, oldest first.
    EventList events() const {
        EventList out;
        out.count = eventCount_;
        for (int i = 0; i < eventCount_; ++i) {
            out.items[static_cast<size_t>(i)] = events_[static_cast<size_t>((eventHead_ + i) % kEventCapacity)];
        }
        return out;
    }
    // The same, and the list is emptied.
    EventList drainEvents() {
        EventList out = events();
        eventCount_ = 0;
        eventHead_ = 0;
        return out;
    }

    // The live Things, in slot order, as the pass's uniform arrays:
    //   [0] x, y (uv), size (frame heights), angle (radians)
    //   [1] shape from, shape to, morph 0..1, melt 0..1
    //   [2] amount (the envelope), palette position, seed (radians), 0
    Packed pack() const {
        Packed out;
        for (const Slot& s : slots_) {
            if (!s.alive) continue;
            float* f = out.data.data() + static_cast<size_t>(out.count) * kFloatsPerThing;
            const int slot = static_cast<int>(&s - slots_.data());
            f[0] = s.x;
            f[1] = s.y;
            f[2] = s.size;
            f[3] = static_cast<float>(std::fmod(static_cast<double>(s.sp.spin) * s.age, 6.283185307179586));
            f[4] = static_cast<float>(s.from);
            f[5] = static_cast<float>(s.to);
            f[6] = morphProgress(slot);
            f[7] = meltProgress(slot);
            f[8] = envelope(s.age, s.end, s.sp.attack_s, s.sp.release_s);
            f[9] = s.sp.palettePos;
            f[10] = static_cast<float>(std::fmod(static_cast<double>(s.seed) * 0.6180339887, 1.0) * 6.283185307179586);
            f[11] = 0.0f;
            out.palettePos[static_cast<size_t>(out.count)] = s.sp.palettePos;
            ++out.count;
        }
        return out;
    }

private:
    struct Slot {
        bool alive = false;
        double age = 0.0;
        double end = 0.0;  // seconds of life; a melt brings it forward
        Spawn sp;
        float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f, size = 0.0f;
        Shape from = Shape::Dot;
        Shape to = Shape::Dot;
        double morphStart = 0.0;
        double morphDur = 0.0;
        bool melting = false;
        double meltStart = 0.0;
        std::uint32_t seed = 0;
    };

    static bool validShape(Shape s) { return static_cast<int>(s) >= 0 && static_cast<int>(s) <= static_cast<int>(Shape::Blob); }

    static bool usable(const Spawn& s) {
        for (float v : {s.x, s.y, s.vx, s.vy, s.size, s.spin, s.life_s, s.palettePos, s.attack_s, s.release_s, s.orbit, s.pull}) {
            if (!std::isfinite(v)) return false;
        }
        return validShape(s.shape) && s.life_s > 0.0f && s.size > 0.0f;
    }

    // Puts a sanitised copy of `in` in a free slot, or the oldest one.
    int place(const Spawn& in) {
        int slot = -1;
        for (int i = 0; i < kMaxThings; ++i) {
            if (!slots_[static_cast<size_t>(i)].alive) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            slot = 0;
            for (int i = 1; i < kMaxThings; ++i) {
                if (slots_[static_cast<size_t>(i)].age > slots_[static_cast<size_t>(slot)].age) slot = i;
            }
        }
        forgetContacts(slot);
        Slot& s = slots_[static_cast<size_t>(slot)];
        s = Slot{};
        s.alive = true;
        s.sp = in;
        Spawn& q = s.sp;
        q.x = std::clamp(in.x, -1.0f, 2.0f);
        q.y = std::clamp(in.y, -1.0f, 2.0f);
        q.vx = std::clamp(in.vx, -kMaxSpeed, kMaxSpeed);
        q.vy = std::clamp(in.vy, -kMaxSpeed, kMaxSpeed);
        q.size = std::clamp(in.size, kMinSize, kMaxSize);
        q.spin = std::clamp(in.spin, -50.0f, 50.0f);
        q.life_s = std::min(in.life_s, kMaxLifeSeconds);
        q.attack_s = std::clamp(in.attack_s, 0.0f, q.life_s);
        q.release_s = std::clamp(in.release_s, 0.0f, q.life_s);
        q.palettePos = in.palettePos - std::floor(in.palettePos);
        q.behaviour = in.behaviour & 15u;
        q.orbit = std::clamp(in.orbit, -20.0f, 20.0f);
        q.pull = std::clamp(in.pull, 0.0f, 2.0f);
        q.chain = std::clamp(in.chain, 0, 3);
        q.chainCount = std::clamp(in.chainCount, 0, 6);
        s.x = q.x;
        s.y = q.y;
        s.vx = q.vx;
        s.vy = q.vy;
        s.size = q.size;
        s.end = static_cast<double>(q.life_s);
        s.from = s.to = q.shape;
        s.seed = ++serial_;
        return slot;
    }

    // Puts `s` at place `i` of `n` on a ring around (cx, cy), far enough out that the ring's own Things do not
    // touch each other at birth, moving outward at `speed`.
    void ring(Spawn& s, float cx, float cy, int i, int n, float speed, float phase) const {
        const float a = phase + 6.283185307f * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        const float c = std::cos(a);
        const float sn = std::sin(a);
        const float sz = std::clamp(s.size, kMinSize, kMaxSize);
        const float d = n > 1 ? 1.2f * sz / std::sin(3.14159265f / static_cast<float>(n)) : 0.0f;
        s.x = cx + c * d / aspect_;
        s.y = cy + sn * d;
        s.vx = c * speed / aspect_;
        s.vy = sn * speed;
    }

    void forgetContacts(int slot) {
        touch_[static_cast<size_t>(slot)].reset();
        for (auto& row : touch_) row.reset(static_cast<size_t>(slot));
    }

    void push(const ThingEvent& e) {
        if (eventCount_ == kEventCapacity) {
            eventHead_ = (eventHead_ + 1) % kEventCapacity;
            --eventCount_;
        }
        events_[static_cast<size_t>((eventHead_ + eventCount_) % kEventCapacity)] = e;
        ++eventCount_;
    }

    static bool drifts(const Slot& s) { return s.alive && (s.sp.behaviour & Drift) != 0u; }

    void substep(float h, const fields::FieldSet* pulls) {
        // Gravity: each Thing with it pulls every drifting one, inverse square, softened; the sum is clamped.
        std::array<float, kMaxThings> ax{};
        std::array<float, kMaxThings> ay{};
        for (int i = 0; i < kMaxThings; ++i) {
            const Slot& g = slots_[static_cast<size_t>(i)];
            if (!g.alive || (g.sp.behaviour & Gravity) == 0u) continue;
            for (int j = 0; j < kMaxThings; ++j) {
                const Slot& t = slots_[static_cast<size_t>(j)];
                if (j == i || !drifts(t)) continue;
                const float dx = (g.x - t.x) * aspect_;
                const float dy = g.y - t.y;
                const float r2 = dx * dx + dy * dy + kSoftening * kSoftening;
                const float f = g.sp.pull / (r2 * std::sqrt(r2));
                ax[static_cast<size_t>(j)] += dx * f / aspect_;
                ay[static_cast<size_t>(j)] += dy * f;
            }
        }
        for (int j = 0; j < kMaxThings; ++j) {
            Slot& t = slots_[static_cast<size_t>(j)];
            if (!drifts(t)) continue;
            float gx = ax[static_cast<size_t>(j)];
            float gy = ay[static_cast<size_t>(j)];
            if (pulls != nullptr) {
                const fields::Force f = pulls->forceAt(t.x, t.y, aspect_);
                gx += f.x;
                gy += f.y;
            }
            const float mag = std::hypot(gx, gy);
            if (mag > kMaxAccel) {
                gx *= kMaxAccel / mag;
                gy *= kMaxAccel / mag;
            }
            t.vx += gx * h;
            t.vy += gy * h;
            if ((t.sp.behaviour & Orbit) != 0u) {
                const float a = t.sp.orbit * h;
                const float c = std::cos(a);
                const float s = std::sin(a);
                const float nx = c * t.vx - s * t.vy;
                t.vy = s * t.vx + c * t.vy;
                t.vx = nx;
            }
            const float speed = std::hypot(t.vx, t.vy);
            if (speed > kMaxSpeed) {
                t.vx *= kMaxSpeed / speed;
                t.vy *= kMaxSpeed / speed;
            }
            t.x += t.vx * h;
            t.y += t.vy * h;
            if ((t.sp.behaviour & Bounce) != 0u) {
                // Never past the middle, or a big Thing on a narrow frame would flip every substep.
                const float mx = std::min(t.size / aspect_, 0.5f - 1e-3f);
                const float my = std::min(t.size, 0.5f - 1e-3f);
                if (t.x < mx) { t.x = 2.0f * mx - t.x; t.vx = std::fabs(t.vx); }
                if (t.x > 1.0f - mx) { t.x = 2.0f * (1.0f - mx) - t.x; t.vx = -std::fabs(t.vx); }
                if (t.y < my) { t.y = 2.0f * my - t.y; t.vy = std::fabs(t.vy); }
                if (t.y > 1.0f - my) { t.y = 2.0f * (1.0f - my) - t.y; t.vy = -std::fabs(t.vy); }
            }
        }
        collide();
    }

    // Two live Things whose circles overlap: one event per contact (the pair is "touching" until it has parted),
    // and an elastic bounce when both carry Bounce.
    // A Thing with no strength (not faded in yet, or at its end) is not there to touch.
    void collide() {
        std::array<bool, kMaxThings> solid{};
        for (int i = 0; i < kMaxThings; ++i) {
            const Slot& s = slots_[static_cast<size_t>(i)];
            solid[static_cast<size_t>(i)] = s.alive && envelope(s.age, s.end, s.sp.attack_s, s.sp.release_s) > 0.0f;
        }
        for (int i = 0; i < kMaxThings; ++i) {
            Slot& a = slots_[static_cast<size_t>(i)];
            if (!solid[static_cast<size_t>(i)]) continue;
            for (int j = i + 1; j < kMaxThings; ++j) {
                Slot& b = slots_[static_cast<size_t>(j)];
                if (!solid[static_cast<size_t>(j)]) continue;
                float dx = (b.x - a.x) * aspect_;
                float dy = b.y - a.y;
                const float reach = a.size + b.size;
                const float dist = std::hypot(dx, dy);
                const bool was = touch_[static_cast<size_t>(i)].test(static_cast<size_t>(j));
                if (dist >= reach) {
                    if (was && dist > reach * 1.05f) touch_[static_cast<size_t>(i)].reset(static_cast<size_t>(j));
                    continue;
                }
                if (!was) {
                    touch_[static_cast<size_t>(i)].set(static_cast<size_t>(j));
                    ThingEvent e;
                    e.a = i;
                    e.b = j;
                    e.x = 0.5f * (a.x + b.x);
                    e.y = 0.5f * (a.y + b.y);
                    push(e);
                }
                if ((a.sp.behaviour & Bounce) == 0u || (b.sp.behaviour & Bounce) == 0u) continue;
                const float wa = drifts(a) ? 1.0f / (a.size * a.size) : 0.0f;
                const float wb = drifts(b) ? 1.0f / (b.size * b.size) : 0.0f;
                if (wa + wb <= 0.0f) continue;
                float nx = 1.0f;
                float ny = 0.0f;
                if (dist > 1e-6f) {
                    nx = dx / dist;
                    ny = dy / dist;
                }
                const float pen = reach - dist;
                a.x -= nx * pen * (wa / (wa + wb)) / aspect_;
                a.y -= ny * pen * (wa / (wa + wb));
                b.x += nx * pen * (wb / (wa + wb)) / aspect_;
                b.y += ny * pen * (wb / (wa + wb));
                // Velocities in the round space, along the normal.
                const float rel = (b.vx - a.vx) * aspect_ * nx + (b.vy - a.vy) * ny;
                if (rel < 0.0f) {
                    const float j2 = -2.0f * rel / (wa + wb);
                    a.vx -= j2 * wa * nx / aspect_;
                    a.vy -= j2 * wa * ny;
                    b.vx += j2 * wb * nx / aspect_;
                    b.vy += j2 * wb * ny;
                }
            }
        }
    }

    // Ages every Thing; the ones past their end go, and the ones with a chain depth owe children.
    void ageAll(double d) {
        for (int i = 0; i < kMaxThings; ++i) {
            Slot& s = slots_[static_cast<size_t>(i)];
            if (!s.alive) continue;
            s.age += d;
            if (s.age < s.end) continue;
            if (s.sp.chain > 0 && s.sp.chainCount > 0 && owedCount_ < kMaxThings) {
                owed_[static_cast<size_t>(owedCount_++)] = Owed{s, s.sp.chainCount};
            }
            s.alive = false;
            forgetContacts(i);
        }
        deliverChildren();
    }

    // At most kMaxChainPerStep children a step, one parent at a time in turn, so parents that die together share
    // the budget (none goes without while another gets it all). What is not delivered is owed to the next steps,
    // so the children that come do not depend on how the time was cut into frames.
    void deliverChildren() {
        int budget = kMaxChainPerStep;
        while (budget > 0 && owedCount_ > 0) {
            if (owedCursor_ >= owedCount_) owedCursor_ = 0;
            Owed& o = owed_[static_cast<size_t>(owedCursor_)];
            child(o.parent, o.parent.sp.chainCount - o.left);
            --budget;
            if (--o.left > 0) {
                ++owedCursor_;
                continue;
            }
            for (int k = owedCursor_; k + 1 < owedCount_; ++k) owed_[static_cast<size_t>(k)] = owed_[static_cast<size_t>(k + 1)];
            --owedCount_;
        }
    }

    // Child `c` of the parent's chainCount, on a ring round the place it died.
    void child(const Slot& par, int c) {
        const int k = par.sp.chainCount;
        const float speed = std::max(std::hypot(par.vx, par.vy) * 0.8f, 0.2f);
        const float phase = static_cast<float>(par.seed % 64u) * 0.1f;
        Spawn ch = par.sp;
        ch.chain = par.sp.chain - 1;
        ch.size = std::max(par.size * 0.5f, kMinSize);
        ch.life_s = std::max(par.sp.life_s * 0.6f, 0.2f);
        ch.behaviour = par.sp.behaviour | Drift;
        ring(ch, par.x, par.y, c, k, speed, phase);
        place(ch);
    }

    // A parent that has died and the children it still owes (deliverChildren).
    struct Owed {
        Slot parent;
        int left = 0;
    };

    std::array<Slot, kMaxThings> slots_{};
    std::array<Owed, kMaxThings> owed_{};
    int owedCount_ = 0;
    int owedCursor_ = 0;
    unsigned revision_ = 0;
    // touch_[a][b] for a < b: the pair is in contact.
    std::array<std::bitset<kMaxThings>, kMaxThings> touch_{};
    std::array<ThingEvent, kEventCapacity> events_{};
    int eventHead_ = 0;
    int eventCount_ = 0;
    double carry_ = 0.0;
    float aspect_ = 1.0f;
    std::uint32_t serial_ = 0;
};

}  // namespace geode::viz::things
