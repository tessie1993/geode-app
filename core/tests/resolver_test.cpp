// Host test of the scene params resolver (signal/Resolver), the chain as a whole rather than each stage
// alone: that the director's look lies under the macro bank and an LFO moves on top of both, that reduced
// motion is applied exactly once, that a resolver given explicit engine settings never hears the process
// globals, that an export's settings latch (signal/EngineSettings, #350) holds its pin while the globals flip
// and decides the export's director as the free-taste rule says, that two resolvers given the same inputs
// give the same bits over 600 frames, and that no stage can push the luminance past safety::apply, which
// runs last. Behaviour, not golden numbers: the stages' own maths is pinned by viz_math_test, director_test
// and note_response_test.
// Then, for #528's mutants: parity with the old chain (main's Renderer::resolveParams before #344) bit for
// bit over seeded random setups, the preset morph and param fade by hand-worked frames, the motion field
// before the clamp, the style moments, disabled stages as exact identity, and live beside export.
// Then the macro bank (viz/MacroBank, process-wide through geode_macro_bank_set): its offset reaches the
// resolved params, an export's pin keeps the bank it was pinned with, and the director never touches it.
// Then a wave and an envelope on a macro knob (LfoTarget::Macro): they turn the knob where the bank runs, so the
// knob's links move, the user's turn is never rewritten, safety stays last, 30/60/120 fps agree, silence holds still;
// and "On a moment" (MomentWatch): a moment restarts a wave's cycle and fires an envelope, the same at any frame rate.
// Built by CI (.github/workflows/android.yml, "Native host tests") with core/signal/Resolver.cpp,
// core/signal/EngineSettings.cpp, core/sound/*.cpp and the GL-free stage sources it links (no GL header anywhere).
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
#include "viz/CustomizeMap.hpp"
#include "viz/Director.hpp"
#include "viz/MacroBank.hpp"
#include "viz/MotionField.hpp"
#include "viz/NoteResponse.hpp"
#include "viz/Palette.hpp"
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
using geode::viz::ReactionEngine;
using geode::viz::ReactRoute;
using geode::viz::ReactSource;
using geode::viz::SceneParams;
namespace safety = geode::viz::safety;
namespace hand = geode::viz::hand;
namespace macrobank = geode::viz::macrobank;

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
    f.drive = 0.8f;  // a loud, dense song: full reach
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

// The four built-in knobs (Shape, Motion, Colour, Depth) turned to `values`, in bank order.
macrobank::Bank turned(std::initializer_list<float> values) {
    macrobank::Bank b = macrobank::defaultBank();
    size_t i = 0;
    for (float v : values) {
        if (i < b.knobs.size()) b = macrobank::setValue(b, b.knobs[i].id, v);
        ++i;
    }
    return b;
}

std::shared_ptr<const macrobank::Bank> shared(const macrobank::Bank& b) { return std::make_shared<const macrobank::Bank>(b); }

int knobId(const char* name) {
    for (const auto& k : macrobank::defaultBank().knobs) {
        if (k.name == name) return k.id;
    }
    std::printf("no knob %s\n", name);
    std::exit(1);
}

StyleContext style(const std::string& id, bool switched) {
    StyleContext s;
    s.id = &id;
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

// The director's look under the macro bank, each added to the user's value (base plus modulation): Colour +1
// takes the saturation up by its link's reach (0.25 of the range), to the top where there was less than that
// to give. An LFO on saturation then shows on top of both. No route is set, so nothing else writes saturation.
void testOrder() {
    EngineSettings s;
    s.director = {1.0f, 42};
    s.bank = shared(macrobank::setValue(macrobank::defaultBank(), knobId("Colour"), 1.0f));
    EngineSettings centred = s;
    centred.bank = shared(macrobank::defaultBank());
    const macrobank::Range sat = macrobank::rangeOf(LfoTarget::Saturation);
    const float reach = 0.25f * (sat.max - sat.min);
    auto plain = std::make_unique<Resolver>();
    auto lfo = std::make_unique<Resolver>();
    auto unturned = std::make_unique<Resolver>();
    bool raised = true;
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
        const SceneParams& c = unturned->step(requested, f, kDt, centred, st).params;
        if (t >= 38.0f) {
            raised = raised && (a.saturation >= sat.max - 1e-6f || std::fabs(a.saturation - (c.saturation + reach)) < 1e-4f);
            maxPlain = std::max(maxPlain, std::fabs(a.saturation));
            maxLfo = std::max(maxLfo, b.saturation);
            lookInPlace = lookInPlace && a.camSmooth != requested.camSmooth && c.saturation == requested.saturation;
        }
    }
    check(plain->director().commits() >= 1 && plain->director().weight() > 0.9f, "the director is fully in");
    check(lookInPlace, "the director's look reaches the final params, and leaves the saturation the user's");
    check(raised, "the bank lies over the director's look (Colour +1 takes the saturation up, peak " + std::to_string(maxPlain) + ")");
    check(maxLfo > 0.2f, "an LFO moves on top of the director and the bank (peak " + std::to_string(maxLfo) + ")");
}

// The owner's two rules, held on the whole chain with Auto and Note & key reactions both on.
//
// "No colour narrowing": every colour param the scene is drawn with is the one the user set, through a
// song with kicks, a melody, a drop and a new section. (Hue drift is the user's own Motion dial; it is
// off here, and its own maths is viz_math_test's.)
//
// "No hardcoded reactions": with no route set and the Motion dials at 0, the params do not hear the
// frame at all. Two resolvers get the same feeling and the same decisions (key, sections, a drop) but
// one hears drums, a melody, a build and chord changes and the other a flat tone: their params are equal
// bit for bit. One reaction route (the user's, or the style's DNA: the same engine) is what makes the
// picture move.
GeodeSoundFrame flat(float t) {
    GeodeSoundFrame f = frame(t);
    f.onset = f.onsetEnv = 0.0f;
    f.bass = f.bassAtt = 1.0f;
    f.low = 0.5f;
    f.pitchClass = 0.0f;
    f.pitchHeight = 0.5f;
    f.pitch = 60.0f;
    f.noteChange = 0.0f;
    return f;
}

