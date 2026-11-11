// Host test of the director's reach (viz/DirectorReach): the shuffle and the modulators it adds over the
// user's params when Auto's reach asks. Behaviour, by the rules the owner set: the same play gives the
// same picture; kept controls and categories, the style's scope and the schema ranges hold; nothing jumps,
// at 30, 60 and 120 fps alike; a modulator is an offset (the base moved by d moves the result by d); Look
// is today's picture bit for bit; an export keeps the reach it was pinned with; the hand mask's moments and
// sources are the user's; safety::apply stays last; and no tempo field changes anything.
// Built by CI (.github/workflows/native-host-tests.yml) with the resolver's sources.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "signal/Resolver.hpp"
#include "viz/CustomizeMap.hpp"
#include "viz/DirectorReach.hpp"
#include "viz/ModTargets.hpp"
#include "viz/ParamRandomizer.hpp"
#include "viz/VisualSafety.hpp"

using geode::signal::EngineSettings;
using geode::signal::Resolver;
using geode::signal::SettingsLatch;
using geode::signal::StyleContext;
using geode::viz::Director;
using geode::viz::DirectorReach;
using geode::viz::LfoTarget;
using geode::viz::Moment;
using geode::viz::ParamFade;
using geode::viz::ParamKind;
using geode::viz::ParamSlot;
using geode::viz::SceneParams;
using Reach = geode::viz::director::Reach;
using Reason = geode::viz::Director::Reason;
namespace customize = geode::viz::customize;
namespace director = geode::viz::director;
namespace hand = geode::viz::hand;
namespace modtargets = geode::viz::modtargets;
namespace randomizer = geode::viz::randomizer;
namespace safety = geode::viz::safety;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

constexpr int kFields = SceneParams::kFieldCount;
using Wire = std::array<float, kFields>;
const std::string kSea = "neon_chrome_sea";

Wire wireOf(const SceneParams& p) {
    Wire w;
    p.toWire(w.data());
    return w;
}

bool sameBits(const SceneParams& a, const SceneParams& b) {
    const Wire x = wireOf(a), y = wireOf(b);
    return std::memcmp(x.data(), y.data(), sizeof(float) * kFields) == 0;
}

bool glides(int slot) {
    const auto& spec = SceneParams::schema()[static_cast<size_t>(slot)];
    return spec.fade == ParamFade::Fade && (spec.kind == ParamKind::Float || spec.kind == ParamKind::Circular);
}

GeodeSoundFrame music(float t) {
    GeodeSoundFrame f{};
    f.loudness = 0.8f;
    f.level = 0.6f;
    f.presence = 1.0f;
    f.intensity = 0.5f + 0.3f * std::sin(t * 0.7f);
    f.bassPresence = 0.5f + 0.4f * std::sin(t * 0.31f);
    f.midPresence = 0.5f + 0.4f * std::sin(t * 0.23f + 1.0f);
    f.treblePresence = 0.5f + 0.4f * std::sin(t * 0.41f + 2.0f);
    f.centroid = 0.5f + 0.2f * std::sin(t * 0.17f);
    f.tension = 0.3f + 0.2f * std::sin(t * 0.13f);
    f.densitySlow = 0.5f;
    return f;
}

// A DirectorReach driven by hand: the director's side of its inputs is given, not heard.
struct Rig {
    DirectorReach reach;
    SceneParams base;
    DirectorReach::Inputs in;
    std::shared_ptr<const director::Kept> none = std::make_shared<const director::Kept>();
    float t = 0.0f;
    float dt = 1.0f / 60.0f;
    bool tempo = false;

