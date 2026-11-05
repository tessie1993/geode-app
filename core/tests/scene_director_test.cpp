// Host test of the Scene director (#519): it decides on moments only (sections, drops, build-up crests,
// track boundaries), the same seed and stream give the same choices at any frame rate, 100 seeds spread
// with no choice above 40 %, a calm song can keep its Scene, Calm never flips, the hand mask keeps it off.
// Synthetic frames are built the way director_test.cpp builds them: a scripted song, frame by frame.
// Built by CI (.github/workflows/android.yml, "Native host tests") with core/scene/SceneDirector.cpp and
// core/signal/params/*.cpp.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "scene/SceneDirector.hpp"
#include "viz/Reactions.hpp"

using geode::scene::Choice;
using geode::scene::ChoiceKind;
using geode::scene::Context;
using geode::scene::MomentKind;
using geode::scene::SceneDirector;
using geode::scene::ShotKind;
using geode::scene::camera::Comfort;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

// One scripted event: a section start or a drop at `at` seconds, then the section state from there on.
struct Event {
    double at;
    bool drop;
    float strength;
    int state;     // GEODE_SECTION_*
    float energy;  // the feeling from here on
    float valence;
    float tension;
};

struct Song {
    float bpm;
    double length;
    std::vector<Event> events;
    // Build-ups: buildUp ramps 0.2 -> 0.95 over 10 s from `from`, holds, and falls to 0 at `to` (the drop).
    // The crest (0.75) comes 7.3 s in: far from the drop, so the moments' spacing is no frame-rate edge.
    std::vector<std::pair<double, double>> builds;
    float energy0, valence0, tension0;
};

// Event times sit off every 30, 60 and 120 fps frame boundary, so each lands in exactly one frame.
Song standardSong() {
    Song s{120.0f, 185.0, {}, {}, 0.35f, 0.2f, 0.3f};
    s.events = {
        {15.37, false, 0.7f, GEODE_SECTION_VERSE, 0.5f, 0.2f, 0.3f},
        {45.37, false, 0.6f, GEODE_SECTION_BUILD, 0.65f, 0.1f, 0.5f},
        {60.37, true, 0.85f, GEODE_SECTION_DROP, 0.85f, 0.3f, 0.4f},
        {90.37, false, 0.6f, GEODE_SECTION_BREAKDOWN, 0.3f, -0.1f, 0.3f},
        {110.37, false, 0.5f, GEODE_SECTION_VERSE, 0.5f, 0.2f, 0.3f},
        {130.37, false, 0.6f, GEODE_SECTION_BUILD, 0.65f, 0.1f, 0.5f},
        {145.37, true, 0.7f, GEODE_SECTION_DROP, 0.8f, 0.3f, 0.4f},
        {170.37, false, 0.5f, GEODE_SECTION_OUTRO, 0.25f, 0.0f, 0.2f},
    };
    s.builds = {{45.37, 60.37}, {130.37, 145.37}};
    return s;
}
constexpr int kStandardMoments = 10;  // six sections, two drops, two crests

Song calmSong() {
    Song s{76.0f, 240.0, {}, {}, 0.15f, 0.2f, 0.1f};
    s.events = {
        {30.37, false, 0.5f, GEODE_SECTION_VERSE, 0.15f, 0.2f, 0.1f},
        {90.37, false, 0.5f, GEODE_SECTION_BREAKDOWN, 0.1f, 0.1f, 0.1f},
        {150.37, false, 0.5f, GEODE_SECTION_VERSE, 0.15f, 0.2f, 0.1f},
        {210.37, false, 0.5f, GEODE_SECTION_OUTRO, 0.1f, 0.1f, 0.1f},
    };
    return s;
}

struct Frame {
    GeodeSoundFrame f;
    geode::scene::Feeling feeling;
};

Frame frameAt(const Song& s, double t, double dt) {
    Frame out{};
    GeodeSoundFrame& f = out.f;
    f.bpm = s.bpm;
    f.beatConf = 0.8f;
    f.loudness = 0.7f;
    f.energyTrend = 0.5f;
    f.sectionState = GEODE_SECTION_INTRO;
    out.feeling = {s.valence0, s.energy0, s.tension0};
    for (const Event& e : s.events) {
        if (e.at < t + dt) {
            f.sectionState = static_cast<float>(e.state);
            out.feeling = {e.valence, e.energy, e.tension};
        }
        if (e.at >= t && e.at < t + dt) {
            if (e.drop) f.drop = e.strength;
            else f.sectionChange = e.strength;
        }
    }
    for (const auto& b : s.builds) {
        if (t >= b.first && t < b.second) {
            f.buildUp = static_cast<float>(std::min(0.95, 0.2 + 0.075 * (t - b.first)));
        }
    }
    return out;
}

