// Morph World's genome and morph driver (core/viz/world), on the host.
//
//   world_genome_test [lib_world.glsl [world_shapes.bin world_textures.bin]]
//                                 run the checks; with lib_world, also check that its WG_* names
//                                 index the same fields; with the baked assets, check their sizes
//                                 and probe every world with the real shapes (else spheres)
//   world_genome_test --dump <seed>          print seed <seed>'s uWorld (88 floats, comma separated)
//                                            for tools/glslcheck --set uWorld=...
//   world_genome_test --names [shapes.bin]   print the seeds' names and open-space probes
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
    check(seedCount() >= 8 && seedCount() <= 64, "8 to 64 seed worlds");
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
        if (valid(g)) ++valids;
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
        {"WG_FLOOR", Floor}, {"WG_FLOOR_Y", FloorY}, {"WG_FLOOR_GLOSS", FloorGloss}, {"WG_SKY", Sky}, {"WG_SKY_HUE", SkyHue}, {"WG_HORIZON", Horizon}, {"WG_TERRAIN", Terrain}, {"WG_TERRAIN_HEIGHT", TerrainHeight}, {"WG_TERRAIN_SCALE", TerrainScale}, {"WG_LATTICE_TILT", LatticeTilt}, {"WG_LATTICE_YAW", LatticeYaw}, {"WG_GAZE_PITCH", GazePitch}, {"WG_GAZE_YAW", GazeYaw}, {"WG_GAZE_X", GazeX}, {"WG_GAZE_Y", GazeY}, {"WG_SPARE", Spare},
    };
    const char comps[4] = {'x', 'y', 'z', 'w'};
    for (const auto& [name, index] : expect) {
        char want[96];
        std::snprintf(want, sizeof want, "#define %s uWorld[%d].%c", name.c_str(), index / 4, comps[index % 4]);
        check(src.find(want) != std::string::npos, std::string("lib_world has ") + want);
    }
    check(src.find("uniform vec4 uWorld[" + std::to_string(kVec4) + "];") != std::string::npos, "lib_world declares uWorld[22]");
}

std::vector<uint8_t> readAll(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
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
}

}  // namespace

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
    seedsAreValid();
    noWorldIsTheFourWayCross();
    randomGenomesAreValid();
    corridorIsOpenAtTheCamera();
    driverIsReplayableAndSeeded();
    aCalmSongCanKeepItsWorld();
    dropRushesAndVerseGlides();
    travelIsSeamless();
    if (argc >= 2) glslNamesMatch(argv[1]);
    if (failures == 0) std::printf("world_genome_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