    explicit Rig(Reach level, std::uint64_t seed = 42, const std::string* style = &kSea) {
        in.reach = level;
        in.seed = seed;
        in.styleId = style;
        in.weight = 1.0f;
        in.kept = none;
    }
    GeodeSoundFrame frame() const {
        GeodeSoundFrame f = music(t);
        if (tempo) {
            f.bpm = 174.0f;
            f.beatConf = 0.9f;
            f.beatPhase = std::fmod(t * 2.9f, 1.0f);
            f.barPhase = std::fmod(t * 0.725f, 1.0f);
            f.phrasePhase = std::fmod(t * 0.09f, 1.0f);
            f.beat = f.beatPhase < 0.05f ? 1.0f : 0.0f;
            f.downbeat = f.barPhase < 0.02f ? 1.0f : 0.0f;
        }
        return f;
    }
    void step() {
        reach.step(base, frame(), dt, in);
        in.committed = false;
        t += dt;
    }
    void run(float seconds) {
        const int n = static_cast<int>(std::lround(seconds / dt));
        for (int i = 0; i < n; ++i) step();
    }
    void moment(Reason reason, float energy = 0.5f) {
        in.committed = true;
        in.reason = reason;
        in.commit += 1;
        in.energy = energy;
        step();
    }
    SceneParams out() const { return reach.applyModulation(reach.applyShuffle(base)); }
    int moved() const {
        int n = 0;
        for (int s = 0; s < kFields; ++s) {
            if (reach.offset(static_cast<ParamSlot>(s)) != 0.0f || reach.holds(static_cast<ParamSlot>(s))) ++n;
        }
        return n;
    }
};

std::shared_ptr<const director::Kept> keptOf(const char* lines) {
    director::setKept(lines);
    auto k = director::kept();
    director::setKept("");
    return k;
}

void testDeterminism() {
    Rig a(Reach::Modulate), b(Reach::Modulate), c(Reach::Modulate, 43);
    for (Rig* r : {&a, &b, &c}) {
        r->run(1.0f);
        r->moment(Reason::First);
        r->run(3.0f);
        r->moment(Reason::Section, 0.7f);
        r->run(6.0f);
        r->moment(Reason::Drop, 0.9f);
        r->run(5.0f);
    }
    check(a.reach.shuffles() == 2, "a section and a drop are the two shuffles; the first look is none");
    check(a.reach.deals() == 3, "every look the director commits deals modulators");
    check(a.moved() > 0, "a shuffle moves something");
    check(sameBits(a.out(), b.out()), "the same seed and moments give the same bits");
    check(!sameBits(a.out(), c.out()), "another play seed gives another shuffle");
    check(!sameBits(a.out(), a.base), "the reach changes the picture");
}

void testStrength() {
    Rig quiet(Reach::Shuffle), loud(Reach::Shuffle), drop(Reach::Shuffle);
    quiet.moment(Reason::Section, 0.0f);
    loud.moment(Reason::Section, 1.0f);
    drop.moment(Reason::Drop, 0.2f);
    for (Rig* r : {&quiet, &loud, &drop}) r->run(6.0f);
    check(quiet.moved() > 0, "a quiet section still changes something");
    check(quiet.moved() < loud.moved() && loud.moved() <= drop.moved(), "the feeling's energy decides how much; a drop the most");
    float q = 0.0f, d = 0.0f;
    for (int s = 0; s < kFields; ++s) {
        const auto& spec = SceneParams::schema()[static_cast<size_t>(s)];
        const float range = spec.max - spec.min;
        if (!(range > 0.0f)) continue;
        q += std::fabs(quiet.reach.offset(static_cast<ParamSlot>(s))) / range;
        d += std::fabs(drop.reach.offset(static_cast<ParamSlot>(s))) / range;
    }
    check(d > q * 1.5f, "a drop goes further than a quiet section");
}