std::string label(const Choice& c) {
    switch (c.kind) {
    case ChoiceKind::Nothing: return "nothing";
    case ChoiceKind::Spawn: return "spawn";
    case ChoiceKind::EnvShift: return "env";
    case ChoiceKind::CameraShot: break;
    }
    static const char* kShots[] = {"orbit", "lockOn", "follow", "flip", "turnRound", "push", "hold"};
    return kShots[static_cast<int>(c.shot.kind)];
}

std::string describe(const Choice& c) {
    char buf[160];
    std::snprintf(buf, sizeof buf, "%s/m%d/%.4f/t%d/%.4f/s%d/%d/%d/%.4f/%.4f", label(c).c_str(),
                  static_cast<int>(c.moment), c.strength, c.shot.targetHint, c.shot.duration, c.spawn.spawner,
                  c.spawn.count, c.spawn.burst ? 1 : 0, c.env.glideSeconds, c.env.mutation);
    return buf;
}

struct Run {
    std::vector<std::vector<Choice>> moments;  // one entry per frame that returned choices
    int framesWithChoices = 0;
    int momentsHeard = 0;
};

Context baseContext() {
    Context ctx;
    ctx.spawners = 3;
    ctx.objects = 4;
    ctx.comfort = Comfort::Normal;
    return ctx;
}

Run play(const Song& s, uint64_t seed, double fps, Context ctx, bool trackStart = false) {
    SceneDirector d;
    if (trackStart) d.onTrackBoundary(seed);
    else d.reset(seed);
    Run r;
    const double dt = 1.0 / fps;
    const long frames = static_cast<long>(s.length * fps);
    for (long i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) * dt;
        const Frame fr = frameAt(s, t, dt);
        ctx.feeling = fr.feeling;
        const auto out = d.step(fr.f, static_cast<float>(dt), ctx);
        if (!out.empty()) {
            ++r.framesWithChoices;
            r.moments.emplace_back(out.begin(), out.end());
        }
    }
    r.momentsHeard = d.moments();
    return r;
}

std::string signature(const Run& r) {
    std::string out;
    for (const auto& m : r.moments) {
        for (const Choice& c : m) out += describe(c) + ";";
        out += "|";
    }
    return out;
}

void testOnlyOnMoments() {
    const Run r = play(standardSong(), 7, 60.0, baseContext());
    check(r.momentsHeard == kStandardMoments, "standard song: ten moments heard, got " + std::to_string(r.momentsHeard));
    check(r.framesWithChoices == kStandardMoments, "choices only on moment frames, got " + std::to_string(r.framesWithChoices));
    for (const auto& m : r.moments) {
        check(!m.empty() && static_cast<int>(m.size()) <= SceneDirector::kMaxChoices, "one to three choices a moment");
        std::set<int> kinds;
        for (const Choice& c : m) kinds.insert(static_cast<int>(c.kind));
        check(kinds.size() == m.size(), "at most one of each kind a moment");
        if (m.front().kind == ChoiceKind::Nothing) check(m.size() == 1, "Nothing is the moment's only choice");
    }

    // A steady song with no moments: nothing at all, however long (no timer).
    Song quiet{120.0f, 600.0, {}, {}, 0.5f, 0.0f, 0.3f};
    const Run q = play(quiet, 7, 60.0, baseContext());
    check(q.framesWithChoices == 0 && q.momentsHeard == 0, "ten quiet minutes: no choice");

    // A held trigger (a frame rate below the trigger's width) is one moment, not one a frame.
    SceneDirector d;
    d.reset(3);
    GeodeSoundFrame f{};
    f.bpm = 120.0f;
    f.sectionChange = 0.6f;
    int fired = 0;
    for (int i = 0; i < 10; ++i) fired += d.step(f, 1.0f / 60.0f, baseContext()).empty() ? 0 : 1;
    check(fired == 1, "a held section trigger is one moment");

    // A track boundary decides at the next frame, as a TrackStart, then waits for the music.
    const Run t = play(quiet, 11, 60.0, baseContext(), true);
    check(t.framesWithChoices == 1, "a track boundary is one moment");
    check(!t.moments.empty() && t.moments.front().front().moment == MomentKind::TrackStart, "it is a TrackStart");
}

void testDeterminism() {
    const Song s = standardSong();
    for (uint64_t seed : {1ull, 42ull, 0xFFFFFFFFFFull}) {
        check(signature(play(s, seed, 60.0, baseContext())) == signature(play(s, seed, 60.0, baseContext())),
              "same seed, same stream: same choices (seed " + std::to_string(seed) + ")");
    }
    std::set<std::string> distinct;
    for (uint64_t seed = 0; seed < 20; ++seed) distinct.insert(signature(play(s, seed, 60.0, baseContext())));
    check(distinct.size() >= 18, "20 seeds give at least 18 different films, got " + std::to_string(distinct.size()));
    // reset() starts the same play again.
    SceneDirector d;
    d.reset(5);
    d.reset(5);
    check(d.seed() == 5 && d.moments() == 0, "reset sets the seed and clears the count");
}

