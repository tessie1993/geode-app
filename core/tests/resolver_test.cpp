// Host test of the scene params resolver (signal/Resolver), the chain as a whole rather than each stage
// alone: that the director's look lies under the macros and an LFO moves on top of both, that reduced
// motion is applied exactly once, that a resolver given explicit engine settings never hears the process
// globals, that two resolvers given the same inputs give the same bits over 600 frames, and that no stage
// can push the luminance past safety::apply, which runs last. Behaviour, not golden numbers: the stages'
// own maths is pinned by viz_math_test, director_test and note_response_test.
// Then, for #528's mutants: parity with the old chain (main's Renderer::resolveParams before #344) bit for
// bit over seeded random setups, the preset morph and param fade by hand-worked frames, the motion field
// before the clamp, the style moments, disabled stages as exact identity, and live beside export.
// Built by CI (.github/workflows/android.yml, "Native host tests") with core/signal/Resolver.cpp,
// core/sound/*.cpp and the GL-free stage sources it links (no GL header anywhere).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "signal/Resolver.hpp"
#include "sound/Harmony.hpp"
#include "viz/Director.hpp"
#include "viz/Macros.hpp"
#include "viz/MotionField.hpp"
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
        const SceneParams& a = plain->step(requested, f, kDt, s, st).params;
        const SceneParams& b = lfo->step(requested, f, kDt, s, st).params;
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
        const SceneParams& pa = a->step(requested, f, kDt, off, st).params;
        const SceneParams& pb = b->step(requested, f, kDt, on, st).params;
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
        for (int i = 0; i < 600; ++i, t += kDt) calm.push_back(wire(r->step(requested, frame(t), kDt, s, style(kStyle, i == 0)).params));
    }
    auto r = std::make_unique<Resolver>();
    configureRich(*r);
    float t = 0.0f;
    bool same = true;
    for (int i = 0; i < 600; ++i, t += kDt) {
        const float x = (i % 3 == 0) ? 1.0f : -1.0f;
        geode::viz::macros::setGlobal(MacroKnobs{x, -x, x, -x, x, -x});
        geode::viz::note_response::setGlobal(i % 2 == 0);
        hand::setGlobal(i % 2 == 0 ? hand::kAll : 0u, true);
        geode::viz::director::setGlobal(i % 2 == 0 ? 0.0f : 1.0f, static_cast<uint64_t>(i));
        same = same && sameBits(calm[static_cast<size_t>(i)], wire(r->step(requested, frame(t), kDt, s, style(kStyle, i == 0)).params));
    }
    geode::viz::macros::setGlobal(MacroKnobs{});
    geode::viz::note_response::setGlobal(false);
    hand::setGlobal(0u, false);
    geode::viz::director::setGlobal(0.0f, 0);
    check(same, "changing the process globals mid-run never reaches a resolver's frames");
}

// Same inputs, same bits: params, camera moves and the note uniforms, over 600 frames
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
        const Resolver::Frame& fa = a->step(requested, f, kDt, s, st);
        const Resolver::Frame& fb = b->step(requested, f, kDt, s, st);
        moments += fa.styleMoments;
        same = same && sameBits(wire(fa.params), wire(fb.params)) && fa.styleMoments == fb.styleMoments &&
               std::memcmp(&fa.cameraMoves, &fb.cameraMoves, sizeof(fa.cameraMoves)) == 0 &&
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
        const SceneParams& p = r->step(requested, frame(t), kDt, s, style(kStyle, i == 0)).params;
        held = held && p.brightness <= top && p.intensity <= top && p.contrast <= top &&
               p.bloom <= safety::kMaxFlashDepth + 1e-6f &&
               p.flash <= safety::kMaxFlashDepth / safety::kFlashShaderDepth + 1e-6f &&
               p.strobe <= safety::kMaxFlashDepth / safety::kStrobeShaderDepth + 1e-6f &&
               p.brightness >= 0.0f && p.intensity >= 0.0f && p.contrast >= 0.0f;
    }
    check(held, "no stage pushes brightness, intensity, contrast, bloom, flash or strobe past safety::apply");
}

// ---- The chain against its own spec (tests for #528) ------------------------------------------------------
// Each case below names the mutants of signal/Resolver.cpp it kills. Expected values come from hand-worked
// literals, the safety module's published caps, or the old chain as it stood in Renderer::resolveParams
// on main before #344 (the move this PR makes); never from the resolver itself.

namespace viz = geode::viz;

