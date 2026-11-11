// Morph World's genome and morph driver (core/viz/world), on the host.
//
//   world_genome_test [lib_world.glsl [world_shapes.bin world_textures.bin]]
//                                 run the checks; with lib_world, also check that its WG_* names
//                                 index the same fields; with the baked assets, check their sizes
//                                 and probe every world with the real shapes (else spheres)
//   world_genome_test --dump <seed>          print seed <seed>'s uWorld (96 floats, comma separated)
//                                            for tools/glslcheck --set uWorld=...
//   world_genome_test --names [shapes.bin]   print the seeds' names and open-space probes
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "viz/CameraRig.hpp"
#include "viz/world/MorphDriver.hpp"
#include "viz/world/WorldAssets.hpp"
#include "viz/world/WorldGenome.hpp"

using namespace geode::viz;
using namespace geode::viz::world;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

bool same(const Genome& a, const Genome& b) {
    for (int f = 0; f < kFieldCount; ++f) {
        if (a[f] != b[f]) return false;
    }
    return true;
}

void lerpEndpointsAreExact() {
    for (int i = 0; i < seedCount(); ++i) {
        const Genome& a = seed(i).genome;
        const Genome& b = seed((i + 3) % seedCount()).genome;
        check(same(lerp(a, b, 0.0f), a), std::string("lerp(a, b, 0) == a for ") + seed(i).name);
        check(same(lerp(a, b, 1.0f), b), std::string("lerp(a, b, 1) == b for ") + seed(i).name);
        check(same(lerp(a, b, -0.5f), a) && same(lerp(a, b, 1.5f), b), "lerp clamps t");
        // Between, every field stays between its ends (the wheel's palette offset the short way round).
        for (float t : {0.1f, 0.37f, 0.5f, 0.81f}) {
            const Genome m = lerp(a, b, t);
            check(inBounds(m), "a blend of two seeds is in bounds");
            for (int f = 0; f < kFieldCount; ++f) {
                if (wraps(f)) continue;
                const float lo = std::fmin(a[f], b[f]) - 1e-5f;
                const float hi = std::fmax(a[f], b[f]) + 1e-5f;
                check(m[f] >= lo && m[f] <= hi, "lerp stays between its ends, field " + std::to_string(f));
            }
        }
    }
    // The palette offset crosses 0 the short way.
    Genome a = seed(0).genome;
    Genome b = a;
    a[PalOffset] = 0.95f;
    b[PalOffset] = 0.05f;
    const float mid = lerp(a, b, 0.5f)[PalOffset];
    check(mid < 0.01f || mid > 0.99f, "the palette offset takes the short way round the wheel");
}

void seedsAreValid() {
    check(seedCount() >= 8, "at least 8 seed worlds");
    std::map<std::string, int> names;
    for (int i = 0; i < seedCount(); ++i) {
        const Seed& s = seed(i);
        ++names[s.name];
        check(inBounds(s.genome), std::string(s.name) + " is in bounds");
        const Space sp = openSpace(s.genome);
        check(sp.open >= kMinOpen, std::string(s.name) + " is open (" + std::to_string(sp.open) + ")");
        check(sp.near >= kMinNear, std::string(s.name) + " has matter to see (" + std::to_string(sp.near) + ")");
        check(brightness(s.genome) > 0.2f, std::string(s.name) + " is not dark");
    }
    check(static_cast<int>(names.size()) == seedCount(), "seed names are unique");
}

// The 4-way cross (#496): a full x/y lattice with no polar fold and no turn shows the camera the
// same split every time. No seed may be one, and a random genome that grows into one is turned.
// A world's open-space probe, for a failure message: which of the two rules it missed.
std::string probeText(const Genome& g) {
    const Space sp = openSpace(g);
    return " (open " + std::to_string(sp.open) + ", near " + std::to_string(sp.near) + ")";
}

bool isFourWayCross(const Genome& g) {
    return g[RepXY] > 0.5f && g[Polar] < 0.5f && g[LatticeTilt] == 0.0f && g[LatticeYaw] == 0.0f;
}

void noWorldIsTheFourWayCross() {
    for (int i = 0; i < seedCount(); ++i) {
        check(!isFourWayCross(seed(i).genome), std::string(seed(i).name) + " is not the 4-way cross");
    }
    int crosses = 0;
    for (int i = 0; i < 400; ++i) {
        Constraints c;
        c.mutation = static_cast<float>(i % 11) / 10.0f;
        c.energy = static_cast<float>((i * 7) % 10) / 9.0f;
        c.mood = static_cast<float>((i * 3) % 10) / 9.0f;
        c.density = static_cast<float>((i * 13) % 10) / 9.0f;
        if (isFourWayCross(random(static_cast<uint64_t>(i) * 104729u + 5u, c))) ++crosses;
    }
    check(crosses == 0, "random never grows the 4-way cross (" + std::to_string(crosses) + "/400)");
    // The turned lattice keeps the travel axis periodic: one mirrored z period (2 CellZ) on, the
    // bare world is the same.
    const Way way;
    float worst = 0.0f;
    for (int i = 0; i < seedCount(); ++i) {
        const Genome& g = seed(i).genome;
        if (g[LatticeTilt] == 0.0f && g[LatticeYaw] == 0.0f) continue;
        for (int k = 0; k < 24; ++k) {
            const float a = static_cast<float>(k) * 0.71f;
            const std::array<float, 3> p{3.1f * std::sin(a), 2.7f * std::cos(1.3f * a), -4.0f + 0.37f * static_cast<float>(k)};
            const std::array<float, 3> q{p[0], p[1], p[2] + 2.0f * g[CellZ]};
            worst = std::fmax(worst, std::fabs(sdfUncarved(g, way, p) - sdfUncarved(g, way, q)));
        }
    }
    check(worst < 2e-3f, "a turned lattice keeps the travel repeat periodic (" + std::to_string(worst) + ")");
}

