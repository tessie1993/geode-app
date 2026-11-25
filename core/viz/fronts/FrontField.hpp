#pragma once
#include <algorithm>
#include <array>
#include <cmath>

// Fronts: up to eight lines, rings and wedges that move across the picture and, inside their band, brighten,
// shift colour, bend, invert or reveal it. This is the state and the maths; FrontPass (viz/passes) draws it.
// It is the engine under Sweep across, Ripple from the hit, Shockwave and Scanner. Pure and header-only, so it
// runs on the host (core/tests/fronts_test.cpp).
//
// Time is seconds from the frame's real dt: a front's place is speed x age, whatever the frame rate, and
// nothing here knows a tempo. Space is the frame's uv (0..1, y up) for an origin, and frame heights for a
// distance, so a speed of 1 crosses the frame's height in a second and a ring is round at any aspect.
// A front never carries a colour: it carries a palette position, which the renderer resolves on the
// current palette.
//
// Called on the render thread only (the frame steps it; whoever spawns does so there too).
namespace geode::viz::fronts {

inline constexpr int kMaxFronts = 8;
// Floats a front takes in the packed uniform array: three vec4.
inline constexpr int kFloatsPerFront = 12;

enum class Kind : int { Line = 0, Ring = 1, Wedge = 2 };
enum class Effect : int { Brighten = 0, ColourShift = 1, Bend = 2, Invert = 3, Reveal = 4 };
enum class Edge : int { Left, Right, Bottom, Top };

struct Spawn {
    Kind kind = Kind::Line;
    Effect effect = Effect::Brighten;
    float originX = 0.5f;      // uv 0..1; a Line passes through it at birth, a Ring or Wedge grows from it
    float originY = 0.5f;
    float dirX = 1.0f;         // the way a Line or Wedge travels (any length; a Ring ignores it)
    float dirY = 0.0f;
    float speed = 0.5f;        // frame heights a second
    float width = 0.05f;       // the band's half width, in frame heights
    float strength = 1.0f;     // 0..1
    float life_s = 2.0f;       // seconds
    float palettePos = 0.0f;   // 0..1 on the current palette (ColourShift; the tint of the others)
    float spread = 0.7f;       // a Wedge's half angle, radians

    // A front that enters at `edge` and travels inward, square to it: a Sweep across when it is a Line.
    static Spawn fromEdge(Edge edge, Kind kind) {
        Spawn s;
        s.kind = kind;
        switch (edge) {
            case Edge::Left: s.originX = 0.0f; s.originY = 0.5f; s.dirX = 1.0f; s.dirY = 0.0f; break;
            case Edge::Right: s.originX = 1.0f; s.originY = 0.5f; s.dirX = -1.0f; s.dirY = 0.0f; break;
            case Edge::Bottom: s.originX = 0.5f; s.originY = 0.0f; s.dirX = 0.0f; s.dirY = 1.0f; break;
            case Edge::Top: s.originX = 0.5f; s.originY = 1.0f; s.dirX = 0.0f; s.dirY = -1.0f; break;
        }
        return s;
    }
};

// The live fronts as the pass reads them, the age already folded in.
struct Packed {
    int count = 0;
    std::array<float, kMaxFronts * kFloatsPerFront> data{};
    // Per front, the palette position the renderer turns into a colour.
    std::array<float, kMaxFronts> palettePos{};
};

// How much of its strength a front has at `age` of `life`: in over the first tenth, out over the last third.
inline float envelope(float age, float life) {
    if (!(life > 0.0f) || !(age >= 0.0f) || age >= life) return 0.0f;
    const float t = age / life;
    const float in = std::min(1.0f, t / 0.1f);
    const float out = std::min(1.0f, (1.0f - t) * 3.0f);
    const float k = std::min(in, out);
    return k * k * (3.0f - 2.0f * k);
}

class FrontField {
public:
    // Starts a front and returns its slot, or -1 when the request is not usable (a non-finite number, or no
    // life). Every slot taken: the oldest is reused, so a hit never goes unanswered.
    int spawnFront(const Spawn& in) {
        if (!usable(in)) return -1;
        int slot = -1;
        for (int i = 0; i < kMaxFronts; ++i) {
            if (!slots_[static_cast<size_t>(i)].alive) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            slot = 0;
            for (int i = 1; i < kMaxFronts; ++i) {
                if (slots_[static_cast<size_t>(i)].age > slots_[static_cast<size_t>(slot)].age) slot = i;
            }
        }
        Slot& s = slots_[static_cast<size_t>(slot)];
        s = Slot{};
        s.alive = true;
        s.spawn = in;
        s.spawn.originX = std::clamp(in.originX, -1.0f, 2.0f);
        s.spawn.originY = std::clamp(in.originY, -1.0f, 2.0f);
        const float len = std::hypot(in.dirX, in.dirY);
        s.spawn.dirX = len > 1e-6f ? in.dirX / len : 1.0f;
        s.spawn.dirY = len > 1e-6f ? in.dirY / len : 0.0f;
        s.spawn.speed = std::max(in.speed, 0.0f);
        s.spawn.width = std::max(in.width, 1e-4f);
        s.spawn.strength = std::clamp(in.strength, 0.0f, 1.0f);
        s.spawn.palettePos = in.palettePos - std::floor(in.palettePos);
        s.spawn.spread = std::clamp(in.spread, 0.01f, 3.1f);
        return slot;
    }

