#include "viz/ShapeGraph.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace geode::viz::shapes {

namespace {

constexpr float kTwoPi = 6.2831853f;

// The graph's fixed tree, in evaluation order. The structure never changes (a new seed or new params only
// pick new primitives, operations, places, sizes and colours, which all glide or crossfade), so nothing
// structural can ever pop. Each entry: the order the shape joins in as Spawn rises (0 first), whether it
// opens a group, the evaluation index of its group's head, and whether it carves (a cut or a mask) rather
// than adds. Heads that add come first, then their children (adding before carving), then the one carving
// head: a cut is never filled back in by a later union.
struct Node {
    int birth;
    bool head;
    int headIndex;
    bool carves;
};
constexpr std::array<Node, kMaxShapes> kTree = {{
    {0, true, 0, false},   // group A's head: always present, the graph's core
    {2, false, 0, false},  // A's adding child
    {7, false, 0, true},   // A's carving child
    {1, true, 3, false},   // group B's head
    {4, false, 3, true},   // B's carving child
    {3, true, 5, false},   // group C's head
    {6, false, 5, true},   // C's carving child
    {5, true, 7, true},    // the carving head: a group that cuts the scene
}};

// The fewest shapes present (Spawn 0): the two first heads, so a blend always has two sides.
constexpr int kMinAlive = 2;
// The four mixes Formula steps through along 0..1.
constexpr int kFormulaMixes = 4;

float finiteIn(float v, float lo, float hi, float fallback) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; }

uint64_t mixBits(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// A roll in [0, 1) for one slot and one purpose, from the seed alone.
float roll(uint64_t seed, int slot, int purpose) {
    const uint64_t h = mixBits(seed ^ mixBits(static_cast<uint64_t>(slot) * 0x100000001B3ull + static_cast<uint64_t>(purpose)));
    return static_cast<float>(h >> 40) / static_cast<float>(1ull << 24);
}

// Purposes of a roll, so no two draws of a slot share one.
enum Purpose : int { kDirX, kDirY, kDirZ, kReach, kRadius, kDim0, kDim1, kDim2, kPrimRoll, kOpRoll, kColour, kSoft, kPhase0 };

// The primitive mixes Formula steps through: soft (balls, capsules, rings), hard (blocks, diamonds, drums),
// crystal (rounded blocks and diamonds, masked in their groups) and carved (round forms cut by drums).
Prim primFor(int mix, float r) {
    static constexpr Prim kSoft[] = {Prim::Ball, Prim::Capsule, Prim::Ring};
    static constexpr Prim kHard[] = {Prim::Block, Prim::Diamond, Prim::Drum};
    static constexpr Prim kCrystal[] = {Prim::RoundBlock, Prim::Diamond, Prim::Block, Prim::Ball};
    static constexpr Prim kCarved[] = {Prim::Ball, Prim::RoundBlock, Prim::Ring, Prim::Drum};
    const auto pick = [r](const auto& set) {
        const int n = static_cast<int>(std::size(set));
        return set[std::clamp(static_cast<int>(r * static_cast<float>(n)), 0, n - 1)];
    };
    switch (mix) {
        case 0: return pick(kSoft);
        case 1: return pick(kHard);
        case 2: return pick(kCrystal);
        default: return pick(kCarved);
    }
}

// The operation a shape lands with under a mix. The soft mix carves nothing (its carving shapes melt in
// instead); the others cut with the carving head and cut or mask with the carving children.
Op opFor(int mix, const Node& node, bool first, float r) {
    if (node.carves) {
        if (mix == 0) return Op::Blend;
        if (node.head || mix == 1) return Op::Cut;
        if (mix == 2) return Op::Mask;
        return r < 0.5f ? Op::Cut : Op::Mask;
    }
    switch (mix) {
        case 0: return Op::Blend;
        case 1: return (first || r < 0.6f) ? Op::Union : Op::Blend;
        case 2: return (first || r < 0.4f) ? Op::Union : Op::Blend;
        default: return Op::Blend;
    }
}

std::array<float, 3> sizeFor(Prim prim, float r, float d0, float d1, float d2) {
    switch (prim) {
        case Prim::Ball: return {r, r, r};
        case Prim::Block: return {0.8f * r * d0, 0.8f * r * d1, 0.8f * r * d2};
        case Prim::RoundBlock: return {0.85f * r * d0, 0.85f * r * d1, 0.85f * r * d2};
        case Prim::Ring: return {r, r * (0.2f + 0.15f * d1), 0.0f};
        case Prim::Capsule: return {0.55f * r, 0.8f * r * d1, 0.0f};
        case Prim::Diamond: return {1.25f * r, 1.25f * r, 1.25f * r};
        case Prim::Drum: return {0.75f * r * d0, 0.75f * r * d1, 0.0f};
    }
    return {r, r, r};
}

// A direction from three rolls: a point in the cube, pushed off the middle and normalised.
std::array<float, 3> direction(uint64_t seed, int slot) {
    float x = roll(seed, slot, kDirX) * 2.0f - 1.0f;
    float y = (roll(seed, slot, kDirY) * 2.0f - 1.0f) * 0.7f;
    float z = roll(seed, slot, kDirZ) * 2.0f - 1.0f;
    float len = std::sqrt(x * x + y * y + z * z);
    if (len < 1e-3f) {
        x = 1.0f;
        len = 1.0f;
    }
    return {x / len, y / len, z / len};
}

float glideToward(float value, float target, float a) { return value + (target - value) * a; }

float rampToward(float value, float target, float step) {
    return value < target ? std::min(value + step, target) : std::max(value - step, target);
}

}  // namespace