void testFrameRates() {
    const Song s = standardSong();
    for (uint64_t seed = 0; seed < 20; ++seed) {
        const std::string at60 = signature(play(s, seed, 60.0, baseContext()));
        check(signature(play(s, seed, 30.0, baseContext())) == at60, "30 fps = 60 fps, seed " + std::to_string(seed));
        check(signature(play(s, seed, 120.0, baseContext())) == at60, "120 fps = 60 fps, seed " + std::to_string(seed));
    }
}

void testSpread() {
    const Song s = standardSong();
    std::map<std::string, int> counts;
    int total = 0;
    for (uint64_t seed = 0; seed < 100; ++seed) {
        const Run r = play(s, seed * 7919 + 13, 60.0, baseContext());
        for (const auto& m : r.moments) {
            for (const Choice& c : m) {
                ++counts[label(c)];
                ++total;
            }
        }
    }
    int seen = 0;
    for (const auto& [name, n] : counts) {
        const double share = static_cast<double>(n) / total;
        std::printf("  %-10s %5.1f %%\n", name.c_str(), 100.0 * share);
        check(share <= 0.40, "no choice above 40 %: " + name);
        if (share >= 0.01) ++seen;
    }
    check(seen >= 9, "at least nine of the ten choices are made, got " + std::to_string(seen));
    // The first choice of each moment, too: no kind owns the moments.
    std::map<int, int> firsts;
    int moments = 0;
    for (uint64_t seed = 0; seed < 100; ++seed) {
        const Run r = play(s, seed * 7919 + 13, 60.0, baseContext());
        for (const auto& m : r.moments) {
            ++firsts[static_cast<int>(m.front().kind)];
            ++moments;
        }
    }
    for (const auto& [kind, n] : firsts) {
        check(static_cast<double>(n) / moments <= 0.55, "no kind leads more than 55 % of moments: " + std::to_string(kind));
    }
}

void testCalmSongKeepsItsScene() {
    const Song s = calmSong();
    int found = -1;
    int allNothing = 0;
    for (uint64_t seed = 0; seed < 1000; ++seed) {
        const Run r = play(s, seed, 60.0, baseContext());
        check(r.framesWithChoices == 4, "calm song: four moments");
        bool only = true;
        for (const auto& m : r.moments) only = only && m.size() == 1 && m.front().kind == ChoiceKind::Nothing;
        if (only) {
            ++allNothing;
            if (found < 0) found = static_cast<int>(seed);
        }
    }
    std::printf("  calm song: %d of 1000 seeds keep the Scene throughout (first: %d)\n", allNothing, found);
    check(found >= 0, "some seed keeps a calm song's Scene for the whole track");
    check(allNothing < 1000, "but not every seed");
}

void testCalmNeverFlips() {
    const Song s = standardSong();
    Context ctx = baseContext();
    ctx.comfort = Comfort::Calm;
    int shots = 0;
    for (uint64_t seed = 0; seed < 200; ++seed) {
        for (const auto& m : play(s, seed, 60.0, ctx).moments) {
            for (const Choice& c : m) {
                if (c.kind != ChoiceKind::CameraShot) continue;
                ++shots;
                check(c.shot.kind != ShotKind::Flip && c.shot.kind != ShotKind::TurnRound, "Calm: no flip, no turn round");
            }
        }
    }
    check(shots > 0, "Calm still takes shots");
    // Normal does flip somewhere, so the check above is not empty by construction.
    int flips = 0;
    for (uint64_t seed = 0; seed < 200; ++seed) {
        for (const auto& m : play(s, seed, 60.0, baseContext()).moments) {
            for (const Choice& c : m) flips += (c.kind == ChoiceKind::CameraShot && c.shot.kind == ShotKind::Flip) ? 1 : 0;
        }
    }
    check(flips > 0, "Normal flips sometimes");
}

