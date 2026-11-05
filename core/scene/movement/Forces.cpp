#include "scene/movement/Forces.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace geode::scene::movement {

namespace {

constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();
constexpr int kConstraintIterations = 3;

float finiteOr(float v, float fallback) { return std::isfinite(v) ? v : fallback; }

// A lattice cell index, clamped so a far-flung body never overflows the integer conversion.
std::int32_t cellIndex(float x, float inv) {
    return static_cast<std::int32_t>(std::floor(std::clamp(x * inv, -1.0e6f, 1.0e6f)));
}

std::uint32_t bucketFor(std::int32_t x, std::int32_t y, std::int32_t z) {
    return hash3(x, y, z, 0x5eedu) & static_cast<std::uint32_t>(FlockScratch::kBuckets - 1);
}

// Two bodies on the same point still need a direction to part in (the one body i moves along): -X for
// the lower index, +X for the higher, so the pair moves apart symmetrically.
Vec3 apart(std::size_t i, std::size_t j) { return i < j ? Vec3{-1.0f, 0.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f}; }

void buildGrid(std::span<const Body> bodies, FlockScratch& s, float cell) {
    s.cellSize = cell;
    const float inv = 1.0f / cell;
    std::fill(s.start.begin(), s.start.end(), 0u);
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const Body& b = bodies[i];
        if (!b.alive) {
            s.bucketOf[i] = kNone;
            continue;
        }
        const std::uint32_t k = bucketFor(cellIndex(b.pos[0], inv), cellIndex(b.pos[1], inv), cellIndex(b.pos[2], inv));
        s.bucketOf[i] = k;
        ++s.start[k + 1];
    }
    for (std::size_t k = 0; k < FlockScratch::kBuckets; ++k) s.start[k + 1] += s.start[k];
    std::copy(s.start.begin(), s.start.begin() + FlockScratch::kBuckets, s.fill.begin());
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const std::uint32_t k = s.bucketOf[i];
        if (k != kNone) s.order[s.fill[k]++] = static_cast<std::uint32_t>(i);
    }
    s.gridBuilt = true;
}

// Calls fn(j, offset = pos_i - pos_j, distance) for every live j != i closer than r. With the grid on,
// only the 27 cells round i's are searched (the grid's cells are at least r wide); hash collisions can
// map two of them to one bucket, so each bucket is searched once.
template <class Fn>
void forNeighbours(std::span<const Body> bodies, const FlockScratch& s, bool grid, std::size_t i, float r, Fn&& fn) {
    const Vec3& pi = bodies[i].pos;
    const float r2 = r * r;
    auto visit = [&](std::size_t j) {
        if (j == i || !bodies[j].alive) return;
        const Vec3 off = sub(pi, bodies[j].pos);
        const float d2 = dot(off, off);
        if (d2 < r2) fn(j, off, std::sqrt(d2));
    };
    if (!grid) {
        for (std::size_t j = 0; j < bodies.size(); ++j) visit(j);
        return;
    }
    const float inv = 1.0f / s.cellSize;
    const std::int32_t cx = cellIndex(pi[0], inv);
    const std::int32_t cy = cellIndex(pi[1], inv);
    const std::int32_t cz = cellIndex(pi[2], inv);
    std::uint32_t seen[27];
    int seenCount = 0;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const std::uint32_t k = bucketFor(cx + dx, cy + dy, cz + dz);
                bool dup = false;
                for (int q = 0; q < seenCount; ++q) dup = dup || seen[q] == k;
                if (dup) continue;
                seen[seenCount++] = k;
                for (std::uint32_t at = s.start[k]; at < s.start[k + 1]; ++at) visit(s.order[at]);
            }
        }
    }
}

Vec3 capLength(const Vec3& v, float cap) {
    if (!(cap > 0.0f)) return v;
    const float l = length(v);
    return l > cap ? scale(v, cap / l) : v;
}