void testKept() {
    const auto kept = keptOf("Zoom\n#EFFECTS\n#CAMERA");
    check(kept->count("Zoom") && kept->count("Vignette") && kept->count("Swing") && !kept->count("#EFFECTS"),
          "the keep set arrives expanded");
    std::set<int> held;
    for (const auto& roll : randomizer::rolledKeys()) {
        if (!kept->count(roll.key)) continue;
        for (const char* f : roll.fields) {
            if (f) held.insert(SceneParams::slotOf(f));
        }
    }
    Rig r(Reach::Modulate);
    r.in.kept = kept;
    bool clean = true, targets = true;
    for (int i = 0; i < 40; ++i) {
        r.moment(i % 3 == 0 ? Reason::Section : Reason::Drop, 0.9f);
        r.run(2.5f);
        const Wire base = wireOf(r.base), out = wireOf(r.out());
        for (int s : held) clean = clean && out[static_cast<size_t>(s)] == base[static_cast<size_t>(s)];
        for (const auto& w : r.reach.waves()) targets = targets && !(w.enabled && held.count(geode::viz::paramSlotOf(w.target)));
        for (const auto& f : r.reach.followers()) targets = targets && !(f.enabled && held.count(geode::viz::paramSlotOf(f.target)));
    }
    check(clean, "a kept control and a kept category are never moved");
    check(targets, "no modulator is dealt onto a kept control");
    check(r.moved() > 0, "the rest still shuffles");

    // Kept while it plays: it lets go at once and comes back to the user's value.
    Rig late(Reach::Shuffle);
    for (int i = 0; i < 6 && late.reach.offset(ParamSlot::zoom) == 0.0f; ++i) {
        late.moment(Reason::Drop);
        late.run(3.0f);
    }
    check(late.reach.offset(ParamSlot::zoom) != 0.0f, "Zoom is rolled within a few drops");
    late.in.kept = keptOf("Zoom");
    late.run(DirectorReach::kLetGoSeconds + 0.1f);
    check(late.reach.offset(ParamSlot::zoom) == 0.0f, "keeping a control hands it back");
    late.moment(Reason::Drop);
    late.run(3.0f);
    check(late.reach.offset(ParamSlot::zoom) == 0.0f, "and the next drop leaves it");
}

void testScopeAndRanges() {
    Rig r(Reach::Modulate);
    std::set<int> outside;
    for (const auto& roll : randomizer::rolledKeys()) {
        if (customize::reaches(roll.key, kSea)) continue;
        for (const char* f : roll.fields) {
            if (f) outside.insert(SceneParams::slotOf(f));
        }
    }
    check(!outside.empty(), "the style leaves some controls out");
    bool scoped = true, ranged = true, reached = true;
    for (int i = 0; i < 60; ++i) {
        r.moment(i % 4 == 0 ? Reason::Long : Reason::Drop, 1.0f);
        for (int k = 0; k < 4; ++k) {
            r.run(0.7f);
            const Wire base = wireOf(r.base), out = wireOf(r.out());
            for (int s : outside) scoped = scoped && out[static_cast<size_t>(s)] == base[static_cast<size_t>(s)];
            for (int s = 0; s < kFields; ++s) {
                const auto& spec = SceneParams::schema()[static_cast<size_t>(s)];
                if (spec.kind == ParamKind::Circular || out[static_cast<size_t>(s)] == base[static_cast<size_t>(s)]) continue;
                ranged = ranged && out[static_cast<size_t>(s)] >= spec.min - 1e-6f && out[static_cast<size_t>(s)] <= spec.max + 1e-6f;
            }
        }
        for (const auto& w : r.reach.waves()) reached = reached && (!w.enabled || modtargets::reaches(w.target, kSea));
        for (const auto& f : r.reach.followers()) reached = reached && (!f.enabled || modtargets::reaches(f.target, kSea));
    }
    check(scoped, "a control the style does not reach is never moved");
    check(ranged, "every moved value stays inside its schema range");
    check(reached, "every modulator's target reaches the style");
}