Target targetOf(const Inputs& in) {
    const float spawn = finiteIn(in.spawn, 0.0f, 1.0f, 0.0f);
    const float morph = finiteIn(in.morph, 0.0f, 1.0f, 0.0f);
    const float formula = finiteIn(in.formula, 0.0f, 1.0f, 0.0f);
    const float fractal = finiteIn(in.fractal, 0.0f, 1.0f, 0.0f);
    const float spread = 0.25f + 0.55f * finiteIn(in.shapes, 0.0f, 1.0f, 0.0f);
    const uint64_t seed = in.seed;
    const int mix = std::clamp(static_cast<int>(formula * static_cast<float>(kFormulaMixes)), 0, kFormulaMixes - 1);

    Target t;
    t.alive = std::clamp(kMinAlive + static_cast<int>(std::floor(spawn * 7.0f)), kMinAlive, kMaxShapes);
    t.foldAmount = std::clamp(fractal * 3.0f, 0.0f, 1.0f);
    t.folds = kMinFolds + std::clamp(static_cast<int>(fractal * 5.0f), 0, kMaxFolds - kMinFolds);
    t.melt = 0.05f + 0.3f * morph;

    // Heads first (a child is placed on its head's target), then children.
    std::array<float, kMaxShapes> headRadius{};
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < kMaxShapes; ++i) {
            const Node& node = kTree[static_cast<size_t>(i)];
            if (node.head != (pass == 0)) continue;
            SlotTarget& s = t.slots[static_cast<size_t>(i)];
            s.alive = node.birth < t.alive;
            s.head = node.head;
            s.prim = primFor(mix, roll(seed, i, kPrimRoll));
            s.op = opFor(mix, node, i == 0, roll(seed, i, kOpRoll));
            s.colour = roll(seed, i, kColour);
            const auto dir = direction(seed, i);
            const float d0 = 0.55f + 0.45f * roll(seed, i, kDim0);
            const float d1 = 0.55f + 0.45f * roll(seed, i, kDim1);
            const float d2 = 0.55f + 0.45f * roll(seed, i, kDim2);
            float r = 0.0f;
            if (node.head) {
                r = 0.2f + 0.18f * roll(seed, i, kRadius);
                // The core sits near the middle; the others reach out as far as Shapes spreads them; the carving
                // head sits on the core's rim, where it has something to cut.
                const float reach = i == 0 ? 0.3f * spread * roll(seed, i, kReach)
                                           : spread * (0.45f + 0.55f * roll(seed, i, kReach));
                const auto& core = t.slots[0].place;
                const auto& from = node.carves ? core : std::array<float, 3>{};
                const float out = node.carves ? headRadius[0] * 0.8f : reach;
                for (int c = 0; c < 3; ++c) s.place[static_cast<size_t>(c)] = from[static_cast<size_t>(c)] + dir[static_cast<size_t>(c)] * out;
                headRadius[static_cast<size_t>(i)] = r;
                // One blend radius per group, so a group's own blends stay within what its landing lifts away.
                s.radius = (0.03f + 0.3f * morph) * (0.7f + 0.6f * roll(seed, i, kSoft));
            } else {
                const SlotTarget& head = t.slots[static_cast<size_t>(node.headIndex)];
                const float rh = headRadius[static_cast<size_t>(node.headIndex)];
                float offset = 0.95f;
                float scale = 0.45f + 0.25f * roll(seed, i, kRadius);
                if (s.op == Op::Cut) {
                    offset = 0.75f;
                    scale = 0.5f + 0.2f * roll(seed, i, kRadius);
                } else if (s.op == Op::Mask) {
                    offset = 0.2f;
                    scale = 1.05f + 0.15f * roll(seed, i, kRadius);
                }
                r = rh * scale;
                for (int c = 0; c < 3; ++c) {
                    s.place[static_cast<size_t>(c)] = head.place[static_cast<size_t>(c)] + dir[static_cast<size_t>(c)] * rh * offset;
                }
                s.radius = head.radius;
            }
            s.size = sizeFor(s.prim, r, d0, d1, d2);
        }
    }
    return t;
}