void randomGenomesAreValid() {
    int valids = 0;
    for (int i = 0; i < 1000; ++i) {
        Constraints c;
        c.mutation = static_cast<float>(i % 11) / 10.0f;
        c.energy = static_cast<float>((i * 7) % 10) / 9.0f;
        c.mood = static_cast<float>((i * 3) % 10) / 9.0f;
        c.density = static_cast<float>((i * 13) % 10) / 9.0f;
        c.avoid = i % seedCount();
        int picked = -1;
        const Genome g = random(static_cast<uint64_t>(i) * 7919u + 17u, c, &picked);
        check(picked >= 0 && picked < seedCount(), "random names its seed");
        check(inBounds(g), "random genome " + std::to_string(i) + " is in bounds");
        if (valid(g)) {
            ++valids;
        } else {
            std::printf("  random genome %d (from %s) is not valid%s\n", i, seed(picked).name, probeText(g).c_str());
        }
        // Replayable.
        check(same(g, random(static_cast<uint64_t>(i) * 7919u + 17u, c)), "random is a function of its inputs");
    }
    check(valids == 1000, "every random genome is open and has matter (" + std::to_string(valids) + "/1000)");
    // Mutation 0 is the seed itself.
    Constraints still;
    still.mutation = 0.0f;
    int picked = -1;
    const Genome g = random(42u, still, &picked);
    check(same(g, seed(picked).genome), "mutation 0 returns the seed as designed");
    // A far mutation really moves.
    Constraints far;
    far.mutation = 1.0f;
    const Genome h = random(42u, far, &picked);
    float moved = 0.0f;
    for (int f = 0; f < kFieldCount; ++f) moved += std::fabs(h[f] - seed(picked).genome[f]);
    check(moved > 0.5f, "mutation 1 moves the genome");
}

void corridorIsOpenAtTheCamera() {
    // The eye on every rig path, at many points of travel, with random worlds, phases and wakes:
    // the carved world is always positive there and along the wake.
    int samples = 0;
    float worst = 1e9f;
    for (int i = 0; i < 200; ++i) {
        Constraints c;
        c.mutation = static_cast<float>(i % 6) / 5.0f;
        const Genome g = random(1000u + static_cast<uint64_t>(i), c);
        for (int path = 0; path < 4; ++path) {
            for (float orbit : {0.0f, 0.5f, 1.0f}) {
                for (int s = 0; s < 6; ++s) {
                    const float travel = 37.0f * static_cast<float>(s + i);
                    const auto here = CameraRig::pathPoint(path, orbit, travel);
                    const auto ahead = CameraRig::pathPoint(path, orbit, travel + 1.6f);
                    Way way;
                    way.eye = here;
                    way.zPhase = std::fmod(travel * 0.37f, 2.0f * g[CellZ]);
                    way.wake = {ahead[0] - here[0], ahead[1] - here[1], ahead[2] - here[2]};
                    way.iterCap = 2 + (i % 4);
                    const float d = sdf(g, way, here);
                    worst = std::fmin(worst, d);
                    check(d > 0.0f, "the eye is in open space (path " + std::to_string(path) + ")");
                    for (float h : {0.25f, 0.5f, 1.0f}) {
                        const std::array<float, 3> p{here[0] + way.wake[0] * h, here[1] + way.wake[1] * h, here[2] + way.wake[2] * h};
                        check(sdf(g, way, p) > 0.0f, "the wake ahead of the eye is open");
                    }
                    ++samples;
                }
            }
        }
    }
    check(samples == 200 * 4 * 3 * 6, "sampled every path");
    check(worst >= kEyeRadius * 0.99f, "the eye keeps at least its bubble's radius (" + std::to_string(worst) + ")");
}

// A uniform 0..1 from the one generator.
float unit01(uint64_t& s) {
    s = mix64(s);
    return static_cast<float>((s >> 40) & 0xFFFFFFu) / 16777216.0f;
}

void landscapesAreOpenAndHoldTheCorridor() {
    // With Terrain at 1 the landscape seeds are valid worlds, and on every rig path, at every
    // orbit, along the whole travel (the rig's wrap included) the eye and its wake stay in air.
    int landscapes = 0;
    for (int i = 0; i < seedCount(); ++i) {
        const Seed& sd = seed(i);
        if (sd.genome[Terrain] < 0.99f) continue;
        ++landscapes;
        const Genome& g = sd.genome;
        check(valid(g), std::string(sd.name) + " (a landscape) is open and has matter");
        float worst = 1e9f;
        for (int path = 0; path < 4; ++path) {
            for (float orbit : {0.0f, 0.5f, 1.0f}) {
                for (float travel : {0.0f, 3.1f, 17.0f, 250.0f, 1999.5f, 7700.0f, 8000.0f, 8150.0f, 8191.0f}) {
                    const auto here = CameraRig::pathPoint(path, orbit, travel);
                    const auto ahead = CameraRig::pathPoint(path, orbit, travel + 1.6f);
                    Way way;
                    way.eye = here;
                    way.zPhase = std::fmod(travel * 0.37f, 2.0f * g[CellZ]);
                    way.wake = {ahead[0] - here[0], ahead[1] - here[1], ahead[2] - here[2]};
                    const float d = sdf(g, way, here);
                    worst = std::fmin(worst, d);
                    check(d > 0.0f, std::string(sd.name) + ": the eye is in air (path " + std::to_string(path) + ")");
                    for (float h : {0.25f, 0.5f, 1.0f}) {
                        const std::array<float, 3> p{here[0] + way.wake[0] * h, here[1] + way.wake[1] * h, here[2] + way.wake[2] * h};
                        check(sdf(g, way, p) > 0.0f, std::string(sd.name) + ": the wake is open (path " + std::to_string(path) + ")");
                    }
                }
            }
        }
        check(worst >= kEyeRadius * 0.99f, std::string(sd.name) + ": the eye keeps its bubble (" + std::to_string(worst) + ")");
    }
    check(landscapes >= 2, "two or more landscape seeds (" + std::to_string(landscapes) + ")");
}