GeodeSoundFrame busy(float t, int i) {
    GeodeSoundFrame f = frame(t);
    f.kick = f.onset;
    f.kickEnv = f.onsetEnv;
    f.snare = i % 24 == 12 ? 1.0f : 0.0f;
    f.snareEnv = f.snare;
    f.hat = i % 6 == 0 ? 0.7f : 0.0f;
    f.kickRandom = 0.37f * static_cast<float>(i % 7);
    f.interval = i % 2 == 0 ? 0.6f : -0.2f;
    f.harmonicChange = i % 150 == 75 ? 0.8f : 0.0f;
    f.buildUp = static_cast<float>(i % 600) / 600.0f;
    f.tension = 0.5f + 0.4f * std::sin(0.01f * static_cast<float>(i));
    f.intensity = 0.5f + 0.5f * std::sin(0.02f * static_cast<float>(i));
    f.stereoWidth = 0.6f;
    f.stereoPan = 0.5f * std::sin(0.03f * static_cast<float>(i));
    f.treble = f.trebleAtt = 1.0f + 0.8f * std::fabs(std::sin(0.2f * static_cast<float>(i)));
    f.mid = f.midAtt = 1.0f + 0.6f * std::fabs(std::sin(0.13f * static_cast<float>(i)));
    f.vol = f.volAtt = 1.0f + 0.5f * f.onsetEnv;
    f.hardCut = i % 400 == 399 ? 1.0f : 0.0f;
    f.presence = 1.0f;
    return f;
}