// A seeded source of raw mt19937 output (its sequence is fixed by the standard; no distribution is used,
// since those differ between standard libraries).
struct Rng {
    std::mt19937 g;
    explicit Rng(std::uint32_t seed) : g(seed) {}
    std::uint32_t raw() { return static_cast<std::uint32_t>(g()); }
    std::uint64_t raw64() {
        const std::uint64_t hi = raw();
        return (hi << 32) ^ raw();
    }
    float unit() { return static_cast<float>(raw() >> 8) * (1.0f / 16777216.0f); }  // [0, 1)
    float range(float lo, float hi) { return lo + (hi - lo) * unit(); }
    int below(int n) { return static_cast<int>(raw() % static_cast<std::uint32_t>(n)); }
    bool coin() { return (raw() & 1u) != 0u; }
};

// The per-frame chain exactly as main's Renderer::resolveParams ran it before #344, the stages called in that
// order on stages of its own. The one change: the director takes the hand mask as an argument, where the old
// step read hand::global(); the oracle hands it the same mask the resolver gets, which is what the global
// held. The parity property below holds the resolver to it bit for bit.
struct OldChain {
    viz::LfoEngine lfo;
    viz::AdsrEngine adsr;
    viz::ReactionEngine reactions;
    viz::ReactionEngine macroReactions;
    viz::Director director;
    int directorCommitsSeen = 0;
    viz::NoteResponse notes;
    viz::MotionField motionField;
    SceneParams displayedParams;
    float morphFadeSec = 0.0f;
    float morphRemainSec = 0.0f;
    MacroKnobs directedKnobs{};
    std::array<float, LfoEngine::kSlots> envRate{};
    std::array<float, LfoEngine::kSlots> envDepth{};
    viz::LuminanceDrive luminanceDrive{};
    SceneParams params;
    int styleMoments = 0;
    viz::CameraRig::Moves cameraMoves{};

    // Renderer::beginParamMorph.
    void beginParamMorph(float seconds) {
        if (seconds <= 0.0f) return;
        morphFadeSec = seconds;
        morphRemainSec = seconds * 3.0f;
    }
    // Renderer::onSurfaceCreated's resets.
    void surfaceCreated() {
        motionField.reset();
        reactions.reset();
        macroReactions.reset();
        director.reset();
        notes.reset();
    }
    // Renderer::resolveActiveScene, on a new style.
    void sceneSwitched(const std::string& id) { motionField.setVariant(viz::styleVariantSeed(id)); }

    void resolve(const SceneParams& requested, const GeodeSoundFrame& soundFrame, float dt, const EngineSettings& s,
                 bool palWheel, float spin) {
        float morph;
        if (morphRemainSec > 0.0f) {
            morphRemainSec -= dt;
            morph = morphFadeSec;
        } else {
            morph = 0.0f;
        }
        const float fade = std::max(requested.paramFadeSec, morph);
        displayedParams = fade <= 0.01f ? requested : viz::lerpParams(displayedParams, requested, std::clamp(dt / fade, 0.0f, 1.0f));
        const viz::DirectorConfig directorConfig = s.director;
        director.configure(directorConfig);
        director.step(soundFrame, dt, s.handMask);
        styleMoments = 0;
        if (director.commits() != directorCommitsSeen) {
            directorCommitsSeen = director.commits();
            const viz::Director::Reason why = director.lastReason();
            if (why == viz::Director::Reason::Section || why == viz::Director::Reason::Drop) styleMoments += 1;
        }
        directedKnobs = director.knobs(s.knobs);
        notes.configure({s.notesOn, directorConfig.seed ^ s.noteEntropy});
        {
            const SceneParams& own = displayedParams;
            viz::NoteResponse::Inputs in;
            in.anchorHue = director.look().hue;
            in.anchorWeight = director.weight();
            in.color = own.noteColor;
            in.pitch = own.notePitch;
            in.intervals = own.noteIntervals;
            in.tension = own.noteTension;
            in.chords = own.noteChords;
            in.drums = own.noteDrums;
            notes.step(soundFrame, director.emotion(), in, dt);
        }
        const auto& envValues = adsr.tick(dt, soundFrame);
        AdsrEngine::lfoOffsets(adsr.configs, envValues, envRate, envDepth);
        const auto& lfoValues = lfo.tick(dt, soundFrame, envRate.data(), envDepth.data());
        SceneParams p = lfo.apply(viz::macros::apply(notes.apply(director.apply(displayedParams, palWheel)), directedKnobs), lfoValues);
        p = AdsrEngine::apply(p, adsr.configs, envValues);
        luminanceDrive.fill(0.0f);
        lfo.addLuminanceDrive(lfoValues, luminanceDrive);
        AdsrEngine::addLuminanceDrive(adsr.configs, envValues, luminanceDrive);
        reactions.tick(soundFrame, dt);
        p = reactions.apply(p);
        macroReactions.routes = viz::macros::routes(directedKnobs);
        macroReactions.tick(soundFrame, dt);
        p = macroReactions.apply(p);
        p = director.applyRoutes(p);
        motionField.step(soundFrame, dt);
        p = motionField.apply(p, s.reducedMotion, spin);
        p = safety::apply(p, s.reducedMotion);
        params = p;
        // Renderer::stepCamera, after the chain.
        cameraMoves = director.moves(viz::macros::camera(directedKnobs));
    }
};