void ShapeGraph::Crossfade::snap(int target) {
    from = target;
    to = target;
    w = 1.0f;
}

void ShapeGraph::Crossfade::step(int target, float amount) {
    if (w >= 1.0f) {
        if (target != to) {
            from = to;
            to = target;
            w = 0.0f;
        }
    } else if (target == from) {
        // Back to where it came from: the same mix, read from the other end.
        std::swap(from, to);
        w = 1.0f - w;
    }
    // A third value waits: the running crossfade lands first, so only two results ever mix.
    w = std::min(w + amount, 1.0f);
    if (w >= 1.0f) from = to;
}

void ShapeGraph::reset() {
    *this = ShapeGraph{};
}

void ShapeGraph::tick(const Inputs& in, float glideDt, float clockDt) {
    const float glide = std::isfinite(glideDt) ? std::max(glideDt, 0.0f) : 0.0f;
    const float clock = std::isfinite(clockDt) ? std::max(clockDt, 0.0f) : 0.0f;
    const float speed = finiteIn(in.speed, 0.0f, 8.0f, 1.0f);
    const Target t = targetOf(in);
    mix_ = finiteIn(in.shapes, 0.0f, 1.0f, 0.0f);

    if (!primed_ || in.seed != seed_) {
        // A new play: its drift starts on phases of its own. The shown shapes still morph to the new target.
        for (int i = 0; i < kMaxShapes; ++i) {
            Slot& s = slots_[static_cast<size_t>(i)];
            for (int c = 0; c < 3; ++c) {
                const size_t ci = static_cast<size_t>(c);
                if (!primed_) s.phase[ci] = kTwoPi * roll(in.seed, i, kPhase0 + c);
                // The rates are the slot's own, whatever the seed.
                s.rate[ci] = kDriftRateLo + (kDriftRateHi - kDriftRateLo) * roll(0x51A9E5ull, i, kPhase0 + c);
            }
        }
        seed_ = in.seed;
    }

    if (!primed_) {
        // The first frame shows its target: nothing was on screen to morph from.
        for (int i = 0; i < kMaxShapes; ++i) {
            Slot& s = slots_[static_cast<size_t>(i)];
            const SlotTarget& g = t.slots[static_cast<size_t>(i)];
            const Node& node = kTree[static_cast<size_t>(i)];
            s.head = node.head;
            s.headIndex = node.headIndex;
            s.place = g.place;
            s.size = g.size;
            s.colour = g.colour;
            s.radius = g.radius;
            s.presence = g.alive ? 1.0f : 0.0f;
            s.prim.snap(static_cast<int>(g.prim));
            s.op.snap(static_cast<int>(g.op));
        }
        folds_.snap(t.folds);
        foldAmount_ = t.foldAmount;
        melt_ = t.melt;
        primed_ = true;
    } else {
        const float a = 1.0f - std::exp(-glide / kGlideSeconds);
        const float ramp = glide / kCrossfadeSeconds;
        for (int i = 0; i < kMaxShapes; ++i) {
            Slot& s = slots_[static_cast<size_t>(i)];
            const SlotTarget& g = t.slots[static_cast<size_t>(i)];
            for (size_t c = 0; c < 3; ++c) {
                s.place[c] = glideToward(s.place[c], g.place[c], a);
                s.size[c] = glideToward(s.size[c], g.size[c], a);
            }
            s.colour = glideToward(s.colour, g.colour, a);
            s.radius = glideToward(s.radius, g.radius, a);
            s.presence = rampToward(s.presence, g.alive ? 1.0f : 0.0f, ramp);
            s.prim.step(static_cast<int>(g.prim), ramp);
            s.op.step(static_cast<int>(g.op), ramp);
        }
        folds_.step(t.folds, ramp);
        foldAmount_ = glideToward(foldAmount_, t.foldAmount, a);
        melt_ = glideToward(melt_, t.melt, a);
    }

    // The drift's clock: Speed times the music's step, integrated per phase so a Speed change moves its rate,
    // never its place, and wrapped a whole turn at a time so the wrap is invisible.
    for (Slot& s : slots_) {
        for (size_t c = 0; c < 3; ++c) s.phase[c] = std::fmod(s.phase[c] + clock * speed * s.rate[c], kTwoPi);
    }
    pack();
}