void testNoColourNarrowingNoFixedReactions() {
    EngineSettings s;
    s.director = {1.0f, 42};
    s.notesOn = true;
    s.noteEntropy = 0xABCDull;
    SceneParams requested;
    requested.palette = 6;
    requested.hueRange = 0.7f;
    requested.saturation = 0.6f;
    requested.brightness = 1.1f;
    requested.temperature = -0.2f;
    requested.contrast = 0.9f;
    requested.colorShift = 0.15f;
    requested.milkdropPaletteTint = 0.3f;
    requested.motionHue = 0.0f;
    requested.paletteStop0 = 0x2244FF;
    requested.paletteStop1 = 0xFF8800;
    requested.paletteStop2 = 0x10E0A0;
    requested.paletteAmount0 = 0.8f;
    requested.paletteAmount1 = 0.3f;
    requested.paletteShift = 0.2f;
    static const char* const kColour[] = {"palette", "paletteBaseOverride", "paletteRangeOverride", "hueRange", "saturation",
                                          "brightness", "temperature", "contrast", "colorShift", "milkdropPaletteTint",
                                          "paletteStop0", "paletteStop1", "paletteStop2",
                                          "paletteAmount0", "paletteAmount1", "paletteAmount2", "paletteAmount3", "paletteAmount4",
                                          "paletteAmount5", "paletteAmount6", "paletteAmount7", "paletteShift"};
    geode::sound::Emotion::State feeling;
    feeling.valence = 0.5f;
    feeling.energy = 0.8f;
    feeling.tension = 0.4f;
    feeling.confidence = 0.9f;
    feeling.gems[geode::sound::Emotion::kJoy] = 0.6f;
    feeling.gems[geode::sound::Emotion::kPower] = 0.4f;

    auto coloured = std::make_unique<Resolver>();
    std::string strayed;
    float knobTravel = 0.0f;
    const int frames = static_cast<int>(40.0f / kDt);
    float t = 0.0f;
    for (int i = 0; i < frames; ++i, t += kDt) {
        GeodeSoundFrame f = busy(t, i);
        if (i == 1200) f.drop = 1.0f;
        if (i == 1800) f.sectionChange = 1.0f;
        const SceneParams& p = coloured->step(requested, f, kDt, s, style(kStyle, i == 0), &feeling).params;
        for (const char* name : kColour) {
            if (strayed.empty() && p.get(name) != requested.get(name)) strayed = name;
        }
        for (float k : coloured->noteUniforms().knobs) knobTravel = std::max(knobTravel, std::fabs(k - 0.5f));
    }
    check(coloured->director().commits() >= 3 && coloured->director().weight() > 0.9f, "the director decided, at the drop and the section too");
    check(coloured->notes().amount() > 0.9f && knobTravel > 0.2f, "the note layer is in and its knobs move");
    check(strayed.empty(), "with Auto and Note & key on every colour param is the user's (strayed: " + strayed + ")");

    SceneParams still = requested;
    still.motionAmount = still.motionBreath = still.motionDrift = still.motionHue = 0.0f;
    for (const bool routed : {false, true}) {
        auto heard = std::make_unique<Resolver>();
        auto deaf = std::make_unique<Resolver>();
        if (routed) {
            std::array<ReactRoute, ReactionEngine::kRoutes> routes{};
            routes[0] = route(ReactSource::Bass, LfoTarget::Zoom, 0.6f);
            heard->setReactionRoutes(routes);
            deaf->setReactionRoutes(routes);
        }
        bool same = true;
        std::string moved;
        t = 0.0f;
        for (int i = 0; i < frames; ++i, t += kDt) {
            GeodeSoundFrame a = busy(t, i);
            GeodeSoundFrame b = flat(t);
            // The decisions are the same in both: the hard cut, the drop and the section are what a look is made at.
            b.hardCut = a.hardCut;
            if (i == 1200) a.drop = b.drop = 1.0f;
            if (i == 1800) a.sectionChange = b.sectionChange = 1.0f;
            const Resolver::Frame& fa = heard->step(still, a, kDt, s, style(kStyle, i == 0), &feeling);
            const Resolver::Frame& fb = deaf->step(still, b, kDt, s, style(kStyle, i == 0), &feeling);
            if (!sameBits(wire(fa.params), wire(fb.params))) {
                if (same) {
                    for (const auto& spec : SceneParams::schema()) {
                        if (fa.params.get(spec.name) != fb.params.get(spec.name)) moved += std::string(spec.name) + " ";
                    }
                }
                same = false;
            }
            same = same && std::memcmp(&fa.cameraMoves, &fb.cameraMoves, sizeof(fa.cameraMoves)) == 0;
        }
        if (routed) {
            check(!same && moved == "zoom ", "a reaction route is what moves the picture, and only its target (moved: " + moved + ")");
        } else {
            check(heard->director().commits() >= 3, "the director decided through it");
            check(same, "with no route and the Motion dials at 0 no param hears the drums, the melody or a build (moved: " + moved + ")");
        }
    }
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

// A rich setup, so every stage runs: the director, notes, LFOs, an envelope, user routes and the bank.
EngineSettings richSettings() {
    EngineSettings s;
    s.director = {1.0f, 7};
    s.bank = shared(turned({0.4f, -0.3f, 0.6f, -0.5f}));
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
        macrobank::setGlobal(turned({x, -x, x, -x}));
        geode::viz::note_response::setGlobal(i % 2 == 0);
        hand::setGlobal(i % 2 == 0 ? hand::kAll : 0u, true);
        geode::viz::director::setGlobal(i % 2 != 0, i % 2 == 0 ? 0.0f : 1.0f, static_cast<uint64_t>(i));
        same = same && sameBits(calm[static_cast<size_t>(i)], wire(r->step(requested, frame(t), kDt, s, style(kStyle, i == 0)).params));
    }
    macrobank::setGlobal(macrobank::defaultBank());
    geode::viz::note_response::setGlobal(false);
    hand::setGlobal(0u, false);
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

// A pinned latch (an export, #350) keeps the macro bank, the Note & key switch, the hand mask and the director it
// was pinned with, with no note entropy, while every process global flips each frame; a live latch follows
// the globals. Reduced motion is the renderer's own and passes through either way.
void testLatchPinnedAgainstGlobals() {
    using geode::signal::SettingsLatch;
    namespace director = geode::viz::director;
    const macrobank::Bank knobs = turned({0.5f, -0.5f, 0.25f, 1.0f});
    const std::uint32_t snare = hand::bit(geode::viz::Moment::Snare);
    macrobank::setGlobal(knobs);
    geode::viz::note_response::setGlobal(true);
    hand::setGlobal(snare, true);
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
        const macrobank::Bank now = turned({x, -x, x, -x});
        macrobank::setGlobal(now);
        geode::viz::note_response::setGlobal(i % 2 == 0);
        hand::setGlobal(i % 2 == 0 ? hand::kAll : 0u, true);
        director::setGlobal(i % 2 != 0, i % 2 == 0 ? 0.0f : 1.0f, static_cast<uint64_t>(i + 100));
        const bool reduced = i % 2 == 0;
        const EngineSettings p = pinned.latch(reduced);
        held = held && p.bank && *p.bank == knobs && p.notesOn && p.handMask == snare && p.director.amount == 1.0f &&
               p.director.seed == 42 && p.noteEntropy == 0 && p.reducedMotion == reduced && pinned.handMask() == snare;
        const EngineSettings l = live.latch(reduced);
        followed = followed && l.bank && *l.bank == now && l.notesOn == (i % 2 == 0) && l.handMask == (i % 2 == 0 ? hand::kAll : 0u) &&
                   l.director.amount == (i % 2 == 0 ? 0.0f : 1.0f) && l.director.seed == static_cast<uint64_t>(i + 100) &&
                   l.reducedMotion == reduced && live.handMask() == (i % 2 == 0 ? hand::kAll : 0u);
    }
    macrobank::setGlobal(macrobank::defaultBank());
    geode::viz::note_response::setGlobal(false);
    hand::setGlobal(0u, false);
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
    s.bank = shared(turned({1.0f, 1.0f, 1.0f, 1.0f}));
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
    const float top = 1.0f + safety::kMaxFlashDepth + 1e-6f;
    float t = 0.0f;
    bool held = true;
    for (int i = 0; i < 600; ++i, t += kDt) {
        const SceneParams& p = r->step(requested, frame(t), kDt, s, style(kStyle, i == 0)).params;
        held = held && p.brightness <= top && p.intensity <= top && p.contrast <= top &&
               p.bloom <= safety::kMaxBloom + 1e-6f &&
               p.brightness >= 0.0f && p.intensity >= 0.0f && p.contrast >= 0.0f;
    }
    check(held, "no stage pushes brightness, intensity, contrast or bloom past safety::apply");
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

// The per-frame chain as Renderer::resolveParams ran it before #344, the stages called in that order on
// stages of its own, less what changed since: the owner's rules took out the director's palette and routes
// and the note layer's params; the old macro knobs became the director's axes and then the user's bank
// (step 2 of the macro bank); the camera's moves are director.moves(). The oracle takes the bank's reach per
// frame from the style id, where the resolver caches it per style, so the cache is held to it too. The
// parity property below holds the resolver to it bit for bit.
struct OldChain {
    viz::LfoEngine lfo;
    viz::AdsrEngine adsr;
    viz::ReactionEngine reactions;
    viz::Director director;
    int directorCommitsSeen = 0;
    viz::NoteResponse notes;
    viz::MotionField motionField;
    SceneParams displayedParams;
    float morphFadeSec = 0.0f;
    float morphRemainSec = 0.0f;
    std::array<float, LfoEngine::kSlots> envRate{};
    std::array<float, LfoEngine::kSlots> envDepth{};
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
        director.reset();
        notes.reset();
    }
    // Renderer::resolveActiveScene, on a new style.
    void sceneSwitched(const std::string& id) { motionField.setVariant(viz::styleVariantSeed(id)); }

    void resolve(const SceneParams& requested, const GeodeSoundFrame& soundFrame, float dt, const EngineSettings& s, float spin,
                 const std::string& styleId) {
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
        director.step(soundFrame, dt);
        styleMoments = 0;
        if (director.commits() != directorCommitsSeen) {
            directorCommitsSeen = director.commits();
            // A section or drop the director commits at, only while it drives (Director::styleMoment).
            if (director.styleMoment()) styleMoments += 1;
        }
        notes.configure({s.notesOn, directorConfig.seed ^ s.noteEntropy});
        {
            const SceneParams& own = displayedParams;
            viz::NoteResponse::Inputs in;
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
        SceneParams p = director.applyAxes(director.apply(displayedParams));
        if (s.bank) p = macrobank::apply(p, *s.bank, styleId);
        p = lfo.apply(p, lfoValues);
        p = AdsrEngine::apply(p, adsr.configs, envValues);
        reactions.tick(soundFrame, dt);
        p = reactions.apply(p);
        motionField.step(soundFrame, dt);
        p = motionField.apply(p, s.reducedMotion, spin);
        p = safety::apply(p, s.reducedMotion);
        params = p;
        // Renderer::stepCamera, after the chain.
        cameraMoves = director.moves();
    }
};

// Targets the random setups draw from: the director's axis routes' own (Bloom, Speed, Morph, DriftX, Twist,
// ChromaAb) and Temperature, the luminance ones, the camera and dive speeds, and three chained (LFO rate and depth) targets,
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
    macrobank::Bank bank = macrobank::defaultBank();
    for (const auto& k : macrobank::defaultBank().knobs) bank = macrobank::setValue(bank, k.id, g.coin() ? 0.0f : g.range(-1.0f, 1.0f));
    if (g.below(8) != 0) s.bank = shared(bank);
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
    g.unit();  // once the retired Palette blend's draw; drawn still, so the runs keep their sequence
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
// note uniforms) over 16 seeded runs of 900 frames, with configs, settings, requests,
// style switches, preset morphs and new surfaces changing at random. Kills any two stages swapped (director/
// director/axes/bank/LFO nesting, LFO/envelope, envelope/reactions, reactions/motion
// field/safety), any stage dropped, the morph's x3 and <= 0, the fade's 0.01 cut, max, clamp and dt/fade,
// the note seed's ^ entropy, the note inputs read from the faded
// params, the style moments' Section || Drop and their reset each frame, the
// camera moves read from the director's axes, the bank's reach per style, reducedMotion and spin passed to the motion field, the style
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
                g.coin();  // once the palette wheel's pick; drawn still, so the runs keep their sequence
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
            StyleContext st;
            st.id = id;
            st.spin = spin;
            st.switched = switched;
            const Resolver::Frame& a = now->step(requested, f, dt, s, st);
            old->resolve(requested, f, dt, s, spin, id != nullptr ? *id : std::string());
            moments += a.styleMoments;
            same = same && sameBits(wire(a.params), wire(old->params)) && a.styleMoments == old->styleMoments &&
                   std::memcmp(&a.cameraMoves, &old->cameraMoves, sizeof(a.cameraMoves)) == 0 &&
                   std::memcmp(&now->noteUniforms(), &old->notes.uniforms(), sizeof(viz::NoteUniforms)) == 0;
            const SceneParams& p = a.params;
            const bool capped = p.brightness <= top && p.intensity <= top && p.contrast <= top &&
                                p.bloom <= safety::kMaxBloom + 1e-6f &&
                                p.brightness >= 0.0f && p.intensity >= 0.0f && p.contrast >= 0.0f &&
                                (!s.reducedMotion || p.speed <= 4.0f * 0.4f + 1e-6f);
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
// field only the director, the bank and the routes write, so it shows the fade alone.
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

// In silence the renderer's step is 0 (no sound, no movement), but a slider moved then still arrives: the
// glide runs on the wall step the renderer hands over beside it.
void testAStoppedClockStillGlidesToTheRequest() {
    const EngineSettings off;
    Resolver r;
    SceneParams requested;
    requested.saturation = 0.0f;
    requested.paramFadeSec = 0.5f;
    const float first = r.step(requested, frame(0.0f), 0.0f, off, style(kStyle, true), nullptr, 0.1f).params.saturation;
    const float second = r.step(requested, frame(0.0f), 0.0f, off, style(kStyle, false), nullptr, 0.1f).params.saturation;
    check(std::fabs(first - 0.8f) < 1e-6f && std::fabs(second - 0.64f) < 1e-6f,
          "a stopped clock glides on the wall step (got " + std::to_string(first) + ", " + std::to_string(second) + ")");
    Resolver held;
    const float stuck = held.step(requested, frame(0.0f), 0.0f, off, style(kStyle, true)).params.saturation;
    check(stuck == 1.0f, "with no wall step the glide follows dt (got " + std::to_string(stuck) + ")");
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
// but switched off give the same bits as none set.
// Kills: a disabled LFO, envelope or route still applied.
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
    for (int i = 0; i < 600; ++i, t += kDt) {
        const GeodeSoundFrame f = frame(t);
        const StyleContext st = style(kStyle, i == 0);
        const Resolver::Frame& a = bare->step(requested, f, kDt, s, st);
        const Resolver::Frame& b = off->step(requested, f, kDt, s, st);
        same = same && sameBits(wire(a.params), wire(b.params));
    }
    check(same, "LFOs, envelopes and routes switched off change no bit of the params");
}

// The note layer switched off is exact identity whatever the play's entropy: the knobs sit at exactly 0.5
// and the params match bit for bit. Switched on, a new entropy swings the chord changes another way (the
// SPACE knob), and still changes no param: the layer writes none.
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
        rest = rest && u.amount == 0.0f;
    }
    check(same, "with the note layer off, the play's entropy changes nothing");
    check(rest, "with the note layer off its knobs sit at exactly 0.5");

    a.notesOn = true;
    b.notesOn = true;
    auto ca = std::make_unique<Resolver>();
    auto cb = std::make_unique<Resolver>();
    t = 0.0f;
    bool differ = false;
    bool paramsSame = true;
    for (int i = 0; i < 600; ++i, t += kDt) {
        GeodeSoundFrame f = frame(t);
        if (i % 120 == 60) f.harmonicChange = 0.7f;
        const StyleContext st = style(kStyle, i == 0);
        const Wire wa = wire(ca->step(requested, f, kDt, a, st).params);
        const Wire wb = wire(cb->step(requested, f, kDt, b, st).params);
        paramsSame = paramsSame && sameBits(wa, wb);
        differ = differ || std::memcmp(&ca->noteUniforms(), &cb->noteUniforms(), sizeof(viz::NoteUniforms)) != 0;
    }
    check(differ, "with the note layer on, a new play's entropy swings the chord changes another way");
    check(paramsSame, "and the note layer still writes no param");
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
    pinned.bank = shared(turned({-0.5f, 0.8f, 0.0f, -1.0f}));
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