void terrainIsABoundAndSeamless() {
    // The slope bound holds: the landscape's distance never changes faster than the point moves, at
    // full strength and while fading, near the origin and in the wrap's blend, so the sphere tracer
    // never steps through a ridge. (The small absolute slack is float noise at z near 8192.)
    uint64_t s = 0x7E22A1Full;
    int pairs = 0;
    int landscapes = 0;
    for (int i = 0; i < seedCount(); ++i) {
        if (seed(i).genome[Terrain] < 0.99f) continue;
        ++landscapes;
        for (float on : {1.0f, 0.4f, 0.05f}) {
            Genome g = seed(i).genome;
            g[Terrain] = on;
            for (int n = 0; n < 3000; ++n) {
                const bool nearWrap = n % 3 == 2;
                const std::array<float, 3> a{-30.0f + 60.0f * unit01(s), -8.0f + 14.0f * unit01(s),
                                             nearWrap ? 7600.0f + 600.0f * unit01(s) : -40.0f + 80.0f * unit01(s)};
                float dx = unit01(s) * 2.0f - 1.0f;
                float dy = unit01(s) * 2.0f - 1.0f;
                float dz = unit01(s) * 2.0f - 1.0f;
                const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (len < 1e-3f) continue;
                dx *= 0.25f / len;
                dy *= 0.25f / len;
                dz *= 0.25f / len;
                const std::array<float, 3> b{a[0] + dx, a[1] + dy, a[2] + dz};
                const float moved = std::sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]) + (b[2] - a[2]) * (b[2] - a[2]));
                const float change = std::fabs(terrainSdf(g, b) - terrainSdf(g, a));
                check(change <= moved * 1.01f + 2e-3f, "the landscape's distance is a bound (" + std::to_string(change) + " over " +
                                                           std::to_string(moved) + ", Terrain " + std::to_string(on) + ")");
                ++pairs;
            }
        }
        // Across the rig's travel wrap a point and its image 8192 behind it stand on the same ground.
        const Genome& g = seed(i).genome;
        for (float z : {8092.5f, 8120.0f, 8150.25f, 8191.75f, 8230.0f}) {
            for (float x : {-12.0f, -3.0f, 0.0f, 2.5f, 9.0f}) {
                for (float y : {-5.0f, -2.5f, 0.0f}) {
                    const float d0 = terrainSdf(g, {x, y, z});
                    const float d1 = terrainSdf(g, {x, y, z - 8192.0f});
                    check(std::fabs(d0 - d1) < 2e-3f, "the landscape is seamless across the travel wrap (" + std::to_string(d0) + " vs " +
                                                          std::to_string(d1) + ")");
                }
            }
        }
        // Off, the layer is out of reach; and the ground stays under the eye's tube at the axis' far
        // sides only through the corridor's carve, which sdf() already checks above.
        Genome off = seed(i).genome;
        off[Terrain] = 0.0f;
        check(terrainSdf(off, {0.0f, -4.0f, 0.0f}) >= 79.0f, "Terrain 0 puts the landscape out of reach");
    }
    check(landscapes == 0 || pairs > 0, "sampled the landscape");
}

void groundKeepsEveryWorldOpen() {
    const float highest = bounds(FloorY).hi;
    // The ground's highest bound lies under the eye's bubble on every rig path, at every orbit.
    float lowestEye = 1e9f;
    for (int path = 0; path < 4; ++path) {
        for (float orbit : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            for (int s = 0; s < 4000; ++s) {
                const auto here = CameraRig::pathPoint(path, orbit, 0.37f * static_cast<float>(s));
                lowestEye = std::fmin(lowestEye, here[1]);
                check(here[1] - kEyeRadius > highest, "the ground stays under the eye's bubble (path " + std::to_string(path) + ")");
            }
        }
    }
    check(lowestEye - kEyeRadius > highest, "the deepest eye clears the highest ground (" + std::to_string(lowestEye) + ")");

    // Floor 0 is the identity, and the fade is continuous: a hair of floor moves the field a hair.
    const Way still;
    for (int i = 0; i < seedCount(); ++i) {
        Genome off = seed(i).genome;
        off[Floor] = 0.0f;
        Genome hair = off;
        hair[Floor] = 1e-4f;
        for (float y : {-5.5f, -3.4f, -1.0f, 0.0f, 2.5f}) {
            const std::array<float, 3> p{1.3f, y, 0.7f};
            check(std::fabs(sdfUncarved(hair, still, p) - sdfUncarved(off, still, p)) < 2e-3f,
                  std::string("the floor fades in continuously in ") + seed(i).name);
        }
        // At full weight, under the ground is matter.
        Genome full = off;
        full[Floor] = 1.0f;
        full[FloorY] = highest;
        check(sdfUncarved(full, still, {0.5f, highest - 1.0f, 0.3f}) < 0.0f, std::string("under the ground is solid in ") + seed(i).name);
        // Every seed, given a full floor at its highest, is still open and has something to see.
        check(valid(full), std::string(seed(i).name) + " stays valid on a floor" + probeText(full));
        Genome half = full;
        half[Floor] = 0.5f;
        check(valid(half), std::string(seed(i).name) + " stays valid on a half-grown floor" + probeText(half));
    }

    // The corridor holds on the four paths with a full floor at its highest, in random worlds.
    float worst = 1e9f;
    for (int i = 0; i < 60; ++i) {
        Constraints c;
        c.mutation = static_cast<float>(i % 6) / 5.0f;
        Genome g = random(5000u + static_cast<uint64_t>(i), c);
        g[Floor] = 1.0f;
        g[FloorY] = highest;
        for (int path = 0; path < 4; ++path) {
            for (float orbit : {0.0f, 0.5f, 1.0f}) {
                for (int s = 0; s < 6; ++s) {
                    const float travel = 41.0f * static_cast<float>(s + i);
                    const auto here = CameraRig::pathPoint(path, orbit, travel);
                    const auto ahead = CameraRig::pathPoint(path, orbit, travel + 1.6f);
                    Way way;
                    way.eye = here;
                    way.zPhase = std::fmod(travel * 0.37f, 2.0f * g[CellZ]);
                    way.wake = {ahead[0] - here[0], ahead[1] - here[1], ahead[2] - here[2]};
                    const float d = sdf(g, way, here);
                    worst = std::fmin(worst, d);
                    for (float h : {0.25f, 0.5f, 1.0f}) {
                        const std::array<float, 3> p{here[0] + way.wake[0] * h, here[1] + way.wake[1] * h, here[2] + way.wake[2] * h};
                        check(sdf(g, way, p) > 0.0f, "the wake ahead of the eye is open over a floor");
                    }
                }
            }
        }
    }
    check(worst >= kEyeRadius * 0.99f, "over a floor the eye keeps its bubble (" + std::to_string(worst) + ")");
}

// A synthetic song: a calm intro, a verse, a build, a drop, a breakdown, repeated.
GeodeSoundFrame songFrame(int frame, float fps) {
    GeodeSoundFrame f{};
    const float t = static_cast<float>(frame) / fps;
    const int part = static_cast<int>(t / 20.0f) % 5;
    const float levels[5] = {0.2f, 0.5f, 0.7f, 0.95f, 0.3f};
    f.presence = 1.0f;
    f.loudness = levels[part];
    f.intensity = levels[part];
    f.scaleBrightness = 0.4f + 0.1f * static_cast<float>(part);
    f.keyMajor = part % 2 == 0 ? 1.0f : 0.0f;
    f.densitySlow = 0.3f + 0.1f * static_cast<float>(part);
    f.bassAtt = 0.5f + 0.5f * std::sin(t * 3.0f);
    f.kickEnv = std::fmod(t * 2.0f, 1.0f) < 0.1f ? 1.0f : 0.0f;
    f.chordHue = std::fmod(t * 0.05f, 1.0f);
    f.buildUp = part == 2 ? std::fmod(t, 20.0f) / 20.0f : 0.0f;
    const int at = frame % static_cast<int>(20.0f * fps);
    f.sectionChange = at == 0 && frame > 0 ? 1.0f : 0.0f;
    f.drop = part == 3 && at == 0 ? 1.0f : 0.0f;
    return f;
}