void testHandMask() {
    namespace hand = geode::viz::hand;
    using geode::viz::Moment;
    const Song s = standardSong();
    Context ctx = baseContext();
    ctx.handMask = hand::bit(Moment::Drop);
    for (uint64_t seed = 0; seed < 20; ++seed) {
        const Run free = play(s, seed, 60.0, baseContext());
        const Run owned = play(s, seed, 60.0, ctx);
        check(owned.momentsHeard == kStandardMoments, "an owned moment is still heard");
        check(owned.framesWithChoices == kStandardMoments - 2, "the two drops get no automatic choice");
        for (const auto& m : owned.moments) check(m.front().moment != MomentKind::Drop, "no choice on a hand-owned drop");
        // The moments before the first drop are untouched.
        for (std::size_t i = 0; i < 3 && i < free.moments.size() && i < owned.moments.size(); ++i) {
            check(describe(free.moments[i].front()) == describe(owned.moments[i].front()), "moments before the drop keep their choice");
        }
    }
    ctx.handMask = hand::bit(Moment::Drop) | hand::bit(Moment::NewSection) | hand::bit(Moment::BuildUp);
    check(play(s, 9, 60.0, ctx).framesWithChoices == 0, "every moment the hand's: nothing automatic");
    Context off = baseContext();
    off.enabled = false;
    check(play(s, 9, 60.0, off).framesWithChoices == 0, "the director off: nothing");
    // A track boundary belongs to no moment of the hand's.
    Song quiet{120.0f, 10.0, {}, {}, 0.5f, 0.0f, 0.3f};
    check(play(quiet, 9, 60.0, ctx, true).framesWithChoices == 1, "a new track is never the hand's");
}

void testPayloads() {
    const Song s = standardSong();
    Context noObjects = baseContext();
    noObjects.objects = 0;
    noObjects.spawners = 0;
    for (uint64_t seed = 0; seed < 100; ++seed) {
        for (const auto& m : play(s, seed, 60.0, noObjects).moments) {
            for (const Choice& c : m) {
                check(c.kind != ChoiceKind::Spawn, "no spawners: no Spawn");
                if (c.kind == ChoiceKind::CameraShot) {
                    check(c.shot.kind != ShotKind::LockOn && c.shot.kind != ShotKind::Follow, "no objects: no subject shots");
                    check(c.shot.targetHint == -1, "no objects: no target");
                }
            }
        }
        for (const auto& m : play(s, seed, 60.0, baseContext()).moments) {
            for (const Choice& c : m) {
                if (c.kind == ChoiceKind::CameraShot) {
                    check(c.shot.targetHint >= -1 && c.shot.targetHint < 4, "target inside the objects");
                    check(c.shot.kind == ShotKind::Hold ? c.shot.duration == 0.0f : c.shot.duration >= 2.0f - 1e-4f,
                          "a shot lasts whole bars (2 s at 120 bpm)");
                }
                if (c.kind == ChoiceKind::Spawn) {
                    check(c.spawn.spawner >= 0 && c.spawn.spawner < 3, "spawner inside the family's");
                    check(c.spawn.count >= 1 && c.spawn.count <= 48, "spawn count bounded");
                    if (c.moment == MomentKind::Drop) check(c.spawn.burst, "a drop spawns a burst");
                }
                if (c.kind == ChoiceKind::EnvShift) {
                    check(c.env.mutation >= 0.05f && c.env.mutation <= 1.0f, "mutation in range");
                    check(c.env.glideSeconds > 0.0f, "a glide takes time");
                }
            }
        }
    }
}

void testSchema() {
    const auto schema = geode::scene::directorSchema();
    check(schema.size() == geode::scene::kDirectorParamCount, "one row per param");
    check(schema.idsUnique(), "unique ids");
    // The kinds' weights are params: zero three of them and only the fourth is ever chosen.
    SceneDirector d;
    using geode::signal::Origin;
    using geode::signal::ParamInput;
    d.params().input(geode::scene::kNothingWeight, ParamInput::set(0.0f, Origin::User));
    d.params().input(geode::scene::kShotWeight, ParamInput::set(0.0f, Origin::User));
    d.params().input(geode::scene::kSpawnWeight, ParamInput::set(0.0f, Origin::User));
    d.reset(4);
    const Song s = standardSong();
    const double dt = 1.0 / 60.0;
    Context ctx = baseContext();
    int envs = 0;
    for (long i = 0; i < static_cast<long>(s.length * 60.0); ++i) {
        const Frame fr = frameAt(s, i * dt, dt);
        ctx.feeling = fr.feeling;
        for (const Choice& c : d.step(fr.f, static_cast<float>(dt), ctx)) {
            check(c.kind == ChoiceKind::EnvShift, "only the weighted kind is chosen");
            ++envs;
        }
    }
    check(envs == kStandardMoments, "every moment shifts the environment");
}

}  // namespace

int main() {
    testOnlyOnMoments();
    testDeterminism();
    testFrameRates();
    testSpread();
    testCalmSongKeepsItsScene();
    testCalmNeverFlips();
    testHandMask();
    testPayloads();
    testSchema();
    if (failures == 0) std::printf("scene_director_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
