// Host-side checks for the pure render-thread math: the reaction matrix and
// the camera rig. Built and run by CI (see .github/workflows/android.yml,
// "Native host tests") with the system compiler; no GL, no device.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include <set>

#include "viz/CameraRig.hpp"
#include "viz/ReactionDna.hpp"
#include "viz/Reactions.hpp"

using namespace geode::viz;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

GeodeFeatureFrame silence() { return GeodeFeatureFrame{}; }

void foldIsBoundedAndContinuous() {
    float prev = ReactionEngine::fold(0.0f);
    check(std::fabs(prev) < 1e-6f, "fold(0) == 0");
    for (int i = 1; i < 4000; ++i) {
        const float x = static_cast<float>(i) * 0.01f;
        const float y = ReactionEngine::fold(x);
        check(y >= -1.0001f && y <= 1.0001f, "fold stays in -1..1");
        check(std::fabs(y - prev) <= 0.0101f, "fold has no jumps");
        prev = y;
    }
}

void followRouteTracksSource() {
    ReactionEngine e;
    e.routes[0] = {true, ReactSource::Kick, LfoTarget::Zoom, ReactMode::Follow, 0.5f, ModCurve::Linear, 0.0f, 0.0f};
    GeodeFeatureFrame f = silence();
    f.kick = 0.8f;
    e.tick(f, 1.0f / 60.0f);
    // The first kick is its own running peak, so the normalised source is 1.
    check(std::fabs(e.output(0) - 0.5f) < 1e-4f, "follow with zero smoothing outputs amount * source");
    const SceneParams p = e.apply(SceneParams{});
    check(std::fabs(p.zoom - 1.5f) < 1e-4f, "follow route lands on its target param");
}

void stepRouteJumpsOncePerEdge() {
    ReactionEngine e;
    e.routes[0] = {true, ReactSource::Section, LfoTarget::CamYaw, ReactMode::Step, 0.25f, ModCurve::Linear, 0.0f, 0.0f};
    GeodeFeatureFrame f = silence();
    f.sectionBoundary = 1.0f;
    e.tick(f, 1.0f / 60.0f);
    check(std::fabs(e.output(0) - 0.25f) < 1e-4f, "step adds amount on the rising edge");
    // The envelope is still high: holding must not add again.
    f.sectionBoundary = 0.0f;
    for (int i = 0; i < 10; ++i) e.tick(f, 1.0f / 60.0f);
    check(std::fabs(e.output(0) - 0.25f) < 1e-4f, "step does not re-fire while the envelope stays high");
    // Let the 1.5 s envelope fall below the re-arm level, then fire again.
    for (int i = 0; i < 240; ++i) e.tick(f, 1.0f / 60.0f);
    f.sectionBoundary = 1.0f;
    e.tick(f, 1.0f / 60.0f);
    check(std::fabs(e.output(0) - 0.5f) < 1e-4f, "step fires again after re-arming");
}

void luminanceRoutesAreSlewLimited() {
    ReactionEngine e;
    e.routes[0] = {true, ReactSource::Kick, LfoTarget::Brightness, ReactMode::Follow, 1.0f, ModCurve::Linear, 0.0f, 0.0f};
    GeodeFeatureFrame f = silence();
    f.kick = 1.0f;
    e.tick(f, 1.0f / 60.0f);
    check(e.output(0) < 0.25f, "a brightness route cannot reach full depth in one frame");
}

void eventsDecayAndSilenceIsQuiet() {
    ReactSignals s;
    GeodeFeatureFrame f = silence();
    f.drop = 1.0f;
    s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::Drop) > 0.99f, "drop rises instantly");
    f.drop = 0.0f;
    for (int i = 0; i < 600; ++i) s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::Drop) < 0.05f, "drop decays over seconds");
    check(s.value(ReactSource::Kick) == 0.0f && s.value(ReactSource::Onset) == 0.0f, "silence leaves hits at zero");
}