// Targets the random setups draw from: the macro routes' own (Bloom, Speed, Morph, Temperature, DriftX, Twist,
// ChromaAb), the luminance ones, the camera and dive speeds, and three chained (LFO rate and depth) targets,
// so stages overlap on one field and run into its clamp, where their order shows.
constexpr std::array<LfoTarget, 18> kTargets = {
    LfoTarget::Speed,      LfoTarget::Zoom,       LfoTarget::Rotation,  LfoTarget::Twist,    LfoTarget::Morph,
    LfoTarget::Saturation, LfoTarget::Brightness, LfoTarget::Intensity, LfoTarget::Bloom,    LfoTarget::Temperature,
    LfoTarget::ChromaAb,   LfoTarget::DriftX,     LfoTarget::CamFly,    LfoTarget::Lfo1Rate, LfoTarget::Lfo2Depth,
    LfoTarget::Lfo3Rate,   LfoTarget::EndlessZoomSpeed, LfoTarget::Warp,
};
LfoTarget anyTarget(Rng& g) { return kTargets[static_cast<size_t>(g.below(static_cast<int>(kTargets.size())))]; }

std::array<LfoConfig, LfoEngine::kSlots> randomLfos(Rng& g) {
    std::array<LfoConfig, LfoEngine::kSlots> out{};
    for (LfoConfig& c : out) {
        c.enabled = g.below(3) != 0;
        c.source = static_cast<viz::ModSource>(g.below(9));
        c.target = anyTarget(g);
        c.wave = static_cast<viz::LfoWave>(g.below(5));
        c.rateSeconds = g.range(0.1f, 4.0f);
        c.depth = g.range(0.0f, 2.0f);
        c.polarity = static_cast<viz::ModPolarity>(g.below(3));
        c.curve = static_cast<viz::ModCurve>(g.below(4));
    }
    return out;
}

std::array<AdsrConfig, AdsrEngine::kCount> randomEnvelopes(Rng& g) {
    std::array<AdsrConfig, AdsrEngine::kCount> out{};
    for (AdsrConfig& c : out) {
        c.enabled = g.coin();
        c.targets.clear();
        const int n = 1 + g.below(2);
        for (int i = 0; i < n; ++i) c.targets.push_back(anyTarget(g));
        c.attack = g.range(0.01f, 0.3f);
        c.decay = g.range(0.05f, 0.5f);
        c.sustain = g.unit();
        c.release = g.range(0.05f, 0.6f);
        c.amount = g.range(0.0f, 2.0f);
        c.band = static_cast<viz::EnvBand>(g.below(6));
        c.gateThreshold = g.range(0.02f, 0.5f);
        c.sustainTrack = g.coin();
        c.retrigger = g.coin();
    }
    return out;
}

std::array<ReactRoute, ReactionEngine::kRoutes> randomRoutes(Rng& g) {
    std::array<ReactRoute, ReactionEngine::kRoutes> out{};
    for (ReactRoute& r : out) {
        r.enabled = g.coin();
        r.source = static_cast<ReactSource>(g.below(viz::kReactSourceCount));
        r.target = anyTarget(g);
        r.mode = static_cast<viz::ReactMode>(g.below(viz::kReactModeCount));
        r.amount = g.range(-2.0f, 2.0f);
        r.curve = static_cast<viz::ModCurve>(g.below(4));
        r.attackSeconds = g.range(0.0f, 0.3f);
        r.releaseSeconds = g.range(0.02f, 0.8f);
    }
    return out;
}

EngineSettings randomSettings(Rng& g) {
    EngineSettings s;
    for (float& k : s.knobs) k = g.coin() ? 0.0f : g.range(-1.0f, 1.0f);
    static constexpr float kAmounts[] = {0.0f, 0.5f, 1.0f, 1.0f};
    s.director.amount = kAmounts[g.below(4)];
    s.director.seed = g.raw64();
    s.notesOn = g.coin();
    s.handMask = g.raw() & hand::kAll;
    s.reducedMotion = g.below(4) == 0;
    s.noteEntropy = g.raw64();
    return s;
}