// The bank's offset reaches the resolved params whole: with nothing else writing Warp, Shape at a full turn
// up moves it by its link's weight (0.8) times Warp's range over the same chain at centre, and a turn down
// (Shape has no Warp down) leaves it. A knob whose links do not reach the style moves nothing there.
void testBankReachesTheParams() {
    EngineSettings centred;
    centred.bank = shared(macrobank::defaultBank());
    EngineSettings up = centred;
    up.bank = shared(macrobank::setValue(macrobank::defaultBank(), knobId("Shape"), 1.0f));
    EngineSettings down = centred;
    down.bank = shared(macrobank::setValue(macrobank::defaultBank(), knobId("Shape"), -1.0f));
    EngineSettings none;
    auto a = std::make_unique<Resolver>();
    auto b = std::make_unique<Resolver>();
    auto c = std::make_unique<Resolver>();
    auto d = std::make_unique<Resolver>();
    const macrobank::Range warp = macrobank::rangeOf(LfoTarget::Warp);
    const float reach = 0.8f * (warp.max - warp.min);
    SceneParams requested;
    requested.warp = 0.1f;
    float t = 0.0f;
    bool whole = true;
    bool untouchedDown = true;
    bool identity = true;
    for (int i = 0; i < 300; ++i, t += kDt) {
        const GeodeSoundFrame f = frame(t);
        const StyleContext st = style(kStyle, i == 0);
        const SceneParams pa = a->step(requested, f, kDt, centred, st).params;
        const SceneParams pb = b->step(requested, f, kDt, up, st).params;
        const SceneParams pc = c->step(requested, f, kDt, down, st).params;
        const SceneParams pd = d->step(requested, f, kDt, none, st).params;
        whole = whole && std::fabs((pb.warp - pa.warp) - std::min(reach, warp.max - pa.warp)) < 1e-5f;
        untouchedDown = untouchedDown && pc.warp == pa.warp && pc.twist < pa.twist;
        identity = identity && sameBits(wire(pa), wire(pd));
    }
    check(whole, "Shape up adds its whole Warp offset to the resolved params");
    check(untouchedDown, "Shape down leaves Warp (no down weight) and turns Twist back");
    check(identity, "a bank at centre resolves the same bits as no bank");
    // Reach: a link on a target the style cannot show is skipped, so the knob is a no-op there.
    check(macrobank::reachMask(kStyle) != 0, "the bank reaches the shader style");
}