void ShapeGraph::pack() {
    packed_.shapes.fill(0.0f);
    packed_.count = 0;
    for (int i = 0; i < kMaxShapes; ++i) {
        const Slot& s = slots_[static_cast<size_t>(i)];
        const Slot& head = slots_[static_cast<size_t>(s.headIndex)];
        // A child lives inside its head's presence, so a leaving group takes its children with it.
        const float presence = s.head ? s.presence : s.presence * head.presence;
        float* v = packed_.shapes.data() + static_cast<size_t>(i * kVec4PerShape * 4);
        // A group drifts as one: every shape moves by its head's drift.
        for (size_t c = 0; c < 3; ++c) v[c] = s.place[c] + kDriftReach * std::sin(head.phase[c]);
        v[3] = s.head ? 0.0f : 1.0f;
        for (size_t c = 0; c < 3; ++c) v[4 + c] = s.size[c] * presence;
        v[7] = s.colour;
        v[8] = static_cast<float>(s.prim.from);
        v[9] = static_cast<float>(s.prim.to);
        v[10] = s.prim.w;
        v[11] = presence;
        v[12] = static_cast<float>(s.op.from);
        v[13] = static_cast<float>(s.op.to);
        v[14] = s.op.w;
        v[15] = s.radius;
        if (presence > 0.0f) packed_.count = i + 1;
    }
    packed_.mix = mix_;
    packed_.fold = {static_cast<float>(folds_.from), static_cast<float>(folds_.to), folds_.w, foldAmount_};
    packed_.melt = melt_;
}

// ---- the shader's maths, for the host tests (lib_shapes.glsl is the one the screen runs) ----------------