// Requests in and past every range, so the clamps of each stage are reached.
SceneParams randomRequested(Rng& g) {
    SceneParams p;
    p.speed = g.range(0.0f, 6.0f);
    p.zoom = g.range(0.2f, 3.5f);
    p.rotation = g.range(-4.0f, 4.0f);
    p.sway = g.range(0.0f, 1.2f);
    p.warp = g.range(0.0f, 1.2f);
    p.morph = g.range(0.0f, 1.2f);
    p.twist = g.range(-1.2f, 1.2f);
    p.saturation = g.range(0.0f, 2.0f);
    p.brightness = g.range(0.0f, 3.0f);
    p.intensity = g.range(0.0f, 3.0f);
    p.contrast = g.range(0.0f, 3.0f);
    p.bloom = g.range(0.0f, 2.0f);
    p.flash = g.range(0.0f, 2.0f);
    p.strobe = g.range(0.0f, 2.0f);
    p.temperature = g.range(-1.2f, 1.2f);
    p.chromaAb = g.unit();
    p.driftX = g.range(-1.2f, 1.2f);
    p.camFly = g.range(0.0f, 3.0f);
    p.endlessZoomSpeed = g.range(0.0f, 1.5f);
    p.motionAmount = g.unit();
    p.motionBreath = g.unit();
    p.motionDrift = g.unit();
    p.motionHue = g.unit();
    p.reactAmount = g.range(0.0f, 2.0f);
    p.paletteMix = g.unit();
    p.noteColor = g.range(0.0f, 2.0f);
    p.notePitch = g.range(0.0f, 2.0f);
    p.noteIntervals = g.range(0.0f, 2.0f);
    p.noteTension = g.range(0.0f, 2.0f);
    p.noteChords = g.range(0.0f, 2.0f);
    p.noteDrums = g.range(0.0f, 2.0f);
    p.paramFadeSec = g.coin() ? 0.0f : g.range(0.0f, 0.6f);
    return p;
}

// frame(t) with the bands, level and brightness shaken, and now and then a drop or a new section.
GeodeSoundFrame noisyFrame(float t, Rng& g) {
    GeodeSoundFrame f = frame(t);
    const float k = g.range(0.5f, 2.0f);
    f.bass *= k;
    f.bassAtt *= k;
    f.level = g.unit();
    f.mid = f.midAtt = g.range(0.5f, 2.0f);
    f.treble = f.trebleAtt = g.range(0.5f, 2.0f);
    f.centroid = g.unit();
    if (g.below(200) == 0) f.drop = 0.8f;
    if (g.below(200) == 0) f.sectionChange = 1.0f;
    return f;
}

// Live frame times: 60, 30 and 120 fps, and jittered ones between.
float randomDt(Rng& g) {
    switch (g.below(4)) {
        case 0: return 1.0f / 60.0f;
        case 1: return 1.0f / 30.0f;
        case 2: return 1.0f / 120.0f;
        default: return g.range(0.004f, 0.06f);
    }
}