struct Run {
    std::vector<int> seeds;
    std::vector<float> fields;
    float maxGainStep = 0.0f;
    float maxFieldStep = 0.0f;
    // The gaze: every frame's look, how far it ever strayed past its range or its rate, its widest.
    std::vector<float> gaze;
    float gazeOutside = 0.0f;
    float gazeOverRate = 0.0f;
    float gazeWidest = 0.0f;
};

Run play(uint64_t seedValue, int frames, float energyScale = 1.0f) {
    MorphDriver d;
    d.reset(seedValue);
    Run run;
    const float fps = 60.0f;
    int last = 0;
    float lastGain = -1.0f;
    Genome prev{};
    bool havePrev = false;
    float lastGX = 0.0f;
    float lastGY = 0.0f;
    for (int i = 0; i < frames; ++i) {
        GeodeSoundFrame f = songFrame(i, fps);
        f.loudness *= energyScale;
        f.intensity *= energyScale;
        DriverInput in = DriverInput::fromSound(f);
        in.morph = 0.4f;
        d.step(in, 1.0f / fps);
        const float z = static_cast<float>(i) * 0.03f;
        d.travel({0.3f * std::sin(z), 0.1f, z}, 1.0f / fps);
        if (d.retargets() != last) {
            last = d.retargets();
            run.seeds.push_back(d.targetSeed());
            run.fields.push_back(d.target()[IfsScale]);
            run.fields.push_back(d.target()[PalOffset]);
        }
        if (lastGain >= 0.0f) run.maxGainStep = std::fmax(run.maxGainStep, std::fabs(d.gain() - lastGain));
        lastGain = d.gain();
        if (havePrev) {
            for (int fi = 0; fi < kFieldCount; ++fi) {
                if (wraps(fi)) continue;
                const Bounds b = bounds(fi);
                run.maxFieldStep = std::fmax(run.maxFieldStep, std::fabs(d.live()[fi] - prev[fi]) / (b.hi - b.lo));
            }
        }
        // The gaze: within the live world's range, and turning at most its rate per frame (plus what
        // the range itself narrowed by, which may pull the look in with it).
        const float gx = d.uniforms()[GazeX];
        const float gy = d.uniforms()[GazeY];
        run.gaze.push_back(gx);
        run.gaze.push_back(gy);
        run.gazeOutside = std::fmax(run.gazeOutside, std::fabs(gx) - d.live()[GazeYaw]);
        run.gazeOutside = std::fmax(run.gazeOutside, std::fabs(gy) - d.live()[GazePitch]);
        run.gazeWidest = std::fmax(run.gazeWidest, std::fmax(std::fabs(gx), std::fabs(gy)));
        if (havePrev) {
            const float rate = MorphDriver::kGazeRatePerSecond / fps;
            const float shrinkX = std::fmax(0.0f, prev[GazeYaw] - d.live()[GazeYaw]);
            const float shrinkY = std::fmax(0.0f, prev[GazePitch] - d.live()[GazePitch]);
            run.gazeOverRate = std::fmax(run.gazeOverRate, std::fabs(gx - lastGX) - rate - shrinkX);
            run.gazeOverRate = std::fmax(run.gazeOverRate, std::fabs(gy - lastGY) - rate - shrinkY);
        }
        lastGX = gx;
        lastGY = gy;
        prev = d.live();
        havePrev = true;
        check(inBounds(d.live()), "the live genome stays in bounds");
        check(d.uniforms()[ZPhase] >= 0.0f && d.uniforms()[ZPhase] <= 2.0f * d.live()[CellZ] + 1e-3f, "the z phase stays in a period");
    }
    return run;
}

void driverIsReplayableAndSeeded() {
    const int frames = 60 * 60 * 4;  // four minutes
    const Run a = play(0xC0FFEEu, frames);
    const Run b = play(0xC0FFEEu, frames);
    const Run c = play(0xBADBEEFu, frames);
    check(!a.seeds.empty(), "a song with sections and drops retargets");
    check(a.seeds == b.seeds && a.fields == b.fields, "same seed and music: the same target sequence");
    check(a.seeds != c.seeds || a.fields != c.fields, "another seed: another sequence");
    // Every drop retargets: four minutes of a 100-second cycle have two drops (t = 60, 160).
    check(a.seeds.size() >= 2, "the drops turned the world");
    // No flash: the gain moves at most its rate per frame, and the genome glides.
    check(a.maxGainStep <= MorphDriver::kGainRatePerSecond / 60.0f + 1e-5f, "the gain envelope is rate-limited");
    check(a.maxFieldStep < 0.05f, "no field jumps more than 5% of its range in a frame (" + std::to_string(a.maxFieldStep) + ")");
}

void gazeIsBoundedReplayableAndRateLimited() {
    const int frames = 60 * 60 * 4;
    const Run a = play(0xC0FFEEu, frames);
    const Run b = play(0xC0FFEEu, frames);
    check(a.gaze == b.gaze, "same seed and music: the same gaze, frame for frame");
    float widest = 0.0f;
    bool differs = false;
    for (uint64_t s = 1; s <= 8; ++s) {
        const Run r = play(s * 104729u + 3u, frames);
        check(r.gazeOutside <= 1e-6f, "the gaze stays within the world's GazeYaw and GazePitch (" + std::to_string(r.gazeOutside) + ")");
        check(r.gazeOverRate <= 1e-5f, "the gaze turns at most kGazeRatePerSecond (" + std::to_string(r.gazeOverRate) + " over)");
        widest = std::fmax(widest, r.gazeWidest);
        if (r.gaze != a.gaze) differs = true;
    }
    check(a.gazeOutside <= 1e-6f && a.gazeOverRate <= 1e-5f, "the reference play's gaze is bounded and rate-limited");
    check(differs, "another seed: another gaze");
    check(widest > 0.02f, "a song with sections looks around (" + std::to_string(widest) + " rad)");
}

