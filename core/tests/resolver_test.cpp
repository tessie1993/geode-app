// Host test of the scene params resolver (signal/Resolver), the chain as a whole rather than each stage
// alone: that the director's look lies under the macros and an LFO moves on top of both, that reduced
// motion is applied exactly once, that a resolver given explicit engine settings never hears the process
// globals, that an export's settings latch (signal/EngineSettings, #350) holds its pin while the globals flip
// and decides the export's director as the free-taste rule says, that two resolvers given the same inputs
// give the same bits over 600 frames, and that no stage can push the luminance past safety::apply, which
// runs last. Behaviour, not golden numbers: the stages' own maths is pinned by viz_math_test, director_test
// and note_response_test. Built by CI (.github/workflows/android.yml, "Native host tests") with
// core/signal/Resolver.cpp, core/signal/EngineSettings.cpp, core/sound/*.cpp and the GL-free stage sources it
// links (no GL header anywhere).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "signal/Resolver.hpp"
#include "sound/Harmony.hpp"
#include "viz/Director.hpp"
#include "viz/Macros.hpp"
#include "viz/NoteResponse.hpp"
#include "viz/Reactions.hpp"
#include "viz/VisualSafety.hpp"

using geode::signal::EngineSettings;
using geode::signal::Resolver;
using geode::signal::StyleContext;
using geode::viz::AdsrConfig;
using geode::viz::AdsrEngine;
using geode::viz::LfoConfig;
using geode::viz::LfoEngine;
using geode::viz::LfoTarget;
using geode::viz::Macro;
using geode::viz::MacroKnobs;
using geode::viz::ReactionEngine;
using geode::viz::ReactRoute;
using geode::viz::ReactSource;
using geode::viz::SceneParams;
namespace safety = geode::viz::safety;
namespace hand = geode::viz::hand;

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

constexpr float kDt = 1.0f / 60.0f;
const std::array<float, 4> kFlat{0.0f, 0.0f, 1.0f, 0.0f};
const std::string kStyle = "neon_chrome_sea";
const std::string kOtherStyle = "glow_vortex";

int scaleIndex(const char* id) {
    for (int i = 0; i < geode::sound::scaleCount(); ++i) {
        if (std::strcmp(geode::sound::scaleInfo(i).id, id) == 0) return i;
    }
    std::printf("no scale %s\n", id);
    std::exit(1);
}

// A steady, loud, major song at 132 bpm (director_test's happy song), with a kick on every onset so the
// envelopes, the reactions and the note layer all have something to hear.
GeodeSoundFrame frame(float t) {
    GeodeSoundFrame f{};
    f.loudness = 0.85f;
    f.level = 0.6f;
    f.scale = static_cast<float>(scaleIndex("ionian"));
    f.scaleRoot = 0.0f;
    f.scaleFit = 0.8f;
    f.keyRoot = 0.0f;
    f.keyClarity = 0.7f;
    f.keyMajor = 1.0f;
    f.bpm = 132.0f;
    f.beatConf = 0.8f;
    f.centroid = 0.6f;
    f.energyTrend = 0.5f;
    f.tension = 0.15f;
    const float phase = std::fmod(t, 0.2f);
    const bool hit = phase < kDt;
    f.onset = hit ? 1.0f : 0.0f;
    f.onsetEnv = std::exp(-phase / 0.08f);
    f.bass = 1.0f + 1.5f * f.onsetEnv;
    f.bassAtt = f.bass;
    f.mid = f.midAtt = 1.0f;
    f.treble = f.trebleAtt = 1.0f;
    f.vol = f.volAtt = 1.0f;
    f.low = std::min(1.0f, 0.4f + 0.6f * f.onsetEnv);
    f.middle = 0.5f;
    f.high = 0.4f;
    const float beat = 60.0f / f.bpm;
    f.downbeat = std::fmod(t, beat * 4.0f) < kDt ? 1.0f : 0.0f;
    // A melody: one note per beat, walking the scale.
    const int step = static_cast<int>(t / beat);
    static constexpr int kMelody[] = {0, 2, 4, 5, 7, 9, 11, 12};
    const int note = kMelody[step % 8];
    f.pitchClass = static_cast<float>(note % 12) / 12.0f;
    f.pitchHeight = 0.4f + 0.02f * static_cast<float>(note);
    f.pitch = 60.0f + static_cast<float>(note);
    f.pitchConfidence = 0.8f;
    f.noteChange = std::fmod(t, beat) < kDt ? 1.0f : 0.0f;
    for (int i = 0; i < 12; ++i) f.chromaFast[i] = 0.2f;
    f.chromaFast[0] = 1.0f;
    return f;
}