void macroEnergyIsSlowAndArrivalIsAnEvent() {
    static_assert(kReactSourceCount == 34, "dev.geode.render.ReactSource has 34 entries");
    ReactSignals s;
    GeodeFeatureFrame f = silence();
    f.macroEnergy = 1.0f;
    s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::MacroEnergy) < 0.05f, "one loud hop barely moves song energy");
    for (int i = 0; i < 600; ++i) s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::MacroEnergy) > 0.95f, "song energy settles on the analyser's value");
    f.macroEnergy = 0.0f;
    f.arrival = 1.0f;
    s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::Arrival) > 0.99f, "arrival rises instantly");
    f.arrival = 0.0f;
    for (int i = 0; i < 900; ++i) s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::Arrival) < 0.05f, "arrival decays over seconds");
}

void deterministicForExport() {
    ReactionEngine a;
    ReactionEngine b;
    a.routes[0] = b.routes[0] = {true, ReactSource::Bass, LfoTarget::Warp, ReactMode::Spin, 0.7f, ModCurve::Smooth, 0.05f, 0.2f};
    for (int i = 0; i < 500; ++i) {
        GeodeFeatureFrame f = silence();
        for (int k = 0; k < GEODE_BAND_COUNT; ++k) f.bands[k] = 0.5f + 0.5f * std::sin(0.1f * static_cast<float>(i + k));
        a.tick(f, 1.0f / 30.0f);
        b.tick(f, 1.0f / 30.0f);
    }
    check(a.output(0) == b.output(0), "same frames give the same output");
}