void aCalmSongKeepsItsGaze() {
    // No sections, no drops, a whisper with a little bass and a slowly turning chord: the view only
    // drifts, staying near where it started (centre) for the whole song.
    MorphDriver d;
    for (uint64_t s = 1; s <= 20; ++s) {
        d.reset(s);
        float worst = 0.0f;
        for (int i = 0; i < 60 * 180; ++i) {
            const float t = static_cast<float>(i) / 60.0f;
            GeodeSoundFrame f{};
            f.presence = 1.0f;
            f.loudness = 0.05f;
            f.intensity = 0.05f;
            f.bassAtt = 0.3f + 0.2f * std::sin(t * 2.0f);
            f.chordHue = std::fmod(t * 0.02f, 1.0f);
            d.step(DriverInput::fromSound(f), 1.0f / 60.0f);
            const float rx = d.live()[GazeYaw];
            const float ry = d.live()[GazePitch];
            worst = std::fmax(worst, std::fabs(d.gazeX()) - 0.2f * rx);
            worst = std::fmax(worst, std::fabs(d.gazeY()) - 0.2f * ry);
        }
        check(worst <= 1e-5f, "a calm song's gaze stays within a fifth of its range (seed " + std::to_string(s) + ")");
    }
}

void aDropBringsTheGazeHome() {
    // Look away on loud sections until the view is well off centre, then drop: about a second later
    // the gaze is back near centre, eased there, never snapped.
    bool tried = false;
    for (uint64_t s = 1; s <= 200 && !tried; ++s) {
        MorphDriver d;
        d.reset(s);
        GeodeSoundFrame f{};
        f.presence = 1.0f;
        f.loudness = 0.8f;
        f.intensity = 0.8f;
        bool away = false;
        for (int i = 0; i < 60 * 40 && !away; ++i) {
            f.sectionChange = (i % (60 * 6) == 0 && i > 0) ? 1.0f : 0.0f;
            d.step(DriverInput::fromSound(f), 1.0f / 60.0f);
            away = std::fmax(std::fabs(d.gazeX()), std::fabs(d.gazeY())) > 0.12f;
        }
        if (!away) continue;
        tried = true;
        f.sectionChange = 0.0f;
        f.drop = 1.0f;
        float lastX = d.gazeX();
        float lastY = d.gazeY();
        float worstStep = 0.0f;
        for (int i = 0; i < 66; ++i) {  // 1.1 s
            d.step(DriverInput::fromSound(f), 1.0f / 60.0f);
            f.drop = 0.0f;
            worstStep = std::fmax(worstStep, std::fmax(std::fabs(d.gazeX() - lastX), std::fabs(d.gazeY() - lastY)));
            lastX = d.gazeX();
            lastY = d.gazeY();
        }
        check(std::fmax(std::fabs(d.gazeX()), std::fabs(d.gazeY())) < 0.05f,
              "a drop brings the gaze back to centre in about a second (" + std::to_string(d.gazeX()) + ", " +
                  std::to_string(d.gazeY()) + ")");
        // The rate, plus room for the drop's new world narrowing the range under the look.
        check(worstStep <= MorphDriver::kGazeRatePerSecond / 60.0f + 0.02f, "the drop's return is eased, not snapped");
    }
    check(tried, "some world looks well away on loud sections");
}

void aCalmSongCanKeepItsWorld() {
    // A whisper with section changes but no drops: over many seeds, some plays never leave the
    // first world, and none is forced to change on a timer.
    MorphDriver d;
    int stayed = 0;
    for (uint64_t s = 1; s <= 40; ++s) {
        d.reset(s);
        for (int i = 0; i < 60 * 180; ++i) {
            GeodeSoundFrame f{};
            f.presence = 1.0f;
            f.loudness = 0.05f;
            f.intensity = 0.05f;
            f.sectionChange = (i % (60 * 30) == 0 && i > 0) ? 1.0f : 0.0f;
            d.step(DriverInput::fromSound(f), 1.0f / 60.0f);
        }
        if (d.retargets() == 0) ++stayed;
    }
    check(stayed > 0, "some calm plays stay in one world all song (" + std::to_string(stayed) + "/40)");
    // And no music at all never retargets.
    d.reset(7);
    for (int i = 0; i < 60 * 600; ++i) d.step(DriverInput{}, 1.0f / 60.0f);
    check(d.retargets() == 0, "silence never changes the world");
}

void dropRushesAndVerseGlides() {
    MorphDriver d;
    d.reset(3);
    GeodeSoundFrame f{};
    f.presence = 1.0f;
    f.loudness = 0.9f;
    f.intensity = 0.9f;
    d.step(DriverInput::fromSound(f), 1.0f / 60.0f);
    f.drop = 1.0f;
    d.step(DriverInput::fromSound(f), 1.0f / 60.0f);
    check(d.retargets() == 1 && d.glideSeconds() == MorphDriver::kDropGlide, "a drop retargets with the fast glide");
    f.drop = 1.0f;
    d.step(DriverInput::fromSound(f), 1.0f / 60.0f);
    check(d.retargets() == 1, "a held trigger is one moment");
    f.drop = 0.0f;
    for (int i = 0; i < 200; ++i) d.step(DriverInput::fromSound(f), 1.0f / 60.0f);
    check(d.progress() >= 1.0f, "the drop's glide lands in under four seconds");
}

void travelIsSeamless() {
    // The eye crossing the rig's 8192 travel wrap moves the z phase by its true step, not the jump.
    MorphDriver d;
    d.reset(9);
    d.step(DriverInput{}, 1.0f / 60.0f);
    d.travel({0.0f, 0.0f, 8191.9f}, 1.0f / 60.0f);
    const float before = d.uniforms()[ZPhase];
    d.travel({0.0f, 0.0f, 0.05f}, 1.0f / 60.0f);
    float moved = d.uniforms()[ZPhase] - before;
    const float pz = 2.0f * d.live()[CellZ];
    if (moved < -pz * 0.5f) moved += pz;
    check(std::fabs(moved - 0.15f) < 1e-3f, "the wrap is invisible to the world (moved " + std::to_string(moved) + ")");
}