// Parity: the resolver gives the old chain's frames bit for bit (params, style moments, camera moves,
// luminance drive, note uniforms) over 16 seeded runs of 900 frames, with configs, settings, requests,
// style switches, preset morphs and new surfaces changing at random. Kills any two stages swapped (director/
// notes/macros/LFO nesting, LFO/envelope, envelope/reactions, reactions/macro routes/director routes, motion
// field/safety), any stage dropped, the morph's x3 and <= 0, the fade's 0.01 cut, max, clamp and dt/fade,
// the hand mask, the note seed's ^ entropy, the anchor hue and weight, the note inputs read from the faded
// params, the style moments' Section || Drop and their reset each frame, the luminance drive's reset, the
// camera moves read from the directed knobs, reducedMotion and spin passed to the motion field, the style
// switch's variant, and every reset() of a stage.
// The same frames also hold the flash caps (safety last) and, under reduced motion, the speed under
// 0.4 x the motion field's top speed 4.
void testParityWithTheOldChain() {
    static const std::array<std::string, 3> kIds = {"neon_chrome_sea", "glow_vortex", "black_hole"};
    static constexpr float kSpins[] = {1.0f, 0.0f, -0.5f};
    const float top = 1.0f + safety::kMaxFlashDepth + 1e-6f;
    int mismatchRuns = 0;
    int capBreaks = 0;
    int moments = 0;
    int switches = 0;
    int morphs = 0;
    int resets = 0;
    for (std::uint32_t seed = 1; seed <= 16; ++seed) {
        Rng g(seed);
        auto now = std::make_unique<Resolver>();
        auto old = std::make_unique<OldChain>();
        EngineSettings s = randomSettings(g);
        SceneParams requested = randomRequested(g);
        const std::string* id = nullptr;
        bool shader = true;
        float spin = 1.0f;
        float t = 0.0f;
        bool same = true;
        for (int i = 0; i < 900; ++i) {
            if (i == 0 || g.below(100) == 0) {
                const auto lfos = randomLfos(g);
                const auto envs = randomEnvelopes(g);
                const auto routes = randomRoutes(g);
                now->setLfoConfigs(lfos);
                now->setAdsrConfigs(envs);
                now->setReactionRoutes(routes);
                old->lfo.configs = lfos;
                old->adsr.configs = envs;
                old->reactions.routes = routes;
            }
            if (g.below(100) == 0) s = randomSettings(g);
            if (g.below(50) == 0) requested = randomRequested(g);
            // A new surface first, as the renderer resets on surface creation before it resolves the scene.
            if (g.below(300) == 0) {
                now->reset();
                old->surfaceCreated();
                ++resets;
            }
            bool switched = false;
            if (i == 30 || g.below(120) == 0) {
                id = &kIds[static_cast<size_t>(g.below(3))];
                shader = g.coin();
                spin = kSpins[g.below(3)];
                switched = true;
                old->sceneSwitched(*id);
                ++switches;
            }
            if (g.below(200) == 0) {
                const float sec = g.range(-0.5f, 2.0f);
                now->beginMorph(sec);
                old->beginParamMorph(sec);
                ++morphs;
            }
            const float dt = randomDt(g);
            const GeodeSoundFrame f = noisyFrame(t, g);
            const bool palWheel = id == nullptr || shader;
            StyleContext st;
            st.id = id;
            st.shaderFamily = palWheel;
            st.spin = spin;
            st.switched = switched;
            const Resolver::Frame& a = now->step(requested, f, dt, s, st);
            old->resolve(requested, f, dt, s, palWheel, spin);
            moments += a.styleMoments;
            same = same && sameBits(wire(a.params), wire(old->params)) && a.styleMoments == old->styleMoments &&
                   std::memcmp(&a.cameraMoves, &old->cameraMoves, sizeof(a.cameraMoves)) == 0 &&
                   std::memcmp(a.luminanceDrive.data(), old->luminanceDrive.data(), sizeof(float) * a.luminanceDrive.size()) == 0 &&
                   std::memcmp(&now->noteUniforms(), &old->notes.uniforms(), sizeof(viz::NoteUniforms)) == 0;
            const SceneParams& p = a.params;
            const bool capped = p.brightness <= top && p.intensity <= top && p.contrast <= top &&
                                p.bloom <= safety::kMaxFlashDepth + 1e-6f &&
                                p.flash <= safety::kMaxFlashDepth / safety::kFlashShaderDepth + 1e-6f &&
                                p.strobe <= safety::kMaxFlashDepth / safety::kStrobeShaderDepth + 1e-6f &&
                                p.brightness >= 0.0f && p.intensity >= 0.0f && p.contrast >= 0.0f && p.flash >= 0.0f &&
                                p.strobe >= 0.0f && (!s.reducedMotion || p.speed <= 4.0f * 0.4f + 1e-6f);
            if (!capped) ++capBreaks;
            t += dt;
        }
        if (!same) {
            ++mismatchRuns;
            std::printf("  parity broke in seed %u\n", seed);
        }
    }
    check(mismatchRuns == 0, "the resolver gives the old chain's frames bit for bit (" + std::to_string(mismatchRuns) + " of 16 runs differ)");
    check(capBreaks == 0, "no random setup passes the flash caps or reduced motion's speed (" + std::to_string(capBreaks) + " frames)");
    // Not vacuous: the runs crossed every event the chain treats specially.
    check(moments > 0 && switches > 16 && morphs > 0 && resets > 0,
          "the parity runs saw style moments, switches, morphs and resets (" + std::to_string(moments) + ", " +
              std::to_string(switches) + ", " + std::to_string(morphs) + ", " + std::to_string(resets) + ")");
}

// A preset morph: a 1 s morph from saturation 0 to 1 at dt 0.1 covers a tenth of what is left each frame
// (0.1, 0.19, 0.6513, 0.9282 at 2.5 s, 0.9529 at 2.9 s), for three times its length, then the params land on
// the request exactly. A new surface (reset) mid-glide keeps it. Every stage is off, and saturation is a
// field only the director, the macros and the routes write, so it shows the fade alone.
// Kills: x3 -> x1 or x2 (lands by 2.9 s), max -> min of the style's fade and the morph (cuts at once),
// dt / fade inverted, beginMorph ignored, the morph clock never running out (still gliding at 3.2 s),
// reset() clearing the fade or the faded params.
void testPresetMorphGlidesForThreeTimesItsLength() {
    const EngineSettings off;
    auto r = std::make_unique<Resolver>();
    SceneParams from;
    from.saturation = 0.0f;
    SceneParams to;
    to.saturation = 1.0f;
    const float dt = 0.1f;
    const float first = r->step(from, frame(0.0f), dt, off, style(kStyle, true)).params.saturation;
    check(first == 0.0f, "with no fade the params cut to the request (got " + std::to_string(first) + ")");
    r->beginMorph(1.0f);
    struct Expect {
        int k;
        float sat;
    };
    static constexpr Expect kGlide[] = {{1, 0.1f}, {2, 0.19f}, {10, 0.6513216f}, {25, 0.9282102f}, {29, 0.9528987f}};
    float t = dt;
    bool glide = true;
    bool landed = true;
    for (int k = 1; k <= 40; ++k, t += dt) {
        if (k == 10) r->reset();
        const float sat = r->step(to, frame(t), dt, off, style(kStyle, false)).params.saturation;
        for (const Expect& e : kGlide) {
            if (e.k == k && std::fabs(sat - e.sat) > 1e-4f) {
                glide = false;
                std::printf("  morph frame %d: %f, want %f\n", k, static_cast<double>(sat), static_cast<double>(e.sat));
            }
        }
        if (k >= 32) landed = landed && sat == 1.0f;
    }
    check(glide, "a 1 s preset morph glides a tenth of the rest each 0.1 s frame for its three seconds, through a reset");
    check(landed, "after three times its length the morph ends and the params sit exactly on the request");
}

