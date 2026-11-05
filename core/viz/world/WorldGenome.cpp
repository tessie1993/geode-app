#include "viz/world/WorldGenome.hpp"

#include "viz/world/WorldAssets.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace geode::viz::world {

namespace {

using V3 = std::array<float, 3>;

constexpr float kTau = 6.2831853f;
// How softly the ground joins the world's matter (lib_world's WG_FLOOR_BLEND).
constexpr float kFloorBlend = 0.4f;

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
    {0.0f, 1.0f},     // Polar
    {3.0f, 12.0f},    // PolarN
    {-0.6f, 0.6f},    // PolarTwist
    {0.6f, 3.0f},     // Scale
    {0.0f, 1.0f},     // PBox
    {0.0f, 1.0f},     // PBar
    {0.0f, 1.0f},     // PTube
    {0.0f, 1.0f},     // PTorus
    {0.0f, 1.0f},     // PShell
    {0.0f, 1.0f},     // PSlab
    {0.0f, 1.0f},     // PMesh
    {0.0f, 1.0f},     // PGyroid
    {0.0f, 1.0f},     // PMenger
    {0.0f, 1.0f},     // PBulb
    {0.0f, 1.0f},     // PMandelbox
    {0.0f, 1.0f},     // PSierp
    {0.0f, 1.0f},     // PApollo
    {0.0f, 1.0f},     // PJulia
    {0.0f, 1.0f},     // PKlein
    {0.0f, 1.0f},     // PKifs
    {0.2f, 2.4f},     // PrimSize
    {0.03f, 0.45f},   // PrimThick
    {0.0f, 23.0f},    // MeshPick (kShapeCount - 1)
    {0.6f, 2.6f},     // MeshSize
    {2.0f, 9.0f},     // BulbPower
    {0.8f, 4.0f},     // GyroScale
    {1.5f, 3.0f},     // BoxScale
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
    {-0.5f, 0.5f},    // Roll
    {0.75f, 1.35f},   // Lens
    {0.0f, 1.0f},     // Floor
    {-6.0f, -3.0f},   // FloorY (under the deepest eye: the spiral path's -2.0, less the 0.9 bubble)
    {0.0f, 1.0f},     // FloorGloss
    {0.0f, 1.0f},     // Sky
    {0.0f, 1.0f},     // SkyHue
    {0.0f, 1.0f},     // Horizon
    {0.0f, 1.0f},     // Terrain
    {0.5f, 4.0f},     // TerrainHeight
    {0.05f, 0.5f},    // TerrainScale
    {-0.8f, 0.8f},    // LatticeTilt
    {-0.8f, 0.8f},    // LatticeYaw
    {0.0f, 0.5f},     // GazePitch
    {0.0f, 0.6f},     // GazeYaw
    {-0.8f, 0.4f},    // JuliaCX: mostly inside the Mandelbrot set's main body, so the set holds together
    {-0.8f, 0.8f},    // JuliaCY
    {-0.8f, 0.8f},    // JuliaCZ
    {-0.8f, 0.8f},    // JuliaCW
    {0.8f, 1.25f},    // KleinBox
    {1.8f, 2.8f},     // KifsScale
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

// lib_world's wGyroid: an endless gyroid sheet of thickness t, at frequency k.
float gyroid(const V3& q, float k, float t) {
    const float g = (std::sin(q[0] * k) * std::cos(q[1] * k) + std::sin(q[1] * k) * std::cos(q[2] * k) +
                     std::sin(q[2] * k) * std::cos(q[0] * k)) / k;
    return std::fabs(g) * 0.55f - t * 0.5f;
}

// GLSL's mod: x - y * floor(x / y).
float glslMod(float x, float y) { return x - y * std::floor(x / y); }

// lib_world's wMenger: a Menger sponge of half-size s, three levels.
float menger(const V3& q, float s) {
    const V3 p{q[0] / s, q[1] / s, q[2] / s};
    float d = boxSd(p, 1.0f);
    float scale = 1.0f;
    for (int m = 0; m < 3; ++m) {
        V3 a;
        for (int i = 0; i < 3; ++i) a[i] = glslMod(p[i] * scale, 2.0f) - 1.0f;
        scale *= 3.0f;
        V3 rr;
        for (int i = 0; i < 3; ++i) rr[i] = std::fabs(1.0f - 3.0f * std::fabs(a[i]));
        const float da = std::max(rr[0], rr[1]);
        const float db = std::max(rr[1], rr[2]);
        const float dc = std::max(rr[2], rr[0]);
        d = std::max(d, (std::min(da, std::min(db, dc)) - 1.0f) / scale);
    }
    return d * s;
}

// lib_world's wBulb: a Mandelbulb of power n, scaled so it fills about s, five iterations.
float bulb(const V3& q, float s, float n) {
    const float k = 0.8f * s;
    const V3 p{q[0] / k, q[1] / k, q[2] / k};
    V3 z = p;
    float dr = 1.0f;
    float r = len3(z);
    for (int i = 0; i < 5; ++i) {
        r = len3(z);
        if (r > 2.0f) break;
        const float rs = std::max(r, 1e-6f);
        const float theta = std::acos(clampf(z[2] / rs, -1.0f, 1.0f)) * n;
        const float phi = std::atan2(z[1], z[0]) * n;
        dr = std::pow(rs, n - 1.0f) * n * dr + 1.0f;
        const float zr = std::pow(rs, n);
        z = {zr * std::sin(theta) * std::cos(phi) + p[0], zr * std::sin(phi) * std::sin(theta) + p[1],
             zr * std::cos(theta) + p[2]};
    }
    r = std::max(r, 1e-6f);
    return 0.5f * std::log(r) * r / dr * k;
}

// lib_world's wMandelbox: a Mandelbox of scale -b (box fold, sphere fold), six iterations, its
// extent (about 2 (b + 1) / (b - 1)) brought to about 2 s.
float mandelbox(const V3& q, float s, float b) {
    const float k = 0.35f * s;
    const V3 p{q[0] / k, q[1] / k, q[2] / k};
    const float scale = -b;
    V3 z = p;
    float dr = 1.0f;
    for (int i = 0; i < 6; ++i) {
        for (float& c : z) c = clampf(c, -1.0f, 1.0f) * 2.0f - c;
        const float r2 = dot3(z);
        if (r2 < 0.25f) {
            for (float& c : z) c *= 4.0f;
            dr *= 4.0f;
        } else if (r2 < 1.0f) {
            for (float& c : z) c /= r2;
            dr /= r2;
        }
        for (int j = 0; j < 3; ++j) z[j] = z[j] * scale + p[j];
        dr = dr * std::fabs(scale) + 1.0f;
    }
    return len3(z) / std::fabs(dr) * k;
}

// lib_world's wSierp: a Sierpinski tetrahedron (corners at (1,1,1) and its three mirror
// images), seven folds toward the nearest corner, scaled by s.
float sierpinski(const V3& q, float s) {
    V3 z{q[0] / s, q[1] / s, q[2] / s};
    float scale = 1.0f;
    for (int i = 0; i < 7; ++i) {
        if (z[0] + z[1] < 0.0f) {
            const float t = z[0];
            z[0] = -z[1];
            z[1] = -t;
        }
        if (z[0] + z[2] < 0.0f) {
            const float t = z[0];
            z[0] = -z[2];
            z[2] = -t;
        }
        if (z[1] + z[2] < 0.0f) {
            const float t = z[1];
            z[1] = -z[2];
            z[2] = -t;
        }
        for (float& c : z) c = c * 2.0f - 1.0f;
        scale *= 2.0f;
    }
    return (len3(z) - 1.0f) / scale * s;
}

// lib_world's wApollo: an Apollonian packing, seven inversions in a lattice of unit cells,
// scaled by s; the distance to its sheets (|y| in the last cell).
float apollonian(const V3& q, float s) {
    V3 p{q[0] / s, q[1] / s, q[2] / s};
    float scale = 1.0f;
    for (int i = 0; i < 7; ++i) {
        for (float& c : p) c = -1.0f + 2.0f * (0.5f * c + 0.5f - std::floor(0.5f * c + 0.5f));
        const float k = 1.2f / std::max(dot3(p), 1e-4f);
        for (float& c : p) c *= k;
        scale *= k;
    }
    return 0.25f * std::fabs(p[1]) / scale * s;
}

// lib_world's wJulia: a quaternion Julia set z -> z^2 + c on the slice w = 0, real part x, filling
// about s, eight iterations; the Hubbard-Douady bound 0.5 |z| log |z| / |dz|, held at one unit
// deep inside the set.
float julia(const V3& q, float s, const std::array<float, 4>& c) {
    const float k = 0.7f * s;
    std::array<float, 4> z{q[0] / k, q[1] / k, q[2] / k, 0.0f};
    float dr = 1.0f;
    for (int i = 0; i < 8; ++i) {
        const float r2 = z[0] * z[0] + z[1] * z[1] + z[2] * z[2] + z[3] * z[3];
        if (r2 > 16.0f) break;
        dr = 2.0f * std::sqrt(r2) * dr;
        const float v2 = z[1] * z[1] + z[2] * z[2] + z[3] * z[3];
        const float a = z[0];
        z = {a * a - v2 + c[0], 2.0f * a * z[1] + c[1], 2.0f * a * z[2] + c[2], 2.0f * a * z[3] + c[3]};
    }
    const float r = std::max(std::sqrt(z[0] * z[0] + z[1] * z[1] + z[2] * z[2] + z[3] * z[3]), 1e-6f);
    return std::max(0.5f * std::log(r) * r / std::max(dr, 1e-6f), -1.0f) * k;
}

// lib_world's wKlein: a pseudo-Kleinian limit set. Fold into the box of half-size b by its face
// reflections (the mirrored cell), invert in the unit sphere (stretch capped at 16), seven times;
// one last fold, then a slab of half-thickness t; held in a ball of 1.3 s.
float klein(const V3& q, float s, float b, float t) {
    const float k = 0.5f * s;
    V3 z{q[0] / k, q[1] / k, q[2] / k};
    float dr = 1.0f;
    for (int i = 0; i < 7; ++i) {
        for (float& c : z) c = cell(c, 2.0f * b);
        const float m = clampf(1.0f / std::max(dot3(z), 1e-4f), 1.0f, 16.0f);
        for (float& c : z) c *= m;
        dr *= m;
    }
    for (float& c : z) c = cell(c, 2.0f * b);
    const float d = (std::fabs(z[1]) - t) / dr * k;
    return std::max(d, len3(q) - 1.3f * s);
}

// lib_world's wKifs: an octahedral kaleidoscopic IFS. Fold across the octahedron's mirror planes
// (abs, then sort x >= y >= z), scale by sc about the vertex (1, 0, 0), seven times; the distance
// to the unit octahedron at the last level. Its hull is the octahedron of radius 1.2 s.
float kifs(const V3& q, float s, float sc) {
    const float k = 1.2f * s;
    V3 z{q[0] / k, q[1] / k, q[2] / k};
    float scale = 1.0f;
    for (int i = 0; i < 7; ++i) {
        for (float& c : z) c = std::fabs(c);
        if (z[0] < z[1]) std::swap(z[0], z[1]);
        if (z[0] < z[2]) std::swap(z[0], z[2]);
        if (z[1] < z[2]) std::swap(z[1], z[2]);
        z = {z[0] * sc - (sc - 1.0f), z[1] * sc, z[2] * sc};
        scale *= sc;
    }
    return (std::fabs(z[0]) + std::fabs(z[1]) + std::fabs(z[2]) - 1.0f) * 0.57735f / scale * k;
}

// lib_world's wPolar: the plane about the travel axis folded into one mirrored wedge of n.
void polarFold(float& x, float& y, float n) {
    const float r = len2(x, y);
    const float seg = kTau / n;
    float a = std::atan2(y, x);
    a = std::fabs(glslMod(a + seg * 0.5f, seg) - seg * 0.5f);
    x = r * std::cos(a);
    y = r * std::sin(a);
}

// lib_world's wPrims: the sixteen primitives, blended by weight.
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
    if (g[PGyroid] > 0.0f) d += g[PGyroid] * gyroid(q, g[GyroScale], t);
    if (g[PMenger] > 0.0f) d += g[PMenger] * menger(q, s);
    if (g[PBulb] > 0.0f) d += g[PBulb] * bulb(q, s, g[BulbPower]);
    if (g[PMandelbox] > 0.0f) d += g[PMandelbox] * mandelbox(q, s, g[BoxScale]);
    if (g[PSierp] > 0.0f) d += g[PSierp] * sierpinski(q, s);
    if (g[PApollo] > 0.0f) d += g[PApollo] * apollonian(q, s);
    if (g[PJulia] > 0.0f) d += g[PJulia] * julia(q, s, {g[JuliaCX], g[JuliaCY], g[JuliaCZ], g[JuliaCW]});
    if (g[PKlein] > 0.0f) d += g[PKlein] * klein(q, s, g[KleinBox], t);
    if (g[PKifs] > 0.0f) d += g[PKifs] * kifs(q, s, g[KifsScale]);
    return d;
}