// The Julia, Kleinian and octahedral KIFS seeds (#499): each named seed exists, is made of its
// primitive, turns round the way by an odd fold (never the 4-way cross), is open with matter to
// see, and keeps the eye and its wake open on every rig path, as designed and lightly mutated.
void newFractalSeedsHold() {
    struct Want {
        const char* name;
        int prim;
    };
    const Want wants[] = {
        {"Julia Bloom", PJulia}, {"Quaternion Reef", PJulia},   {"Julia Lanterns", PJulia},  {"Klein Foam", PKlein},
        {"Limit Set Nave", PKlein}, {"Octa Spires", PKifs}, {"Octahedral Choir", PKifs},
    };
    for (const Want& w : wants) {
        int at = -1;
        for (int i = 0; i < seedCount(); ++i) {
            if (std::string(seed(i).name) == w.name) at = i;
        }
        check(at >= 0, std::string("seed ") + w.name + " exists");
        if (at < 0) continue;
        const Genome& g = seed(at).genome;
        check(g[w.prim] > 0.99f, std::string(w.name) + " is made of its primitive");
        const float n = g[PolarN];
        const bool oddFold = g[Polar] > 0.99f && std::fabs(n - std::round(n)) < 1e-4f && static_cast<int>(std::round(n)) % 2 == 1;
        const bool tilted = std::fabs(g[LatticeTilt]) > 0.05f || std::fabs(g[LatticeYaw]) > 0.05f;
        check(oddFold || tilted, std::string(w.name) + " turns by an odd fold or a tilted lattice");
        check(valid(g), std::string(w.name) + " is valid");
        for (float mutation : {0.0f, 0.3f}) {
            for (int path = 0; path < 4; ++path) {
                for (int s = 0; s < 8; ++s) {
                    Genome h = g;
                    if (mutation > 0.0f) {
                        // A light, replayable wobble of the space's operators and the primitive's numbers.
                        uint64_t r = mix64(static_cast<uint64_t>(at * 131 + path * 17 + s));
                        for (int f : {static_cast<int>(PrimSize), static_cast<int>(JuliaCX), static_cast<int>(JuliaCY),
                                      static_cast<int>(KleinBox), static_cast<int>(KifsScale), static_cast<int>(PolarTwist)}) {
                            r = mix64(r);
                            const Bounds b = bounds(f);
                            h[f] += (static_cast<float>((r >> 40) & 0xFFFFu) / 65535.0f * 2.0f - 1.0f) * mutation * (b.hi - b.lo) * 0.5f;
                        }
                        h = clampFields(h);
                    }
                    const float travel = 23.0f * static_cast<float>(s) + 5.0f * static_cast<float>(path);
                    const auto here = CameraRig::pathPoint(path, 0.5f, travel);
                    const auto ahead = CameraRig::pathPoint(path, 0.5f, travel + 1.6f);
                    Way way;
                    way.eye = here;
                    way.zPhase = std::fmod(travel * 0.37f, 2.0f * h[CellZ]);
                    way.wake = {ahead[0] - here[0], ahead[1] - here[1], ahead[2] - here[2]};
                    const float d = sdf(h, way, here);
                    check(std::isfinite(d) && d >= kEyeRadius * 0.99f,
                          std::string(w.name) + " keeps the eye open (path " + std::to_string(path) + ")");
                    for (float t : {0.5f, 1.0f}) {
                        const std::array<float, 3> p{here[0] + way.wake[0] * t, here[1] + way.wake[1] * t, here[2] + way.wake[2] * t};
                        check(sdf(h, way, p) > 0.0f, std::string(w.name) + " keeps the wake open");
                    }
                }
            }
        }
    }
}