StyleContext style(const std::string& id, bool switched) {
    StyleContext s;
    s.id = &id;
    s.shaderFamily = true;
    s.spin = 1.0f;
    s.switched = switched;
    return s;
}

using Wire = std::array<float, SceneParams::kFieldCount>;
Wire wire(const SceneParams& p) {
    Wire w{};
    p.toWire(w.data());
    return w;
}
bool sameBits(const Wire& a, const Wire& b) { return std::memcmp(a.data(), b.data(), sizeof(float) * a.size()) == 0; }

LfoConfig sineOn(LfoTarget target, float depth, float rateSeconds) {
    LfoConfig c;
    c.enabled = true;
    c.target = target;
    c.wave = geode::viz::LfoWave::Sine;
    c.depth = depth;
    c.rateSeconds = rateSeconds;
    return c;
}

ReactRoute route(ReactSource source, LfoTarget target, float amount) {
    ReactRoute r;
    r.enabled = true;
    r.source = source;
    r.target = target;
    r.amount = amount;
    return r;
}

// The director's look under the macros: Mood -1 drives saturation to exactly 0 whatever the director chose
// (a director laid after the macros would lerp it back toward its own saturation, never below 0.2). An LFO
// on saturation then shows on top of both (one laid before the macros would be wiped out by Mood -1).
// The hand mask keeps the director's own routes off every moment, so nothing else writes saturation.
void testOrder() {
    EngineSettings s;
    s.director = {1.0f, 42};
    s.knobs[static_cast<int>(Macro::Mood)] = -1.0f;
    s.handMask = hand::kAll;
    auto plain = std::make_unique<Resolver>();
    auto lfo = std::make_unique<Resolver>();
    std::array<LfoConfig, LfoEngine::kSlots> configs{};
    configs[0] = sineOn(LfoTarget::Saturation, 0.3f, 1.0f);
    lfo->setLfoConfigs(configs);
    const SceneParams requested;
    float t = 0.0f;
    float maxPlain = 0.0f;
    float maxLfo = 0.0f;
    bool lookInPlace = true;
    const int frames = static_cast<int>(40.0f / kDt);
    for (int i = 0; i < frames; ++i, t += kDt) {
        const GeodeSoundFrame f = frame(t);
        const StyleContext st = style(kStyle, i == 0);
        const SceneParams& a = plain->step(requested, f, kDt, s, st, kFlat).params;
        const SceneParams& b = lfo->step(requested, f, kDt, s, st, kFlat).params;
        if (t >= 38.0f) {
            maxPlain = std::max(maxPlain, std::fabs(a.saturation));
            maxLfo = std::max(maxLfo, b.saturation);
            lookInPlace = lookInPlace && a.paletteBaseOverride != SceneParams::kUnsetOverride;
        }
    }
    check(plain->director().commits() >= 1 && plain->director().weight() > 0.9f, "the director is fully in");
    check(lookInPlace, "the director's look reaches the final params");
    check(maxPlain < 1e-6f, "the macros lie over the director's look (Mood -1: saturation 0, got " + std::to_string(maxPlain) + ")");
    check(maxLfo > 0.2f, "an LFO moves on top of the director and the macros (peak " + std::to_string(maxLfo) + ")");
}

// Reduced motion scales the forward speeds once, by kReducedMotionScale (0.4), never 0.4 x 0.4.
void testReducedMotionOnce() {
    EngineSettings off;
    EngineSettings on;
    on.reducedMotion = true;
    auto a = std::make_unique<Resolver>();
    auto b = std::make_unique<Resolver>();
    SceneParams requested;
    requested.camFly = 1.0f;
    requested.endlessZoomSpeed = 1.0f;
    float t = 0.0f;
    bool once = true;
    for (int i = 0; i < 300; ++i, t += kDt) {
        const GeodeSoundFrame f = frame(t);
        const StyleContext st = style(kStyle, i == 0);
        const SceneParams& pa = a->step(requested, f, kDt, off, st, kFlat).params;
        const SceneParams& pb = b->step(requested, f, kDt, on, st, kFlat).params;
        once = once && pa.camFly > 0.0f && pa.endlessZoomSpeed > 0.0f &&
               std::fabs(pb.camFly - pa.camFly * safety::kReducedMotionScale) < 1e-6f &&
               std::fabs(pb.endlessZoomSpeed - pa.endlessZoomSpeed * safety::kReducedMotionScale) < 1e-6f;
    }
    check(once, "reduced motion scales the fly-through and the dive exactly once");
}