namespace {

struct V3 {
    float x, y, z;
};
V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float len3(V3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
float len2(float a, float b) { return std::sqrt(a * a + b * b); }
float mixf(float a, float b, float t) { return a + (b - a) * t; }

float block(V3 q, V3 e) {
    const float dx = std::fabs(q.x) - e.x;
    const float dy = std::fabs(q.y) - e.y;
    const float dz = std::fabs(q.z) - e.z;
    const float outside = len3({std::max(dx, 0.0f), std::max(dy, 0.0f), std::max(dz, 0.0f)});
    return outside + std::min(std::max(dx, std::max(dy, dz)), 0.0f);
}

float prim(int kind, V3 q, V3 e) {
    switch (kind) {
        case 0: return len3(q) - e.x;
        case 1: return block(q, e);
        case 2: {
            const float round = 0.25f * std::min(e.x, std::min(e.y, e.z));
            return block(q, {e.x - round, e.y - round, e.z - round}) - round;
        }
        case 3: return len2(len2(q.x, q.z) - e.x, q.y) - e.y;
        case 4: {
            const float y = q.y - std::clamp(q.y, -e.y, e.y);
            return len3({q.x, y, q.z}) - e.x;
        }
        case 5: return (std::fabs(q.x) + std::fabs(q.y) + std::fabs(q.z) - e.x) * 0.57735027f;
        default: {
            const float dr = len2(q.x, q.z) - e.x;
            const float dh = std::fabs(q.y) - e.y;
            return std::min(std::max(dr, dh), 0.0f) + len2(std::max(dr, 0.0f), std::max(dh, 0.0f));
        }
    }
}

float primMix(const float* v, V3 q) {
    const V3 e{v[4], v[5], v[6]};
    const float a = prim(static_cast<int>(v[8] + 0.5f), q, e);
    if (v[10] <= 0.0f) return a;
    return mixf(a, prim(static_cast<int>(v[9] + 0.5f), q, e), v[10]);
}

Sample sminCol(Sample a, Sample b, float k) {
    k = std::max(k, 1e-4f);
    const float h = std::clamp(0.5f + 0.5f * (b.distance - a.distance) / k, 0.0f, 1.0f);
    return {mixf(b.distance, a.distance, h) - k * h * (1.0f - h), mixf(b.colour, a.colour, h)};
}

Sample smaxCol(Sample a, Sample b, float k) {
    const Sample r = sminCol({-a.distance, a.colour}, {-b.distance, b.colour}, k);
    return {-r.distance, r.colour};
}

Sample opOnto(Sample acc, Sample s, int op, float k, float presence) {
    // A shape on its way in or out is lifted off by up to one and a half blend radii, past the reach of its
    // own and its group's blends, so at presence 0 it touches nothing (lib_shapes' shapeOp).
    const Sample lifted{s.distance + (1.0f - presence) * 1.5f * std::max(k, 1e-4f), s.colour};
    switch (op) {
        case 0: return lifted.distance < acc.distance ? lifted : acc;
        case 1: return sminCol(acc, lifted, k);
        case 2: return smaxCol(acc, {-lifted.distance, lifted.colour}, k);
        default: {
            const Sample m = smaxCol(acc, s, k);
            return {mixf(acc.distance, m.distance, presence), mixf(acc.colour, m.colour, presence)};
        }
    }
}

Sample opsOnto(Sample acc, Sample s, const float* o, float presence) {
    const Sample a = opOnto(acc, s, static_cast<int>(o[0] + 0.5f), o[3], presence);
    if (o[2] <= 0.0f) return a;
    const Sample b = opOnto(acc, s, static_cast<int>(o[1] + 0.5f), o[3], presence);
    return {mixf(a.distance, b.distance, o[2]), mixf(a.colour, b.colour, o[2])};
}

V3 foldAbout(V3 p, float folds) {
    const float n = std::max(folds, 1.0f);
    const float r = len2(p.x, p.z);
    const float sector = kTwoPi / n;
    const float ang = std::atan2(p.z, p.x);
    const float m = ang - sector * std::floor(ang / sector);
    const float folded = std::fabs(m - 0.5f * sector);
    return {r * std::cos(folded), p.y, r * std::sin(folded)};
}

V3 foldSpace(const Packed& g, V3 p) {
    const float amount = g.fold[3];
    if (amount <= 0.0f) return p;
    V3 f = foldAbout(p, g.fold[0]);
    if (g.fold[2] > 0.0f) {
        const V3 b = foldAbout(p, g.fold[1]);
        f = {mixf(f.x, b.x, g.fold[2]), mixf(f.y, b.y, g.fold[2]), mixf(f.z, b.z, g.fold[2])};
    }
    return {mixf(p.x, f.x, amount), mixf(p.y, f.y, amount), mixf(p.z, f.z, amount)};
}

}  // namespace

Sample sample(const Packed& g, float x, float y, float z) {
    Sample scene{kFar, 0.0f};
    if (g.count <= 0) return scene;
    const V3 p = foldSpace(g, {x, y, z});
    Sample group{kFar, 0.0f};
    const float* land = nullptr;
    float landPresence = 0.0f;
    bool open = false;
    for (int i = 0; i < std::min(g.count, kMaxShapes); ++i) {
        const float* v = g.shapes.data() + static_cast<size_t>(i * kVec4PerShape * 4);
        const bool head = v[3] < 0.5f;
        const float presence = v[11];
        if (head) {
            if (open) scene = opsOnto(scene, group, land, landPresence);
            open = presence > 0.0f;
            if (!open) continue;
            group = {primMix(v, sub(p, {v[0], v[1], v[2]})), v[7]};
            land = v + 12;
            landPresence = presence;
        } else {
            if (!open || presence <= 0.0f) continue;
            const Sample s{primMix(v, sub(p, {v[0], v[1], v[2]})), v[7]};
            group = opsOnto(group, s, v + 12, presence);
        }
    }
    if (open) scene = opsOnto(scene, group, land, landPresence);
    return scene;
}

Sample sampleInto(const Packed& g, float d, float colour, float x, float y, float z) {
    if (!(g.mix > 0.0f) || g.count <= 0) return {d, colour};
    const Sample s = sample(g, x, y, z);
    const Sample melted = sminCol({d, colour}, s, g.melt);
    return {mixf(d, melted.distance, g.mix), mixf(colour, melted.colour, g.mix)};
}

}  // namespace geode::viz::shapes