// The largest step any slider takes in one frame, as a share of its range.
float worstStep(float fps, Reach level, Wire* at9s) {
    Rig r(level);
    r.dt = 1.0f / fps;
    float worst = 0.0f;
    Wire last = wireOf(r.out());
    bool flipsOnMoments = true;
    const auto frame = [&](bool momentFrame) {
        const Wire now = wireOf(r.out());
        for (int s = 0; s < kFields; ++s) {
            const auto i = static_cast<size_t>(s);
            const auto& spec = SceneParams::schema()[i];
            if (!glides(s)) {
                flipsOnMoments = flipsOnMoments && (now[i] == last[i] || momentFrame);
            } else if (spec.max > spec.min) {
                worst = std::max(worst, std::fabs(now[i] - last[i]) / (spec.max - spec.min));
            }
        }
        last = now;
    };
    const auto run = [&](float seconds) {
        const int n = static_cast<int>(std::lround(seconds * fps));
        for (int i = 0; i < n; ++i) {
            r.step();
            frame(false);
        }
    };
    run(1.0f);
    r.moment(Reason::Section, 0.8f);
    frame(true);
    run(3.0f - r.dt);
    r.moment(Reason::Drop, 1.0f);
    frame(true);
    run(5.0f - r.dt);
    if (at9s != nullptr) *at9s = wireOf(r.reach.applyShuffle(r.base));
    r.moment(Reason::Drop, 1.0f);
    frame(true);
    run(4.0f);
    check(flipsOnMoments, "a toggle or a choice changes only on the moment");
    return worst;
}

void testNoJumps() {
    Wire at30, at60, at120;
    const float fps[] = {30.0f, 60.0f, 120.0f};
    Wire* out[] = {&at30, &at60, &at120};
    float withModulators[3];
    for (int i = 0; i < 3; ++i) {
        // A full-range roll over the drop's glide, at smoothstep's steepest (1.5x the mean), with headroom
        // for a glide that starts from the end of another.
        const float allowed = 2.0f * 1.5f / (DirectorReach::kDropGlideSeconds * fps[i]);
        const float shuffle = worstStep(fps[i], Reach::Shuffle, out[i]);
        check(shuffle > 0.0f && shuffle <= allowed, "no shuffled slider steps further than its glide allows at " + std::to_string(fps[i]));
        const float both = worstStep(fps[i], Reach::Modulate, nullptr);
        check(both <= allowed, "nor with the modulators on top at " + std::to_string(fps[i]));
        withModulators[i] = both;
    }
    // A snap is the same size at every frame rate; a glide's step halves when the rate doubles.
    check(withModulators[1] < withModulators[0] * 0.6f && withModulators[2] < withModulators[1] * 0.6f,
          "the largest step shrinks with the frame time: nothing snaps");
    bool same = true;
    for (int s = 0; s < kFields; ++s) {
        const auto i = static_cast<size_t>(s);
        const auto& spec = SceneParams::schema()[i];
        const float tol = 2e-3f * std::max(1.0f, spec.max - spec.min);
        same = same && std::fabs(at30[i] - at60[i]) <= tol && std::fabs(at120[i] - at60[i]) <= tol;
    }
    check(same, "the shuffle is the same picture at 30, 60 and 120 fps");
}

void testBasePlusModulation() {
    Rig a(Reach::Modulate), b(Reach::Modulate);
    // Mid-range bases, so no clamp is in reach of a quarter-range offset.
    const ParamSlot slots[] = {ParamSlot::warp, ParamSlot::ripple, ParamSlot::morph, ParamSlot::sway,
                               ParamSlot::vignette, ParamSlot::chromaAb, ParamSlot::camOrbit, ParamSlot::flowStrength};
    for (ParamSlot s : slots) {
        a.base.setSlot(static_cast<int>(s), 0.45f);
        b.base.setSlot(static_cast<int>(s), 0.55f);
    }
    a.base.bloom = 0.1f;
    b.base.bloom = 0.12f;
    bool offset = true;
    int seen = 0;
    for (int i = 0; i < 12; ++i) {
        // Looks that are not shuffle moments: only the modulators move.
        a.moment(Reason::Modulation, 0.8f);
        b.moment(Reason::Modulation, 0.8f);
        for (int k = 0; k < 200; ++k) {
            a.step();
            b.step();
            const SceneParams x = a.out(), y = b.out();
            for (ParamSlot s : slots) {
                const float da = x.getSlot(static_cast<int>(s)) - 0.45f;
                const float db = y.getSlot(static_cast<int>(s)) - 0.55f;
                offset = offset && std::fabs(da - db) < 1e-5f;
                if (da != 0.0f) ++seen;
            }
        }
    }
    check(a.reach.shuffles() == 0, "a modulation's look is not a shuffle moment");
    check(seen > 1000, "the modulators move the sliders");
    check(offset, "a modulator is an offset: the base moved by d moves the result by d");

    bool bounded = true;
    for (const auto& w : a.reach.waves()) {
        bounded = bounded && w.depth <= DirectorReach::kMaxDepth + 1e-6f && w.beats == 0.0f &&
                  (!w.enabled || (w.rateSeconds >= DirectorReach::kMinWaveSeconds && w.rateSeconds <= DirectorReach::kMaxWaveSeconds));
    }
    for (const auto& f : a.reach.followers()) {
        bounded = bounded && std::fabs(f.amount) <= DirectorReach::kMaxDepth + 1e-6f &&
                  (!f.enabled || (f.attackSeconds >= 0.5f && f.releaseSeconds >= 2.0f));
    }
    check(bounded, "depth is bounded, waves are slow and free-running, followers are slow");
}