void cameraTravelsAndStaysOrthonormal() {
    CameraRig rig;
    SceneParams p;
    p.camFly = 1.0f;
    for (int i = 0; i < 600; ++i) rig.step(p, {}, 1.0f / 60.0f, false);
    const CameraPose& c = rig.pose();
    check(c.travel > 10.0f, "glide path advances");
    auto dot = [](const std::array<float, 3>& a, const std::array<float, 3>& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
    check(std::fabs(dot(c.fwd, c.fwd) - 1.0f) < 1e-3f, "forward is unit");
    check(std::fabs(dot(c.fwd, c.right)) < 1e-3f && std::fabs(dot(c.fwd, c.up)) < 1e-3f, "basis is orthogonal");
    check(c.fwd[2] > 0.8f, "glide looks down the travel axis");
}

void cameraSpringsInsteadOfSnapping() {
    CameraRig rig;
    SceneParams p;
    rig.step(p, {}, 1.0f / 60.0f, false);
    p.camPush = 1.0f;
    rig.step(p, {}, 1.0f / 60.0f, false);
    check(rig.pose().dolly > 0.0f && rig.pose().dolly < 0.3f, "dolly eases toward a pushed target");
    for (int i = 0; i < 300; ++i) rig.step(p, {}, 1.0f / 60.0f, false);
    check(std::fabs(rig.pose().dolly - 1.0f) < 0.01f, "dolly settles on the target");
}

void cameraPathRepeatsAcrossTheWrap() {
    for (int path = 0; path < 4; ++path) {
        const auto a = CameraRig::pathPoint(path, 0.5f, 3.0f);
        const auto b = CameraRig::pathPoint(path, 0.5f, 3.0f + CameraRig::kPathPeriod);
        check(std::fabs(a[0] - b[0]) < 1e-2f && std::fabs(a[1] - b[1]) < 1e-2f, "path x/y repeat every kPathPeriod");
    }
}

void reducedMotionSlowsTravel() {
    CameraRig fast;
    CameraRig slow;
    SceneParams p;
    p.camFly = 1.0f;
    for (int i = 0; i < 300; ++i) {
        fast.step(p, {}, 1.0f / 60.0f, false);
        slow.step(p, {}, 1.0f / 60.0f, true);
    }
    check(slow.pose().travel < fast.pose().travel * 0.6f, "reduced motion slows the fly-through");
}

bool finite3(const std::array<float, 3>& v) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

GeodeFeatureFrame busyFrame(int i) {
    GeodeFeatureFrame f = silence();
    for (int k = 0; k < GEODE_BAND_COUNT; ++k) f.bands[k] = 0.5f + 0.5f * std::sin(0.3f * static_cast<float>(i + k));
    f.kick = (i % 7 == 0) ? 1.0f : 0.1f;
    f.transient = (i % 5 == 0) ? 0.8f : 0.0f;
    f.drop = (i % 50 == 0) ? 1.0f : 0.0f;
    f.sectionBoundary = (i % 40 == 0) ? 1.0f : 0.0f;
    return f;
}

void oddFrameTimesStayFiniteAndBounded() {
    const float odd[] = {0.0f, -1.0f, -1e9f, 1e6f, NAN, INFINITY, -INFINITY};
    for (float dt : odd) {
        ReactionEngine e;
        e.routes[0] = {true, ReactSource::Bass, LfoTarget::Rotation, ReactMode::Spin, 1.0f, ModCurve::Linear, 0.05f, 0.3f};
        e.routes[1] = {true, ReactSource::Kick, LfoTarget::CamPush, ReactMode::Follow, 0.5f, ModCurve::Smooth, 0.0f, 0.2f};
        e.routes[2] = {true, ReactSource::Section, LfoTarget::CamYaw, ReactMode::Step, 0.6f, ModCurve::Linear, 0.5f, 0.5f};
        CameraRig rig;
        SceneParams p;
        p.camFly = 2.0f;
        p.camShake = 1.0f;
        for (int i = 0; i < 120; ++i) {
            e.tick(busyFrame(i), dt);
            const SceneParams q = e.apply(p);
            rig.step(q, {0.5f, -0.5f}, dt, false);
            for (int r = 0; r < 3; ++r) check(std::isfinite(e.output(r)) && std::fabs(e.output(r)) <= 1.0001f, "route output finite and bounded for odd dt");
        }
        const CameraPose& c = rig.pose();
        check(finite3(c.pos) && finite3(c.fwd) && finite3(c.up) && finite3(c.right), "camera pose finite for odd dt");
        check(std::isfinite(c.focal) && c.focal > 0.0f, "camera focal finite and positive for odd dt");
        if (!(dt > 0.0f) || !std::isfinite(dt)) check(c.travel == 0.0f, "no time passing means no travel");
    }
}

void zeroDtFreezesTheCameraAndHugeDtIsOneClampedStep() {
    SceneParams p;
    p.camFly = 1.0f;
    CameraRig a;
    CameraRig b;
    for (int i = 0; i < 30; ++i) {
        a.step(p, {}, 1.0f / 60.0f, false);
        b.step(p, {}, 1.0f / 60.0f, false);
    }
    const float before = a.pose().travel;
    a.step(p, {}, 0.0f, false);
    check(a.pose().travel == before, "dt = 0 does not move the camera");
    a.step(p, {}, 1e6f, false);
    b.step(p, {}, 0.0f, false);
    b.step(p, {}, 0.1f, false);
    check(std::fabs(a.pose().travel - b.pose().travel) < 1e-5f, "a huge dt is treated as the 0.1 s cap");
}

void stillStylesKeepTheBankOutOfTheFlatRoll() {
    // A style that does not turn (SceneRegistry::spinRate 0) must not roll as
    // the glide path curves; the turning ones still bank.
    SceneParams p;
    p.camFly = 1.0f;
    CameraRig still;
    CameraRig turning;
    float maxStill = 0.0f;
    float maxTurning = 0.0f;
    for (int i = 0; i < 1200; ++i) {
        still.step(p, {}, 1.0f / 60.0f, false, false);
        turning.step(p, {}, 1.0f / 60.0f, false, true);
        maxStill = std::fmax(maxStill, std::fabs(still.pose().flat[3]));
        maxTurning = std::fmax(maxTurning, std::fabs(turning.pose().flat[3]));
    }
    check(maxStill < 1e-6f, "a still style's flat picture never rolls on its own");
    check(maxTurning > 0.01f, "a turning style still banks into the curve");
}

// One beat (and a downbeat on count one) per 0.5 s at 60 fps, for `beats` beats.
void playBeats(ReactionEngine& e, int beats, int& count) {
    for (int b = 0; b < beats; ++b) {
        for (int i = 0; i < 30; ++i) {
            GeodeFeatureFrame f = silence();
            if (i == 0) {
                f.beat = 1.0f;
                f.beatStrength = 1.0f;
                f.pulseConfidence = 1.0f;
                if (count % 4 == 0) f.downbeat = 1.0f;
            }
            e.tick(f, 1.0f / 60.0f);
        }
        ++count;
    }
}

void jumpHoldsANewSeededValuePerHit() {
    static_assert(kReactModeCount == 4, "dev.geode.render.ReactMode has 4 entries");
    ReactionEngine a;
    ReactionEngine b;
    a.reset();
    b.reset();
    a.routes[0] = b.routes[0] = {true, ReactSource::Beat, LfoTarget::Twist, ReactMode::Jump, 1.0f, ModCurve::Linear, 0.0f, 0.0f};
    int ca = 0;
    int cb = 0;
    float last = 0.0f;
    int moved = 0;
    for (int hit = 0; hit < 12; ++hit) {
        playBeats(a, 1, ca);
        playBeats(b, 1, cb);
        const float v = a.output(0);
        check(v >= -1.0f && v <= 1.0f, "jump stays within the amount");
        if (std::fabs(v - last) >= 0.49f) ++moved;
        last = v;
        check(a.output(0) == b.output(0), "jump draws the same values for the same frames");
    }
    check(moved == 12, "every hit lands at least half a unit from the last");
}

void phraseBeatsAlternateEveryEightBars() {
    ReactionEngine e;
    e.reset();
    e.routes[0] = {true, ReactSource::PhraseBeatA, LfoTarget::Zoom, ReactMode::Step, 0.1f, ModCurve::Linear, 0.0f, 0.0f};
    e.routes[1] = {true, ReactSource::PhraseBeatB, LfoTarget::Twist, ReactMode::Step, 0.1f, ModCurve::Linear, 0.0f, 0.0f};
    int count = 0;
    playBeats(e, 32, count);  // phrase 0: eight bars of four
    check(std::fabs(e.output(0) - ReactionEngine::fold(3.2f)) < 1e-3f, "phrase A hears every beat of its phrase");
    check(e.output(1) == 0.0f, "phrase B is silent during phrase A");
    check(e.signals().phraseCount() == 0, "eight bars are still one phrase");
    const float a = e.output(0);
    playBeats(e, 32, count);
    check(e.signals().phraseCount() == 1, "the ninth bar starts the next phrase");
    check(e.output(0) == a, "phrase A is silent during phrase B");
    check(e.output(1) != 0.0f, "phrase B hears its own phrase");
}

void sectionStartsAPhraseAndOnsetsStandInForBeats() {
    ReactSignals s;
    GeodeFeatureFrame f = silence();
    f.beat = 1.0f;
    f.downbeat = 1.0f;
    s.step(f, 1.0f / 60.0f);
    f = silence();
    for (int i = 0; i < 30; ++i) s.step(f, 1.0f / 60.0f);
    f.sectionBoundary = 1.0f;
    s.step(f, 1.0f / 60.0f);
    check(s.phraseCount() == 1, "a section change starts a phrase");
    check(s.value(ReactSource::Phrase) > 0.99f, "the phrase event fires");
    // Three beatless seconds, then an onset: it stands in for the beat.
    f = silence();
    for (int i = 0; i < 180; ++i) s.step(f, 1.0f / 60.0f);
    f.transient = 1.0f;
    s.step(f, 1.0f / 60.0f);
    check(s.value(ReactSource::PhraseBeatB) > 0.5f, "an onset drives the phrase beat when no beat comes");
    check(s.phraseCount() == 1, "a lone onset does not start a phrase");
}

void backbeatIsCountsTwoAndFour() {
    ReactionEngine e;
    e.reset();
    e.routes[0] = {true, ReactSource::Backbeat, LfoTarget::Warp, ReactMode::Step, 0.1f, ModCurve::Linear, 0.0f, 0.0f};
    int count = 0;
    playBeats(e, 8, count);
    check(std::fabs(e.output(0) - ReactionEngine::fold(0.4f)) < 1e-3f, "two backbeats a bar");
}

void phrasesTurnOverWithoutBars() {
    ReactSignals s;
    GeodeFeatureFrame f = silence();
    for (int i = 0; i < 60 * 21; ++i) s.step(f, 1.0f / 60.0f);
    check(s.phraseCount() == 1, "a phrase turns over on time with no bars at all");
}

// A spread of real style ids with made-up traits: every fourth one turns, two
// in three are shader styles.
constexpr const char* kStyleIds[] = {
    "kaleido", "plasma", "julia", "tunnel", "aurora", "voronoi", "arbor_grove", "arbor_bolt", "bulb_julia", "bulb_ink",
    "chrys_bloom", "chrys_fall", "lucid_mandala", "milkdrop", "fluid", "water", "silk", "bars", "spiral", "kifs",
    "chrome_nebula", "crystal_canyon", "gyroid_cavern", "jelly_abyss", "fractal_dive", "aurora_ocean", "bead_vortex",
    "curl_bloom", "acid_show", "blacklight_bloom",
};
constexpr int kStyleCount = static_cast<int>(sizeof(kStyleIds) / sizeof(kStyleIds[0]));

StyleTraits traitsAt(int i) { return {i % 4 == 0 ? (i % 8 == 0 ? 1.0f : -0.7f) : 0.0f, i % 3 != 2}; }

bool sameRoutes(const StyleDna& a, const StyleDna& b) {
    if (a.count != b.count) return false;
    for (int i = 0; i < a.count; ++i) {
        const ReactRoute& x = a.routes[static_cast<size_t>(i)];
        const ReactRoute& y = b.routes[static_cast<size_t>(i)];
        if (x.source != y.source || x.target != y.target || x.mode != y.mode || x.amount != y.amount || x.curve != y.curve ||
            x.attackSeconds != y.attackSeconds || x.releaseSeconds != y.releaseSeconds) {
            return false;
        }
    }
    return true;
}

void dnaIsDeterministicAndSeeded() {
    for (int i = 0; i < kStyleCount; ++i) {
        check(sameRoutes(buildStyleDna(kStyleIds[i], 3, traitsAt(i)), buildStyleDna(kStyleIds[i], 3, traitsAt(i))),
              "the same id and seed give the same DNA (live view and export agree)");
    }
    check(!sameRoutes(buildStyleDna("kifs", 0, traitsAt(0)), buildStyleDna("kifs", 1, traitsAt(0))), "re-rolling the seed changes the DNA");
}

void stylesGetDifferentPersonalities() {
    std::set<int> leads;
    int pulses = 0;
    int beatZooms = 0;
    for (int i = 0; i < kStyleCount; ++i) {
        const StyleDna dna = buildStyleDna(kStyleIds[i], 0, traitsAt(i));
        leads.insert(static_cast<int>(dna.lead));
        if (dna.lead == Personality::Pulse) ++pulses;
        for (int r = 0; r < dna.count; ++r) {
            const ReactRoute& route = dna.routes[static_cast<size_t>(r)];
            const bool beat = route.source == ReactSource::PhraseBeatA || route.source == ReactSource::PhraseBeatB ||
                              route.source == ReactSource::Beat || route.source == ReactSource::Kick;
            if (beat && (route.target == LfoTarget::CamPush || route.target == LfoTarget::Zoom)) {
                ++beatZooms;
                break;
            }
        }
    }
    check(leads.size() >= 6, "thirty styles draw at least six different beat personalities");
    check(pulses * 4 <= kStyleCount, "at most a quarter of styles push in on the beat");
    check(beatZooms * 3 <= kStyleCount, "most styles do not zoom on the beat at all");
}

void phrasesAnswerTheBeatDifferently() {
    for (int i = 0; i < kStyleCount; ++i) {
        const StyleDna dna = buildStyleDna(kStyleIds[i], 0, traitsAt(i));
        std::set<int> a;
        std::set<int> b;
        for (int r = 0; r < dna.count; ++r) {
            const ReactRoute& route = dna.routes[static_cast<size_t>(r)];
            if (route.source == ReactSource::PhraseBeatA) a.insert(static_cast<int>(route.target));
            if (route.source == ReactSource::PhraseBeatB) b.insert(static_cast<int>(route.target));
        }
        check(!a.empty() && !b.empty(), "both phrases answer the beat");
        check(dna.lead != dna.second && a != b, "the two phrases answer it differently");
    }
}

void dnaRespectsStyleTraits() {
    for (int i = 0; i < kStyleCount; ++i) {
        for (int seed = 0; seed < 6; ++seed) {
            const StyleTraits t = traitsAt(i);
            const StyleDna dna = buildStyleDna(kStyleIds[i], seed, t);
            for (int r = 0; r < dna.count; ++r) {
                const LfoTarget target = dna.routes[static_cast<size_t>(r)].target;
                if (t.spinRate == 0.0f) check(target != LfoTarget::Rotation && target != LfoTarget::CamRoll, "still styles never spin or roll");
                if (!t.shaderLook) check(target != LfoTarget::Morph && target != LfoTarget::PaletteMix, "shader-only targets stay on shader styles");
            }
        }
    }
}

void dnaLeavesRoomAndStaysInRange() {
    for (int i = 0; i < kStyleCount; ++i) {
        for (int seed = 0; seed < 6; ++seed) {
            const StyleDna dna = buildStyleDna(kStyleIds[i], seed, traitsAt(i));
            check(dna.count >= 5 && dna.count <= StyleDna::kMaxRoutes, "the DNA fills most slots and leaves three free");
            for (int r = 0; r < dna.count; ++r) {
                const ReactRoute& route = dna.routes[static_cast<size_t>(r)];
                check(route.enabled && route.target != LfoTarget::None, "every route moves something");
                check(route.amount >= -2.0f && route.amount <= 2.0f, "amount within what native accepts");
                check(route.attackSeconds >= 0.0f && route.attackSeconds <= 5.0f, "attack within range");
                check(route.releaseSeconds >= 0.0f && route.releaseSeconds <= 10.0f, "release within range");
            }
        }
    }
}

}  // namespace