// An export pins the bank it was prepared with: frames resolved from a pinned latch while the live bank flips
// every frame equal the frames of a resolver handed that bank outright, and a live latch follows the flips.
void testExportPinKeepsItsBank() {
    using geode::signal::SettingsLatch;
    const macrobank::Bank atPrepare = turned({0.7f, -0.4f, 0.9f, 0.3f});
    macrobank::setGlobal(atPrepare);
    SettingsLatch pinned;
    pinned.pinForExport(true, 30'000);
    SettingsLatch live;
    EngineSettings fixed = geode::signal::exportSettings(true, 30'000);
    fixed.bank = shared(atPrepare);
    auto a = std::make_unique<Resolver>();
    auto b = std::make_unique<Resolver>();
    auto c = std::make_unique<Resolver>();
    const SceneParams requested;
    float t = 0.0f;
    bool same = true;
    bool heard = false;
    for (int i = 0; i < 300; ++i, t += kDt) {
        const float x = (i % 2 == 0) ? 1.0f : -1.0f;
        macrobank::setGlobal(turned({x, x, x, x}));
        const GeodeSoundFrame f = frame(t);
        const StyleContext st = style(kStyle, i == 0);
        const EngineSettings sp = pinned.latch(false);
        const EngineSettings sl = live.latch(false);
        const Wire wa = wire(a->step(requested, f, kDt, sp, st).params);
        const Wire wb = wire(b->step(requested, f, kDt, fixed, st).params);
        const Wire wc = wire(c->step(requested, f, kDt, sl, st).params);
        same = same && sameBits(wa, wb);
        heard = heard || !sameBits(wa, wc);
    }
    macrobank::setGlobal(macrobank::defaultBank());
    check(same, "an export's frames keep the bank pinned at prepare while the live bank changes");
    check(heard, "a live renderer hears the bank's edits");
}

// The director writes its axes on the params and never the user's bank: a full director run through a live
// latch leaves the process-wide bank the very value that was set, and the frame's copy unchanged.
// The music at a measured tempo (77, 93, 141 or 174: never a tempo the engine assumes), the happy song's levels.
GeodeSoundFrame musicAt(float t, float bpm) {
    GeodeSoundFrame f = frame(t);
    f.bpm = bpm;
    return f;
}

float maxOf(const std::vector<float>& v) { return *std::max_element(v.begin(), v.end()); }
float minOf(const std::vector<float>& v) { return *std::min_element(v.begin(), v.end()); }

// A wave on the Colour knob moves what the knob links (saturation among them), on top of the user's turn, which
// stays where the user left it; the knob's live modulation says a wave turns it.
void testAWaveTurnsAKnob() {
    const int colour = knobId("Colour");
    EngineSettings s;
    s.bank = shared(macrobank::setValue(macrobank::defaultBank(), colour, 0.2f));
    std::array<LfoConfig, LfoEngine::kSlots> configs{};
    configs[0] = sineOn(LfoTarget::Macro, 0.5f, 2.0f);
    configs[0].knob = colour;
    auto waved = std::make_unique<Resolver>();
    auto still = std::make_unique<Resolver>();
    waved->setLfoConfigs(configs);
    const SceneParams requested;
    std::vector<float> sat;
    std::vector<float> steady;
    float t = 0.0f;
    for (int i = 0; i < 240; ++i, t += kDt) {
        const GeodeSoundFrame f = musicAt(t, 93.0f);
        sat.push_back(waved->step(requested, f, kDt, s, style(kStyle, i == 0)).params.saturation);
        steady.push_back(still->step(requested, f, kDt, s, style(kStyle, i == 0)).params.saturation);
        const auto* mod = waved->frame().knobMods.of(colour);
        check(mod != nullptr && mod->sources == macrobank::KnobMods::kWave, "the knob's modulation says a wave turns it");
        check(mod != nullptr && std::fabs(mod->add) <= 0.5f + 1e-5f, "by no more than the wave's depth");
        check(mod != nullptr && mod->lo == -0.5f && mod->hi == 0.5f, "and says the range it can reach, both ways");
        const SceneParams& p = waved->frame().params;
        check(p.bloom <= safety::kMaxBloom + 1e-6f && p.intensity <= 1.0f + safety::kMaxFlashDepth + 1e-6f,
              "safety still has the last word on what the knob moves");
        check(still->frame().knobMods.count == 0, "no slot on a knob, no modulation");
    }
    check(maxOf(sat) - minOf(sat) > 0.05f, "the wave on the knob moves the knob's saturation link");
    check(maxOf(steady) - minOf(steady) < 1e-6f, "without it the same turn holds still");
    check(s.bank->knobs[static_cast<size_t>(colour - 1)].value == 0.2f, "the user's turn is never rewritten");
    // A slot on a knob the bank does not have, or with no knob, moves nothing.
    configs[0].knob = 4242;
    auto lost = std::make_unique<Resolver>();
    lost->setLfoConfigs(configs);
    std::vector<float> none;
    t = 0.0f;
    for (int i = 0; i < 120; ++i, t += kDt) none.push_back(lost->step(requested, musicAt(t, 93.0f), kDt, s, style(kStyle, i == 0)).params.saturation);
    check(maxOf(none) - minOf(none) < 1e-6f, "a wave on a knob that is gone moves nothing");
}

// A wave and an envelope on one knob: both sources show; the envelope alone on another.
void testAnEnvelopeTurnsAKnob() {
    const int depth = knobId("Depth");
    const int colour = knobId("Colour");
    EngineSettings s;
    s.bank = shared(macrobank::defaultBank());
    std::array<AdsrConfig, AdsrEngine::kCount> envs{};
    envs[0].enabled = true;
    envs[0].targets = {LfoTarget::Macro};
    envs[0].knob = depth;
    envs[0].amount = 1.0f;
    envs[1] = envs[0];
    envs[1].knob = colour;
    std::array<LfoConfig, LfoEngine::kSlots> waves{};
    waves[0] = sineOn(LfoTarget::Macro, 0.3f, 3.0f);
    waves[0].knob = colour;
    auto r = std::make_unique<Resolver>();
    auto plain = std::make_unique<Resolver>();
    r->setAdsrConfigs(envs);
    r->setLfoConfigs(waves);
    const SceneParams requested;
    float moved = 0.0f;
    float t = 0.0f;
    for (int i = 0; i < 240; ++i, t += kDt) {
        const GeodeSoundFrame f = musicAt(t, 141.0f);
        const float zoom = r->step(requested, f, kDt, s, style(kStyle, i == 0)).params.zoom;
        moved = std::max(moved, std::fabs(zoom - plain->step(requested, f, kDt, s, style(kStyle, i == 0)).params.zoom));
        const auto* d = r->frame().knobMods.of(depth);
        const auto* c = r->frame().knobMods.of(colour);
        check(d != nullptr && d->sources == macrobank::KnobMods::kEnvelope && d->add >= 0.0f && d->lo == 0.0f && d->hi == 1.0f,
              "an envelope turns its knob up, by up to its amount");
        check(c != nullptr && c->sources == (macrobank::KnobMods::kWave | macrobank::KnobMods::kEnvelope), "a wave and an envelope on one knob");
    }
    check(moved > 0.01f, "the envelope on Depth moves its zoom link with the hits");
}

// The same knob turn at 30, 60 and 120 fps at the same moments of the song.
void testKnobModulationAtAnyFrameRate() {
    const int colour = knobId("Colour");
    EngineSettings s;
    s.bank = shared(macrobank::defaultBank());
    std::array<LfoConfig, LfoEngine::kSlots> configs{};
    configs[0] = sineOn(LfoTarget::Macro, 0.4f, 1.7f);
    configs[0].knob = colour;
    std::array<float, 3> at{};
    const std::array<int, 3> rates = {30, 60, 120};
    for (size_t k = 0; k < rates.size(); ++k) {
        auto r = std::make_unique<Resolver>();
        r->setLfoConfigs(configs);
        const float dt = 1.0f / static_cast<float>(rates[k]);
        const SceneParams requested;
        const int frames = rates[k] * 3;
        for (int i = 0; i < frames; ++i) r->step(requested, musicAt(static_cast<float>(i) * dt, 174.0f), dt, s, style(kStyle, i == 0));
        at[k] = r->frame().knobMods.of(colour)->add;
    }
    check(std::fabs(at[0] - at[1]) < 2e-3f && std::fabs(at[2] - at[1]) < 2e-3f, "a wave turns a knob the same at 30, 60 and 120 fps");
}

// No sound, no movement: the renderer's step is 0 in silence, so a wave on a knob holds and so does the picture.
void testSilenceHoldsAKnobStill() {
    const int colour = knobId("Colour");
    EngineSettings s;
    s.bank = shared(macrobank::defaultBank());
    std::array<LfoConfig, LfoEngine::kSlots> configs{};
    configs[0] = sineOn(LfoTarget::Macro, 0.5f, 1.0f);
    configs[0].knob = colour;
    auto r = std::make_unique<Resolver>();
    r->setLfoConfigs(configs);
    const SceneParams requested;
    for (int i = 0; i < 30; ++i) r->step(requested, musicAt(static_cast<float>(i) * kDt, 77.0f), kDt, s, style(kStyle, i == 0));
    const GeodeSoundFrame silent{};
    const float sat = r->step(requested, silent, 0.0f, s, style(kStyle, false), nullptr, 0.0f).params.saturation;
    const float add = r->frame().knobMods.of(colour)->add;
    bool still = true;
    for (int i = 0; i < 120; ++i) {
        still = still && r->step(requested, silent, 0.0f, s, style(kStyle, false), nullptr, 0.0f).params.saturation == sat;
        still = still && r->frame().knobMods.of(colour)->add == add;
    }
    check(still, "in silence a wave on a knob moves nothing");
}

// Customize › Music's moments as triggers: each moment's trigger, the Step rule's edge, and the same count of moments
// at 30, 60 and 120 fps from one pulse train (a kick at 93 bpm, 60 ms long).
void testMomentWatch() {
    using geode::viz::Moment;
    using geode::viz::MomentWatch;
    check(hand::trigger(Moment::Kick) == ReactSource::Kick && hand::trigger(Moment::Snare) == ReactSource::Snare &&
              hand::trigger(Moment::Drop) == ReactSource::Drop && hand::trigger(Moment::NewSection) == ReactSource::SectionChange &&
              hand::trigger(Moment::ChordChange) == ReactSource::HarmonicChange && hand::trigger(Moment::Note) == ReactSource::NoteChange,
          "a moment is heard on its event");
    check(hand::trigger(Moment::Bass) == ReactSource::Bass && hand::trigger(Moment::BuildUp) == ReactSource::BuildUp,
          "a moment with no event on its first source");
    MomentWatch w;
    std::array<float, geode::viz::kMomentCount> v{};
    const std::uint32_t kick = 1u << static_cast<int>(Moment::Kick);
    check(w.step(v) == 0u, "silence brings no moment");
    v[0] = 1.0f;
    check(w.step(v) == kick, "a rise through 0.5 is a moment");
    check(w.step(v) == 0u, "a held level is one moment");
    v[0] = 0.4f;
    w.step(v);
    v[0] = 1.0f;
    check(w.step(v) == 0u, "it comes again only after falling below 0.3");
    v[0] = 0.1f;
    w.step(v);
    v[0] = 0.9f;
    check(w.step(v) == kick, "and then it does");
    std::array<int, 3> counts{};
    const std::array<int, 3> rates = {30, 60, 120};
    const float beat = 60.0f / 93.0f;
    for (size_t k = 0; k < rates.size(); ++k) {
        MomentWatch m;
        const float dt = 1.0f / static_cast<float>(rates[k]);
        for (int i = 0; i < rates[k] * 10; ++i) {
            std::array<float, geode::viz::kMomentCount> trig{};
            trig[0] = std::fmod(static_cast<float>(i) * dt, beat) < 0.06f ? 1.0f : 0.0f;
            counts[k] += (m.step(trig) & kick) != 0 ? 1 : 0;
        }
    }
    check(counts[0] == counts[1] && counts[1] == counts[2] && counts[1] >= 15, "the same moments at 30, 60 and 120 fps");
}

// A wave set "On a moment" starts its cycle over when the moment comes; one running free does not. An envelope on a
// moment fires on it and not on a note start.
void testAMomentRestartsAWaveAndFiresAnEnvelope() {
    using geode::viz::Moment;
    const std::uint32_t drop = 1u << static_cast<int>(Moment::Drop);
    LfoEngine loose;
    LfoEngine onDrop;
    LfoConfig saw = sineOn(LfoTarget::Zoom, 1.0f, 4.0f);
    saw.wave = geode::viz::LfoWave::Saw;
    loose.configs[0] = saw;
    saw.moment = static_cast<int>(Moment::Drop);
    onDrop.configs[0] = saw;
    const GeodeSoundFrame f = musicAt(0.0f, 141.0f);
    for (int i = 0; i < 90; ++i) {
        loose.tick(kDt, f);
        onDrop.tick(kDt, f);
    }
    const float before = onDrop.tick(kDt, f)[0];
    loose.tick(kDt, f);
    const float restarted = onDrop.tick(kDt, f, nullptr, nullptr, drop)[0];
    const float running = loose.tick(kDt, f, nullptr, nullptr, drop)[0];
    check(restarted < before && std::fabs(restarted - (-1.0f + 2.0f * kDt / 4.0f)) < 1e-3f, "a drop starts the wave's cycle over");
    check(std::fabs(running - (before + 2.0f * kDt / 4.0f)) < 1e-3f, "a free wave runs on through it");
    AdsrEngine env;
    env.configs[0].enabled = true;
    env.configs[0].targets = {LfoTarget::Zoom};
    env.configs[0].moment = static_cast<int>(Moment::Drop);
    GeodeSoundFrame hit = f;
    hit.onset = 1.0f;
    for (int i = 0; i < 30; ++i) check(env.tick(kDt, hit)[0] == 0.0f, "an envelope on a moment ignores note starts");
    env.tick(kDt, hit, drop);
    check(env.tick(kDt, hit)[0] > 0.0f, "and fires on its moment");
}

void testDirectorLeavesTheBankAlone() {
    using geode::signal::SettingsLatch;
    const macrobank::Bank mine = turned({0.2f, -0.6f, 0.0f, 0.5f});
    macrobank::setGlobal(mine);
    const auto before = macrobank::global();
    geode::viz::director::setGlobal(true, 1.0f, 77);
    SettingsLatch live;
    auto r = std::make_unique<Resolver>();
    configureRich(*r);
    const SceneParams requested;
    float t = 0.0f;
    bool untouched = true;
    for (int i = 0; i < 1200; ++i, t += kDt) {
        GeodeSoundFrame f = frame(t);
        if (i == 900) f.drop = 0.8f;
        const EngineSettings s = live.latch(false);
        r->step(requested, f, kDt, s, style(kStyle, i == 0));
        untouched = untouched && s.bank == before && *s.bank == mine;
    }
    check(r->director().weight() > 0.9f && r->director().commits() >= 1, "the director drove the run");
    bool turned = false;
    for (float v : r->director().axes()) turned = turned || v != 0.0f;
    check(turned, "and turned its own axes");
    check(untouched && macrobank::global() == before && *macrobank::global() == mine, "the user's bank is exactly as set");
    geode::viz::director::setGlobal(false, 0.0f, 0);
    macrobank::setGlobal(macrobank::defaultBank());
}

// Customize › Look end to end through the chain: every control the map files under Look names schema
// fields, and moving its first field (within what the safety clamp lets show) moves the resolved value,
// with the director off and at full weight (base plus modulation: it must never replace the slider).
void testEveryLookControlReachesTheResolvedParams() {
    namespace customize = geode::viz::customize;
    const auto resolved = [](const SceneParams& req, float amount) {
        Resolver r;
        EngineSettings s;
        s.director = {amount, 7};
        StyleContext style;
        style.id = &kStyle;
        style.switched = true;
        SceneParams out;
        for (int i = 0; i < 600; ++i) {
            out = r.step(req, frame(static_cast<float>(i) * kDt), kDt, s, style, nullptr).params;
            style.switched = false;
        }
        return out;
    };
    int controls = 0;
    for (const auto& section : customize::sections()) {
        if (section.category != customize::Category::Look) continue;
        for (const char* control : section.controls) {
            if (control[0] == '@') continue;
            ++controls;
            const auto fields = customize::fieldsOf(control);
            check(!fields.empty(), std::string(control) + ": a Look control with no schema field");
            if (fields.empty()) continue;
            const std::string field = fields.front();
            const int slot = SceneParams::slotOf(field);
            check(slot >= 0, std::string(control) + ": " + field + " is not in the params schema");
            if (slot < 0) continue;
            const auto& spec = SceneParams::schema()[static_cast<size_t>(slot)];
            const float hi = std::min(spec.max, spec.min + 1.0f + safety::kMaxFlashDepth);
            SceneParams a;
            SceneParams b;
            a.set(field, spec.min + 0.1f * (hi - spec.min));
            b.set(field, spec.min + 0.9f * (hi - spec.min));
            for (float amount : {0.0f, 1.0f}) {
                const SceneParams ra = resolved(a, amount);
                const SceneParams rb = resolved(b, amount);
                const bool moved = field == "palette" ? ra.paletteBase() != rb.paletteBase() : ra.get(field) != rb.get(field);
                check(moved, std::string(control) + " does not move " + field + (amount > 0.0f ? " with the director" : ""));
            }
        }
    }
    check(controls >= 10, "Look has its controls");
}

int main() {
    testEveryLookControlReachesTheResolvedParams();
    testOrder();
    testNoColourNarrowingNoFixedReactions();
    testReducedMotionOnce();
    testPinnedAgainstGlobals();
    testDirectorForExport();
    testLatchPinnedAgainstGlobals();
    testDeterministic();
    testSafetyLast();
    testParityWithTheOldChain();
    testPresetMorphGlidesForThreeTimesItsLength();
    testParamFadeCutsLandsAndGlides();
    testAStoppedClockStillGlidesToTheRequest();
    testReducedMotionHasTheLastWordOnSpeed();
    testStyleMomentsOnlyAtDropsAndSections();
    testDisabledStagesAreExactIdentity();
    testNoteLayerOffIgnoresEntropyOnHearsIt();
    testLiveAndExportNeverHearEachOther();
    testBankReachesTheParams();
    testExportPinKeepsItsBank();
    testDirectorLeavesTheBankAlone();
    testAWaveTurnsAKnob();
    testAnEnvelopeTurnsAKnob();
    testKnobModulationAtAnyFrameRate();
    testSilenceHoldsAKnobStill();
    testMomentWatch();
    testAMomentRestartsAWaveAndFiresAnEnvelope();
    if (failures) {
        std::printf("resolver_test: %d failed\n", failures);
        return 1;
    }
    std::printf("resolver_test: all passed\n");
    return 0;
}