void testUserEdit() {
    Rig r(Reach::Modulate);
    for (int i = 0; i < 8 && (r.reach.offset(ParamSlot::warp) == 0.0f || !r.reach.holds(ParamSlot::mirror)); ++i) {
        r.moment(Reason::Drop);
        r.run(3.0f);
    }
    check(r.reach.offset(ParamSlot::warp) != 0.0f && r.reach.holds(ParamSlot::mirror), "Warp and Mirror are both rolled within a few drops");
    r.base.warp = 0.37f;
    r.base.mirror = !r.base.mirror;
    r.step();
    check(r.reach.leftAlone(ParamSlot::warp) && r.reach.leftAlone(ParamSlot::mirror), "a control the user moves is theirs");
    check(!r.reach.holds(ParamSlot::mirror) && r.out().mirror == r.base.mirror, "a toggle the user flips shows what they set, at once");
    r.run(DirectorReach::kLetGoSeconds + 1.0f);
    check(r.out().warp == 0.37f, "a slider the user moves comes back to exactly their value");
    r.moment(Reason::Modulation);
    r.run(4.0f);
    check(r.out().warp == 0.37f, "and stays theirs through looks that are not a new section or a drop");
    // In silence the renderer's step is 0: nothing glides, but a control the user moves still comes home.
    Rig quiet(Reach::Shuffle);
    for (int i = 0; i < 8 && quiet.reach.offset(ParamSlot::warp) == 0.0f; ++i) {
        quiet.moment(Reason::Drop);
        quiet.run(1.0f);
    }
    const float mid = quiet.reach.offset(ParamSlot::twist);
    quiet.base.warp = 0.21f;
    quiet.in.wallDt = quiet.dt;
    quiet.dt = 0.0f;
    for (int i = 0; i < 60; ++i) quiet.step();
    check(quiet.out().warp == 0.21f, "in silence too");
    check(quiet.reach.offset(ParamSlot::twist) == mid, "while the director's own glides wait for the music");

    bool again = false;
    for (int i = 0; i < 8 && !again; ++i) {
        r.moment(Reason::Drop);
        check(!r.reach.leftAlone(ParamSlot::warp), "the next moment gives the control back to the director");
        r.run(3.0f);
        again = r.out().warp != 0.37f;
    }
    check(again, "which moves it again");
}

void testHandMask() {
    Rig drops(Reach::Modulate), sections(Reach::Modulate), all(Reach::Modulate);
    drops.in.handMask = hand::bit(Moment::Drop);
    drops.moment(Reason::Drop);
    check(drops.reach.shuffles() == 0, "a drop set by hand is not shuffled");
    drops.moment(Reason::Section);
    check(drops.reach.shuffles() == 1, "its sections still are");
    sections.in.handMask = hand::bit(Moment::NewSection);
    sections.moment(Reason::Section);
    check(sections.reach.shuffles() == 0, "a new section set by hand is not shuffled");
    sections.moment(Reason::Drop);
    check(sections.reach.shuffles() == 1, "its drops still are");

    all.in.handMask = hand::kAll;
    all.moment(Reason::First);
    all.run(4.0f);
    bool dealt = false, silent = true, waves = false;
    for (const auto& f : all.reach.followers()) dealt = dealt || f.enabled;
    for (int i = 0; i < DirectorReach::kFollowers; ++i) silent = silent && all.reach.modulatorMix(DirectorReach::kWaves + i) == 0.0f;
    for (int i = 0; i < DirectorReach::kWaves; ++i) waves = waves || all.reach.modulatorMix(i) == 1.0f;
    check(dealt && silent, "a follower on a source the hand mask gives the user never plays");
    check(waves, "the waves, which hear no source, still do");
}