int main() {
    foldIsBoundedAndContinuous();
    followRouteTracksSource();
    stepRouteJumpsOncePerEdge();
    luminanceRoutesAreSlewLimited();
    eventsDecayAndSilenceIsQuiet();
    macroEnergyIsSlowAndArrivalIsAnEvent();
    deterministicForExport();
    jumpHoldsANewSeededValuePerHit();
    phraseBeatsAlternateEveryEightBars();
    sectionStartsAPhraseAndOnsetsStandInForBeats();
    backbeatIsCountsTwoAndFour();
    phrasesTurnOverWithoutBars();
    dnaIsDeterministicAndSeeded();
    stylesGetDifferentPersonalities();
    phrasesAnswerTheBeatDifferently();
    dnaRespectsStyleTraits();
    dnaLeavesRoomAndStaysInRange();
    cameraTravelsAndStaysOrthonormal();
    cameraSpringsInsteadOfSnapping();
    cameraPathRepeatsAcrossTheWrap();
    reducedMotionSlowsTravel();
    oddFrameTimesStayFiniteAndBounded();
    zeroDtFreezesTheCameraAndHugeDtIsOneClampedStep();
    stillStylesKeepTheBankOutOfTheFlatRoll();
    if (failures == 0) std::printf("viz_math_test: all passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