// A rich setup, so every stage runs: the director, notes, LFOs, an envelope, user routes and the macros.
EngineSettings richSettings() {
    EngineSettings s;
    s.director = {1.0f, 7};
    s.knobs = MacroKnobs{0.4f, -0.3f, 0.6f, 0.2f, -0.5f, 0.7f};
    s.notesOn = true;
    s.handMask = hand::bit(geode::viz::Moment::Snare);
    s.noteEntropy = 0x1234ABCDull;
    return s;
}

void configureRich(Resolver& r) {
    std::array<LfoConfig, LfoEngine::kSlots> lfos{};
    lfos[0] = sineOn(LfoTarget::Zoom, 0.2f, 3.0f);
    lfos[1] = sineOn(LfoTarget::Brightness, 0.2f, 0.7f);
    r.setLfoConfigs(lfos);
    std::array<AdsrConfig, AdsrEngine::kCount> envs{};
    envs[0].enabled = true;
    envs[0].targets = {LfoTarget::Warp, LfoTarget::Intensity};
    envs[0].amount = 0.8f;
    r.setAdsrConfigs(envs);
    std::array<ReactRoute, ReactionEngine::kRoutes> routes{};
    routes[0] = route(ReactSource::Bass, LfoTarget::Twist, 0.6f);
    routes[1] = route(ReactSource::Level, LfoTarget::Bloom, 0.5f);
    r.setReactionRoutes(routes);
}

// A resolver hears only the settings it is given: flipping every process global between frames changes
// nothing in a run against one where they never moved.
void testPinnedAgainstGlobals() {
    const EngineSettings s = richSettings();
    const SceneParams requested;
    std::vector<Wire> calm;
    {
        auto r = std::make_unique<Resolver>();
        configureRich(*r);
        float t = 0.0f;
        for (int i = 0; i < 600; ++i, t += kDt) calm.push_back(wire(r->step(requested, frame(t), kDt, s, style(kStyle, i == 0), kFlat).params));
    }
    auto r = std::make_unique<Resolver>();
    configureRich(*r);
    float t = 0.0f;
    bool same = true;
    for (int i = 0; i < 600; ++i, t += kDt) {
        const float x = (i % 3 == 0) ? 1.0f : -1.0f;
        geode::viz::macros::setGlobal(MacroKnobs{x, -x, x, -x, x, -x});
        geode::viz::note_response::setGlobal(i % 2 == 0);
        hand::setGlobal(i % 2 == 0 ? hand::kAll : 0u);
        geode::viz::director::setGlobal(i % 2 != 0, i % 2 == 0 ? 0.0f : 1.0f, static_cast<uint64_t>(i));
        same = same && sameBits(calm[static_cast<size_t>(i)], wire(r->step(requested, frame(t), kDt, s, style(kStyle, i == 0), kFlat).params));
    }
    geode::viz::macros::setGlobal(MacroKnobs{});
    geode::viz::note_response::setGlobal(false);
    hand::setGlobal(0u);
    geode::viz::director::setGlobal(false, 0.0f, 0);
    check(same, "changing the process globals mid-run never reaches a resolver's frames");
}