void testReducedMotionAndOff() {
    Rig full(Reach::Modulate), reduced(Reach::Modulate);
    reduced.in.reducedMotion = true;
    for (Rig* r : {&full, &reduced}) {
        r->moment(Reason::First);
        r->run(DirectorReach::kCrossSeconds + 3.0f);
    }
    bool scaled = true;
    int any = 0;
    const Wire base = wireOf(full.base), a = wireOf(full.out()), b = wireOf(reduced.out());
    for (int s = 0; s < kFields; ++s) {
        const auto i = static_cast<size_t>(s);
        if (a[i] != base[i]) ++any;
        scaled = scaled && std::fabs((b[i] - base[i]) - (a[i] - base[i]) * safety::kReducedMotionScale) < 1e-5f;
    }
    check(any >= 3 && scaled, "reduced motion scales every modulator's offset");

    // The director not driving (Auto off, or the free taste over): nothing, bit for bit.
    Rig off(Reach::Modulate);
    off.in.weight = 0.0f;
    off.moment(Reason::First);
    off.moment(Reason::Drop);
    off.run(3.0f);
    check(off.reach.shuffles() == 0 && sameBits(off.out(), off.base), "without the director driving the reach does nothing");

    // Reach turned down while it plays: everything glides home.
    Rig down(Reach::Modulate);
    down.moment(Reason::Drop, 1.0f);
    down.run(5.0f);
    check(!sameBits(down.out(), down.base), "the reach was playing");
    down.in.reach = Reach::Look;
    down.step();
    const Wire first = wireOf(down.out()), home = wireOf(down.base);
    bool noSnap = false;
    for (int s = 0; s < kFields; ++s) noSnap = noSnap || (glides(s) && first[static_cast<size_t>(s)] != home[static_cast<size_t>(s)]);
    check(noSnap, "turning the reach down does not snap the sliders");
    down.run(DirectorReach::kCrossSeconds + 1.0f);
    check(sameBits(down.out(), down.base), "and ends on the user's own look, bit for bit");

    // A new play starts from the user's look.
    Rig play(Reach::Shuffle);
    play.moment(Reason::Drop, 1.0f);
    play.run(3.0f);
    play.in.seed = 99;
    play.run(DirectorReach::kLetGoSeconds + 0.2f);
    check(sameBits(play.out(), play.base), "a new play seed lets the last play's shuffle go");
}

void testNoTempo() {
    Rig plain(Reach::Modulate), timed(Reach::Modulate);
    timed.tempo = true;
    bool same = true;
    for (int i = 0; i < 6; ++i) {
        for (Rig* r : {&plain, &timed}) {
            r->moment(i % 2 ? Reason::Drop : Reason::Section, 0.7f);
            r->run(5.0f);
        }
        same = same && sameBits(plain.out(), timed.out());
    }
    check(same, "tempo, beat and bar fields change nothing");
}

GeodeSoundFrame song(float t, float dt) {
    GeodeSoundFrame f = music(t);
    f.scaleFit = 0.8f;
    f.keyClarity = 0.7f;
    f.keyMajor = 1.0f;
    f.energyTrend = 0.5f;
    for (int i = 0; i < 12; ++i) f.chromaFast[i] = 0.2f;
    f.chromaFast[0] = 1.0f;
    f.onset = std::fmod(t, 0.2f) < dt ? 1.0f : 0.0f;
    return f;
}

