#include "viz/world/WorldGenome.hpp"

#include "viz/world/WorldAssets.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz::world {

namespace {

using V3 = std::array<float, 3>;

constexpr float kTau = 6.2831853f;

// Field bounds, in Field order. Wide enough that the worlds differ, narrow enough that every corner
// still renders: no scale that blows the IFS up, no inversion sphere smaller than a cell's detail.
constexpr Bounds kBounds[kFieldCount] = {
    {0.0f, 1.0f},     // RepXY
    {3.0f, 10.0f},    // CellXY
    {3.0f, 10.0f},    // CellZ
    {0.0f, 1.0f},     // CellShift
    {0.0f, 1.0f},     // Mirror
    {0.0f, 1.5f},     // MirrorOff
    {0.0f, 1.0f},     // Ifs
    {1.0f, 5.0f},     // IfsIter
    {1.15f, 2.2f},    // IfsScale
    {-1.3f, 1.3f},    // IfsAngA
    {-1.3f, 1.3f},    // IfsAngB
    {0.0f, 1.6f},     // IfsOffX
    {0.0f, 1.6f},     // IfsOffY
    {0.0f, 1.6f},     // IfsOffZ
    {0.0f, 1.0f},     // Invert
    {0.6f, 2.4f},     // InvRadius
    {0.0f, 1.0f},     // AbsInv
    {0.2f, 1.0f},     // AbsInvC
    {-1.2f, 1.2f},    // Twist
    {0.6f, 3.0f},     // Scale
    {0.0f, 1.0f},     // PBox
    {0.0f, 1.0f},     // PBar
    {0.0f, 1.0f},     // PTube
    {0.0f, 1.0f},     // PTorus
    {0.0f, 1.0f},     // PShell
    {0.0f, 1.0f},     // PSlab
    {0.0f, 1.0f},     // PMesh
    {0.2f, 2.4f},     // PrimSize
    {0.03f, 0.45f},   // PrimThick
    {0.0f, 5.0f},     // MeshPick (kShapeCount - 1)
    {0.6f, 2.6f},     // MeshSize
    {0.0f, 1.0f},     // MSurface
    {0.0f, 1.0f},     // MGlow
    {0.0f, 1.0f},     // MDensity
    {0.03f, 0.35f},   // DensBand
    {0.0f, 1.0f},     // TrapX
    {0.0f, 1.0f},     // TrapY
    {0.0f, 1.0f},     // TrapZ
    {0.0f, 1.0f},     // TrapR
    {0.0f, 1.0f},     // PalOffset (wraps)
    {0.15f, 1.0f},    // PalSpan
    {0.3f, 1.6f},     // Emission
    {0.015f, 0.12f},  // Fog
    {1.3f, 2.6f},     // Corridor
    {0.0f, 1.0f},     // TexAmount
    {0.0f, 7.0f},     // TexPick (kTexCount - 1)
    {0.25f, 3.0f},    // TexScale
    {0.0f, 1.0f},     // TexEmit
};
static_assert(sizeof(kBounds) / sizeof(kBounds[0]) == kFieldCount, "a bound per field");

enum class Ease { Space, Linear, Wheel };

Ease easeOf(int f) {
    if (f == PalOffset) return Ease::Wheel;
    if (f >= TrapX && f <= PalSpan) return Ease::Linear;
    return Ease::Space;
}

float smootherstep(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float wrap01(float x) { return x - std::floor(x); }

void normalize(Genome& g, int first, int count, const Genome* fallback) {
    float sum = 0.0f;
    for (int i = 0; i < count; ++i) sum += g[first + i];
    if (sum < 1e-4f) {
        for (int i = 0; i < count; ++i) g[first + i] = fallback ? (*fallback)[first + i] : (i == 0 ? 1.0f : 0.0f);
        sum = 0.0f;
        for (int i = 0; i < count; ++i) sum += g[first + i];
    }
    for (int i = 0; i < count; ++i) g[first + i] /= sum;
}

// ---- the GLSL helpers, on the CPU -------------------------------------------------

float clampf(float x, float lo, float hi) { return std::min(std::max(x, lo), hi); }
float mixf(float a, float b, float t) { return a + (b - a) * t; }
float len2(float x, float y) { return std::sqrt(x * x + y * y); }
float len3(const V3& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
float dot3(const V3& v) { return v[0] * v[0] + v[1] * v[1] + v[2] * v[2]; }
// lib_world's wSmin / wSmax: polynomial smooth min, and the smooth max that is never below max().
float smin(float a, float b, float k) {
    const float h = clampf(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
    return mixf(b, a, h) - k * h * (1.0f - h);
}
float smax(float a, float b, float k) { return -smin(-a, -b, k); }
// lib_world's wCell: the mirrored cell. Each cell is its neighbour's reflection, so the field is
// continuous across every wall (a translated repeat cuts any shape that reaches the wall in two).
// Period 2 * size, range -size/2 .. size/2, the identity near 0.
float cell(float x, float size) {
    const float m = x + 0.5f * size;
    const float twice = 2.0f * size;
    return 0.5f * size - std::fabs(m - twice * std::floor(m / twice) - size);
}
void rot(float& x, float& y, float a) {
    const float c = std::cos(a);
    const float s = std::sin(a);
    const float nx = c * x - s * y;
    const float ny = s * x + c * y;
    x = nx;
    y = ny;
}

float boxSd(const V3& q, float b) {
    const float dx = std::fabs(q[0]) - b;
    const float dy = std::fabs(q[1]) - b;
    const float dz = std::fabs(q[2]) - b;
    const float outside = len3({std::max(dx, 0.0f), std::max(dy, 0.0f), std::max(dz, 0.0f)});
    return outside + std::min(std::max(dx, std::max(dy, dz)), 0.0f);
}

// lib_world's wPrims: the six primitives, blended by weight.
float prims(const Genome& g, const V3& q) {
    const float s = g[PrimSize];
    const float t = g[PrimThick];
    const float ax = std::fabs(q[0]);
    const float ay = std::fabs(q[1]);
    const float az = std::fabs(q[2]);
    float d = 0.0f;
    if (g[PBox] > 0.0f) d += g[PBox] * (boxSd(q, s * 0.6f) - t * 0.3f);
    if (g[PBar] > 0.0f) d += g[PBar] * (std::min(std::max(ax, ay), std::min(std::max(ay, az), std::max(az, ax))) - t);
    if (g[PTube] > 0.0f) d += g[PTube] * (std::fabs(len2(q[0], q[1]) - s) - t);
    if (g[PTorus] > 0.0f) d += g[PTorus] * (len2(len2(q[0], q[1]) - s, q[2]) - t);
    if (g[PShell] > 0.0f) d += g[PShell] * (std::fabs(len3(q) - s) - t);
    if (g[PSlab] > 0.0f) d += g[PSlab] * (ay - t);
    if (g[PMesh] > 0.0f) {
        // lib_world's wMesh: two neighbouring baked models, blended by the pick's fraction.
        const float size = g[MeshSize];
        const float pick = g[MeshPick];
        const int a = static_cast<int>(std::floor(pick));
        const int b = std::min(a + 1, kShapeCount - 1);
        const float f = pick - static_cast<float>(a);
        const float da = shapeDistance(a, q[0] / size, q[1] / size, q[2] / size);
        const float db = f > 0.0f ? shapeDistance(b, q[0] / size, q[1] / size, q[2] / size) : da;
        d += g[PMesh] * (da + (db - da) * f) * size;
    }
    return d;
}

float bareWorld(const Genome& g, const Way& way, const V3& p) {
    V3 w = p;
    const float pz = g[CellZ];
    w[2] = cell(p[2] - way.eye[2] + way.zPhase, pz);
    const float sxy = g[CellXY] / std::max(g[RepXY], 0.08f);
    const float shift = 0.5f * g[CellXY] * g[CellShift];
    w[0] = cell(w[0] + shift, sxy);
    w[1] = cell(w[1] + shift, sxy);
    rot(w[0], w[1], g[Twist] * std::sin(kTau * w[2] / pz));
    const float scale = g[Scale];
    V3 q{w[0] / scale, w[1] / scale, w[2] / scale};
    float k = 1.0f / scale;
    for (int i = 0; i < 3; ++i) q[i] = mixf(q[i], std::fabs(q[i]) - g[MirrorOff], g[Mirror]);
    const float steps = std::min(g[IfsIter], static_cast<float>(way.iterCap));
    const float sc = g[IfsScale];
    const V3 off{g[IfsOffX], g[IfsOffY], g[IfsOffZ]};
    for (int i = 0; i < kMaxIfsSteps; ++i) {
        const float f = g[Ifs] * clampf(steps - static_cast<float>(i), 0.0f, 1.0f);
        if (f <= 0.0f) break;
        V3 r{std::fabs(q[0]), std::fabs(q[1]), std::fabs(q[2])};
        rot(r[0], r[1], g[IfsAngA]);
        rot(r[1], r[2], g[IfsAngB]);
        for (int j = 0; j < 3; ++j) r[j] = r[j] * sc - off[j] * (sc - 1.0f);
        for (int j = 0; j < 3; ++j) q[j] = mixf(q[j], r[j], f);
        k *= 1.0f + f * (sc - 1.0f);
    }
    {
        const float rr = g[InvRadius] * g[InvRadius];
        const float m = mixf(1.0f, clampf(rr / std::max(dot3(q), 1e-4f), 0.25f, 4.0f), g[Invert]);
        for (float& c : q) c *= m;
        k *= m;
    }
    {
        const float r2 = std::max(dot3(q), 0.25f);
        const float a = g[AbsInv];
        for (float& c : q) c = mixf(c, std::fabs(c) / r2 - g[AbsInvC], a);
        k *= mixf(1.0f, 1.0f / r2, a);
    }
    return prims(g, q) / k;
}

float corridor(const Genome& g, const Way& way, const V3& p) {
    const float tube = len2(p[0], p[1]) - g[Corridor];
    const V3 e{p[0] - way.eye[0], p[1] - way.eye[1], p[2] - way.eye[2]};
    const float eye = len3(e) - way.eyeRadius;
    // The wake: a capsule from the eye along where it is heading.
    const float ww = dot3(way.wake);
    float h = 0.0f;
    if (ww > 1e-6f) h = clampf((e[0] * way.wake[0] + e[1] * way.wake[1] + e[2] * way.wake[2]) / ww, 0.0f, 1.0f);
    const V3 c{e[0] - way.wake[0] * h, e[1] - way.wake[1] * h, e[2] - way.wake[2] * h};
    const float wake = len3(c) - way.eyeRadius * 0.85f;
    return smin(smin(tube, eye, 0.5f), wake, 0.5f);
}

// ---- the seeds -----------------------------------------------------------------------

Genome base() {
    Genome g;
    for (int f = 0; f < kFieldCount; ++f) g[f] = kBounds[f].lo;
    g[CellXY] = 6.0f;
    g[CellZ] = 6.0f;
    g[IfsIter] = 3.0f;
    g[IfsScale] = 1.6f;
    g[IfsAngA] = 0.3f;
    g[IfsAngB] = 0.3f;
    g[IfsOffX] = 0.8f;
    g[IfsOffY] = 0.8f;
    g[IfsOffZ] = 0.8f;
    g[InvRadius] = 1.4f;
    g[AbsInvC] = 0.6f;
    g[Twist] = 0.0f;
    g[Scale] = 1.0f;
    g[PrimSize] = 0.8f;
    g[PrimThick] = 0.12f;
    g[MSurface] = 1.0f;
    g[DensBand] = 0.12f;
    g[TrapX] = 0.3f;
    g[TrapR] = 0.5f;
    g[PalSpan] = 0.5f;
    g[Emission] = 0.9f;
    g[Fog] = 0.05f;
    g[Corridor] = 1.6f;
    g[MeshSize] = 1.4f;
    g[TexScale] = 1.0f;
    return g;
}

struct Set {
    int field;
    float value;
};
template <size_t N>
Genome make(const Set (&sets)[N]) {
    Genome g = base();
    for (const Set& s : sets) g[s.field] = s.value;
    normalize(g, PBox, 7, nullptr);
    normalize(g, MSurface, 3, nullptr);
    return g;
}

std::array<Seed, 12> buildSeeds() {
    return {{
        // A nave of soap-thin spheres, inverted into one another: bubbles inside bubbles, lit from within.
        {"Bubble Nave", 0.35f, 0.7f, 0.5f,
         make({{RepXY, 1.0f}, {CellXY, 6.0f}, {CellZ, 5.0f}, {CellShift, 1.0f}, {PShell, 1.0f}, {PrimSize, 1.6f}, {PrimThick, 0.05f},
               {Invert, 0.6f}, {InvRadius, 1.1f}, {Mirror, 0.5f}, {MirrorOff, 0.6f}, {MSurface, 0.8f}, {MGlow, 0.2f},
               {TrapR, 0.8f}, {TrapX, 0.2f}, {PalOffset, 0.55f}, {PalSpan, 0.4f}, {Emission, 0.9f}, {Fog, 0.04f}})},
        // Girders crossing girders, folded: the inside of a crane built by a crystal.
        {"Girder Yard", 0.7f, 0.45f, 0.75f,
         make({{RepXY, 1.0f}, {CellXY, 5.0f}, {CellZ, 4.0f}, {CellShift, 0.5f}, {PBar, 1.0f}, {PrimThick, 0.07f}, {Mirror, 1.0f},
               {MirrorOff, 0.9f}, {Ifs, 0.8f}, {IfsIter, 2.0f}, {IfsScale, 1.5f}, {IfsAngA, 0.35f}, {IfsAngB, 0.2f},
               {IfsOffX, 0.8f}, {IfsOffY, 0.6f}, {IfsOffZ, 0.7f}, {MSurface, 1.0f}, {TrapX, 0.6f}, {TrapY, 0.3f},
               {PalOffset, 0.08f}, {PalSpan, 0.6f}, {Emission, 1.0f}, {Fog, 0.05f}, {TexAmount, 0.6f}, {TexPick, 1.0f}, {TexScale, 1.5f}})},
        // Plates of light, cracked and turned: march through them and every edge glows.
        {"Shard Plates", 0.8f, 0.6f, 0.45f,
         make({{RepXY, 0.5f}, {CellXY, 6.0f}, {CellZ, 5.0f}, {PSlab, 1.0f}, {PrimThick, 0.05f}, {Ifs, 1.0f},
               {IfsIter, 3.0f}, {IfsScale, 1.7f}, {IfsAngA, 0.7f}, {IfsAngB, 0.5f}, {IfsOffX, 1.0f}, {IfsOffY, 0.5f}, {Scale, 2.0f}, {CellShift, 1.0f},
               {IfsOffZ, 1.2f}, {MGlow, 1.0f}, {TrapY, 0.7f}, {TrapR, 0.3f}, {PalOffset, 0.78f}, {PalSpan, 0.7f},
               {Emission, 1.1f}, {Fog, 0.035f}})},
        // Boxes folded on boxes: quartz grown in a mirror.
        {"Folded Quartz", 0.5f, 0.55f, 0.7f,
         make({{RepXY, 0.8f}, {CellXY, 6.0f}, {CellZ, 6.0f}, {PBox, 1.0f}, {PrimSize, 0.6f}, {PrimThick, 0.1f},
               {Ifs, 1.0f}, {IfsIter, 4.0f}, {IfsScale, 1.9f}, {Scale, 2.2f}, {CellShift, 0.4f}, {IfsAngA, 0.25f}, {IfsAngB, 0.45f}, {IfsOffX, 1.2f},
               {IfsOffY, 1.0f}, {IfsOffZ, 1.1f}, {MSurface, 0.85f}, {MGlow, 0.15f}, {TrapX, 0.4f}, {TrapZ, 0.5f},
               {PalOffset, 0.4f}, {PalSpan, 0.35f}, {Emission, 0.95f}, {Fog, 0.045f}, {TexAmount, 0.5f}, {TexPick, 4.0f}, {TexEmit, 0.3f}})},
        // Rings that hold heat, twisted down the travel: a furnace you drive through.
        {"Ember Rings", 0.85f, 0.35f, 0.55f,
         make({{RepXY, 0.4f}, {CellXY, 7.0f}, {CellZ, 3.5f}, {PTorus, 1.0f}, {PrimSize, 2.4f}, {PrimThick, 0.2f},
               {Twist, 0.9f}, {Mirror, 0.6f}, {MirrorOff, 0.3f}, {Invert, 0.35f}, {InvRadius, 2.0f}, {MSurface, 0.6f},
               {MGlow, 0.4f}, {TrapR, 0.6f}, {TrapY, 0.4f}, {PalOffset, 0.02f}, {PalSpan, 0.25f}, {Emission, 1.2f},
               {Fog, 0.06f}, {TexAmount, 0.5f}, {TexPick, 2.0f}, {TexEmit, 0.6f}})},
        // A gas of folded inversions: no surface at all, a cloud with a fractal inside.
        {"Fractal Gas", 0.25f, 0.5f, 0.3f,
         make({{RepXY, 0.7f}, {CellXY, 5.0f}, {CellZ, 5.0f}, {PShell, 1.0f}, {PrimSize, 0.7f}, {PrimThick, 0.08f},
               {AbsInv, 0.9f}, {AbsInvC, 0.55f}, {Ifs, 0.5f}, {IfsIter, 2.0f}, {IfsScale, 1.4f}, {IfsOffX, 0.5f},
               {IfsOffY, 0.5f}, {IfsOffZ, 0.5f}, {MDensity, 1.0f}, {DensBand, 0.2f}, {TrapR, 1.0f}, {TrapX, 0.3f},
               {PalOffset, 0.62f}, {PalSpan, 0.8f}, {Emission, 1.0f}, {Fog, 0.03f}})},
        // Sheets of glowing fog hung across the way and turned: the calmest world.
        {"Veil Sheets", 0.15f, 0.65f, 0.25f,
         make({{RepXY, 0.0f}, {CellXY, 8.0f}, {CellZ, 6.0f}, {PSlab, 0.6f}, {PTube, 0.4f}, {PrimSize, 3.2f / 1.6f},
               {PrimThick, 0.06f}, {Twist, 1.1f}, {Mirror, 0.3f}, {MirrorOff, 0.8f}, {MDensity, 1.0f}, {DensBand, 0.1f},
               {TrapY, 0.5f}, {TrapR, 0.4f}, {PalOffset, 0.45f}, {PalSpan, 0.3f}, {Emission, 0.85f}, {Fog, 0.025f}})},
        // Arches on arches, a mirrored aqueduct with no end.
        {"Endless Arches", 0.45f, 0.4f, 0.6f,
         make({{RepXY, 1.0f}, {CellXY, 5.0f}, {CellZ, 4.0f}, {PTube, 0.7f}, {PBar, 0.3f}, {PrimSize, 1.4f},
               {PrimThick, 0.16f}, {Mirror, 1.0f}, {MirrorOff, 0.5f}, {Invert, 0.2f}, {InvRadius, 1.2f}, {MSurface, 1.0f},
               {TrapX, 0.5f}, {TrapZ, 0.3f}, {PalOffset, 0.15f}, {PalSpan, 0.3f}, {Emission, 0.9f}, {Fog, 0.055f}, {TexAmount, 0.7f}, {TexPick, 0.0f}, {TexScale, 0.8f}})},
        // A lattice of small boxes thrown through an abs-inversion: blossoms of light along every edge.
        {"Lattice Bloom", 0.65f, 0.8f, 0.5f,
         make({{RepXY, 1.0f}, {CellXY, 4.0f}, {CellZ, 4.0f}, {PBox, 0.6f}, {PShell, 0.4f}, {PrimSize, 0.5f},
               {PrimThick, 0.06f}, {AbsInv, 0.6f}, {AbsInvC, 0.4f}, {Mirror, 0.7f}, {MirrorOff, 0.4f}, {MGlow, 0.7f},
               {MSurface, 0.3f}, {TrapX, 0.4f}, {TrapR, 0.6f}, {PalOffset, 0.85f}, {PalSpan, 0.55f}, {Emission, 1.1f},
               {Fog, 0.04f}, {TexEmit, 0.7f}, {TexPick, 7.0f}, {TexScale, 1.2f}})},
        // Folded spheres under a tide of light: a reef half stone, half glowing mist.
        {"Coral Drift", 0.4f, 0.6f, 0.65f,
         make({{RepXY, 0.8f}, {CellXY, 5.5f}, {CellZ, 5.5f}, {PShell, 0.7f}, {PTorus, 0.3f}, {PrimSize, 0.9f},
               {PrimThick, 0.12f}, {Ifs, 0.9f}, {IfsIter, 3.0f}, {IfsScale, 1.45f}, {IfsAngA, 0.4f}, {IfsAngB, 0.6f}, {Scale, 2.6f}, {CellShift, 0.35f},
               {IfsOffX, 0.6f}, {IfsOffY, 0.9f}, {IfsOffZ, 0.4f}, {MSurface, 0.55f}, {MDensity, 0.45f}, {DensBand, 0.15f},
               {TrapR, 0.7f}, {TrapZ, 0.4f}, {PalOffset, 0.3f}, {PalSpan, 0.5f}, {Emission, 1.0f}, {Fog, 0.045f}})},
        // Statues and skulls in mirrored niches down a marble gallery: the baked models (WorldAssets).
        {"Reliquary", 0.3f, 0.35f, 0.6f,
         make({{RepXY, 1.0f}, {CellXY, 6.0f}, {CellZ, 5.0f}, {CellShift, 1.0f}, {PMesh, 1.0f}, {MeshPick, 1.0f},
               {MeshSize, 1.8f}, {Mirror, 0.3f}, {MirrorOff, 0.4f}, {MSurface, 1.0f}, {TrapX, 0.3f}, {TrapR, 0.5f},
               {PalOffset, 0.12f}, {PalSpan, 0.3f}, {Emission, 1.4f}, {Fog, 0.03f}, {TexAmount, 0.7f}, {TexPick, 0.0f},
               {TexScale, 1.2f}})},
        // Shells and chandeliers hung in light, half glass, half glow.
        {"Shell Choir", 0.6f, 0.75f, 0.45f,
         make({{RepXY, 1.0f}, {CellXY, 5.0f}, {CellZ, 4.5f}, {CellShift, 1.0f}, {PMesh, 0.85f}, {PShell, 0.15f},
               {PrimSize, 1.2f}, {PrimThick, 0.04f}, {MeshPick, 5.0f}, {MeshSize, 2.3f}, {Invert, 0.2f}, {InvRadius, 1.6f},
               {MSurface, 0.65f}, {MGlow, 0.35f}, {TrapR, 0.6f}, {TrapY, 0.3f}, {PalOffset, 0.7f}, {PalSpan, 0.45f},
               {Emission, 1.05f}, {Fog, 0.04f}, {TexEmit, 0.5f}, {TexPick, 3.0f}, {TexScale, 1.4f}})},
    }};
}

const std::array<Seed, 12>& seeds() {
    static const std::array<Seed, 12> kSeeds = buildSeeds();
    return kSeeds;
}

// A uniform 0..1 from a 64-bit stream.
float unit(uint64_t& s) {
    s = mix64(s);
    return static_cast<float>((s >> 40) & 0xFFFFFFu) / 16777216.0f;
}

}  // namespace

uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

Bounds bounds(int field) { return kBounds[field]; }

bool wraps(int field) { return field == PalOffset; }

Genome lerp(const Genome& a, const Genome& b, float t) {
    if (!(t > 0.0f)) return a;
    if (t >= 1.0f) return b;
    const float space = smootherstep(t);
    Genome out;
    for (int f = 0; f < kFieldCount; ++f) {
        switch (easeOf(f)) {
            case Ease::Space: out[f] = a[f] + (b[f] - a[f]) * space; break;
            case Ease::Linear: out[f] = a[f] + (b[f] - a[f]) * t; break;
            case Ease::Wheel: {
                float d = b[f] - a[f];
                d -= std::floor(d + 0.5f);  // the short way round
                out[f] = wrap01(a[f] + d * t);
                break;
            }
        }
    }
    return out;
}

Genome clampFields(Genome g) {
    for (int f = 0; f < kFieldCount; ++f) {
        if (!std::isfinite(g[f])) g[f] = kBounds[f].lo;
        g[f] = wraps(f) ? wrap01(g[f]) : std::clamp(g[f], kBounds[f].lo, kBounds[f].hi);
    }
    normalize(g, PBox, 7, nullptr);
    normalize(g, MSurface, 3, nullptr);
    return g;
}

bool inBounds(const Genome& g) {
    for (int f = 0; f < kFieldCount; ++f) {
        if (!std::isfinite(g[f])) return false;
        const Bounds b = kBounds[f];
        if (wraps(f) ? (g[f] < 0.0f || g[f] >= 1.0f) : (g[f] < b.lo - 1e-5f || g[f] > b.hi + 1e-5f)) return false;
    }
    for (int first : {static_cast<int>(PBox), static_cast<int>(MSurface)}) {
        const int n = first == PBox ? 7 : 3;
        float sum = 0.0f;
        for (int i = 0; i < n; ++i) sum += g[first + i];
        if (std::fabs(sum - 1.0f) > 1e-3f) return false;
    }
    return true;
}

float sdfUncarved(const Genome& g, const Way& way, const std::array<float, 3>& p) { return bareWorld(g, way, p); }

float sdf(const Genome& g, const Way& way, const std::array<float, 3>& p) {
    return smax(bareWorld(g, way, p), -corridor(g, way, p), 0.5f);
}

Space openSpace(const Genome& g) {
    // A jittered 6 x 6 x 6 lattice in a 12-unit box about the eye, which sits on the travel axis.
    const Way way;
    int open = 0;
    int near = 0;
    int total = 0;
    uint64_t s = 0x5EED0F5BACEull;
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            for (int k = 0; k < 6; ++k) {
                const V3 p{-6.0f + 2.0f * (static_cast<float>(i) + unit(s)), -6.0f + 2.0f * (static_cast<float>(j) + unit(s)),
                           -6.0f + 2.0f * (static_cast<float>(k) + unit(s))};
                const float d = bareWorld(g, way, p);
                if (d > 0.25f) ++open;
                if (d < 0.6f) ++near;
                ++total;
            }
        }
    }
    return {static_cast<float>(open) / static_cast<float>(total), static_cast<float>(near) / static_cast<float>(total)};
}

bool valid(const Genome& g) {
    if (!inBounds(g)) return false;
    const Space s = openSpace(g);
    return s.open >= kMinOpen && s.near >= kMinNear;
}

int seedCount() { return static_cast<int>(seeds().size()); }

const Seed& seed(int index) { return seeds()[static_cast<size_t>(std::clamp(index, 0, seedCount() - 1))]; }

Genome random(uint64_t seedValue, const Constraints& c, int* picked) {
    uint64_t s = mix64(seedValue ^ 0x4D6F727068576F72ull);
    // Pick the seed world: closeness to the music's wants, the current world far less likely.
    float weights[16];
    float total = 0.0f;
    for (int i = 0; i < seedCount(); ++i) {
        const Seed& sd = seed(i);
        const float de = sd.energy - c.energy;
        const float dm = sd.mood - c.mood;
        const float dd = sd.density - c.density;
        float w = std::exp(-(de * de * 1.6f + dm * dm + dd * dd) / 0.18f) + 0.04f;
        if (i == c.avoid) w *= 0.12f;
        weights[i] = w;
        total += w;
    }
    float pick = unit(s) * total;
    int chosen = seedCount() - 1;
    for (int i = 0; i < seedCount(); ++i) {
        if (pick < weights[i]) {
            chosen = i;
            break;
        }
        pick -= weights[i];
    }
    if (picked) *picked = chosen;
    const Genome& origin = seed(chosen).genome;
    const float amount = std::clamp(c.mutation, 0.0f, 1.0f);

    Genome m = origin;
    for (int f = 0; f < kFieldCount; ++f) {
        const Bounds b = kBounds[f];
        const float span = b.hi - b.lo;
        const float scale = (f >= PBox && f <= MDensity) ? 0.6f : 1.0f;
        m[f] += (unit(s) * 2.0f - 1.0f) * amount * span * scale * 0.5f;
    }
    // Far mutations also wake operators the seed leaves off: a new kind of fold, not only new numbers.
    for (int f : {static_cast<int>(Mirror), static_cast<int>(Ifs), static_cast<int>(Invert), static_cast<int>(AbsInv),
                  static_cast<int>(PMesh), static_cast<int>(TexAmount), static_cast<int>(TexEmit)}) {
        if (origin[f] < 0.05f && unit(s) < amount * 0.5f) m[f] = unit(s) * amount;
    }
    m = clampFields(m);
    // Pull back toward the seed until the world is open and has something in it; the seed always is.
    for (int attempt = 0; attempt <= 5; ++attempt) {
        const float t = 1.0f - static_cast<float>(attempt) / 5.0f;
        Genome g;
        for (int f = 0; f < kFieldCount; ++f) g[f] = origin[f] + (m[f] - origin[f]) * t;
        g = clampFields(g);
        if (attempt == 5 || valid(g)) return g;
    }
    return origin;
}

float brightness(const Genome& g) { return g[Emission] * (0.55f * g[MSurface] + 1.0f * g[MGlow] + 0.85f * g[MDensity]); }

}  // namespace geode::viz::world