// ---- the landscape (lib_world's wTerrain, the same numbers) -----------------------------------
//
// A ridged fBm height field in world space with valley walls to the sides, its distance the
// vertical gap times the height field's Lipschitz bound 1 / sqrt(1 + s^2). The noise hashes
// integer lattice points (no sin, no float hash), so this port builds the GPU's ground.

constexpr int kTerrainOctaves = 5;
constexpr float kTerrainGain = 0.45f;
// The ridged sum's steepest slope per unit of base frequency and relief: value noise rises at most
// 3 per lattice unit (the cubic fade's 1.5 times a corner difference of 2), ridging keeps that, and
// the octaves add sum(gain^i 2^i) / sum(gain^i) = 2.295 of it. 3 x 2.295, rounded up.
constexpr float kTerrainSlope = 6.9f;
constexpr float kTerrainWall = 2.0f;
constexpr float kTerrainWidth = 8.0f;
// WALL x max(d/dx x^2 / (x^2 + W^2)) = WALL x 0.6495 / W.
constexpr float kTerrainWallSlope = 0.1625f;
constexpr float kTerrainSink = 8.0f;
constexpr float kTerrainFar = 80.0f;
// CameraRig::kTravelWrap, and the stretch before it that blends into the ground behind the wrap.
constexpr float kTravelWrap = 8192.0f;
constexpr float kWrapLo = 7692.0f;
constexpr float kWrapHi = 8092.0f;
constexpr float kWrapSlope = 0.004f;