// Through the whole chain, with the real director deciding the moments.
SceneParams resolve(Resolver& r, const EngineSettings& s, float seconds, float& t, bool* safe = nullptr) {
    constexpr float dt = 1.0f / 60.0f;
    StyleContext style;
    style.id = &kSea;
    SceneParams requested;
    requested.brightness = 2.0f;
    requested.intensity = 2.0f;
    requested.bloom = 1.0f;
    SceneParams out;
    int n = 0;
    for (const float end = t + seconds; t < end; t += dt, ++n) {
        GeodeSoundFrame f = song(t, dt);
        if (n > 0 && n % 600 == 0) f.drop = 1.0f;
        if (n % 600 == 300) f.sectionChange = 1.0f;
        out = r.step(requested, f, dt, s, style).params;
        if (safe != nullptr) *safe = *safe && sameBits(out, safety::apply(out, s.reducedMotion));
    }
    return out;
}

void testResolver() {
    EngineSettings look;
    look.director = {1.0f, 42};
    EngineSettings modulate = look;
    modulate.reach = Reach::Modulate;
    EngineSettings lookKept = look;
    lookKept.kept = keptOf("#MOTION\nWarp");
    EngineSettings autoOff = modulate;
    autoOff.director = {0.0f, 42};
    EngineSettings plainOff = autoOff;
    plainOff.reach = Reach::Look;

    Resolver a, b, c, d, e, again;
    float ta = 0.0f, tb = 0.0f, tc = 0.0f, td = 0.0f, te = 0.0f, tg = 0.0f;
    bool safe = true;
    const SceneParams pa = resolve(a, look, 60.0f, ta);
    const SceneParams pb = resolve(b, modulate, 60.0f, tb, &safe);
    const SceneParams pc = resolve(c, lookKept, 60.0f, tc);
    const SceneParams pd = resolve(d, autoOff, 60.0f, td);
    const SceneParams pe = resolve(e, plainOff, 60.0f, te);
    const SceneParams pg = resolve(again, modulate, 60.0f, tg);
    check(b.director().commits() >= 6, "the director decided at the drops and sections");
    check(b.directorReach().shuffles() >= 5 && b.directorReach().deals() >= 6, "and the reach followed it");
    check(a.directorReach().shuffles() == 0 && a.directorReach().deals() == 0, "Look shuffles and deals nothing");
    check(sameBits(pa, pc), "Look is the same picture whatever the keep set");
    check(sameBits(pd, pe), "with Auto off the reach is the same picture as Look");
    check(!sameBits(pa, pb), "Modulate is another picture than Look");
    check(sameBits(pb, pg), "and the same one every time: an export equals the screen");
    check(safe, "safety::apply is still last: every frame is already inside its limits");
    const float top = 1.0f + safety::kMaxFlashDepth + 1e-6f;
    check(pb.brightness <= top && pb.intensity <= top && pb.bloom <= safety::kMaxFlashDepth + 1e-6f, "the luminance caps hold");
}

void testExportPin() {
    director::setReach(static_cast<int>(Reach::Modulate));
    director::setKept("Zoom\n#LIQUID");
    SettingsLatch pinned, live;
    pinned.pinForExport(true, 30'000);
    director::setReach(static_cast<int>(Reach::Look));
    director::setKept("");
    const EngineSettings p = pinned.latch(false), l = live.latch(false);
    check(p.reach == Reach::Modulate && p.kept && p.kept->count("Zoom") && p.kept->count("Flow curl"),
          "an export keeps the reach and the keep set it was pinned with");
    check(l.reach == Reach::Look && l.kept && l.kept->empty(), "while the live surface follows the setting");
    director::setReach(7);
    check(director::reach() == Reach::Look, "a level outside the enum is Look");
    director::setReach(static_cast<int>(Reach::Shuffle));
    check(live.latch(false).reach == Reach::Shuffle && pinned.latch(false).reach == Reach::Modulate, "and the pin still holds");
    director::setReach(0);
}

}  // namespace

int main() {
    testDeterminism();
    testStrength();
    testKept();
    testScopeAndRanges();
    testNoJumps();
    testBasePlusModulation();
    testUserEdit();
    testHandMask();
    testReducedMotionAndOff();
    testNoTempo();
    testResolver();
    testExportPin();
    if (failures == 0) std::printf("director_reach_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