void flockAccelerations(std::span<const Body> bodies, const FlockParams& p, FlockScratch& s, bool grid) {
    const float radius = std::max(finiteOr(p.radius, 0.0f), 1e-3f);
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        const Body& b = bodies[i];
        if (!b.alive) {
            s.accel[i] = {0.0f, 0.0f, 0.0f};
            continue;
        }
        Vec3 sep{0.0f, 0.0f, 0.0f};
        Vec3 sumVel{0.0f, 0.0f, 0.0f};
        Vec3 sumPos{0.0f, 0.0f, 0.0f};
        int count = 0;
        forNeighbours(bodies, s, grid, i, radius, [&](std::size_t j, const Vec3& off, float d) {
            // Away from j, stronger the closer it is, 0 at the edge of the radius.
            const Vec3 dir = d > 1e-6f ? scale(off, 1.0f / d) : apart(i, j);
            sep = add(sep, scale(dir, radius / std::max(d, 1e-3f) - 1.0f));
            sumVel = add(sumVel, bodies[j].vel);
            sumPos = add(sumPos, bodies[j].pos);
            ++count;
        });
        Vec3 a{0.0f, 0.0f, 0.0f};
        if (count > 0) {
            const float inv = 1.0f / static_cast<float>(count);
            a = add(a, scale(sep, finiteOr(p.separation, 0.0f)));
            a = add(a, scale(sub(scale(sumVel, inv), b.vel), finiteOr(p.alignment, 0.0f)));
            a = add(a, scale(sub(scale(sumPos, inv), b.pos), finiteOr(p.cohesion, 0.0f)));
        }
        const float cruise = finiteOr(p.cruise, 0.0f);
        const float speed = length(b.vel);
        if (cruise > 0.0f && speed > 1e-4f) a = add(a, scale(b.vel, (cruise - speed) / speed));
        s.accel[i] = capLength(a, finiteOr(p.maxAccel, 0.0f));
    }
}

void separate(std::span<Body> bodies, const FlockParams& p, FlockScratch& s, bool grid) {
    const float minD = finiteOr(p.minDistance, 0.0f);
    if (!(minD > 0.0f)) return;
    for (int iter = 0; iter < kConstraintIterations; ++iter) {
        bool any = false;
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            s.correction[i] = {0.0f, 0.0f, 0.0f};
            if (!bodies[i].alive) continue;
            forNeighbours(std::span<const Body>(bodies.data(), bodies.size()), s, grid, i, minD,
                          [&](std::size_t j, const Vec3& off, float d) {
                              const Vec3 dir = d > 1e-6f ? scale(off, 1.0f / d) : apart(i, j);
                              // Half the overlap each: the pair seen from both sides parts by all of it.
                              s.correction[i] = add(s.correction[i], scale(dir, 0.5f * (minD - d)));
                              any = true;
                          });
        }
        if (!any) return;
        for (std::size_t i = 0; i < bodies.size(); ++i) bodies[i].pos = add(bodies[i].pos, s.correction[i]);
    }
}

Vec3 reactAccel(const Body& b, const ReactParams& p, const ForceInput& in) {
    const float radius = std::max(finiteOr(p.radius, 0.0f), 0.0f);
    const float strength = finiteOr(p.strength, 0.0f);
    const Vec3 off = sub(b.pos, in.eye);
    const float d = length(off);
    const Vec3 away = d > 1e-6f ? scale(off, 1.0f / d) : Vec3{0.0f, 0.0f, 1.0f};
    switch (static_cast<ReactMode>(p.mode)) {
        case ReactMode::Scatter:
            // A constant push inside the radius (not one that fades to 0 at its edge), so a scattering
            // body always leaves it: its terminal speed strength/drag is above 0 everywhere inside.
            return d < radius ? scale(away, strength) : Vec3{0.0f, 0.0f, 0.0f};
        case ReactMode::Swarm: {
            if (!(radius > 0.0f)) return scale(away, -strength);
            const float pull = std::clamp((d - radius) / radius, -1.0f, 1.0f);
            return scale(away, -strength * pull);
        }
        case ReactMode::Collide:
        default:
            return {0.0f, 0.0f, 0.0f};
    }
}

void collide(Body& b, const ReactParams& p, const ForceInput& in) {
    const float radius = std::max(finiteOr(p.radius, 0.0f), 0.0f);
    const Vec3 off = sub(b.pos, in.eye);
    const float d = length(off);
    if (!(d < radius)) return;
    const Vec3 n = d > 1e-6f ? scale(off, 1.0f / d) : Vec3{0.0f, 0.0f, 1.0f};
    b.pos = add(in.eye, scale(n, radius));
    const Vec3 rel = sub(b.vel, in.eyeVel);
    const float vn = dot(rel, n);
    if (vn < 0.0f) {
        const float e = std::clamp(finiteOr(p.restitution, 0.0f), 0.0f, 1.0f);
        b.vel = sub(b.vel, scale(n, (1.0f + e) * vn));
    }
}

}  // namespace