// The style's own param fade, from the default saturation 1 toward 0. A fade of 10 ms or less is a cut; a
// fade shorter than the frame lands exactly on the request, never past it; a 0.5 s fade at dt 0.1 covers a
// fifth of what is left each frame: 0.8, then 0.64.
// Kills: <= 0.01 -> < 0.01, the clamp of dt / fade removed (overshoots to -1), dt / fade inverted (cuts),
// the lerp's ends swapped (0.2), the fade restarting from the request instead of the last faded params.
void testParamFadeCutsLandsAndGlides() {
    const EngineSettings off;
    auto run = [&](float fadeSec, float dt, int frames) {
        auto r = std::make_unique<Resolver>();
        SceneParams requested;
        requested.saturation = 0.0f;
        requested.paramFadeSec = fadeSec;
        std::vector<float> out;
        float t = 0.0f;
        for (int i = 0; i < frames; ++i, t += dt) out.push_back(r->step(requested, frame(t), dt, off, style(kStyle, i == 0)).params.saturation);
        return out;
    };
    const std::vector<float> cut = run(0.01f, 0.001f, 1);
    check(cut[0] == 0.0f, "a 10 ms param fade is a cut (got " + std::to_string(cut[0]) + ")");
    const std::vector<float> lands = run(0.05f, 0.1f, 1);
    check(lands[0] == 0.0f, "a fade shorter than the frame lands exactly on the request (got " + std::to_string(lands[0]) + ")");
    const std::vector<float> glide = run(0.5f, 0.1f, 2);
    check(std::fabs(glide[0] - 0.8f) < 1e-6f && std::fabs(glide[1] - 0.64f) < 1e-6f,
          "a 0.5 s fade at dt 0.1 goes 1 -> 0.8 -> 0.64 (got " + std::to_string(glide[0]) + ", " + std::to_string(glide[1]) + ")");
}

// The motion field runs before the clamp, so reduced motion is the last word on speed: the field caps speed
// at 4, and reduced motion then scales it to exactly 4 x 0.4 = 1.6 (4 without it). Requested 20, every
// other stage off.
// Kills: safety::apply and the motion field swapped (20 x 0.4 = 8, then capped at 4), the safety clamp
// handed reducedMotion = false.
void testReducedMotionHasTheLastWordOnSpeed() {
    EngineSettings on;
    on.reducedMotion = true;
    const EngineSettings off;
    auto a = std::make_unique<Resolver>();
    auto b = std::make_unique<Resolver>();
    SceneParams requested;
    requested.speed = 20.0f;
    float t = 0.0f;
    bool reduced = true;
    bool full = true;
    for (int i = 0; i < 300; ++i, t += kDt) {
        const GeodeSoundFrame f = frame(t);
        reduced = reduced && std::fabs(a->step(requested, f, kDt, on, style(kStyle, i == 0)).params.speed - 1.6f) < 1e-6f;
        full = full && std::fabs(b->step(requested, f, kDt, off, style(kStyle, i == 0)).params.speed - 4.0f) < 1e-6f;
    }
    check(reduced, "under reduced motion the motion field's top speed 4 reaches the scene as exactly 1.6");
    check(full, "without reduced motion the top speed stays 4");
}