// An export's director (#350), the rule the shell's AutoDirectorPolicy.exportAmount had: off with the switch
// off; Pro and the trial throughout; free throughout only when the whole export fits the free taste (60 s),
// else not at all. Never the live amount, and always the play's seed.
void testDirectorForExport() {
    namespace director = geode::viz::director;
    check(director::kFreeTasteMs == 60'000, "the free taste is the shell's FREE_AUTO_DIRECTOR_TASTE_MS");
    bool off = true;
    for (bool entitled : {false, true})
        for (long long ms : {0LL, 30'000LL, 60'000LL, 300'000LL}) off = off && director::forExport(false, 9, entitled, ms).amount == 0.0f;
    check(off, "the switch off directs no export, Pro or free, short or long");
    check(director::forExport(true, 9, true, 300'000).amount == 1.0f, "Pro and the trial direct a long export throughout");
    check(director::forExport(true, 9, false, 0).amount == 1.0f, "free directs an empty export");
    check(director::forExport(true, 9, false, 60'000).amount == 1.0f, "free directs an export of exactly the taste");
    check(director::forExport(true, 9, false, 60'001).amount == 0.0f, "free does not direct an export a millisecond past the taste");
    check(director::forExport(true, 9, false, 300'000).amount == 0.0f, "free does not direct a long export");
    check(director::forExport(true, 0xDEADBEEFull, true, 1).seed == 0xDEADBEEFull, "an export keeps the play's seed");
}

// A pinned latch (an export, #350) keeps the knobs, the Note & key switch, the hand mask and the director it
// was pinned with, with no note entropy, while every process global flips each frame; a live latch follows
// the globals. Reduced motion is the renderer's own and passes through either way.
void testLatchPinnedAgainstGlobals() {
    using geode::signal::SettingsLatch;
    namespace director = geode::viz::director;
    const MacroKnobs knobs{0.5f, -0.5f, 0.25f, 0.0f, -0.25f, 1.0f};
    const std::uint32_t snare = hand::bit(geode::viz::Moment::Snare);
    geode::viz::macros::setGlobal(knobs);
    geode::viz::note_response::setGlobal(true);
    hand::setGlobal(snare);
    // The live free taste has run out (amount 0), but a 30 s free export is still directed throughout.
    director::setGlobal(true, 0.0f, 42);
    SettingsLatch pinned;
    check(!pinned.pinned(), "a latch starts live");
    const std::uint32_t mask = pinned.pinForExport(false, 30'000);
    check(pinned.pinned(), "pinForExport pins the latch");
    check(mask == snare, "the pin hands back the hand mask it pinned");
    SettingsLatch live;
    bool held = true;
    bool followed = true;
    for (int i = 0; i < 120; ++i) {
        const float x = (i % 3 == 0) ? 1.0f : -1.0f;
        const MacroKnobs now{x, -x, x, -x, x, -x};
        geode::viz::macros::setGlobal(now);
        geode::viz::note_response::setGlobal(i % 2 == 0);
        hand::setGlobal(i % 2 == 0 ? hand::kAll : 0u);
        director::setGlobal(i % 2 != 0, i % 2 == 0 ? 0.0f : 1.0f, static_cast<uint64_t>(i + 100));
        const bool reduced = i % 2 == 0;
        const EngineSettings p = pinned.latch(reduced);
        held = held && p.knobs == knobs && p.notesOn && p.handMask == snare && p.director.amount == 1.0f &&
               p.director.seed == 42 && p.noteEntropy == 0 && p.reducedMotion == reduced;
        const EngineSettings l = live.latch(reduced);
        followed = followed && l.knobs == now && l.notesOn == (i % 2 == 0) && l.handMask == (i % 2 == 0 ? hand::kAll : 0u) &&
                   l.director.amount == (i % 2 == 0 ? 0.0f : 1.0f) && l.director.seed == static_cast<uint64_t>(i + 100) &&
                   l.reducedMotion == reduced;
    }
    geode::viz::macros::setGlobal(MacroKnobs{});
    geode::viz::note_response::setGlobal(false);
    hand::setGlobal(0u);
    director::setGlobal(false, 0.0f, 0);
    check(held, "a pinned latch never hears the process globals again");
    check(followed, "a live latch follows the process globals every frame");

    // A pinned free export longer than the taste is not directed, whatever the live amount says.
    director::setGlobal(true, 1.0f, 7);
    SettingsLatch longFree;
    longFree.pinForExport(false, 120'000);
    check(longFree.latch(false).director.amount == 0.0f, "a free export past the taste is not directed, even mid-taste live");
    director::setGlobal(false, 0.0f, 0);
}

// Same inputs, same bits: params, camera moves, luminance drive and the note uniforms, over 600 frames
// with a style switch halfway.
void testDeterministic() {
    const EngineSettings s = richSettings();
    auto a = std::make_unique<Resolver>();
    auto b = std::make_unique<Resolver>();
    configureRich(*a);
    configureRich(*b);
    SceneParams requested;
    requested.paramFadeSec = 0.5f;
    float t = 0.0f;
    bool same = true;
    int moments = 0;
    for (int i = 0; i < 600; ++i, t += kDt) {
        GeodeSoundFrame f = frame(t);
        if (i == 400) f.sectionChange = 1.0f;
        if (i == 300) requested.zoom = 1.6f;
        const std::string& id = i < 300 ? kStyle : kOtherStyle;
        const StyleContext st = style(id, i == 0 || i == 300);
        const Resolver::Frame& fa = a->step(requested, f, kDt, s, st, kFlat);
        const Resolver::Frame& fb = b->step(requested, f, kDt, s, st, kFlat);
        moments += fa.styleMoments;
        same = same && sameBits(wire(fa.params), wire(fb.params)) && fa.styleMoments == fb.styleMoments &&
               std::memcmp(&fa.cameraMoves, &fb.cameraMoves, sizeof(fa.cameraMoves)) == 0 &&
               std::memcmp(fa.luminanceDrive.data(), fb.luminanceDrive.data(), sizeof(float) * fa.luminanceDrive.size()) == 0 &&
               std::memcmp(&a->noteUniforms(), &b->noteUniforms(), sizeof(geode::viz::NoteUniforms)) == 0;
    }
    check(same, "two resolvers given the same inputs give bit-identical frames over 600 frames");
    check(a->notes().amount() > 0.5f, "the note layer is in");
    check(a->director().commits() >= 1, "the director decided (moments this run: " + std::to_string(moments) + ")");
}

// Every stage pushes the luminance up as hard as it can; the clamp still holds, because it runs last.
void testSafetyLast() {
    EngineSettings s;
    s.director = {1.0f, 3};
    s.knobs[static_cast<int>(Macro::Energy)] = 1.0f;
    s.knobs[static_cast<int>(Macro::Mood)] = 1.0f;
    s.notesOn = true;
    auto r = std::make_unique<Resolver>();
    std::array<LfoConfig, LfoEngine::kSlots> lfos{};
    lfos[0] = sineOn(LfoTarget::Brightness, 2.0f, 0.4f);
    lfos[1] = sineOn(LfoTarget::Intensity, 2.0f, 0.5f);
    lfos[2] = sineOn(LfoTarget::Bloom, 2.0f, 0.6f);
    for (LfoConfig& c : lfos) c.polarity = geode::viz::ModPolarity::Positive;
    r->setLfoConfigs(lfos);
    std::array<AdsrConfig, AdsrEngine::kCount> envs{};
    envs[0].enabled = true;
    envs[0].targets = {LfoTarget::Brightness, LfoTarget::Bloom};
    envs[0].amount = 2.0f;
    envs[0].gateThreshold = 0.05f;
    r->setAdsrConfigs(envs);
    std::array<ReactRoute, ReactionEngine::kRoutes> routes{};
    routes[0] = route(ReactSource::Level, LfoTarget::Brightness, 2.0f);
    routes[1] = route(ReactSource::Bass, LfoTarget::Intensity, 2.0f);
    routes[2] = route(ReactSource::BassAtt, LfoTarget::Bloom, 2.0f);
    r->setReactionRoutes(routes);
    SceneParams requested;
    requested.brightness = 3.0f;
    requested.intensity = 3.0f;
    requested.contrast = 3.0f;
    requested.bloom = 2.0f;
    requested.flash = 2.0f;
    requested.strobe = 2.0f;
    const float top = 1.0f + safety::kMaxFlashDepth + 1e-6f;
    float t = 0.0f;
    bool held = true;
    for (int i = 0; i < 600; ++i, t += kDt) {
        const SceneParams& p = r->step(requested, frame(t), kDt, s, style(kStyle, i == 0), kFlat).params;
        held = held && p.brightness <= top && p.intensity <= top && p.contrast <= top &&
               p.bloom <= safety::kMaxFlashDepth + 1e-6f &&
               p.flash <= safety::kMaxFlashDepth / safety::kFlashShaderDepth + 1e-6f &&
               p.strobe <= safety::kMaxFlashDepth / safety::kStrobeShaderDepth + 1e-6f &&
               p.brightness >= 0.0f && p.intensity >= 0.0f && p.contrast >= 0.0f;
    }
    check(held, "no stage pushes brightness, intensity, contrast, bloom, flash or strobe past safety::apply");
}

}  // namespace

int main() {
    testOrder();
    testReducedMotionOnce();
    testPinnedAgainstGlobals();
    testDirectorForExport();
    testLatchPinnedAgainstGlobals();
    testDeterministic();
    testSafetyLast();
    if (failures) {
        std::printf("resolver_test: %d failed\n", failures);
        return 1;
    }
    std::printf("resolver_test: all passed\n");
    return 0;
}