    // Ages every front by `dt` seconds; a front past its life is gone. A non-finite or non-positive dt changes nothing.
    void step(float dt) {
        if (!std::isfinite(dt) || dt <= 0.0f) return;
        for (Slot& s : slots_) {
            if (!s.alive) continue;
            s.age += static_cast<double>(dt);
            if (s.age >= static_cast<double>(s.spawn.life_s)) s.alive = false;
        }
    }

    void clear() { slots_ = {}; }

    int count() const {
        int n = 0;
        for (const Slot& s : slots_) n += s.alive ? 1 : 0;
        return n;
    }
    // False: nothing to draw, and the frame skips the pass.
    bool live() const { return count() > 0; }

    bool alive(int slot) const { return slot >= 0 && slot < kMaxFronts && slots_[static_cast<size_t>(slot)].alive; }
    // Seconds since the front in `slot` was spawned; 0 for a free slot.
    float age(int slot) const { return alive(slot) ? static_cast<float>(slots_[static_cast<size_t>(slot)].age) : 0.0f; }
    // How far it has travelled, in frame heights; 0 for a free slot.
    float distance(int slot) const {
        if (!alive(slot)) return 0.0f;
        const Slot& s = slots_[static_cast<size_t>(slot)];
        return static_cast<float>(static_cast<double>(s.spawn.speed) * s.age);
    }

    // The live fronts, in slot order, as the pass's uniform arrays:
    //   [0] originX, originY, dirX, dirY
    //   [1] kind, effect, distance travelled (frame heights), half width (frame heights)
    //   [2] amount (strength x envelope), wedge half angle, palette position, 0
    Packed pack() const {
        Packed out;
        for (const Slot& s : slots_) {
            if (!s.alive) continue;
            float* f = out.data.data() + static_cast<size_t>(out.count) * kFloatsPerFront;
            const Spawn& sp = s.spawn;
            f[0] = sp.originX;
            f[1] = sp.originY;
            f[2] = sp.dirX;
            f[3] = sp.dirY;
            f[4] = static_cast<float>(sp.kind);
            f[5] = static_cast<float>(sp.effect);
            f[6] = static_cast<float>(static_cast<double>(sp.speed) * s.age);
            f[7] = sp.width;
            f[8] = sp.strength * envelope(static_cast<float>(s.age), sp.life_s);
            f[9] = sp.spread;
            f[10] = sp.palettePos;
            f[11] = 0.0f;
            out.palettePos[static_cast<size_t>(out.count)] = sp.palettePos;
            ++out.count;
        }
        return out;
    }

private:
    struct Slot {
        bool alive = false;
        double age = 0.0;
        Spawn spawn;
    };

    static bool usable(const Spawn& s) {
        for (float v : {s.originX, s.originY, s.dirX, s.dirY, s.speed, s.width, s.strength, s.life_s, s.palettePos, s.spread}) {
            if (!std::isfinite(v)) return false;
        }
        return s.life_s > 0.0f;
    }

    std::array<Slot, kMaxFronts> slots_{};
};

}  // namespace geode::viz::fronts