// lib_world's wHash: Knuth's multiplicative hash per axis, then xor-shift-multiply rounds.
uint32_t latticeHash(int32_t x, int32_t y) {
    uint32_t h = (static_cast<uint32_t>(x) * 0x9E3779B1u) ^ (static_cast<uint32_t>(y) * 0x85EBCA77u);
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

// lib_world's wLattice: the hash's top 24 bits as -1..1, exact in any float.
float lattice(int32_t x, int32_t y) { return static_cast<float>(latticeHash(x, y) >> 8) * (1.0f / 8388608.0f) - 1.0f; }

// lib_world's wValue: smooth value noise, -1..1, lattice unit 1, cubic fade.
float valueNoise(float sx, float sy) {
    const float ix = std::floor(sx);
    const float iy = std::floor(sy);
    const float fx = sx - ix;
    const float fy = sy - iy;
    const float ux = fx * fx * (3.0f - 2.0f * fx);
    const float uy = fy * fy * (3.0f - 2.0f * fy);
    const auto x = static_cast<int32_t>(ix);
    const auto y = static_cast<int32_t>(iy);
    const float a = lattice(x, y);
    const float b = lattice(x + 1, y);
    const float c = lattice(x, y + 1);
    const float d = lattice(x + 1, y + 1);
    return mixf(mixf(a, b, ux), mixf(c, d, ux), uy);
}

// lib_world's wRidged: ridged fBm, 0..1, each octave turned (a 3-4-5 rotation) and twice as fine.
float ridged(float sx, float sy) {
    float sum = 0.0f;
    float norm = 0.0f;
    float amp = 1.0f;
    for (int i = 0; i < kTerrainOctaves; ++i) {
        const float n = valueNoise(sx, sy);
        sum += amp * (1.0f - std::fabs(n));
        norm += amp;
        amp *= kTerrainGain;
        const float nx = (0.8f * sx - 0.6f * sy) * 2.0f + 5.3f;
        const float ny = (0.6f * sx + 0.8f * sy) * 2.0f - 2.9f;
        sx = nx;
        sy = ny;
    }
    return sum / norm;
}

// GLSL's smoothstep.
float smoothstepf(float e0, float e1, float x) {
    const float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// lib_world's wLandscape: the ridged ground at world (x, z), the wrap's last stretch blended into
// the ground 8192 behind it, so the eye crosses the rig's travel wrap onto the same ground.
float landscape(float x, float z, float scale) {
    float h = ridged(x * scale, z * scale);
    const float w = smoothstepf(kWrapLo, kWrapHi, z);
    if (w > 0.0f) h = mixf(h, ridged(x * scale, (z - kTravelWrap) * scale), w);
    return h;
}

// lib_world's wTerrain: the landscape's distance, rising from under FloorY as Terrain fades in.
float terrain(const Genome& g, const V3& p) {
    const float on = g[Terrain];
    const float relief = on * g[TerrainHeight];
    const float base = g[FloorY] - (1.0f - on) * kTerrainSink;
    const float h = landscape(p[0], p[2], g[TerrainScale]);
    const float v = p[0] * p[0] / (p[0] * p[0] + kTerrainWidth * kTerrainWidth);
    const float y = base + relief * (h + kTerrainWall * v);
    const float slope = relief * (kTerrainSlope * g[TerrainScale] + kTerrainWallSlope + kWrapSlope);
    float d = (p[1] - y) / std::sqrt(1.0f + slope * slope);
    d = std::max(d, p[1] - (base + relief * (1.0f + kTerrainWall)));
    return mixf(kTerrainFar, d, std::min(on * 8.0f, 1.0f));
}

float bareWorld(const Genome& g, const Way& way, const V3& p) {
    V3 w = p;
    const float pz = g[CellZ];
    w[2] = cell(p[2] - way.eye[2] + way.zPhase, pz);
    if (g[Polar] > 0.0f) {
        // The kaleidoscope: both neighbouring segment counts, blended by the fraction, then faded in.
        float ax = w[0];
        float ay = w[1];
        rot(ax, ay, g[PolarTwist] * w[2]);
        float bx = ax;
        float by = ay;
        const float n0 = std::floor(g[PolarN]);
        const float fn = g[PolarN] - n0;
        polarFold(ax, ay, n0);
        if (fn > 0.0f) polarFold(bx, by, n0 + 1.0f);
        else {
            bx = ax;
            by = ay;
        }
        w[0] = mixf(w[0], mixf(ax, bx, fn), g[Polar]);
        w[1] = mixf(w[1], mixf(ay, by, fn), g[Polar]);
    }
    if (g[LatticeTilt] != 0.0f || g[LatticeYaw] != 0.0f) {
        // The lattice turned off the view (lib_world's world(), line for line): tipped about x,
        // turned about y, then the turned z folded again on the same period. An isometry and a
        // fold, so the distance stays a true bound and the travel repeat stays periodic.
        rot(w[1], w[2], g[LatticeTilt]);
        rot(w[2], w[0], g[LatticeYaw]);
        w[2] = cell(w[2], pz);
    }
    const float sxy = g[CellXY] / std::max(g[RepXY], 0.08f);
    const float shift = 0.5f * g[CellXY] * g[CellShift];
    // The shift moves the cells' content out along +x, the side the kaleidoscope folds onto, so a
    // polar world rings its content round the axis at that radius; y moves only without the fold.
    w[0] = cell(w[0] - shift, sxy);
    w[1] = cell(w[1] + shift * (1.0f - g[Polar]), sxy);
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
    float d = prims(g, q) / k;
    if (g[Floor] > 0.0f) {
        // The ground, joined smoothly, then faded in: continuous in Floor, the identity at 0.
        const float fd = p[1] - g[FloorY];
        d = mixf(d, smin(d, fd, kFloorBlend), g[Floor]);
    }
    // The landscape, joined by min (lib_world's wWithTerrain).
    return g[Terrain] > 0.0f ? std::min(d, terrain(g, p)) : d;
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
    g[PolarN] = 6.0f;
    g[BulbPower] = 8.0f;
    g[GyroScale] = 1.6f;
    g[BoxScale] = 2.0f;
    g[TexScale] = 1.0f;
    g[Roll] = 0.0f;
    g[Lens] = 1.0f;
    g[Floor] = 0.0f;
    g[FloorY] = -3.0f;
    g[FloorGloss] = 0.5f;
    g[Sky] = 0.0f;
    g[SkyHue] = 0.5f;
    g[Horizon] = 0.0f;
    g[Terrain] = 0.0f;
    g[TerrainHeight] = 1.5f;
    g[TerrainScale] = 0.15f;
    g[LatticeTilt] = 0.0f;
    g[LatticeYaw] = 0.0f;
    g[GazePitch] = 0.0f;
    g[GazeYaw] = 0.0f;
    g[JuliaCX] = -0.2f;
    g[JuliaCY] = 0.6f;
    g[JuliaCZ] = 0.0f;
    g[JuliaCW] = 0.0f;
    g[KleinBox] = 1.0f;
    g[KifsScale] = 2.0f;
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
    normalize(g, PBox, kPrimCount, nullptr);
    normalize(g, MSurface, 3, nullptr);
    return g;
}

std::array<Seed, 67> buildSeeds() {
    return {{
        // A nave of soap-thin spheres, inverted into one another: bubbles inside bubbles, lit from within.
        {"Bubble Nave", 0.35f, 0.7f, 0.5f,
         make({{LatticeTilt, 0.35f}, {LatticeYaw, 0.25f}, {RepXY, 1.0f}, {CellXY, 6.0f}, {CellZ, 5.0f}, {CellShift, 1.0f}, {PShell, 1.0f}, {PrimSize, 1.6f}, {PrimThick, 0.05f},
               {Invert, 0.6f}, {InvRadius, 1.1f}, {Mirror, 0.5f}, {MirrorOff, 0.6f}, {MSurface, 0.8f}, {MGlow, 0.2f},
               {TrapR, 0.8f}, {TrapX, 0.2f}, {PalOffset, 0.55f}, {PalSpan, 0.4f}, {Emission, 0.9f}, {Fog, 0.04f}, {GazePitch, 0.3f}, {GazeYaw, 0.3f}})},
        // Girders crossing girders, folded: the inside of a crane built by a crystal, over a wet yard at dusk.
        {"Girder Yard", 0.7f, 0.45f, 0.75f,
         make({{LatticeTilt, 0.45f}, {LatticeYaw, -0.3f}, {RepXY, 1.0f}, {CellXY, 5.0f}, {CellZ, 4.0f}, {CellShift, 0.5f}, {PBar, 1.0f}, {PrimThick, 0.07f}, {Mirror, 1.0f},
               {MirrorOff, 0.9f}, {Ifs, 0.8f}, {IfsIter, 2.0f}, {IfsScale, 1.5f}, {IfsAngA, 0.35f}, {IfsAngB, 0.2f},
               {IfsOffX, 0.8f}, {IfsOffY, 0.6f}, {IfsOffZ, 0.7f}, {MSurface, 1.0f}, {TrapX, 0.6f}, {TrapY, 0.3f},
               {PalOffset, 0.08f}, {PalSpan, 0.6f}, {Emission, 1.0f}, {Fog, 0.05f}, {TexAmount, 0.6f}, {TexPick, 1.0f}, {TexScale, 1.5f}, {GazePitch, 0.4f}, {GazeYaw, 0.3f},
               {Floor, 1.0f}, {FloorY, -3.5f}, {FloorGloss, 0.6f}, {Sky, 0.7f}, {SkyHue, 0.12f}, {Horizon, 0.6f}})},
        // Plates of light, cracked and turned: march through them and every edge glows.
        {"Shard Plates", 0.8f, 0.6f, 0.45f,
         make({{RepXY, 0.5f}, {CellXY, 6.0f}, {CellZ, 5.0f}, {PSlab, 1.0f}, {PrimThick, 0.05f}, {Ifs, 1.0f},
               {IfsIter, 3.0f}, {IfsScale, 1.7f}, {IfsAngA, 0.7f}, {IfsAngB, 0.5f}, {IfsOffX, 1.0f}, {IfsOffY, 0.5f}, {Scale, 2.0f}, {CellShift, 1.0f},
               {IfsOffZ, 1.2f}, {MGlow, 1.0f}, {TrapY, 0.7f}, {TrapR, 0.3f}, {PalOffset, 0.78f}, {PalSpan, 0.7f},
               {Emission, 1.1f}, {Fog, 0.035f}})},
        // Boxes folded on boxes: quartz grown in a mirror.
        {"Folded Quartz", 0.5f, 0.55f, 0.7f,
         make({{LatticeTilt, 0.2f}, {LatticeYaw, 0.4f}, {RepXY, 0.8f}, {CellXY, 6.0f}, {CellZ, 6.0f}, {PBox, 1.0f}, {PrimSize, 0.6f}, {PrimThick, 0.1f},
               {Ifs, 1.0f}, {IfsIter, 4.0f}, {IfsScale, 1.9f}, {Scale, 2.2f}, {CellShift, 0.4f}, {IfsAngA, 0.25f}, {IfsAngB, 0.45f}, {IfsOffX, 1.2f},
               {IfsOffY, 1.0f}, {IfsOffZ, 1.1f}, {MSurface, 0.85f}, {MGlow, 0.15f}, {TrapX, 0.4f}, {TrapZ, 0.5f},
               {PalOffset, 0.4f}, {PalSpan, 0.35f}, {Emission, 0.95f}, {Fog, 0.045f}, {TexAmount, 0.5f}, {TexPick, 4.0f}, {TexEmit, 0.3f}})},
        // Rings that hold heat, twisted down the travel: a furnace you drive through.
        {"Ember Rings", 0.85f, 0.35f, 0.55f,
         make({{RepXY, 0.4f}, {CellXY, 7.0f}, {CellZ, 3.5f}, {PTorus, 1.0f}, {PrimSize, 2.4f}, {PrimThick, 0.2f},
               {Twist, 0.9f}, {Mirror, 0.6f}, {MirrorOff, 0.3f}, {Invert, 0.35f}, {InvRadius, 2.0f}, {MSurface, 0.6f},
               {MGlow, 0.4f}, {TrapR, 0.6f}, {TrapY, 0.4f}, {PalOffset, 0.02f}, {PalSpan, 0.25f}, {Emission, 1.2f},
               {Fog, 0.06f}, {TexAmount, 0.5f}, {TexPick, 2.0f}, {TexEmit, 0.6f}, {GazePitch, 0.05f}, {GazeYaw, 0.05f}})},
        // A gas of folded inversions: no surface at all, a cloud with a fractal inside.
        {"Fractal Gas", 0.25f, 0.5f, 0.3f,
         make({{LatticeTilt, -0.3f}, {LatticeYaw, 0.35f}, {RepXY, 0.7f}, {CellXY, 5.0f}, {CellZ, 5.0f}, {PShell, 1.0f}, {PrimSize, 0.7f}, {PrimThick, 0.08f},
               {AbsInv, 0.9f}, {AbsInvC, 0.55f}, {Ifs, 0.5f}, {IfsIter, 2.0f}, {IfsScale, 1.4f}, {IfsOffX, 0.5f},
               {IfsOffY, 0.5f}, {IfsOffZ, 0.5f}, {MDensity, 1.0f}, {DensBand, 0.2f}, {TrapR, 1.0f}, {TrapX, 0.3f},
               {PalOffset, 0.62f}, {PalSpan, 0.8f}, {Emission, 1.0f}, {Fog, 0.03f}})},
        // Sheets of glowing fog hung across the way and turned: the calmest world.
        {"Veil Sheets", 0.15f, 0.65f, 0.25f,
         make({{RepXY, 0.0f}, {CellXY, 8.0f}, {CellZ, 6.0f}, {PSlab, 0.6f}, {PTube, 0.4f}, {PrimSize, 3.2f / 1.6f},
               {PrimThick, 0.06f}, {Twist, 1.1f}, {Mirror, 0.3f}, {MirrorOff, 0.8f}, {MDensity, 1.0f}, {DensBand, 0.1f},
               {TrapY, 0.5f}, {TrapR, 0.4f}, {PalOffset, 0.45f}, {PalSpan, 0.3f}, {Emission, 0.85f}, {Fog, 0.025f}})},
        // Arches on arches, a mirrored aqueduct with no end, standing in still water under a pale sky.
        {"Endless Arches", 0.45f, 0.4f, 0.6f,
         make({{LatticeTilt, 0.15f}, {LatticeYaw, 0.3f}, {RepXY, 1.0f}, {CellXY, 5.0f}, {CellZ, 4.0f}, {PTube, 0.7f}, {PBar, 0.3f}, {PrimSize, 1.4f},
               {PrimThick, 0.16f}, {Mirror, 1.0f}, {MirrorOff, 0.5f}, {Invert, 0.2f}, {InvRadius, 1.2f}, {MSurface, 1.0f},
               {TrapX, 0.5f}, {TrapZ, 0.3f}, {PalOffset, 0.15f}, {PalSpan, 0.3f}, {Emission, 0.9f}, {Fog, 0.055f}, {TexAmount, 0.7f}, {TexPick, 0.0f}, {TexScale, 0.8f}, {GazePitch, 0.35f}, {GazeYaw, 0.35f},
               {Floor, 1.0f}, {FloorY, -3.2f}, {FloorGloss, 0.8f}, {Sky, 0.8f}, {SkyHue, 0.6f}, {Horizon, 0.7f}})},
        // A lattice of small boxes thrown through an abs-inversion: blossoms of light along every edge,
        // mirrored in a dark glass floor.
        {"Lattice Bloom", 0.65f, 0.8f, 0.5f,
         make({{LatticeTilt, 0.5f}, {LatticeYaw, 0.5f}, {RepXY, 1.0f}, {CellXY, 4.0f}, {CellZ, 4.0f}, {PBox, 0.6f}, {PShell, 0.4f}, {PrimSize, 0.5f},
               {PrimThick, 0.06f}, {AbsInv, 0.6f}, {AbsInvC, 0.4f}, {Mirror, 0.7f}, {MirrorOff, 0.4f}, {MGlow, 0.7f},
               {MSurface, 0.3f}, {TrapX, 0.4f}, {TrapR, 0.6f}, {PalOffset, 0.85f}, {PalSpan, 0.55f}, {Emission, 1.1f},
               {Fog, 0.04f}, {TexEmit, 0.7f}, {TexPick, 7.0f}, {TexScale, 1.2f}, {GazePitch, 0.25f}, {GazeYaw, 0.25f},
               {Floor, 0.7f}, {FloorY, -4.0f}, {FloorGloss, 0.9f}, {Sky, 0.5f}, {SkyHue, 0.85f}, {Horizon, 0.8f}})},
        // Folded spheres under a tide of light: a reef half stone, half glowing mist.
        {"Coral Drift", 0.4f, 0.6f, 0.65f,
         make({{LatticeTilt, -0.25f}, {LatticeYaw, 0.3f}, {RepXY, 0.8f}, {CellXY, 5.5f}, {CellZ, 5.5f}, {PShell, 0.7f}, {PTorus, 0.3f}, {PrimSize, 0.9f},
               {PrimThick, 0.12f}, {Ifs, 0.9f}, {IfsIter, 3.0f}, {IfsScale, 1.45f}, {IfsAngA, 0.4f}, {IfsAngB, 0.6f}, {Scale, 2.6f}, {CellShift, 0.35f},
               {IfsOffX, 0.6f}, {IfsOffY, 0.9f}, {IfsOffZ, 0.4f}, {MSurface, 0.55f}, {MDensity, 0.45f}, {DensBand, 0.15f},
               {TrapR, 0.7f}, {TrapZ, 0.4f}, {PalOffset, 0.3f}, {PalSpan, 0.5f}, {Emission, 1.0f}, {Fog, 0.045f}})},
        // Statues and skulls in mirrored niches down a marble gallery: the baked models (WorldAssets). A
        // third of glow fills the dark between the niches, so the gallery never reads as a black hall.
        {"Reliquary", 0.3f, 0.35f, 0.6f,
         make({{LatticeTilt, -0.15f}, {LatticeYaw, 0.35f}, {RepXY, 1.0f}, {CellXY, 6.0f}, {CellZ, 5.0f}, {CellShift, 1.0f}, {PMesh, 1.0f}, {MeshPick, 1.0f},
               {MeshSize, 1.8f}, {Mirror, 0.3f}, {MirrorOff, 0.4f}, {MSurface, 0.7f}, {MGlow, 0.3f}, {TrapX, 0.3f},
               {TrapR, 0.5f}, {PalOffset, 0.38f}, {PalSpan, 0.3f}, {Emission, 1.4f}, {Fog, 0.025f}, {TexAmount, 0.7f}, {TexPick, 0.0f},
               {TexScale, 1.2f}, {GazePitch, 0.15f}, {GazeYaw, 0.45f}})},
        // Shells and chandeliers hung in light, half glass, half glow.
        {"Shell Choir", 0.6f, 0.75f, 0.45f,
         make({{LatticeTilt, 0.4f}, {LatticeYaw, -0.2f}, {RepXY, 1.0f}, {CellXY, 5.0f}, {CellZ, 4.5f}, {CellShift, 1.0f}, {PMesh, 0.85f}, {PShell, 0.15f},
               {PrimSize, 1.2f}, {PrimThick, 0.04f}, {MeshPick, 5.0f}, {MeshSize, 2.3f}, {Invert, 0.2f}, {InvRadius, 1.6f},
               {MSurface, 0.55f}, {MGlow, 0.45f}, {TrapR, 0.6f}, {TrapY, 0.3f}, {PalOffset, 0.3f}, {PalSpan, 0.45f},
               {Emission, 1.05f}, {Fog, 0.025f}, {TexEmit, 0.5f}, {TexPick, 3.0f}, {TexScale, 1.4f}})},
        // Mandelbulbs grown in mirrored cells, lichen on their folds: a garden of fractal fruit.
        {"Bulb Garden", 0.45f, 0.6f, 0.65f,
         make({{LatticeTilt, 0.3f}, {LatticeYaw, 0.4f}, {RepXY, 1.0f}, {CellXY, 5.0f}, {CellZ, 5.0f}, {CellShift, 1.0f}, {PBulb, 1.0f}, {BulbPower, 8.0f},
               {PrimSize, 1.3f}, {MSurface, 0.8f}, {MGlow, 0.2f}, {TrapR, 0.6f}, {TrapX, 0.3f}, {PalOffset, 0.55f},
               {PalSpan, 0.6f}, {Emission, 1.1f}, {Fog, 0.03f}, {TexAmount, 0.5f}, {TexPick, 3.0f}, {GazePitch, 0.3f}, {GazeYaw, 0.4f}})},
        // Menger sponges set in a hexagonal hall: the kaleidoscope turns the square sponge round.
        {"Sponge Hall", 0.55f, 0.45f, 0.75f,
         make({{RepXY, 0.6f}, {CellXY, 6.0f}, {CellZ, 4.0f}, {CellShift, 0.6f}, {PMenger, 1.0f}, {PrimSize, 1.4f},
               {Polar, 1.0f}, {PolarN, 6.0f}, {MSurface, 0.85f}, {MGlow, 0.15f}, {TrapX, 0.4f}, {TrapZ, 0.4f},
               {PalOffset, 0.08f}, {PalSpan, 0.45f}, {Emission, 1.0f}, {Fog, 0.03f}, {TexAmount, 0.5f}, {TexPick, 6.0f}})},
        // One endless gyroid sheet, breathing light through its holes: a lung of glass.
        {"Gyroid Lung", 0.35f, 0.7f, 0.4f,
         make({{RepXY, 0.0f}, {CellXY, 8.0f}, {CellZ, 6.0f}, {PGyroid, 1.0f}, {GyroScale, 1.5f}, {PrimThick, 0.1f},
               {MSurface, 0.35f}, {MGlow, 0.65f}, {TrapR, 0.5f}, {TrapY, 0.4f}, {PalOffset, 0.9f}, {PalSpan, 0.55f},
               {Emission, 1.05f}, {Fog, 0.03f}, {Corridor, 1.8f}, {TexEmit, 0.4f}, {TexPick, 4.0f}})},
        // Rings and bubbles folded seven ways and turned into a spiral: a rose you fly down.
        {"Rose Tunnel", 0.6f, 0.75f, 0.5f,
         make({{RepXY, 1.0f}, {CellXY, 4.0f}, {CellZ, 3.5f}, {CellShift, 1.0f}, {Polar, 1.0f}, {PolarN, 7.0f},
               {PolarTwist, 0.25f}, {PTorus, 0.6f}, {PShell, 0.4f}, {PrimSize, 1.0f}, {PrimThick, 0.1f}, {Mirror, 0.4f},
               {MirrorOff, 0.3f}, {MSurface, 0.6f}, {MGlow, 0.4f}, {TrapR, 0.6f}, {PalOffset, 0.95f}, {PalSpan, 0.5f},
               {Emission, 1.1f}, {Fog, 0.03f}, {Roll, 0.1f}, {GazePitch, 0.06f}, {GazeYaw, 0.06f}})},
        // Horses ridden round the travel axis, eight of them: a carousel without end.
        {"Carousel", 0.65f, 0.7f, 0.55f,
         make({{RepXY, 0.3f}, {CellXY, 7.0f}, {CellZ, 4.5f}, {CellShift, 0.9f}, {Polar, 1.0f}, {PolarN, 8.0f},
               {PMesh, 1.0f}, {MeshPick, 4.0f}, {MeshSize, 1.4f}, {MSurface, 0.75f}, {MGlow, 0.25f}, {TrapR, 0.5f},
               {PalOffset, 0.4f}, {PalSpan, 0.35f}, {Emission, 1.2f}, {Fog, 0.03f}, {TexAmount, 0.6f}, {TexPick, 0.0f}, {Roll, -0.12f}})},
        // Bull and cow skulls melted into one another, five round the way, in a lichen haze.
        {"Bone Orchard", 0.3f, 0.3f, 0.5f,
         make({{RepXY, 0.4f}, {CellXY, 6.0f}, {CellZ, 5.0f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 5.0f},
               {PMesh, 1.0f}, {MeshPick, 2.5f}, {MeshSize, 1.6f}, {MSurface, 0.7f}, {MDensity, 0.3f}, {DensBand, 0.12f},
               {TrapR, 0.5f}, {PalOffset, 0.45f}, {PalSpan, 0.4f}, {Emission, 1.2f}, {Fog, 0.03f}, {TexAmount, 0.5f},
               {TexPick, 3.0f}})},
        // Shells spiralling nine ways down the way, glowing like a lantern of nacre.
        {"Shell Spiral", 0.5f, 0.8f, 0.45f,
         make({{RepXY, 0.4f}, {CellXY, 6.0f}, {CellZ, 4.0f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 9.0f},
               {PolarTwist, 0.35f}, {PMesh, 1.0f}, {MeshPick, 0.0f}, {MeshSize, 1.5f}, {MSurface, 0.5f}, {MGlow, 0.5f},
               {TrapR, 0.6f}, {PalOffset, 0.75f}, {PalSpan, 0.5f}, {Emission, 1.1f}, {Fog, 0.03f}, {TexEmit, 0.4f},
               {TexPick, 4.0f}, {Roll, 0.15f}})},
        // Mushrooms ringed eight ways in a grove, a gyroid moss between them, cedar-bark skin, on a forest floor.
        {"Mushroom Grove", 0.3f, 0.55f, 0.6f,
         make({{RepXY, 1.0f}, {CellXY, 5.5f}, {CellZ, 5.0f}, {CellShift, 1.0f}, {PMesh, 0.8f}, {PGyroid, 0.2f},
               {MeshPick, 6.0f}, {MeshSize, 1.8f}, {GyroScale, 2.5f}, {PrimThick, 0.06f}, {Polar, 1.0f}, {PolarN, 8.0f},
               {MSurface, 0.8f}, {MGlow, 0.2f}, {TrapR, 0.5f}, {PalOffset, 0.45f}, {PalSpan, 0.45f}, {Emission, 1.1f}, {Fog, 0.03f}, {TexAmount, 0.6f},
               {TexPick, 5.0f}, {GazePitch, 0.2f}, {GazeYaw, 0.3f},
               {Floor, 1.0f}, {FloorY, -3.4f}, {FloorGloss, 0.15f}, {Sky, 0.6f}, {SkyHue, 0.4f}, {Horizon, 0.5f}})},
        // Ferns hung six ways round the way and lit from within: a green cathedral.
        {"Fern Cathedral", 0.4f, 0.65f, 0.45f,
         make({{RepXY, 0.5f}, {CellXY, 6.0f}, {CellZ, 4.5f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 6.0f},
               {PMesh, 1.0f}, {MeshPick, 7.0f}, {MeshSize, 2.2f}, {MSurface, 0.5f}, {MGlow, 0.5f}, {TrapR, 0.5f},
               {PalOffset, 0.35f}, {PalSpan, 0.4f}, {Emission, 1.15f}, {Fog, 0.03f}})},
        // Dead trunks twisted along the way in bark, six round it: driftwood in a slow current.
        {"Driftwood", 0.25f, 0.4f, 0.55f,
         make({{RepXY, 0.8f}, {CellXY, 5.5f}, {CellZ, 5.0f}, {CellShift, 1.0f}, {PMesh, 1.0f}, {MeshPick, 10.0f},
               {MeshSize, 2.2f}, {Twist, 0.6f}, {Polar, 1.0f}, {PolarN, 6.0f}, {MSurface, 0.7f}, {MGlow, 0.3f}, {TrapX, 0.4f},
               {PalOffset, 0.4f},
               {PalSpan, 0.35f}, {Emission, 1.1f}, {Fog, 0.03f}, {TexAmount, 0.8f}, {TexPick, 5.0f}})},
        // Succulents opened ten ways, half stone, half glowing mist: a bloom you pass through.
        {"Succulent Bloom", 0.45f, 0.75f, 0.5f,
         make({{RepXY, 0.4f}, {CellXY, 6.0f}, {CellZ, 4.0f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 10.0f},
               {PMesh, 1.0f}, {MeshPick, 11.0f}, {MeshSize, 1.6f}, {MSurface, 0.6f}, {MDensity, 0.4f}, {DensBand, 0.1f},
               {TrapR, 0.5f}, {PalOffset, 0.6f}, {PalSpan, 0.45f}, {Emission, 1.15f}, {Fog, 0.03f}})},
        // Quiver trees ringed seven ways under a glow: a desert forest at dusk, its sand meeting a burning horizon.
        {"Quiver Forest", 0.35f, 0.5f, 0.5f,
         make({{RepXY, 1.0f}, {CellXY, 5.0f}, {CellZ, 5.0f}, {CellShift, 1.0f}, {PMesh, 1.0f}, {MeshPick, 8.0f},
               {MeshSize, 2.4f}, {Polar, 1.0f}, {PolarN, 7.0f}, {MSurface, 0.6f}, {MGlow, 0.4f}, {TrapR, 0.5f},
               {PalOffset, 0.3f}, {PalSpan, 0.4f},
               {Emission, 1.15f}, {Fog, 0.03f}, {TexAmount, 0.5f}, {TexPick, 5.0f}, {GazePitch, 0.3f}, {GazeYaw, 0.25f},
               {Floor, 1.0f}, {FloorY, -3.6f}, {FloorGloss, 0.1f}, {Sky, 0.9f}, {SkyHue, 0.05f}, {Horizon, 0.9f}})},
        // Low-power bulbs fused with sponges, folded five ways, in circuit light: a crystal machine.
        {"Crystal Bulb", 0.75f, 0.6f, 0.6f,
         make({{RepXY, 0.5f}, {CellXY, 6.0f}, {CellZ, 4.5f}, {CellShift, 0.7f}, {Polar, 1.0f}, {PolarN, 5.0f},
               {PBulb, 0.7f}, {PMenger, 0.3f}, {BulbPower, 5.0f}, {PrimSize, 1.4f}, {MSurface, 0.4f}, {MGlow, 0.6f},
               {TrapR, 0.6f}, {TrapY, 0.3f}, {PalOffset, 0.65f}, {PalSpan, 0.55f}, {Emission, 1.1f}, {Fog, 0.03f},
               {TexEmit, 0.5f}, {TexPick, 7.0f}})},
        // Mandelboxes in mirrored cells: folded halls of boxes inside boxes, marble underfoot.
        {"Mandelbox Vault", 0.5f, 0.5f, 0.7f,
         make({{LatticeTilt, -0.35f}, {LatticeYaw, 0.25f}, {RepXY, 1.0f}, {CellXY, 6.0f}, {CellZ, 6.0f}, {CellShift, 1.0f}, {PMandelbox, 1.0f}, {BoxScale, 2.0f},
               {PrimSize, 1.2f}, {MSurface, 0.8f}, {MGlow, 0.2f}, {TrapR, 0.6f}, {TrapX, 0.3f}, {PalOffset, 0.5f},
               {PalSpan, 0.55f}, {Emission, 1.15f}, {Fog, 0.03f}, {TexAmount, 0.4f}, {TexPick, 0.0f}, {GazePitch, 0.35f}, {GazeYaw, 0.3f}})},
        // A wider-scaled Mandelbox folded eight ways: a round cathedral whose walls are its own fractal.
        {"Box Cathedral", 0.6f, 0.45f, 0.65f,
         make({{RepXY, 0.4f}, {CellXY, 7.0f}, {CellZ, 5.0f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 8.0f},
               {PMandelbox, 1.0f}, {BoxScale, 2.6f}, {PrimSize, 1.3f}, {MSurface, 0.6f}, {MGlow, 0.4f}, {TrapR, 0.6f},
               {PalOffset, 0.08f}, {PalSpan, 0.5f}, {Emission, 1.15f}, {Fog, 0.03f}, {TexEmit, 0.3f}, {TexPick, 7.0f}, {Roll, 0.05f}})},
        // Sierpinski tetrahedra folded three ways round the way: a triangular forest of spires.
        {"Sierpinski Spires", 0.55f, 0.6f, 0.55f,
         make({{RepXY, 0.5f}, {CellXY, 6.0f}, {CellZ, 4.5f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 3.0f},
               {PSierp, 1.0f}, {PrimSize, 1.4f}, {MSurface, 0.8f}, {MGlow, 0.2f}, {TrapR, 0.5f}, {TrapY, 0.3f},
               {PalOffset, 0.62f}, {PalSpan, 0.5f}, {Emission, 1.15f}, {Fog, 0.03f}, {TexAmount, 0.5f}, {TexPick, 4.0f}, {GazePitch, 0.35f}, {GazeYaw, 0.15f}})},
        // Small Sierpinski tetrahedra in rows, twisted and glowing: a choir of triangles.
        {"Triangle Choir", 0.7f, 0.7f, 0.6f,
         make({{LatticeTilt, 0.3f}, {LatticeYaw, -0.4f}, {RepXY, 1.0f}, {CellXY, 4.5f}, {CellZ, 4.0f}, {CellShift, 1.0f}, {PSierp, 1.0f}, {PrimSize, 1.1f},
               {Twist, 0.5f}, {MSurface, 0.4f}, {MGlow, 0.6f}, {TrapR, 0.6f}, {PalOffset, 0.85f}, {PalSpan, 0.55f},
               {Emission, 1.1f}, {Fog, 0.03f}, {TexEmit, 0.5f}, {TexPick, 7.0f}})},
        // An Apollonian packing, spheres inverted into spheres without end: a foam of pearls.
        {"Apollonian Foam", 0.4f, 0.65f, 0.6f,
         make({{RepXY, 0.0f}, {CellXY, 8.0f}, {CellZ, 6.0f}, {PApollo, 1.0f}, {PrimSize, 1.6f}, {MSurface, 0.7f},
               {MGlow, 0.3f}, {TrapR, 0.6f}, {TrapX, 0.2f}, {PalOffset, 0.3f}, {PalSpan, 0.6f}, {Emission, 1.1f},
               {Fog, 0.035f}})},
        // The packing laced through bubbles and folded six ways: a lattice of pearls in light.
        {"Pearl Lattice", 0.5f, 0.75f, 0.5f,
         make({{RepXY, 0.5f}, {CellXY, 6.0f}, {CellZ, 5.0f}, {CellShift, 0.7f}, {Polar, 1.0f}, {PolarN, 6.0f},
               {PApollo, 0.6f}, {PShell, 0.4f}, {PrimSize, 1.2f}, {PrimThick, 0.06f}, {MSurface, 0.5f}, {MGlow, 0.5f},
               {TrapR, 0.6f}, {PalOffset, 0.7f}, {PalSpan, 0.5f}, {Emission, 1.1f}, {Fog, 0.03f}, {TexEmit, 0.3f},
               {TexPick, 4.0f}})},
        // Abs-inversions over folded boxes and bubbles, seven ways round: a gas that blooms like a flower.
        {"Kali Bloom", 0.45f, 0.6f, 0.4f,
         make({{RepXY, 0.6f}, {CellXY, 5.0f}, {CellZ, 5.0f}, {CellShift, 0.7f}, {Polar, 1.0f}, {PolarN, 7.0f},
               {AbsInv, 1.0f}, {AbsInvC, 0.55f}, {Ifs, 0.6f}, {IfsIter, 3.0f}, {IfsScale, 1.5f}, {PBox, 0.5f},
               {PShell, 0.5f}, {PrimSize, 0.6f}, {PrimThick, 0.08f}, {MDensity, 0.6f}, {MGlow, 0.4f}, {DensBand, 0.15f},
               {TrapR, 0.8f}, {PalOffset, 0.15f}, {PalSpan, 0.8f}, {Emission, 1.1f}, {Fog, 0.03f}})},
        // Low-power Mandelbulbs spiralling five ways down the way: a slow whirl of fractal fruit.
        {"Bulb Spiral", 0.5f, 0.55f, 0.55f,
         make({{RepXY, 0.5f}, {CellXY, 6.0f}, {CellZ, 4.5f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 5.0f},
               {PolarTwist, 0.3f}, {PBulb, 1.0f}, {BulbPower, 4.0f}, {PrimSize, 1.2f}, {MSurface, 0.7f}, {MGlow, 0.3f},
               {TrapR, 0.6f}, {PalOffset, 0.45f}, {PalSpan, 0.5f}, {Emission, 1.15f}, {Fog, 0.03f}, {TexAmount, 0.4f},
               {TexPick, 2.0f}, {Roll, 0.1f}})},
        // Folded quartz turned six ways: the crystal of Folded Quartz as a round kaleidoscope.
        {"Quartz Kaleido", 0.55f, 0.6f, 0.65f,
         make({{RepXY, 0.8f}, {CellXY, 6.0f}, {CellZ, 6.0f}, {CellShift, 0.6f}, {Polar, 1.0f}, {PolarN, 6.0f},
               {PBox, 1.0f}, {PrimSize, 0.6f}, {PrimThick, 0.1f}, {Ifs, 1.0f}, {IfsIter, 4.0f}, {IfsScale, 1.9f},
               {Scale, 2.2f}, {IfsAngA, 0.25f}, {IfsAngB, 0.45f}, {IfsOffX, 1.2f}, {IfsOffY, 1.0f}, {IfsOffZ, 1.1f},
               {MSurface, 0.7f}, {MGlow, 0.3f}, {TrapX, 0.4f}, {TrapZ, 0.5f}, {PalOffset, 0.42f}, {PalSpan, 0.4f},
               {Emission, 1.1f}, {Fog, 0.035f}, {TexAmount, 0.5f}, {TexPick, 4.0f}})},
        // The iceplant's star rosettes, large, in mirrored cells folded eight ways and rolling slowly:
        // a field of frost flowers turning round the way.
        {"Frost Flowers", 0.4f, 0.75f, 0.6f,
         make({{RepXY, 1.0f}, {CellXY, 4.5f}, {CellZ, 4.0f}, {CellShift, 1.0f}, {Polar, 1.0f}, {PolarN, 8.0f},
               {PMesh, 0.8f}, {PTorus, 0.2f}, {MeshPick, 9.0f}, {MeshSize, 2.6f}, {PrimSize, 1.4f}, {PrimThick, 0.05f},
               {MSurface, 0.5f}, {MGlow, 0.5f}, {TrapR, 0.6f}, {TrapY, 0.3f}, {PalOffset, 0.55f}, {PalSpan, 0.45f},
               {Emission, 1.2f}, {Fog, 0.03f}, {TexAmount, 0.5f}, {TexPick, 4.0f}, {Roll, 0.12f}, {Lens, 0.9f}})},
        // Iceplant rosettes on a gyroid sheet through ice: a frozen garden seen through a long lens.
        {"Ice Garden", 0.3f, 0.65f, 0.7f,
         make({{LatticeTilt, 0.3f}, {LatticeYaw, -0.35f}, {RepXY, 1.0f}, {CellXY, 5.0f}, {CellZ, 4.5f}, {CellShift, 1.0f}, {PMesh, 0.7f}, {PGyroid, 0.3f},
               {MeshPick, 9.0f}, {MeshSize, 2.5f}, {GyroScale, 1.4f}, {PrimThick, 0.08f}, {MSurface, 0.6f},
               {MDensity, 0.4f}, {DensBand, 0.12f}, {TrapR, 0.5f}, {TrapX, 0.3f}, {PalOffset, 0.6f}, {PalSpan, 0.35f},
               {Emission, 1.2f}, {Fog, 0.035f}, {TexAmount, 0.7f}, {TexPick, 4.0f}, {TexScale, 1.3f}, {Lens, 1.2f}})},
        // Iceplant stars folded twelve ways with a spiral: a mandala of frost the view rolls through.
        {"Frost Mandala", 0.65f, 0.7f, 0.55f,
         make({{RepXY, 0.4f}, {CellXY, 6.0f}, {CellZ, 4.0f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 12.0f},
               {PolarTwist, 0.25f}, {PMesh, 0.8f}, {PShell, 0.2f}, {MeshPick, 9.0f}, {MeshSize, 2.4f}, {PrimSize, 1.2f},
               {PrimThick, 0.05f}, {MSurface, 0.35f}, {MGlow, 0.65f}, {TrapR, 0.6f}, {PalOffset, 0.85f}, {PalSpan, 0.55f},
               {Emission, 1.15f}, {Fog, 0.03f}, {TexEmit, 0.5f}, {TexPick, 4.0f}, {Roll, -0.2f}, {Lens, 0.85f}, {GazePitch, 0.06f}, {GazeYaw, 0.06f}})},
        // Stone lanterns in rows along the way, warm light, a long lens: a night path to a shrine, its stones wet.
        {"Lantern Path", 0.35f, 0.55f, 0.45f,
         make({{LatticeYaw, 0.45f}, {RepXY, 1.0f}, {CellXY, 5.0f}, {CellZ, 4.0f}, {CellShift, 1.0f}, {PMesh, 1.0f}, {MeshPick, 12.0f},
               {MeshSize, 1.8f}, {MSurface, 0.8f}, {MGlow, 0.2f}, {TrapR, 0.5f}, {TrapY, 0.4f}, {PalOffset, 0.05f},
               {PalSpan, 0.3f}, {Emission, 1.25f}, {Fog, 0.04f}, {TexAmount, 0.6f}, {TexPick, 3.0f}, {Lens, 1.15f}, {GazePitch, 0.25f}, {GazeYaw, 0.45f},
               {Floor, 1.0f}, {FloorY, -3.3f}, {FloorGloss, 0.5f}, {Sky, 0.4f}, {SkyHue, 0.7f}, {Horizon, 0.4f}})},
        // Seated figures folded six ways round the way under a slow roll: a round hall of calm.
        {"Buddha Hall", 0.25f, 0.6f, 0.5f,
         make({{RepXY, 0.5f}, {CellXY, 6.5f}, {CellZ, 5.0f}, {CellShift, 0.9f}, {Polar, 1.0f}, {PolarN, 6.0f},
               {PMesh, 1.0f}, {MeshPick, 13.0f}, {MeshSize, 1.9f}, {MSurface, 0.75f}, {MGlow, 0.25f}, {TrapR, 0.6f},
               {PalOffset, 0.12f}, {PalSpan, 0.3f}, {Emission, 1.2f}, {Fog, 0.03f}, {TexAmount, 0.5f}, {TexPick, 0.0f},
               {Roll, 0.06f}, {Lens, 0.85f}})},
        // Ceramic vases in an eight-way spiral that the view rolls against: a turning potter's wheel of light.
        {"Vase Carousel", 0.6f, 0.65f, 0.6f,
         make({{RepXY, 0.4f}, {CellXY, 6.0f}, {CellZ, 4.5f}, {CellShift, 0.9f}, {Polar, 1.0f}, {PolarN, 8.0f},
               {PolarTwist, 0.3f}, {PMesh, 1.0f}, {MeshPick, 14.0f}, {MeshSize, 1.7f}, {MSurface, 0.55f}, {MGlow, 0.45f},
               {TrapR, 0.5f}, {TrapX, 0.3f}, {PalOffset, 0.32f}, {PalSpan, 0.5f}, {Emission, 1.15f}, {Fog, 0.03f},
               {TexAmount, 0.5f}, {TexPick, 6.0f}, {Roll, -0.15f}})},
        // Owls in mirrored cells, through bark and a glowing mist: a night wood.
        {"Owl Wood", 0.4f, 0.45f, 0.65f,
         make({{LatticeTilt, 0.15f}, {LatticeYaw, -0.4f}, {RepXY, 1.0f}, {CellXY, 5.0f}, {CellZ, 4.5f}, {CellShift, 1.0f}, {PMesh, 1.0f}, {MeshPick, 15.0f},
               {MeshSize, 1.6f}, {MSurface, 0.65f}, {MDensity, 0.35f}, {DensBand, 0.1f}, {TrapR, 0.6f}, {TrapZ, 0.3f},
               {PalOffset, 0.72f}, {PalSpan, 0.45f}, {Emission, 1.2f}, {Fog, 0.035f}, {TexAmount, 0.6f}, {TexPick, 5.0f},
               {Lens, 1.1f}, {GazePitch, 0.3f}, {GazeYaw, 0.35f}})},
        // Marble busts folded five ways round the way, a slow spiral and roll: a round gallery of faces.
        {"Bust Rotunda", 0.3f, 0.5f, 0.55f,
         make({{RepXY, 0.5f}, {CellXY, 6.5f}, {CellZ, 5.0f}, {CellShift, 0.9f}, {Polar, 1.0f}, {PolarN, 5.0f},
               {PolarTwist, 0.15f}, {PMesh, 1.0f}, {MeshPick, 16.0f}, {MeshSize, 1.9f}, {MSurface, 0.75f}, {MGlow, 0.25f},
               {TrapR, 0.5f}, {TrapY, 0.3f}, {PalOffset, 0.4f}, {PalSpan, 0.3f}, {Emission, 1.25f}, {Fog, 0.03f},
               {TexAmount, 0.7f}, {TexPick, 0.0f}, {TexScale, 1.2f}, {Roll, 0.08f}, {Lens, 0.9f}})},
        // Cat statues on a lattice tipped and turned off the axes, half stone, half glow: a crooked temple.
        {"Cat Temple", 0.45f, 0.55f, 0.6f,
         make({{RepXY, 0.5f}, {CellXY, 5.5f}, {CellZ, 4.5f}, {CellShift, 1.0f}, {LatticeTilt, 0.35f}, {LatticeYaw, -0.25f},
               {PMesh, 1.0f}, {MeshPick, 17.0f}, {MeshSize, 1.8f}, {MSurface, 0.6f}, {MGlow, 0.4f}, {TrapR, 0.6f},
               {TrapX, 0.3f}, {PalOffset, 0.12f}, {PalSpan, 0.4f}, {Emission, 1.2f}, {Fog, 0.035f}, {TexAmount, 0.5f},
               {TexPick, 2.0f}, {Roll, -0.1f}, {Lens, 1.1f}})},
        // Stone pillars folded seven ways and turning: a colonnade that spirals round the way.
        {"Spiral Colonnade", 0.5f, 0.6f, 0.5f,
         make({{RepXY, 0.4f}, {CellXY, 6.0f}, {CellZ, 4.0f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 7.0f},
               {PolarTwist, 0.3f}, {PMesh, 1.0f}, {MeshPick, 18.0f}, {MeshSize, 2.2f}, {MSurface, 0.8f}, {MGlow, 0.2f},
               {TrapR, 0.5f}, {TrapZ, 0.4f}, {PalOffset, 0.08f}, {PalSpan, 0.35f}, {Emission, 1.2f}, {Fog, 0.035f},
               {TexAmount, 0.7f}, {TexPick, 2.0f}, {TexScale, 1.1f}, {Roll, 0.15f}, {Lens, 0.85f}})},
        // Stumps and pumpkins blended, folded three ways, under a glowing mist: an autumn hollow.
        {"Harvest Hollow", 0.35f, 0.45f, 0.65f,
         make({{RepXY, 0.5f}, {CellXY, 6.0f}, {CellZ, 5.0f}, {CellShift, 0.9f}, {Polar, 1.0f}, {PolarN, 3.0f},
               {PolarTwist, -0.2f}, {PMesh, 1.0f}, {MeshPick, 19.5f}, {MeshSize, 1.8f}, {MSurface, 0.6f}, {MDensity, 0.4f},
               {DensBand, 0.12f}, {TrapR, 0.6f}, {TrapY, 0.3f}, {PalOffset, 0.06f}, {PalSpan, 0.35f}, {Emission, 1.2f},
               {Fog, 0.035f}, {TexAmount, 0.6f}, {TexPick, 5.0f}, {Roll, -0.08f}, {Lens, 1.15f}})},
        // Teapots on a lattice tipped and yawed, through a spiral nine-way fold: a tea party in a kaleidoscope.
        {"Teapot Kaleido", 0.6f, 0.7f, 0.55f,
         make({{RepXY, 0.5f}, {CellXY, 5.5f}, {CellZ, 4.5f}, {CellShift, 1.0f}, {LatticeTilt, -0.3f}, {LatticeYaw, 0.4f},
               {Polar, 1.0f}, {PolarN, 9.0f}, {PolarTwist, 0.2f}, {PMesh, 1.0f}, {MeshPick, 21.0f}, {MeshSize, 1.8f},
               {MSurface, 0.5f}, {MGlow, 0.5f}, {TrapR, 0.6f}, {TrapX, 0.3f}, {PalOffset, 0.62f}, {PalSpan, 0.5f},
               {Emission, 1.15f}, {Fog, 0.03f}, {TexEmit, 0.5f}, {TexPick, 6.0f}, {Roll, 0.2f}, {Lens, 0.9f}})},
        // Bronze whales and hanging lanterns blended, folded eleven ways round a slow roll: a deep-sea vigil.
        {"Whale Lanterns", 0.3f, 0.6f, 0.6f,
         make({{RepXY, 0.4f}, {CellXY, 6.5f}, {CellZ, 5.0f}, {CellShift, 0.9f}, {Polar, 1.0f}, {PolarN, 11.0f},
               {PolarTwist, -0.25f}, {PMesh, 1.0f}, {MeshPick, 22.4f}, {MeshSize, 2.1f}, {MSurface, 0.45f}, {MGlow, 0.55f},
               {TrapR, 0.5f}, {TrapZ, 0.4f}, {PalOffset, 0.58f}, {PalSpan, 0.4f}, {Emission, 1.25f}, {Fog, 0.03f},
               {TexAmount, 0.5f}, {TexPick, 4.0f}, {Roll, -0.18f}, {Lens, 1.2f}})},
        // A ridged canyon under a low warm light, thin glowing spheres hung above it: the way runs
        // down the valley floor between rising walls, cut through every ridge that crosses it.
        {"Ember Canyon", 0.45f, 0.55f, 0.45f,
         make({{RepXY, 1.0f}, {CellXY, 10.0f}, {CellZ, 8.0f}, {CellShift, 1.0f}, {PShell, 1.0f}, {PrimSize, 0.9f},
               {PrimThick, 0.05f}, {Terrain, 1.0f}, {TerrainHeight, 2.6f}, {TerrainScale, 0.12f}, {FloorY, -5.0f},
               {MSurface, 0.85f}, {MGlow, 0.15f}, {TrapX, 0.7f}, {TrapY, 0.4f}, {TrapR, 0.2f}, {PalOffset, 0.04f},
               {PalSpan, 0.45f}, {Emission, 1.15f}, {Fog, 0.03f}, {TexAmount, 0.5f}, {TexPick, 3.0f}, {Lens, 0.85f}})},
        // Fine ridged dunes with great rings half sunk in them: a desert of arches, a little haze.
        {"Dune Arches", 0.3f, 0.7f, 0.35f,
         make({{RepXY, 0.0f}, {CellXY, 8.0f}, {CellZ, 5.0f}, {PTorus, 1.0f}, {PrimSize, 2.4f}, {PrimThick, 0.2f},
               {Scale, 1.6f}, {Terrain, 1.0f}, {TerrainHeight, 1.4f}, {TerrainScale, 0.22f}, {FloorY, -3.6f},
               {MSurface, 0.8f}, {MDensity, 0.2f}, {DensBand, 0.1f}, {TrapX, 0.8f}, {TrapZ, 0.3f}, {TrapR, 0.2f},
               {PalOffset, 0.12f}, {PalSpan, 0.35f}, {Emission, 1.1f}, {Fog, 0.035f}, {TexAmount, 0.4f}, {TexPick, 6.0f},
               {Roll, 0.04f}})},
        // Broad fractal highlands with Mandelbulbs floating over them and boulders of bulbs half buried.
        {"Bulb Highlands", 0.55f, 0.5f, 0.55f,
         make({{RepXY, 1.0f}, {CellXY, 8.0f}, {CellZ, 7.0f}, {CellShift, 1.0f}, {PBulb, 1.0f}, {BulbPower, 8.0f},
               {PrimSize, 1.2f}, {Terrain, 1.0f}, {TerrainHeight, 3.0f}, {TerrainScale, 0.08f}, {FloorY, -5.5f},
               {MSurface, 0.85f}, {MGlow, 0.15f}, {TrapX, 0.4f}, {TrapY, 0.3f}, {TrapR, 0.5f}, {PalOffset, 0.36f},
               {PalSpan, 0.5f}, {Emission, 1.15f}, {Fog, 0.03f}, {TexAmount, 0.5f}, {TexPick, 2.0f}, {Lens, 0.9f}})},
        // A seated figure held in a ring of Menger sponges, five round the way: a relic in a stone frame.
        {"Sponge Reliquary", 0.3f, 0.4f, 0.75f,
         make({{RepXY, 0.5f}, {CellXY, 6.0f}, {CellZ, 5.0f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 5.0f},
               {PMenger, 0.55f}, {PMesh, 0.45f}, {PrimSize, 1.5f}, {MeshPick, 13.0f}, {MeshSize, 1.4f}, {MSurface, 0.75f},
               {MGlow, 0.25f}, {TrapX, 0.4f}, {TrapR, 0.5f}, {PalOffset, 0.1f}, {PalSpan, 0.35f}, {Emission, 1.2f},
               {Fog, 0.03f}, {TexAmount, 0.6f}, {TexPick, 0.0f}, {TexScale, 1.2f}, {Lens, 1.05f}})},
        // One low, wide gyroid drawn only in light, the view rolling slowly through it: veils of glowing silk.
        {"Gyroid Veils", 0.2f, 0.8f, 0.3f,
         make({{RepXY, 0.0f}, {CellXY, 8.0f}, {CellZ, 6.0f}, {PGyroid, 1.0f}, {GyroScale, 1.1f}, {PrimThick, 0.06f},
               {MSurface, 0.15f}, {MGlow, 0.85f}, {TrapR, 0.5f}, {TrapY, 0.5f}, {PalOffset, 0.52f}, {PalSpan, 0.45f},
               {Emission, 1.05f}, {Fog, 0.025f}, {Corridor, 1.8f}, {TexEmit, 0.3f}, {TexPick, 4.0f}, {Roll, 0.08f},
               {Lens, 0.9f}})},
        // The Apollonian packing as thin glowing sheets in a dark haze: a mist of pearls at night.
        {"Apollonian Mist", 0.1f, 0.25f, 0.2f,
         make({{RepXY, 0.0f}, {CellXY, 8.0f}, {CellZ, 6.0f}, {PApollo, 1.0f}, {PrimSize, 1.6f}, {MDensity, 0.85f},
               {MGlow, 0.15f}, {DensBand, 0.18f}, {TrapR, 0.7f}, {TrapZ, 0.3f}, {PalOffset, 0.68f}, {PalSpan, 0.35f},
               {Emission, 1.0f}, {Fog, 0.025f}, {Roll, -0.04f}})},
        // Sierpinski tetrahedra turned inside out by a sphere, three ways round: a hollow nave of triangles.
        {"Tetra Cathedral", 0.55f, 0.3f, 0.65f,
         make({{RepXY, 0.5f}, {CellXY, 6.0f}, {CellZ, 4.5f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 3.0f},
               {PSierp, 1.0f}, {PrimSize, 1.4f}, {Invert, 0.35f}, {InvRadius, 2.0f}, {MSurface, 0.7f}, {MGlow, 0.3f},
               {TrapR, 0.6f}, {TrapX, 0.3f}, {PalOffset, 0.02f}, {PalSpan, 0.4f}, {Emission, 1.2f}, {Fog, 0.03f},
               {TexAmount, 0.5f}, {TexPick, 2.0f}, {Lens, 0.85f}})},
        // Cow skulls spiralling three ways down the way, seen through a long lens: a slow, dark procession.
        {"Skull Spiral", 0.6f, 0.15f, 0.45f,
         make({{RepXY, 0.4f}, {CellXY, 6.0f}, {CellZ, 4.0f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 3.0f},
               {PolarTwist, 0.35f}, {PMesh, 1.0f}, {MeshPick, 3.0f}, {MeshSize, 2.0f}, {MSurface, 0.75f}, {MGlow, 0.25f},
               {TrapR, 0.5f}, {TrapY, 0.3f}, {PalOffset, 0.92f}, {PalSpan, 0.3f}, {Emission, 1.25f}, {Fog, 0.035f},
               {TexAmount, 0.6f}, {TexPick, 3.0f}, {Roll, 0.06f}, {Lens, 1.3f}})},
        // Chandeliers hung five ways in a reverse spiral, mostly glow, in iron: a ballroom at the bottom of the sea.
        {"Chandelier Abyss", 0.85f, 0.25f, 0.5f,
         make({{RepXY, 0.4f}, {CellXY, 6.0f}, {CellZ, 4.5f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 5.0f},
               {PolarTwist, -0.25f}, {PMesh, 1.0f}, {MeshPick, 5.0f}, {MeshSize, 2.0f}, {MSurface, 0.3f}, {MGlow, 0.7f},
               {TrapR, 0.6f}, {TrapZ, 0.3f}, {PalOffset, 0.7f}, {PalSpan, 0.35f}, {Emission, 1.1f}, {Fog, 0.04f},
               {TexEmit, 0.5f}, {TexPick, 1.0f}, {Roll, -0.1f}})},
        // Folded girders wound seven ways into a fast spiral, corrugated iron: the inside of a turbine.
        {"Iron Helix", 0.9f, 0.3f, 0.85f,
         make({{RepXY, 0.8f}, {CellXY, 5.0f}, {CellZ, 4.0f}, {CellShift, 0.5f}, {Polar, 1.0f}, {PolarN, 7.0f},
               {PolarTwist, 0.4f}, {PBar, 1.0f}, {PrimThick, 0.07f}, {Mirror, 1.0f}, {MirrorOff, 0.9f}, {Ifs, 0.8f},
               {IfsIter, 2.0f}, {IfsScale, 1.5f}, {IfsAngA, 0.35f}, {IfsAngB, 0.2f}, {IfsOffX, 0.8f}, {IfsOffY, 0.6f},
               {IfsOffZ, 0.7f}, {MSurface, 0.85f}, {MGlow, 0.15f}, {TrapX, 0.6f}, {TrapY, 0.3f}, {PalOffset, 0.05f},
               {PalSpan, 0.45f}, {Emission, 1.1f}, {Fog, 0.045f}, {TexAmount, 0.7f}, {TexPick, 1.0f}, {TexScale, 1.5f},
               {Roll, 0.2f}})},
        // Low-power Mandelbulbs ringed nine ways and spun hard, all glow: a wheel of comets.
        {"Comet Wheel", 0.9f, 0.85f, 0.3f,
         make({{RepXY, 0.3f}, {CellXY, 6.0f}, {CellZ, 4.5f}, {CellShift, 0.9f}, {Polar, 1.0f}, {PolarN, 9.0f},
               {PolarTwist, 0.5f}, {PBulb, 1.0f}, {BulbPower, 3.0f}, {PrimSize, 1.1f}, {MSurface, 0.3f}, {MGlow, 0.7f},
               {TrapR, 0.7f}, {TrapY, 0.3f}, {PalOffset, 0.18f}, {PalSpan, 0.7f}, {Emission, 1.15f}, {Fog, 0.025f},
               {TexEmit, 0.4f}, {TexPick, 7.0f}, {Roll, -0.25f}, {Lens, 0.85f}})},
        // Ferns in ice, seven ways round a slow spiral, stone half lost in frost mist: a still winter glade.
        {"Frozen Ferns", 0.15f, 0.8f, 0.8f,
         make({{RepXY, 0.8f}, {CellXY, 5.0f}, {CellZ, 4.0f}, {CellShift, 0.9f}, {Polar, 1.0f}, {PolarN, 7.0f},
               {PolarTwist, 0.15f}, {PMesh, 1.0f}, {MeshPick, 7.0f}, {MeshSize, 2.0f}, {MSurface, 0.6f}, {MDensity, 0.4f},
               {DensBand, 0.12f}, {TrapR, 0.5f}, {TrapX, 0.3f}, {PalOffset, 0.56f}, {PalSpan, 0.3f}, {Emission, 1.2f},
               {Fog, 0.035f}, {TexAmount, 0.6f}, {TexPick, 4.0f}, {TexScale, 1.3f}, {Lens, 0.85f}})},
        // Lambis shells hollowed by a sphere inversion, five ways in a spiral, glowing: a bloom of nacre.
        {"Nacre Bloom", 0.7f, 0.9f, 0.6f,
         make({{RepXY, 0.4f}, {CellXY, 6.0f}, {CellZ, 4.0f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 5.0f},
               {PolarTwist, 0.2f}, {PMesh, 1.0f}, {MeshPick, 0.0f}, {MeshSize, 1.6f}, {Invert, 0.3f}, {InvRadius, 1.6f},
               {MSurface, 0.4f}, {MGlow, 0.6f}, {TrapR, 0.7f}, {TrapX, 0.2f}, {PalOffset, 0.8f}, {PalSpan, 0.5f},
               {Emission, 1.1f}, {Fog, 0.03f}, {TexEmit, 0.4f}, {TexPick, 4.0f}, {Roll, 0.12f}})},
        // The newer fractals, each round the way by an odd fold (never the 4-way cross). The Julia
        // constants all sit inside the Mandelbrot set's main cardioid, so each set is one body.
        // Quaternion Julia sets, five round the way: soft fruit grown from z^2 + c.
        {"Julia Bloom", 0.45f, 0.7f, 0.55f,
         make({{RepXY, 0.5f}, {CellXY, 6.0f}, {CellZ, 4.5f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 5.0f},
               {PJulia, 1.0f}, {JuliaCX, -0.2f}, {JuliaCY, 0.6f}, {JuliaCZ, 0.0f}, {JuliaCW, 0.0f}, {PrimSize, 1.4f},
               {MSurface, 0.75f}, {MGlow, 0.25f}, {TrapR, 0.6f}, {TrapX, 0.3f}, {PalOffset, 0.58f}, {PalSpan, 0.5f},
               {Emission, 1.15f}, {Fog, 0.03f}, {TexAmount, 0.4f}, {TexPick, 4.0f}})},
        // A Julia set leaning off its axis, seven ways in a slow spiral, half stone, half mist: a reef.
        {"Quaternion Reef", 0.4f, 0.55f, 0.6f,
         make({{RepXY, 0.5f}, {CellXY, 6.0f}, {CellZ, 4.5f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 7.0f},
               {PolarTwist, 0.2f}, {PJulia, 1.0f}, {JuliaCX, -0.4f}, {JuliaCY, 0.55f}, {JuliaCZ, 0.0f}, {JuliaCW, 0.15f},
               {PrimSize, 1.3f}, {MSurface, 0.55f}, {MDensity, 0.45f}, {DensBand, 0.14f}, {TrapR, 0.6f}, {TrapZ, 0.4f},
               {PalOffset, 0.3f}, {PalSpan, 0.45f}, {Emission, 1.15f}, {Fog, 0.03f}, {Roll, 0.08f}})},
        // Three round Julia bodies lit through: lanterns of a set that never escapes.
        {"Julia Lanterns", 0.3f, 0.65f, 0.45f,
         make({{RepXY, 0.5f}, {CellXY, 6.0f}, {CellZ, 5.0f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 3.0f},
               {PJulia, 1.0f}, {JuliaCX, -0.3f}, {JuliaCY, 0.45f}, {JuliaCZ, 0.1f}, {JuliaCW, 0.1f}, {PrimSize, 1.5f},
               {MSurface, 0.4f}, {MGlow, 0.6f}, {TrapR, 0.7f}, {TrapY, 0.3f}, {PalOffset, 0.08f}, {PalSpan, 0.4f},
               {Emission, 1.1f}, {Fog, 0.03f}, {TexEmit, 0.4f}, {TexPick, 7.0f}, {Lens, 1.1f}})},
        // Balls of Kleinian foam, five round the way: discs nested in discs, without end.
        {"Klein Foam", 0.5f, 0.6f, 0.6f,
         make({{RepXY, 0.5f}, {CellXY, 6.0f}, {CellZ, 4.5f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 5.0f},
               {PKlein, 1.0f}, {KleinBox, 1.0f}, {PrimSize, 1.2f}, {PrimThick, 0.08f}, {MSurface, 0.7f}, {MGlow, 0.3f},
               {TrapR, 0.6f}, {TrapX, 0.3f}, {PalOffset, 0.66f}, {PalSpan, 0.5f}, {Emission, 1.15f}, {Fog, 0.03f},
               {TexAmount, 0.4f}, {TexPick, 0.0f}})},
        // The limit set nine ways round in a slow counter-spiral, glowing sheets: a nave of caps and rings.
        {"Limit Set Nave", 0.6f, 0.5f, 0.55f,
         make({{RepXY, 0.4f}, {CellXY, 6.0f}, {CellZ, 5.0f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 9.0f},
               {PolarTwist, -0.15f}, {PKlein, 1.0f}, {KleinBox, 0.9f}, {PrimSize, 1.1f}, {PrimThick, 0.06f},
               {MSurface, 0.35f}, {MGlow, 0.4f}, {MDensity, 0.25f}, {DensBand, 0.12f}, {TrapR, 0.7f}, {TrapY, 0.3f},
               {PalOffset, 0.9f}, {PalSpan, 0.55f}, {Emission, 1.1f}, {Fog, 0.03f}, {TexEmit, 0.3f}, {TexPick, 4.0f},
               {Roll, -0.1f}, {Lens, 0.9f}})},
        // Octahedral fractals, three round the way: spires of octahedra on octahedra.
        {"Octa Spires", 0.6f, 0.55f, 0.6f,
         make({{RepXY, 0.5f}, {CellXY, 6.0f}, {CellZ, 4.5f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 3.0f},
               {PKifs, 1.0f}, {KifsScale, 2.0f}, {PrimSize, 1.2f}, {MSurface, 0.8f}, {MGlow, 0.2f}, {TrapR, 0.5f},
               {TrapX, 0.4f}, {PalOffset, 0.2f}, {PalSpan, 0.45f}, {Emission, 1.15f}, {Fog, 0.03f}, {TexAmount, 0.5f},
               {TexPick, 6.0f}})},
        // A sparser octahedral dust eleven ways round, spiralling and glowing: a choir of crystals.
        {"Octahedral Choir", 0.7f, 0.7f, 0.5f,
         make({{RepXY, 0.4f}, {CellXY, 6.0f}, {CellZ, 4.5f}, {CellShift, 0.8f}, {Polar, 1.0f}, {PolarN, 11.0f},
               {PolarTwist, 0.2f}, {PKifs, 1.0f}, {KifsScale, 2.4f}, {PrimSize, 1.1f}, {MSurface, 0.45f}, {MGlow, 0.55f},
               {TrapR, 0.6f}, {TrapZ, 0.3f}, {PalOffset, 0.76f}, {PalSpan, 0.55f}, {Emission, 1.1f}, {Fog, 0.03f},
               {TexEmit, 0.5f}, {TexPick, 7.0f}, {Roll, 0.12f}})},
    }};
}

const std::array<Seed, 67>& seeds() {
    static const std::array<Seed, 67> kSeeds = buildSeeds();
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
    normalize(g, PBox, kPrimCount, nullptr);
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
        const int n = first == PBox ? kPrimCount : 3;
        float sum = 0.0f;
        for (int i = 0; i < n; ++i) sum += g[first + i];
        if (std::fabs(sum - 1.0f) > 1e-3f) return false;
    }
    return true;
}

float sdfUncarved(const Genome& g, const Way& way, const std::array<float, 3>& p) { return bareWorld(g, way, p); }

float terrainSdf(const Genome& g, const std::array<float, 3>& p) { return terrain(g, p); }

float sdf(const Genome& g, const Way& way, const std::array<float, 3>& p) {
    return smax(bareWorld(g, way, p), -corridor(g, way, p), 0.5f);
}

Space openSpace(const Genome& g) {
    // A jittered 6 x 6 x 6 lattice in a 12-unit box about the eye, which sits on the travel axis.
    const Way way;
    // The open threshold plus the blend: above this, the ground alone never makes a point "not open".
    constexpr float kFloorReach = 0.25f + kFloorBlend;
    int open = 0;
    int near = 0;
    int total = 0;
    uint64_t s = 0x5EED0F5BACEull;
    for (int i = 0; i < 6; ++i) {
        for (int j = 0; j < 6; ++j) {
            for (int k = 0; k < 6; ++k) {
                const V3 p{-6.0f + 2.0f * (static_cast<float>(i) + unit(s)), -6.0f + 2.0f * (static_cast<float>(j) + unit(s)),
                           -6.0f + 2.0f * (static_cast<float>(k) + unit(s))};
                // The ground and what lies under it are not the camera's space: a point there, or in
                // the band the ground's own surface and blend reach, is not judged at all, so a floor
                // never fails a world for the rock beneath it (every point above the band still is).
                if (g[Floor] > 0.0f && p[1] < g[FloorY] + kFloorReach) continue;
                const float d = bareWorld(g, way, p);
                if (d > 0.25f) ++open;
                if (d < 0.6f) ++near;
                ++total;
            }
        }
    }
    if (total == 0) return {};
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
    float weights[64];
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
        // The view's feel mutates gently: a tunnel should not learn to stare at its walls.
        const bool view = f == Roll || f == GazePitch || f == GazeYaw;
        const float scale = (f >= PBox && f <= MDensity) ? 0.6f : (view ? 0.4f : 1.0f);
        m[f] += (unit(s) * 2.0f - 1.0f) * amount * span * scale * 0.5f;
    }
    // Far mutations also wake operators the seed leaves off: a new kind of fold, not only new numbers.
    for (int f : {static_cast<int>(Mirror), static_cast<int>(Ifs), static_cast<int>(Invert), static_cast<int>(AbsInv),
                  static_cast<int>(PMesh), static_cast<int>(TexAmount), static_cast<int>(TexEmit),
                  static_cast<int>(Polar), static_cast<int>(PGyroid), static_cast<int>(PMenger), static_cast<int>(PBulb),
                  static_cast<int>(PMandelbox), static_cast<int>(PSierp), static_cast<int>(PApollo),
                  static_cast<int>(PJulia), static_cast<int>(PKlein), static_cast<int>(PKifs),
                  static_cast<int>(Terrain), static_cast<int>(Floor), static_cast<int>(Sky)}) {
        if (origin[f] < 0.05f && unit(s) < amount * 0.5f) m[f] = unit(s) * amount;
    }
    m = clampFields(m);
    // A full x/y lattice with no polar fold and no turn reads from the camera as the same 4-way
    // cross every time (#496): turn such a mutation's lattice off the view, by a draw of its own.
    if (m[RepXY] > 0.5f && m[Polar] < 0.5f && std::fabs(m[LatticeTilt]) < 0.15f && std::fabs(m[LatticeYaw]) < 0.15f) {
        const float tilt = 0.2f + 0.3f * unit(s);
        const float yaw = 0.2f + 0.3f * unit(s);
        m[LatticeTilt] = unit(s) < 0.5f ? -tilt : tilt;
        m[LatticeYaw] = unit(s) < 0.5f ? -yaw : yaw;
    }
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