// lib_world's WG_* names must index the fields the C++ side uploads.
void glslNamesMatch(const char* path) {
    std::ifstream in(path);
    check(in.good(), std::string("read ") + path);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string src = ss.str();
    const std::map<std::string, int> expect = {
        {"WG_REP_XY", RepXY},       {"WG_CELL_XY", CellXY},     {"WG_CELL_Z", CellZ},       {"WG_CELL_SHIFT", CellShift}, {"WG_MIRROR", Mirror},
        {"WG_MIRROR_OFF", MirrorOff}, {"WG_IFS", Ifs},          {"WG_IFS_ITER", IfsIter},   {"WG_IFS_SCALE", IfsScale},
        {"WG_IFS_ANG_A", IfsAngA},  {"WG_IFS_ANG_B", IfsAngB},  {"WG_IFS_OFF_X", IfsOffX},  {"WG_IFS_OFF_Y", IfsOffY},
        {"WG_IFS_OFF_Z", IfsOffZ},  {"WG_INVERT", Invert},      {"WG_INV_RADIUS", InvRadius}, {"WG_ABS_INV", AbsInv},
        {"WG_ABS_INV_C", AbsInvC},  {"WG_TWIST", Twist},        {"WG_SCALE", Scale},
        {"WG_POLAR", Polar},        {"WG_POLAR_N", PolarN},     {"WG_POLAR_TWIST", PolarTwist}, {"WG_P_GYROID", PGyroid},
        {"WG_P_MENGER", PMenger},   {"WG_P_BULB", PBulb},       {"WG_BULB_POWER", BulbPower}, {"WG_GYRO_SCALE", GyroScale},
        {"WG_P_MANDELBOX", PMandelbox}, {"WG_P_SIERP", PSierp}, {"WG_P_APOLLO", PApollo}, {"WG_BOX_SCALE", BoxScale},        {"WG_P_BOX", PBox},
        {"WG_P_BAR", PBar},         {"WG_P_TUBE", PTube},       {"WG_P_TORUS", PTorus},     {"WG_P_SHELL", PShell},
        {"WG_P_SLAB", PSlab},       {"WG_P_MESH", PMesh},       {"WG_PRIM_SIZE", PrimSize},
        {"WG_MESH_PICK", MeshPick}, {"WG_MESH_SIZE", MeshSize}, {"WG_TEX_AMOUNT", TexAmount}, {"WG_TEX_PICK", TexPick},
        {"WG_TEX_SCALE", TexScale}, {"WG_TEX_EMIT", TexEmit},   {"WG_ASSETS", Assets}, {"WG_PRIM_THICK", PrimThick}, {"WG_M_SURFACE", MSurface},
        {"WG_M_GLOW", MGlow},       {"WG_M_DENSITY", MDensity}, {"WG_DENS_BAND", DensBand}, {"WG_TRAP_X", TrapX},
        {"WG_TRAP_Y", TrapY},       {"WG_TRAP_Z", TrapZ},       {"WG_TRAP_R", TrapR},       {"WG_PAL_OFFSET", PalOffset},
        {"WG_PAL_SPAN", PalSpan},   {"WG_EMISSION", Emission},  {"WG_FOG", Fog},            {"WG_CORRIDOR", Corridor},
        {"WG_Z_PHASE", ZPhase},     {"WG_GAIN", Gain},          {"WG_ITER_CAP", IterCap},   {"WG_HUE_TURN", HueTurn},
        {"WG_WAKE_X", WakeX},       {"WG_WAKE_Y", WakeY},       {"WG_WAKE_Z", WakeZ},
        {"WG_ROLL", Roll},          {"WG_LENS", Lens},          {"WG_ROLL_ANGLE", RollAngle}, {"WG_LENS_NOW", LensNow},
        {"WG_FLOOR", Floor}, {"WG_FLOOR_Y", FloorY}, {"WG_FLOOR_GLOSS", FloorGloss}, {"WG_SKY", Sky}, {"WG_SKY_HUE", SkyHue}, {"WG_HORIZON", Horizon}, {"WG_TERRAIN", Terrain}, {"WG_TERRAIN_HEIGHT", TerrainHeight}, {"WG_TERRAIN_SCALE", TerrainScale}, {"WG_LATTICE_TILT", LatticeTilt}, {"WG_LATTICE_YAW", LatticeYaw}, {"WG_GAZE_PITCH", GazePitch}, {"WG_GAZE_YAW", GazeYaw}, {"WG_GAZE_X", GazeX}, {"WG_GAZE_Y", GazeY},
        {"WG_P_JULIA", PJulia}, {"WG_P_KLEIN", PKlein}, {"WG_P_KIFS", PKifs}, {"WG_JULIA_CX", JuliaCX}, {"WG_JULIA_CY", JuliaCY},
        {"WG_JULIA_CZ", JuliaCZ}, {"WG_JULIA_CW", JuliaCW}, {"WG_KLEIN_BOX", KleinBox}, {"WG_KIFS_SCALE", KifsScale},
    };
    const char comps[4] = {'x', 'y', 'z', 'w'};
    for (const auto& [name, index] : expect) {
        char want[96];
        std::snprintf(want, sizeof want, "#define %s uWorld[%d].%c", name.c_str(), index / 4, comps[index % 4]);
        check(src.find(want) != std::string::npos, std::string("lib_world has ") + want);
    }
    check(src.find("uniform vec4 uWorld[" + std::to_string(kVec4) + "];") != std::string::npos, "lib_world declares uWorld[" + std::to_string(kVec4) + "]");
    // Every field and runtime value has a name: a new one cannot be left out of the map.
    check(static_cast<int>(expect.size()) == kUniformCount, "every uWorld slot is named (" + std::to_string(expect.size()) + "/" + std::to_string(kUniformCount) + ")");
    // The shape atlas: wShape() addresses the tiles WorldAssets packs.
    for (const auto& [name, value] : std::map<std::string, int>{
             {"WG_SHAPES", kShapeCount}, {"WG_SHAPE_RES", kShapeRes}, {"WG_ATLAS_COLS", kAtlasCols}, {"WG_ATLAS_ROWS", kAtlasRows}}) {
        const std::string want = "#define " + name + " " + std::to_string(value) + ".0\n";
        check(src.find(want) != std::string::npos, "lib_world has #define " + name + " " + std::to_string(value) + ".0");
    }
}