// Auto's style moments: one on the frame the director commits a look at a drop or a new section, none for
// its first look and none on any other frame. A steady song: the first look by 12 s, a drop at 14 s and a
// new section at 20 s (6 s after the drop, past the director's 4 s floor) give exactly two moments, on
// those two frames.
// Kills: Section || Drop -> && (no moments), a reason test inverted (the first look counts), the moment
// count not reset each frame (a moment every frame after), commits() != seen -> == .
void testStyleMomentsOnlyAtDropsAndSections() {
    EngineSettings s;
    s.director = {1.0f, 11};
    auto r = std::make_unique<Resolver>();
    const SceneParams requested;
    constexpr int kDropFrame = 840;      // 14 s
    constexpr int kSectionFrame = 1200;  // 20 s
    int total = 0;
    int firstLookMoments = 0;
    bool firstLookSeen = false;
    bool onlyThere = true;
    int atDrop = -1;
    int atSection = -1;
    for (int i = 0; i < 1500; ++i) {
        GeodeSoundFrame f = frame(static_cast<float>(i) * kDt);
        if (i == kDropFrame) f.drop = 0.8f;
        if (i == kSectionFrame) f.sectionChange = 1.0f;
        const int before = r->director().commits();
        const int m = r->step(requested, f, kDt, s, style(kStyle, i == 0)).styleMoments;
        if (i < kDropFrame && r->director().commits() > before) {
            firstLookSeen = true;
            firstLookMoments += m;
        }
        total += m;
        if (i == kDropFrame) {
            atDrop = m;
        } else if (i == kSectionFrame) {
            atSection = m;
        } else {
            onlyThere = onlyThere && m == 0;
        }
    }
    check(firstLookSeen && firstLookMoments == 0, "the director's first look is no style moment");
    check(atDrop == 1, "a drop the director commits at is one style moment (got " + std::to_string(atDrop) + ")");
    check(atSection == 1, "a new section the director commits at is one style moment (got " + std::to_string(atSection) + ")");
    check(onlyThere && total == 2, "no other frame is a style moment (total " + std::to_string(total) + ")");
}

// A disabled stage is an exact identity: LFOs, envelopes and routes set to full strength on brightness
// but switched off give the same bits as none set, and add nothing to the luminance drive.
// Kills: a disabled LFO, envelope or route still applied or driving the flash limiter.
void testDisabledStagesAreExactIdentity() {
    const EngineSettings s = richSettings();
    auto bare = std::make_unique<Resolver>();
    auto off = std::make_unique<Resolver>();
    std::array<LfoConfig, LfoEngine::kSlots> lfos{};
    for (LfoConfig& c : lfos) {
        c = sineOn(LfoTarget::Brightness, 2.0f, 0.3f);
        c.enabled = false;
    }
    off->setLfoConfigs(lfos);
    std::array<AdsrConfig, AdsrEngine::kCount> envs{};
    for (AdsrConfig& c : envs) {
        c.enabled = false;
        c.targets = {LfoTarget::Brightness, LfoTarget::Bloom};
        c.amount = 2.0f;
        c.gateThreshold = 0.01f;
    }
    off->setAdsrConfigs(envs);
    std::array<ReactRoute, ReactionEngine::kRoutes> routes{};
    for (ReactRoute& r : routes) {
        r = route(ReactSource::Level, LfoTarget::Brightness, 2.0f);
        r.enabled = false;
    }
    off->setReactionRoutes(routes);
    const SceneParams requested;
    float t = 0.0f;
    bool same = true;
    bool noDrive = true;
    for (int i = 0; i < 600; ++i, t += kDt) {
        const GeodeSoundFrame f = frame(t);
        const StyleContext st = style(kStyle, i == 0);
        const Resolver::Frame& a = bare->step(requested, f, kDt, s, st);
        const Resolver::Frame& b = off->step(requested, f, kDt, s, st);
        same = same && sameBits(wire(a.params), wire(b.params));
        for (float d : b.luminanceDrive) noDrive = noDrive && d == 0.0f;
    }
    check(same, "LFOs, envelopes and routes switched off change no bit of the params");
    check(noDrive, "switched-off LFOs and envelopes add nothing to the luminance drive");
}

// The note layer switched off is exact identity whatever the play's entropy: the knobs sit at exactly 0.5,
// no tint, and the params match bit for bit. Switched on, a new entropy is a new personality.
// Kills: the note layer configured on whatever the switch, the entropy left out of the note seed.
void testNoteLayerOffIgnoresEntropyOnHearsIt() {
    EngineSettings a = richSettings();
    a.notesOn = false;
    EngineSettings b = a;
    b.noteEntropy = ~a.noteEntropy;
    auto ra = std::make_unique<Resolver>();
    auto rb = std::make_unique<Resolver>();
    const SceneParams requested;
    float t = 0.0f;
    bool same = true;
    bool rest = true;
    for (int i = 0; i < 600; ++i, t += kDt) {
        const GeodeSoundFrame f = frame(t);
        const StyleContext st = style(kStyle, i == 0);
        const Wire wa = wire(ra->step(requested, f, kDt, a, st).params);
        const Wire wb = wire(rb->step(requested, f, kDt, b, st).params);
        same = same && sameBits(wa, wb);
        const viz::NoteUniforms& u = ra->noteUniforms();
        for (float k : u.knobs) rest = rest && k == 0.5f;
        rest = rest && u.amount == 0.0f && u.tint[3] == 0.0f;
    }
    check(same, "with the note layer off, the play's entropy changes nothing");
    check(rest, "with the note layer off its knobs sit at exactly 0.5 and there is no tint");

    a.notesOn = true;
    b.notesOn = true;
    auto ca = std::make_unique<Resolver>();
    auto cb = std::make_unique<Resolver>();
    t = 0.0f;
    bool differ = false;
    for (int i = 0; i < 600; ++i, t += kDt) {
        GeodeSoundFrame f = frame(t);
        if (i == 400) f.sectionChange = 1.0f;
        const StyleContext st = style(kStyle, i == 0);
        const Wire wa = wire(ca->step(requested, f, kDt, a, st).params);
        const Wire wb = wire(cb->step(requested, f, kDt, b, st).params);
        differ = differ || !sameBits(wa, wb) ||
                 std::memcmp(&ca->noteUniforms(), &cb->noteUniforms(), sizeof(viz::NoteUniforms)) != 0;
    }
    check(differ, "with the note layer on, a new play's entropy gives the notes a new personality");
}