FlockScratch::FlockScratch(std::size_t capacity)
    : accel(capacity), correction(capacity), bucketOf(capacity), order(capacity) {
    // A scratch for no flock owns nothing, so a single-behaviour step can make one on the stack for free.
    if (capacity > 0) {
        start.assign(kBuckets + 1, 0u);
        fill.assign(kBuckets, 0u);
    }
}

Vec3 driftField(const DriftParams& p, const Vec3& pos, float time) {
    const float f = finiteOr(p.frequency, 0.0f);
    const float t = finiteOr(time, 0.0f) * finiteOr(p.speed, 0.0f);
    // The field slides through time along a fixed diagonal, so a still body sees it change smoothly.
    const Vec3 q = add(scale(pos, f), Vec3{t * 0.57735f, t * 0.57735f, t * 0.57735f});
    const float a = finiteOr(p.amplitude, 0.0f);
    // Three channels, three seeds; each offset off the lattice so their zeros do not line up.
    return {a * valueNoise(q, p.seed * 3u + 0u), a * valueNoise(add(q, Vec3{17.3f, 5.1f, 9.7f}), p.seed * 3u + 1u),
            a * valueNoise(add(q, Vec3{3.9f, 23.1f, 13.3f}), p.seed * 3u + 2u)};
}

void stepForces(std::span<Body> bodies, const ForceSet& set, const ForceInput& in, float dt, FlockScratch& scratch) {
    dt = clampDt(dt);
    const int steps = substepsFor(dt);
    if (steps == 0 || bodies.empty()) return;
    const float h = dt / static_cast<float>(steps);
    const float drag = std::max(finiteOr(set.motion.drag, 0.0f), 0.0f);
    const float keep = std::exp(-drag * h);
    const float maxSpeed = finiteOr(set.motion.maxSpeed, 0.0f);

    const std::size_t flockCount = set.flock ? std::min(bodies.size(), scratch.capacity()) : 0;
    std::span<Body> flock = bodies.first(flockCount);
    bool grid = false;
    if (flockCount > 1) {
        std::size_t live = 0;
        for (const Body& b : flock) live += b.alive ? 1 : 0;
        grid = live > scratch.gridAbove && !scratch.start.empty();
    }
    const bool react = set.react != nullptr;
    const bool collideOn = react && static_cast<ReactMode>(set.react->mode) == ReactMode::Collide;

    for (int step = 0; step < steps; ++step) {
        const float t = in.time + h * static_cast<float>(step);
        if (flockCount > 1) {
            if (grid) {
                const float cell = std::max({finiteOr(set.flock->radius, 0.0f), finiteOr(set.flock->minDistance, 0.0f), 1e-3f});
                buildGrid(flock, scratch, cell);
            }
            flockAccelerations(flock, *set.flock, scratch, grid);
        }
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            Body& b = bodies[i];
            if (!b.alive) continue;
            Vec3 a{0.0f, 0.0f, 0.0f};
            if (i < flockCount && flockCount > 1) a = add(a, scratch.accel[i]);
            if (set.drift) {
                const float response = std::max(finiteOr(set.drift->response, 1.0f), 1e-3f);
                a = add(a, scale(sub(driftField(*set.drift, b.pos, t), b.vel), 1.0f / response));
            }
            if (react && !collideOn) a = add(a, reactAccel(b, *set.react, in));
            b.vel = scale(add(b.vel, scale(a, h)), keep);
            b.vel = capLength(b.vel, maxSpeed);
            if (!finite(b.vel)) b.vel = {0.0f, 0.0f, 0.0f};
            advance(b, h);
        }
        if (flockCount > 1) separate(flock, *set.flock, scratch, grid);
        if (collideOn) {
            for (Body& b : bodies) {
                if (b.alive) collide(b, *set.react, in);
            }
        }
    }
}

void stepDrift(std::span<Body> bodies, const DriftParams& p, const MotionParams& m, float time, float dt) {
    FlockScratch none(0);
    ForceSet set;
    set.drift = &p;
    set.motion = m;
    ForceInput in;
    in.time = time;
    stepForces(bodies, set, in, dt, none);
}

void stepFlock(std::span<Body> bodies, const FlockParams& p, const MotionParams& m, float dt, FlockScratch& scratch) {
    ForceSet set;
    set.flock = &p;
    set.motion = m;
    stepForces(bodies, set, ForceInput{}, dt, scratch);
}

void stepReact(std::span<Body> bodies, const ReactParams& p, const MotionParams& m, const ForceInput& in, float dt) {
    FlockScratch none(0);
    ForceSet set;
    set.react = &p;
    set.motion = m;
    stepForces(bodies, set, in, dt, none);
}

}  // namespace geode::scene::movement