std::vector<uint8_t> readAll(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

// The GPU's tile atlas (#555): every side within GLES 3.0's 256, one tile per model, and lib_world's
// addressing (atlasDistance) sampling it equal to shapeDistance() reading the file's slabs.
void atlasLayoutIsSound() {
    check(atlasMaxDimension() <= kGles3Max3dSize, "the shape atlas fits a 256^3 GL_MAX_3D_TEXTURE_SIZE");
    check(kAtlasWidth == 192 && kAtlasHeight == 128 && kAtlasDepth == 32, "the shape atlas is 192 x 128 x 32");
    std::vector<bool> used(static_cast<size_t>(kAtlasCols * kAtlasRows), false);
    for (int i = 0; i < kShapeCount; ++i) {
        const TileOrigin o = tileOrigin(i);
        const bool inside = o.x >= 0 && o.y >= 0 && o.x + kShapeRes <= kAtlasWidth && o.y + kShapeRes <= kAtlasHeight &&
                            o.x % kShapeRes == 0 && o.y % kShapeRes == 0;
        check(inside, "tile " + std::to_string(i) + " lies whole inside the atlas");
        if (!inside) continue;
        const size_t cell = static_cast<size_t>((o.y / kShapeRes) * kAtlasCols + o.x / kShapeRes);
        check(!used[cell], "tile " + std::to_string(i) + " has a tile of its own");
        used[cell] = true;
    }
    check(packAtlas(std::vector<uint8_t>(10, 0)).empty(), "packAtlas refuses the wrong size");
    // No bleed: a model of all 0 between neighbours of all 255 reads 0 everywhere, edges included.
    std::vector<uint8_t> slabs(kShapeBytes, 255);
    const int probe = 7;  // a tile with neighbours on all four sides in the 6 x 4 grid
    const size_t slab = static_cast<size_t>(kShapeRes) * kShapeRes * kShapeRes;
    std::fill_n(slabs.begin() + static_cast<std::ptrdiff_t>(slab * probe), slab, 0);
    const std::vector<uint8_t> atlas = packAtlas(slabs);
    check(atlas.size() == kAtlasBytes, "packAtlas makes kAtlasBytes");
    float worst = 0.0f;
    for (float x : {-1.0f, -0.99f, 0.0f, 0.99f, 1.0f}) {
        for (float y : {-1.0f, -0.99f, 0.0f, 0.99f, 1.0f}) {
            for (float z : {-1.0f, 0.5f, 1.0f}) worst = std::fmax(worst, std::fabs(atlasDistance(atlas, probe, x, y, z) - kShapeLo));
        }
    }
    check(worst < 1e-5f, "linear filtering never reads a neighbouring tile");
}

void atlasMatchesTheSlabs(const std::vector<uint8_t>& slabs) {
    const std::vector<uint8_t> atlas = packAtlas(slabs);
    check(atlas.size() == kAtlasBytes, "the baked shapes pack into the atlas");
    if (atlas.size() != kAtlasBytes) return;
    // Byte for byte: each slab's first and last texel lands at its tile's corners.
    for (int i = 0; i < kShapeCount; ++i) {
        const TileOrigin o = tileOrigin(i);
        const size_t slab = static_cast<size_t>(kShapeRes) * kShapeRes * kShapeRes;
        const size_t first = (static_cast<size_t>(o.y) * kAtlasWidth) + static_cast<size_t>(o.x);
        const size_t last = ((static_cast<size_t>(kShapeRes - 1) * kAtlasHeight + static_cast<size_t>(o.y + kShapeRes - 1)) * kAtlasWidth) +
                            static_cast<size_t>(o.x + kShapeRes - 1);
        check(atlas[first] == slabs[slab * static_cast<size_t>(i)] && atlas[last] == slabs[slab * static_cast<size_t>(i + 1) - 1],
              "shape " + std::to_string(i) + "'s corners land in its tile");
    }
    // Sampled: lib_world's addressing on the atlas equals shapeDistance() on the file, inside the
    // cube, on its faces and outside it.
    float worst = 0.0f;
    for (int i = 0; i < kShapeCount; ++i) {
        for (int k = 0; k < 343; ++k) {
            const float x = -1.2f + 2.4f * static_cast<float>(k % 7) / 6.0f;
            const float y = -1.0f + 2.0f * static_cast<float>((k / 7) % 7) / 6.0f + 0.013f;
            const float z = -1.05f + 2.1f * static_cast<float>(k / 49) / 6.0f;
            worst = std::fmax(worst, std::fabs(atlasDistance(atlas, i, x, y, z) - shapeDistance(i, x, y, z)));
        }
        worst = std::fmax(worst, std::fabs(atlasDistance(atlas, i, 0.31f, -0.47f, 0.77f) - shapeDistance(i, 0.31f, -0.47f, 0.77f)));
    }
    check(worst < 1e-4f, "the atlas samples like the slabs (worst " + std::to_string(worst) + ")");
}

void loadAssets(const char* shapes, const char* textures) {
    const auto s = readAll(shapes);
    check(s.size() == kShapeBytes, "world_shapes.bin is " + std::to_string(kShapeBytes) + " bytes");
    check(setShapeVolumes(s) && hasShapes(), "the shapes load");
    if (textures != nullptr) check(readAll(textures).size() == kTexBytes, "world_textures.bin is " + std::to_string(kTexBytes) + " bytes");
    // Every baked model has an inside and an outside, and the distance outside the cube is a bound.
    for (int i = 0; i < kShapeCount; ++i) {
        float lo = 1e9f;
        float hi = -1e9f;
        for (int k = 0; k < 4096; ++k) {
            const float x = -0.95f + 1.9f * static_cast<float>(k % 16) / 15.0f;
            const float y = -0.95f + 1.9f * static_cast<float>((k / 16) % 16) / 15.0f;
            const float z = -0.95f + 1.9f * static_cast<float>(k / 256) / 15.0f;
            const float d = shapeDistance(i, x, y, z);
            lo = std::fmin(lo, d);
            hi = std::fmax(hi, d);
        }
        check(lo < 0.0f && hi > 0.0f, "baked shape " + std::to_string(i) + " has an inside and an outside");
        check(shapeDistance(i, 3.0f, 0.0f, 0.0f) <= 2.0f + 1e-4f && shapeDistance(i, 3.0f, 0.0f, 0.0f) >= 2.0f - 1e-4f,
              "outside the cube the distance is the way to the cube");
    }
    atlasMatchesTheSlabs(s);
}

}  // namespace

// Audio drive reaches Morph World: the levels the driver hears scale with it, and 1 is the frame as heard.
void audioDriveScalesWhatTheWorldHears() {
    GeodeSoundFrame f{};
    f.presence = 1.0f;
    f.loudness = 0.3f;
    f.intensity = 0.3f;
    f.bassAtt = 0.5f;
    f.kickEnv = 0.4f;
    const DriverInput at1 = DriverInput::fromSound(f);
    const DriverInput same = DriverInput::fromSound(f, 1.0f);
    check(at1.energy == same.energy && at1.bass == same.bass && at1.kick == same.kick, "Audio drive 1 is the frame as heard");
    const DriverInput loud = DriverInput::fromSound(f, 2.0f);
    const DriverInput soft = DriverInput::fromSound(f, 0.2f);
    check(loud.energy > at1.energy && loud.bass > at1.bass && loud.kick > at1.kick, "Audio drive 2 makes the world hear more");
    check(soft.energy < at1.energy && soft.bass < at1.bass && soft.kick < at1.kick, "Audio drive 0.2 makes it hear less");
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--dump") {
        const Genome& g = seed(std::atoi(argv[2])).genome;
        std::array<float, kUniformCount> u{};
        for (int f = 0; f < kFieldCount; ++f) u[static_cast<size_t>(f)] = g[f];
        u[Gain] = std::fmin(std::fmax(0.85f / std::fmax(brightness(g), 0.2f), 0.6f), 1.5f);
        u[IterCap] = 5.0f;
        u[WakeZ] = 1.9f;
        u[Assets] = 1.0f;
        for (int i = 0; i < kUniformCount; ++i) std::printf("%s%g", i ? "," : "", static_cast<double>(u[static_cast<size_t>(i)]));
        std::printf("\n");
        return 0;
    }
    if (argc >= 2 && std::string(argv[1]) == "--names") {
        if (argc == 3) setShapeVolumes(readAll(argv[2]));
        for (int i = 0; i < seedCount(); ++i) {
            const Space sp = openSpace(seed(i).genome);
            std::printf("%s\topen %.2f\tnear %.2f\tbright %.2f\n", seed(i).name, static_cast<double>(sp.open),
                        static_cast<double>(sp.near), static_cast<double>(brightness(seed(i).genome)));
        }
        return 0;
    }
    if (argc == 4) loadAssets(argv[2], argv[3]);
    lerpEndpointsAreExact();
    audioDriveScalesWhatTheWorldHears();
    seedsAreValid();
    noWorldIsTheFourWayCross();
    randomGenomesAreValid();
    corridorIsOpenAtTheCamera();
    landscapesAreOpenAndHoldTheCorridor();
    terrainIsABoundAndSeamless();
    groundKeepsEveryWorldOpen();
    driverIsReplayableAndSeeded();
    gazeIsBoundedReplayableAndRateLimited();
    aCalmSongKeepsItsGaze();
    aDropBringsTheGazeHome();
    aCalmSongCanKeepItsWorld();
    dropRushesAndVerseGlides();
    travelIsSeamless();
    newFractalSeedsHold();
    atlasLayoutIsSound();
    if (argc >= 2) glslNamesMatch(argv[1]);
    if (failures == 0) std::printf("world_genome_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