// A live renderer and an export render share one process: three resolvers stepped in turn (live at
// jittered frame times; the export at a fixed 1/30 s with its own pinned settings; and a live one whose
// frames happen to come at that same fixed 1/30 s, given the export's settings) each give exactly the
// frames they give alone, and the steady live one equals the export bit for bit.
// Kills: any state the step keeps outside the resolver (a static commit counter, a shared stage).
void testLiveAndExportNeverHearEachOther() {
    const EngineSettings live = richSettings();
    EngineSettings pinned = richSettings();
    pinned.director.amount = 1.0f;
    pinned.director.seed = 99;
    pinned.reducedMotion = true;
    pinned.knobs = MacroKnobs{-0.5f, 0.8f, 0.0f, -0.2f, 0.9f, -1.0f};
    pinned.noteEntropy = 0xC0FFEEull;
    constexpr int kFrames = 600;
    Rng g(2026);
    std::vector<float> liveDt(kFrames);
    for (float& d : liveDt) d = g.range(0.008f, 0.038f);
    const float exportDt = 1.0f / 30.0f;
    auto sound = [](int i, float t) {
        GeodeSoundFrame f = frame(t);
        if (i == 450) f.drop = 0.8f;
        return f;
    };
    const SceneParams requested;
    std::vector<Wire> liveSolo;
    std::vector<Wire> exportSolo;
    {
        auto r = std::make_unique<Resolver>();
        configureRich(*r);
        float t = 0.0f;
        for (int i = 0; i < kFrames; ++i) {
            const float dt = liveDt[static_cast<size_t>(i)];
            liveSolo.push_back(wire(r->step(requested, sound(i, t), dt, live, style(kStyle, i == 0)).params));
            t += dt;
        }
    }
    {
        auto r = std::make_unique<Resolver>();
        configureRich(*r);
        for (int i = 0; i < kFrames; ++i) {
            const float t = static_cast<float>(i) * exportDt;
            exportSolo.push_back(wire(r->step(requested, sound(i, t), exportDt, pinned, style(kOtherStyle, i == 0)).params));
        }
    }
    auto a = std::make_unique<Resolver>();
    auto b = std::make_unique<Resolver>();
    auto c = std::make_unique<Resolver>();
    configureRich(*a);
    configureRich(*b);
    configureRich(*c);
    float ta = 0.0f;
    bool liveSame = true;
    bool exportSame = true;
    bool steadySame = true;
    for (int i = 0; i < kFrames; ++i) {
        const size_t n = static_cast<size_t>(i);
        const float te = static_cast<float>(i) * exportDt;
        liveSame = liveSame && sameBits(liveSolo[n], wire(a->step(requested, sound(i, ta), liveDt[n], live, style(kStyle, i == 0)).params));
        exportSame = exportSame && sameBits(exportSolo[n], wire(b->step(requested, sound(i, te), exportDt, pinned, style(kOtherStyle, i == 0)).params));
        steadySame = steadySame && sameBits(exportSolo[n], wire(c->step(requested, sound(i, te), exportDt, pinned, style(kOtherStyle, i == 0)).params));
        ta += liveDt[n];
    }
    check(liveSame, "a live resolver stepped beside others gives the frames it gives alone");
    check(exportSame, "an export resolver stepped beside a live one gives the frames it gives alone");
    check(steadySame, "live frames at a fixed 1/30 s with the export's settings equal the export's, bit for bit");
}

}  // namespace

int main() {
    testOrder();
    testReducedMotionOnce();
    testPinnedAgainstGlobals();
    testDeterministic();
    testSafetyLast();
    testParityWithTheOldChain();
    testPresetMorphGlidesForThreeTimesItsLength();
    testParamFadeCutsLandsAndGlides();
    testReducedMotionHasTheLastWordOnSpeed();
    testStyleMomentsOnlyAtDropsAndSections();
    testDisabledStagesAreExactIdentity();
    testNoteLayerOffIgnoresEntropyOnHearsIt();
    testLiveAndExportNeverHearEachOther();
    if (failures) {
        std::printf("resolver_test: %d failed\n", failures);
        return 1;
    }
    std::printf("resolver_test: all passed\n");
    return 0;
}
