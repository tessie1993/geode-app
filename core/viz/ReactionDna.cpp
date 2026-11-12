#include "viz/ReactionDna.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "viz/Catalogue.hpp"

namespace geode::viz {

namespace {

// Seeded draws on 32-bit integers only, so every platform and compiler builds
// the same DNA from the same id and seed.
class Draw {
public:
    explicit Draw(std::uint32_t seed) : x_(seed != 0u ? seed : 0x9E3779B9u) {}

    std::uint32_t next() {
        x_ ^= x_ << 13;
        x_ ^= x_ >> 17;
        x_ ^= x_ << 5;
        return x_;
    }
    float unit() { return static_cast<float>(next() >> 8) / 16777216.0f; }
    float range(float from, float to) { return from + (to - from) * unit(); }
    int below(int n) { return static_cast<int>(next() % static_cast<std::uint32_t>(n)); }
    bool coin() { return ((next() >> 16) & 1u) != 0u; }
    float sign() { return coin() ? 1.0f : -1.0f; }

private:
    std::uint32_t x_;
};

std::uint32_t fnv1a(std::string_view s) {
    std::uint32_t h = 2166136261u;
    for (const char c : s) {
        h ^= static_cast<std::uint8_t>(c);
        h *= 16777619u;
    }
    return h;
}

// Spreads neighbouring seeds apart before the xorshift sees them.
std::uint32_t mix(std::uint32_t h) {
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

// Personalities in the same group look alike from across the room (a push
// and a bulge both read as zoom), so a style's two phrases never share one.
int groupOf(Personality p) {
    switch (p) {
        case Personality::Pulse: case Personality::Lens: return 0;
        case Personality::Chroma: case Personality::Flash: return 1;
        case Personality::Twist: case Personality::Warp: case Personality::Ripple: case Personality::Morph: return 2;
        case Personality::Swing: return 3;
        case Personality::Split: return 4;
    }
    return 4;
}

bool allowed(Personality p, const StyleTraits& t) {
    if (p == Personality::Morph) return t.shaderLook;
    return true;
}

class Builder {
public:
    StyleDna dna;
    // How many routes the layers being drawn may fill: the groove's room, then the whole DNA's.
    int cap = StyleDna::kGrooveRoutes;

    void add(ReactSource source, LfoTarget target, ReactMode mode, float amount, float attack, float release,
             ModCurve curve = ModCurve::Linear) {
        if (dna.count >= cap) return;
        for (int i = 0; i < dna.count; ++i) {
            const ReactRoute& r = dna.routes[static_cast<size_t>(i)];
            if (r.source == source && r.target == target) return;
        }
        dna.routes[static_cast<size_t>(dna.count++)] = {true, source, target, mode, amount, curve, attack, release};
    }

    // The zoom personalities (Pulse, Lens) also zoom on the beat they hear: a fixed amount, so it draws nothing
    // and every other layer draws what it drew, and one more slot of room, so it takes no other layer's.
    void addBeatZoom(ReactSource source) {
        ++cap;
        add(source, LfoTarget::Zoom, ReactMode::Follow, 0.2f, 0.02f, 0.25f);
    }

    bool uses(LfoTarget target) const {
        for (int i = 0; i < dna.count; ++i) {
            if (dna.routes[static_cast<size_t>(i)].target == target) return true;
        }
        return false;
    }

    // The first of `options` (starting at a drawn index) whose target is still free.
    template <size_t N>
    LfoTarget freeTarget(Draw& d, const std::array<LfoTarget, N>& options, const StyleTraits& t) const {
        const int start = d.below(static_cast<int>(N));
        for (size_t k = 0; k < N; ++k) {
            const LfoTarget target = options[(static_cast<size_t>(start) + k) % N];
            if ((target == LfoTarget::Morph || target == LfoTarget::PaletteMix) && !t.shaderLook) continue;
            if ((target == LfoTarget::Rotation || target == LfoTarget::CamRoll) && t.spinRate == 0.0f) continue;
            if (!uses(target)) return target;
        }
        return LfoTarget::None;
    }
};

// What the picture does on the beat, for one personality. Every target here moves every style (its catalogue
// scope is Universal: the composite's lens, the rig or a flat style's flat camera, and the Look, Shape and Motion
// controls style_reads_test holds every style to reading), but Morph, which allowed() keeps to the shader styles
// that read it; and none turns the picture.
void addBeat(Builder& b, Draw& d, Personality p, ReactSource s) {
    // A hi-hat comes too often for a jump to land between hits: on the hats a jump waits for its glide.
    const ReactMode jump = s == ReactSource::Hat ? ReactMode::JumpSettled : ReactMode::Jump;
    switch (p) {
        case Personality::Pulse:
            b.add(s, LfoTarget::CamPush, ReactMode::Follow, d.range(0.35f, 0.6f), 0.02f, 0.28f);
            b.addBeatZoom(s);
            break;
        case Personality::Chroma:
            b.add(s, LfoTarget::ColorShift, jump, d.range(0.25f, 0.45f), 0.08f, 0.35f);
            break;
        case Personality::Flash:
            b.add(s, LfoTarget::Bloom, ReactMode::Follow, d.range(0.45f, 0.75f), 0.08f, 0.4f);
            b.add(s, LfoTarget::Brightness, ReactMode::Follow, d.range(0.15f, 0.3f), 0.08f, 0.45f);
            break;
        case Personality::Twist:
            b.add(s, LfoTarget::Twist, jump, d.range(0.3f, 0.6f), 0.04f, 0.4f);
            break;
        case Personality::Warp:
            b.add(s, LfoTarget::Warp, ReactMode::Follow, d.range(0.45f, 0.8f), 0.03f, 0.45f, ModCurve::Smooth);
            break;
        case Personality::Ripple:
            b.add(s, LfoTarget::Ripple, ReactMode::Follow, d.range(0.6f, 1.0f), 0.03f, 0.5f);
            break;
        case Personality::Split:
            b.add(s, LfoTarget::ChromaAb, ReactMode::Follow, d.range(0.5f, 0.9f), 0.01f, 0.2f);
            break;
        case Personality::Swing:
            b.add(s, LfoTarget::CamYaw, jump, d.range(0.3f, 0.6f), 0.15f, 0.5f);
            break;
        case Personality::Lens:
            b.add(s, LfoTarget::Fisheye, ReactMode::Follow, d.range(0.3f, 0.6f), 0.03f, 0.35f, ModCurve::Smooth);
            b.addBeatZoom(s);
            break;
        case Personality::Morph:
            b.add(s, LfoTarget::Morph, ReactMode::Follow, d.range(0.3f, 0.5f), 0.04f, 0.4f, ModCurve::Smooth);
            break;
    }
}

Personality pickPersonality(Draw& d, const StyleTraits& t, int avoidGroup) {
    std::array<Personality, kPersonalityCount> pool{};
    int n = 0;
    for (int i = 0; i < kPersonalityCount; ++i) {
        const auto p = static_cast<Personality>(i);
        if (allowed(p, t) && groupOf(p) != avoidGroup) pool[static_cast<size_t>(n++)] = p;
    }
    return pool[static_cast<size_t>(d.below(n))];
}

// One of `pool`, drawn: no layer of the DNA has a fixed source, so two styles with the same
// personality still listen to different parts of the music.
template <size_t N>
ReactSource pick(Draw& d, const std::array<ReactSource, N>& pool) {
    return pool[static_cast<size_t>(d.below(static_cast<int>(N)))];
}

// A musical layer's target: the first of `options` (from a drawn start) the style does not move yet, else the
// first option, beside whatever moves it already (two sources on one target are allowed), so the layer always
// lands. The first option must be one every style reaches.
template <size_t N>
LfoTarget landing(Builder& b, Draw& d, const std::array<LfoTarget, N>& options, const StyleTraits& t) {
    const LfoTarget free = b.freeTarget(d, options, t);
    return free != LfoTarget::None ? free : options[0];
}

// The raymarched families whose shaders switch between alternative distance-field formulas on `formula`
// (the owner: "music changes shape but also shader formula"). Fractal Flame, Opal and Hollows march too but
// draw no formula of their own, so they are not here.
constexpr std::array<std::string_view, 6> kFormulaFamilies = {"shard_", "prism_", "fluor_", "loom_", "petal_", "fold_"};

bool switchesFormula(std::string_view styleId) {
    for (const std::string_view prefix : kFormulaFamilies) {
        if (styleId.starts_with(prefix)) return true;
    }
    return false;
}

// How far a musical layer moves a structural target at most: about what the groove's own layers move it.
float structuralAmount(Draw& d, LfoTarget target) {
    switch (target) {
        case LfoTarget::Zoom: return d.range(0.1f, 0.2f);
        case LfoTarget::CamFov: return d.range(0.2f, 0.35f);
        case LfoTarget::Morph: return d.range(0.3f, 0.5f);
        case LfoTarget::Turbulence: case LfoTarget::Warp: case LfoTarget::Ripple: return d.range(0.2f, 0.4f);
        default: return d.range(0.15f, 0.3f);
    }
}

}  // namespace

StyleDna buildStyleDna(std::string_view styleId, int seed, const StyleTraits& traits) {
    Draw d(mix(fnv1a(styleId) ^ (static_cast<std::uint32_t>(seed) * 0x9E3779B9u)));
    Builder b;

    // The pulse: one personality on the kick side, one from another group on the snare side, so
    // the two halves of a groove look different. Which kick and which snare is the style's own:
    // every hit, every other one, or the hit's envelope; the snare's or the hats'. The owner: "make
    // sure all styles have capacity to react to drums". So both sides always hear a drum (the kick
    // side could hear any onset and the snare side the melody's notes, so a style that drew both had
    // no route on a drum), and they are drawn first, so no cap ever drops them. Each source pool
    // keeps its size, so a style that drew a kick and a snare before draws every layer as it did.
    b.dna.lead = pickPersonality(d, traits, -1);
    b.dna.second = pickPersonality(d, traits, groupOf(b.dna.lead));
    addBeat(b, d, b.dna.lead, pick(d, std::array{ReactSource::Kick, ReactSource::Kick, ReactSource::Kick2, ReactSource::KickEnv}));
    addBeat(b, d, b.dna.second, pick(d, std::array{ReactSource::Snare, ReactSource::Snare2, ReactSource::Hat, ReactSource::SnareEnv}));

    // The big moment (a hard cut, or the level surging back): always a surge forward, plus one
    // look the pulse does not already use.
    const ReactSource big = pick(d, std::array{ReactSource::HardCut, ReactSource::HardCut, ReactSource::Surge});
    b.add(big, LfoTarget::CamFly, ReactMode::Follow, d.range(0.8f, 1.4f), 0.25f, 2.2f);
    switch (b.freeTarget(d, std::array{LfoTarget::Bloom, LfoTarget::Twist, LfoTarget::CamFov, LfoTarget::ColorShift}, traits)) {
        case LfoTarget::Bloom: b.add(big, LfoTarget::Bloom, ReactMode::Follow, d.range(0.5f, 0.8f), 0.2f, 2.0f); break;
        case LfoTarget::Twist: b.add(big, LfoTarget::Twist, ReactMode::Follow, d.range(0.4f, 0.7f), 0.2f, 2.0f, ModCurve::Smooth); break;
        case LfoTarget::CamFov: b.add(big, LfoTarget::CamFov, ReactMode::Follow, d.range(0.2f, 0.4f), 0.15f, 1.8f); break;
        case LfoTarget::ColorShift: b.add(big, LfoTarget::ColorShift, ReactMode::Step, d.range(0.4f, 0.6f), 0.3f, 0.3f); break;
        default: break;
    }

    // No layer turns a style on a music clock (owner 2026-11-12, no style spins to the music): a turning
    // style turns at the user's Spin dial alone.

    // The body: a sustained level moves something continuous, not another hit thing. Each band against its own
    // mean, or how many partials sound: never the absolute loudness (volume is not what the picture hears).
    {
        const ReactSource body = pick(d, std::array{ReactSource::BassAtt, ReactSource::Low, ReactSource::SubAtt, ReactSource::DensitySlow,
                                                    ReactSource::MidPresence, ReactSource::BassPresence, ReactSource::MidAtt});
        const std::array bodies = {LfoTarget::Zoom, LfoTarget::Saturation, LfoTarget::Ripple, LfoTarget::Bloom,
                                   LfoTarget::Fisheye, LfoTarget::CamOrbit};
        switch (b.freeTarget(d, bodies, traits)) {
            case LfoTarget::Zoom: b.add(body, LfoTarget::Zoom, ReactMode::Follow, d.range(0.15f, 0.3f), 0.08f, 0.45f, ModCurve::Smooth); break;
            case LfoTarget::Saturation: b.add(body, LfoTarget::Saturation, ReactMode::Follow, d.range(0.2f, 0.4f), 0.3f, 0.8f); break;
            case LfoTarget::Ripple: b.add(body, LfoTarget::Ripple, ReactMode::Follow, d.range(0.2f, 0.4f), 0.1f, 0.5f, ModCurve::Smooth); break;
            case LfoTarget::Bloom: b.add(body, LfoTarget::Bloom, ReactMode::Follow, d.range(0.15f, 0.3f), 0.2f, 0.6f); break;
            case LfoTarget::Fisheye: b.add(body, LfoTarget::Fisheye, ReactMode::Follow, d.range(0.15f, 0.3f), 0.08f, 0.5f, ModCurve::Smooth); break;
            case LfoTarget::CamOrbit: b.add(body, LfoTarget::CamOrbit, ReactMode::Follow, d.range(0.3f, 0.6f), 0.4f, 1.0f); break;
            default: break;
        }
    }

    // Colour over the song, with no fixed driver: the melody's pitch class, a note change, every
    // sixteenth kick, a hard cut, a per-hit random or the timbre. When the pulse already moves
    // colour, the palette swaps or the temperature lands somewhere new every sixteen kicks instead.
    if (b.uses(LfoTarget::ColorShift)) {
        if (traits.shaderLook) b.add(ReactSource::Kick16, LfoTarget::PaletteMix, ReactMode::Toggle, 1.0f, 0.6f, 0.6f);
        else b.add(ReactSource::Kick16, LfoTarget::Temperature, ReactMode::Jump, d.range(0.3f, 0.5f), 0.6f, 0.6f);
    } else {
        switch (d.below(6)) {
            case 0: b.add(ReactSource::PitchClass, LfoTarget::ColorShift, ReactMode::Follow, d.range(0.4f, 0.8f), 0.4f, 0.8f); break;
            case 1: b.add(ReactSource::NoteChange, LfoTarget::ColorShift, ReactMode::Step, d.range(0.08f, 0.2f), 0.25f, 0.25f); break;
            case 2: b.add(ReactSource::Kick16, LfoTarget::ColorShift, ReactMode::Step, d.range(0.15f, 0.3f), 0.3f, 0.3f); break;
            case 3: b.add(ReactSource::HardCut, LfoTarget::ColorShift, ReactMode::Jump, d.range(0.3f, 0.5f), 0.4f, 0.4f); break;
            case 4: b.add(ReactSource::KickRandom, LfoTarget::ColorShift, ReactMode::Follow, d.range(0.1f, 0.25f), 0.3f, 0.6f); break;
            default: b.add(ReactSource::Centroid, LfoTarget::ColorShift, ReactMode::Follow, d.range(0.2f, 0.4f), 0.6f, 1.2f); break;
        }
    }

    // The top end gives it texture.
    {
        const ReactSource top = pick(d, std::array{ReactSource::TrebleAtt, ReactSource::High, ReactSource::HatEnv, ReactSource::Flatness,
                                                   ReactSource::Centroid, ReactSource::DensityFast, ReactSource::TreblePresence});
        const LfoTarget target = b.freeTarget(
            d, std::array{LfoTarget::ChromaAb, LfoTarget::Saturation, LfoTarget::Temperature, LfoTarget::PaletteMix, LfoTarget::Vignette}, traits);
        if (target != LfoTarget::None) b.add(top, target, ReactMode::Follow, d.range(0.15f, 0.4f), 0.05f, 0.3f);
    }

    // Some styles accent a counted hit with something of their own.
    if (d.coin()) {
        const ReactSource accent = pick(d, std::array{ReactSource::Snare2, ReactSource::Kick4, ReactSource::Snare4, ReactSource::Reattack});
        const std::array accents = {LfoTarget::Ripple, LfoTarget::ColorShift, LfoTarget::Twist, LfoTarget::CamRoll, LfoTarget::Warp};
        switch (b.freeTarget(d, accents, traits)) {
            case LfoTarget::Ripple: b.add(accent, LfoTarget::Ripple, ReactMode::Follow, d.range(0.3f, 0.5f), 0.02f, 0.3f); break;
            case LfoTarget::ColorShift: b.add(accent, LfoTarget::ColorShift, ReactMode::Step, d.range(0.08f, 0.15f), 0.2f, 0.2f); break;
            case LfoTarget::Twist: b.add(accent, LfoTarget::Twist, ReactMode::Jump, d.range(0.15f, 0.3f), 0.03f, 0.3f); break;
            case LfoTarget::CamRoll: b.add(accent, LfoTarget::CamRoll, ReactMode::Step, d.sign() * d.range(0.1f, 0.2f), 0.3f, 0.3f); break;
            case LfoTarget::Warp: b.add(accent, LfoTarget::Warp, ReactMode::Follow, d.range(0.25f, 0.4f), 0.03f, 0.3f, ModCurve::Smooth); break;
            default: break;
        }
    }

    // A rise (the level climbing over its 8 s average) tightens something until a big moment releases it.
    if (d.coin()) {
        switch (b.freeTarget(d, std::array{LfoTarget::CamFov, LfoTarget::ChromaAb, LfoTarget::Vignette}, traits)) {
            case LfoTarget::CamFov: b.add(ReactSource::EnergyTrend, LfoTarget::CamFov, ReactMode::Follow, -d.range(0.15f, 0.3f), 0.5f, 0.5f); break;
            case LfoTarget::ChromaAb: b.add(ReactSource::EnergyTrend, LfoTarget::ChromaAb, ReactMode::Follow, d.range(0.3f, 0.5f), 0.5f, 0.5f); break;
            case LfoTarget::Vignette: b.add(ReactSource::EnergyTrend, LfoTarget::Vignette, ReactMode::Follow, d.range(0.3f, 0.5f), 0.5f, 0.5f); break;
            default: break;
        }
    }

    // Some cruise faster the longer the bass keeps going (its activity against its own level, never the volume),
    // lean with the stereo field, or rise with the melody.
    if (d.coin()) b.add(ReactSource::BassPresence, LfoTarget::CamFly, ReactMode::Follow, d.range(0.3f, 0.6f), 1.5f, 3.0f, ModCurve::Smooth);
    if (d.coin() && !b.uses(LfoTarget::CamYaw)) {
        b.add(ReactSource::StereoPan, LfoTarget::CamYaw, ReactMode::Follow, d.range(0.2f, 0.45f), 0.3f, 0.6f);
    }
    if (d.coin() && !b.uses(LfoTarget::CamPitch)) {
        b.add(ReactSource::PitchHeight, LfoTarget::CamPitch, ReactMode::Follow, d.range(0.1f, 0.25f), 0.6f, 1.2f, ModCurve::Smooth);
    }

    // Every style hears transients (core/sound/Transient: the sharp start of any sound), so a pluck, a struck
    // chord or a consonant moves the picture where no drum plays. The last layer drawn, so every route and draw
    // above is what it was before it came; when the DNA is already full it takes the slot of the layer drawn
    // last (the style's least: a texture, accent, rise, cruise, lean or lift, never the pulse, big moment or
    // colour), so the cap holds and every style has one. Its source (the hit or its envelope) and its target, one
    // the style does not already move, are drawn like any layer's; when every one is taken it zooms beside
    // whatever zooms already (a camera shake, the old last resort, was too small to see).
    {
        if (b.dna.count >= b.cap) b.dna.count = b.cap - 1;
        const ReactSource attack = pick(d, std::array{ReactSource::Transient, ReactSource::TransientEnv});
        const std::array attacks = {LfoTarget::Ripple, LfoTarget::Warp, LfoTarget::Twist, LfoTarget::ChromaAb, LfoTarget::Zoom};
        LfoTarget hit = b.freeTarget(d, attacks, traits);
        if (hit == LfoTarget::None) hit = LfoTarget::Zoom;
        switch (hit) {
            case LfoTarget::Ripple: b.add(attack, LfoTarget::Ripple, ReactMode::Follow, d.range(0.25f, 0.45f), 0.01f, 0.25f); break;
            case LfoTarget::Warp: b.add(attack, LfoTarget::Warp, ReactMode::Follow, d.range(0.2f, 0.35f), 0.01f, 0.3f, ModCurve::Smooth); break;
            case LfoTarget::Twist: b.add(attack, LfoTarget::Twist, ReactMode::Follow, d.range(0.15f, 0.3f), 0.01f, 0.3f, ModCurve::Smooth); break;
            case LfoTarget::ChromaAb: b.add(attack, LfoTarget::ChromaAb, ReactMode::Follow, d.range(0.3f, 0.6f), 0.01f, 0.2f); break;
            case LfoTarget::Zoom: b.add(attack, LfoTarget::Zoom, ReactMode::Follow, d.range(0.1f, 0.2f), 0.01f, 0.25f); break;
            default: break;
        }
    }

    // The owner: react to transients "but also to longer things like 8 beats or 16, or melody lines", and "respond
    // in varied ways to musical content rather than volume". So after the groove every style has four more ordinary
    // routes, drawn like any layer and seen and edited like any route: one on the music's bars, one on its tune, one
    // on its harmony and form, one on its feel and timbre. They come after every draw above.
    b.cap = StyleDna::kMaxRoutes;

    // The song's long time: every 4 bars or every phrase (Rhythm's own bars, counted only while a beat is heard),
    // the shape settles somewhere new and holds it. A hold followed, or a pulse jumping to a new value in the
    // route's range, or a flip ping-ponging between two states every 2 bars or every phrase, glided over a second
    // or more, onto something structural the style does not move yet: depth, field of view, the shape, the
    // turbulence. Never a colour, never a turn, and no tempo is weighed: the bars come from the music or not at
    // all. Turbulence is never taken above, so there is always one free.
    {
        ReactSource bars = ReactSource::Bar4Hold;
        ReactMode mode = ReactMode::Follow;
        switch (d.below(6)) {
            case 0: break;
            case 1: bars = ReactSource::PhraseHold; break;
            case 2: bars = ReactSource::Bar4Pulse; mode = ReactMode::Jump; break;
            case 3: bars = ReactSource::Phrase; mode = ReactMode::Jump; break;
            case 4: bars = ReactSource::Bar2Flip; break;
            default: bars = ReactSource::PhraseFlip; break;
        }
        const std::array longs = {LfoTarget::Zoom, LfoTarget::CamFov, LfoTarget::Morph, LfoTarget::Turbulence};
        const float rise = d.range(1.0f, 2.0f);
        const float fall = d.range(1.2f, 2.5f);
        switch (b.freeTarget(d, longs, traits)) {
            case LfoTarget::Zoom: b.add(bars, LfoTarget::Zoom, mode, d.range(0.08f, 0.15f), rise, fall, ModCurve::Smooth); break;
            case LfoTarget::CamFov: b.add(bars, LfoTarget::CamFov, mode, d.range(0.2f, 0.35f), rise, fall, ModCurve::Smooth); break;
            case LfoTarget::Morph: b.add(bars, LfoTarget::Morph, mode, d.range(0.3f, 0.5f), rise, fall, ModCurve::Smooth); break;
            case LfoTarget::Turbulence: b.add(bars, LfoTarget::Turbulence, mode, d.range(0.2f, 0.4f), rise, fall, ModCurve::Smooth); break;
            default: break;
        }
    }

    // The tune: the melody's line rising and falling, its height, each new note, or simply whether one sounds,
    // onto a look the style does not move yet. Speed is never taken above, so there is always one free.
    {
        const ReactSource line = pick(d, std::array{ReactSource::MelodyContour, ReactSource::MelodyContour, ReactSource::PitchHeight,
                                                    ReactSource::MelodyNote, ReactSource::MelodyPresence});
        const bool note = line == ReactSource::MelodyNote;
        const float rise = note ? 0.02f : d.range(0.25f, 0.5f);
        const float fall = note ? d.range(0.3f, 0.5f) : d.range(0.6f, 1.2f);
        const std::array tunes = {LfoTarget::CamPitch, LfoTarget::Warp, LfoTarget::Ripple, LfoTarget::Fisheye,
                                  LfoTarget::Zoom, LfoTarget::CamFov, LfoTarget::Speed};
        switch (b.freeTarget(d, tunes, traits)) {
            case LfoTarget::CamPitch: b.add(line, LfoTarget::CamPitch, ReactMode::Follow, d.range(0.15f, 0.3f), rise, fall, ModCurve::Smooth); break;
            case LfoTarget::Warp: b.add(line, LfoTarget::Warp, ReactMode::Follow, d.range(0.15f, 0.3f), rise, fall, ModCurve::Smooth); break;
            case LfoTarget::Ripple: b.add(line, LfoTarget::Ripple, ReactMode::Follow, d.range(0.2f, 0.4f), rise, fall, ModCurve::Smooth); break;
            case LfoTarget::Fisheye: b.add(line, LfoTarget::Fisheye, ReactMode::Follow, d.range(0.15f, 0.3f), rise, fall, ModCurve::Smooth); break;
            case LfoTarget::Zoom: b.add(line, LfoTarget::Zoom, ReactMode::Follow, d.range(0.05f, 0.1f), rise, fall, ModCurve::Smooth); break;
            case LfoTarget::CamFov: b.add(line, LfoTarget::CamFov, ReactMode::Follow, d.range(0.15f, 0.3f), rise, fall, ModCurve::Smooth); break;
            case LfoTarget::Speed: b.add(line, LfoTarget::Speed, ReactMode::Follow, d.range(0.15f, 0.3f), rise, fall, ModCurve::Smooth); break;
            default: break;
        }
    }

    // Harmony or form, half the styles each. Harmony: a chord change or the leading scale degree moving (each a
    // new value that holds, settled so a run of changes cannot flicker it, or a flip between two states); or the
    // chord's tension, how clearly the key sounds, the tension rising or easing, how unstable the scale is or how
    // fast the chords move round the circle of fifths. Form: a new section, a section coming back or a drop
    // (events, as above); or a build-up, how new the bar is, how far through its section the music is, or being in
    // a build, a drop or a breakdown. The levels are followed, or for tension, a build-up and being in a build
    // added up, so a tight passage carries the shape on rather than pushing it out and back. Onto the shape,
    // depth, field of view, turbulence, warp or the camera's look; never a colour (the owner's rule: the palette
    // is the style's and the user's) and never a turn.
    //
    // A style that switches formulas hears its harmony or form in the formula instead: a new section or one coming
    // back lands on a new formula (settled, so a run of them cannot flicker it), a chord change or every other
    // phrase steps it on, or the chord's tension slowly pulls it. Which, how far and how fast is the style's own.
    if (switchesFormula(styleId)) {
        // Each draw on its own line: the order a call's arguments are worked out in is the compiler's, and the DNA
        // must draw alike on every one.
        ReactSource heard = ReactSource::Tension;
        ReactMode mode = ReactMode::Follow;
        float amount = 0.0f;
        float rise = 0.0f;
        float fall = 0.0f;
        switch (d.below(5)) {
            case 0:
            case 1:
                heard = d.coin() ? ReactSource::SectionChange : ReactSource::SectionReturn;
                mode = ReactMode::JumpSettled;
                amount = d.range(0.6f, 1.0f);
                rise = d.range(0.4f, 0.9f);
                fall = d.range(0.8f, 1.6f);
                break;
            case 2:
            case 3:
                heard = d.coin() ? ReactSource::HarmonicChange : ReactSource::PhraseFlip;
                mode = ReactMode::Step;
                amount = d.range(0.25f, 0.5f);
                rise = d.range(0.3f, 0.8f);
                fall = d.range(0.3f, 0.8f);
                break;
            default:
                amount = d.range(0.4f, 0.8f);
                rise = d.range(2.0f, 4.0f);
                fall = d.range(3.0f, 6.0f);
                break;
        }
        b.add(heard, LfoTarget::Formula, mode, amount, rise, fall, ModCurve::Smooth);
    } else {
        const ReactSource heard =
            d.coin() ? pick(d, std::array{ReactSource::HarmonicChange, ReactSource::DegreeAccent, ReactSource::Tension, ReactSource::KeyClarity,
                                          ReactSource::TensionRise, ReactSource::ScaleTension, ReactSource::ChordMotion})
                     : pick(d, std::array{ReactSource::SectionChange, ReactSource::SectionReturn, ReactSource::Drop, ReactSource::BuildUp,
                                          ReactSource::Novelty, ReactSource::SectionProgress, ReactSource::InBuild, ReactSource::InDrop,
                                          ReactSource::InBreakdown});
        const bool event = reactSourceInfo(heard).event;
        const LfoTarget target = landing(b, d,
                                         std::array{LfoTarget::Warp, LfoTarget::Morph, LfoTarget::Zoom, LfoTarget::CamFov,
                                                    LfoTarget::Turbulence, LfoTarget::CamPitch, LfoTarget::CamYaw},
                                         traits);
        ReactMode mode = ReactMode::Follow;
        if (event) {
            const int m = d.below(3);
            mode = m == 0 ? ReactMode::JumpSettled : (m == 1 ? ReactMode::Jump : ReactMode::Toggle);
        } else if ((heard == ReactSource::Tension || heard == ReactSource::BuildUp || heard == ReactSource::InBuild) &&
                   target != LfoTarget::CamPitch && target != LfoTarget::CamYaw && d.coin()) {
            mode = ReactMode::Spin;  // "Add up": a rate, so half the reach a second at most
        }
        const float amount = structuralAmount(d, target) * (mode == ReactMode::Spin ? 0.5f : 1.0f);
        const float rise = event ? d.range(0.4f, 0.9f) : d.range(0.5f, 1.0f);
        const float fall = event ? d.range(0.8f, 1.6f) : d.range(1.0f, 2.0f);
        b.add(heard, target, mode, amount, rise, fall, ModCurve::Smooth);
    }

    // Feel and timbre, followed slowly with long glides: the mood, the felt tension, the scale's brightness, the
    // key's character, the kind of feeling or its wonder; the noisiness, brightness, roll-off, hollowness or width
    // of the sound; or how the tune is played (a busy run, vibrato, glides, slides, the size of its leaps). Onto a
    // look the style does not move yet. Never the felt energy: it carries the loudness, and volume is not what the
    // picture hears.
    {
        ReactSource feel = ReactSource::Valence;
        switch (d.below(3)) {
            case 0:
                feel = pick(d, std::array{ReactSource::Valence, ReactSource::FeltTension, ReactSource::ScaleBrightness, ReactSource::KeySharpness,
                                          ReactSource::FeelingKind, ReactSource::FeelWonder});
                break;
            case 1:
                feel = pick(d, std::array{ReactSource::Flatness, ReactSource::Centroid, ReactSource::Rolloff, ReactSource::Evenness,
                                          ReactSource::StereoWidth});
                break;
            default:
                feel = pick(d, std::array{ReactSource::RunDensity, ReactSource::Vibrato, ReactSource::Glide, ReactSource::Legato,
                                          ReactSource::Interval});
                break;
        }
        const LfoTarget target = landing(b, d,
                                         std::array{LfoTarget::Turbulence, LfoTarget::Warp, LfoTarget::Morph, LfoTarget::CamFov,
                                                    LfoTarget::Zoom, LfoTarget::CamPitch, LfoTarget::Fisheye, LfoTarget::Ripple,
                                                    LfoTarget::Speed},
                                         traits);
        b.add(feel, target, ReactMode::Follow, structuralAmount(d, target), d.range(0.8f, 1.5f), d.range(1.5f, 3.0f), ModCurve::Smooth);
    }

    return b.dna;
}

// ── Customize › Music ───────────────────────────────────────────────────────────────────────────────

namespace {

// How a moment is acted on (its sources are hand::momentOf's, in viz/Reactions.cpp): the source a new smooth
// action follows (`pulse`), the one a stepped action fires on (`hit`, when it has a single instant), and how
// much longer every action's fall lasts for the moments that come rarely and should be felt for longer (a
// drop, a new section).
struct MomentSpec {
    ReactSource pulse;
    bool hasHit;
    ReactSource hit;
    float releaseScale;
};

using S = ReactSource;
const std::array<MomentSpec, kMomentCount> kMoments{{
    {S::KickEnv, true, S::Kick, 1.0f},
    {S::SnareEnv, true, S::Snare, 1.0f},
    {S::HatEnv, true, S::Hat, 1.0f},
    {S::BassAtt, false, S::BassAtt, 1.0f},
    {S::Drop, true, S::Drop, 4.0f},
    {S::BuildUp, false, S::BuildUp, 2.0f},
    {S::HarmonicChange, true, S::HarmonicChange, 2.0f},
    {S::SectionChange, true, S::SectionChange, 3.0f},
    {S::NoteChange, true, S::NoteChange, 1.0f},
    {S::Loudness, false, S::Loudness, 2.0f},
}};

// An action: the target a new route moves, every target that reads as this action on screen (a DNA route on
// Camera push lights "Zoom", and turning "Zoom" off takes it away), its route's amount at Strength 1×, its
// smoothing, and whether it jumps once per hit instead of following the moment.
struct ActionSpec {
    LfoTarget target;
    std::array<LfoTarget, 5> family;
    int familyCount;
    float amount;
    float attack;
    float release;
    bool stepped;
};

using T = LfoTarget;
const std::array<ActionSpec, kMusicActionCount> kActions{{
    {T::Bloom, {T::Bloom, T::Brightness}, 2, 0.4f, 0.01f, 0.3f, false},
    {T::Zoom, {T::Zoom, T::CamPush, T::CamFov}, 3, 0.15f, 0.02f, 0.3f, false},
    {T::CamShake, {T::CamShake}, 1, 0.3f, 0.01f, 0.3f, false},
    {T::ColorShift, {T::ColorShift, T::PaletteMix, T::Saturation, T::Temperature}, 4, 0.15f, 0.1f, 0.5f, true},
    {T::CamYaw, {T::CamYaw, T::CamPitch, T::CamRoll, T::CamOrbit, T::CamFly}, 5, 0.25f, 0.3f, 0.6f, true},
    {T::Warp, {T::Warp, T::Twist, T::Ripple, T::Turbulence}, 4, 0.3f, 0.03f, 0.3f, false},
}};

// The 1× amount of a route on a target no action claims.
constexpr float kDefaultAmount = 0.3f;
// Below this every amount is ~0, and a ratio against it means nothing.
constexpr float kMinScalable = 0.01f;

const MomentSpec& spec(Moment m) { return kMoments[static_cast<size_t>(m)]; }
const ActionSpec& spec(MusicAction a) { return kActions[static_cast<size_t>(a)]; }

bool inMoment(ReactSource s, Moment m) { return hand::momentOf(s) == static_cast<int>(m); }

bool inFamily(LfoTarget t, MusicAction a) {
    const ActionSpec& s = spec(a);
    for (int i = 0; i < s.familyCount; ++i) {
        if (s.family[static_cast<size_t>(i)] == t) return true;
    }
    return false;
}

// The 1× amount of whichever action `t` reads as.
float unitAmount(LfoTarget t) {
    for (int a = 0; a < kMusicActionCount; ++a) {
        if (inFamily(t, static_cast<MusicAction>(a))) return kActions[static_cast<size_t>(a)].amount;
    }
    return kDefaultAmount;
}

bool sameSlot(const ReactRoute& a, const ReactRoute& b) { return a.source == b.source && a.target == b.target; }

// The route `action` adds to `m` at `strength`. A stepped action on a moment with no single instant follows
// it instead, on the target that reads as a move.
ReactRoute routeFor(Moment m, MusicAction action, float strength) {
    const MomentSpec& ms = spec(m);
    const ActionSpec& as = spec(action);
    ReactRoute r;
    r.enabled = true;
    r.curve = ModCurve::Linear;
    r.amount = std::clamp(as.amount * strength, -2.0f, 2.0f);
    const float release = std::min(as.release * ms.releaseScale, 10.0f);
    if (as.stepped && ms.hasHit) {
        r.source = ms.hit;
        r.target = as.target;
        r.mode = ReactMode::Step;
        r.attackSeconds = release;
        r.releaseSeconds = release;
        return r;
    }
    r.source = ms.pulse;
    r.target = as.stepped && action == MusicAction::Camera ? LfoTarget::CamOrbit : as.target;
    r.mode = ReactMode::Follow;
    r.attackSeconds = as.attack;
    r.releaseSeconds = release;
    return r;
}

bool isOn(const RouteArray& playing, int count, Moment m, MusicAction a) {
    for (int i = 0; i < count; ++i) {
        const ReactRoute& r = playing[static_cast<size_t>(i)];
        if (r.enabled && inMoment(r.source, m) && inFamily(r.target, a)) return true;
    }
    return false;
}

bool fits(const RouteArray& playing, int count, Moment m, MusicAction a) {
    const ReactRoute added = routeFor(m, a, 1.0f);
    for (int i = 0; i < count; ++i) {
        if (sameSlot(playing[static_cast<size_t>(i)], added)) return true;
    }
    return count + 1 <= ReactionEngine::kRoutes;
}

float strengthOf(const RouteArray& playing, int count, Moment m) {
    float sum = 0.0f;
    int n = 0;
    for (int i = 0; i < count; ++i) {
        const ReactRoute& r = playing[static_cast<size_t>(i)];
        if (!inMoment(r.source, m)) continue;
        sum += std::fabs(r.amount) / unitAmount(r.target);
        n += 1;
    }
    return n == 0 ? 1.0f : std::clamp(sum / static_cast<float>(n), 0.0f, kMaxMomentStrength);
}

void push(RouteArray& out, int& n, const ReactRoute& r) {
    if (n < ReactionEngine::kRoutes) out[static_cast<size_t>(n++)] = r;
}

// `mask`'s list: the user's routes it still owns, minus `m`'s, then `m`'s new ones. "All Auto" once the
// hand owns nothing.
HandRoutes commit(const HandRoutes& user, std::uint32_t mask, Moment m, const RouteArray& moment, int momentCount) {
    HandRoutes out;
    if (mask == 0u) return out;
    out.user = true;
    out.mask = mask;
    const ReactSourceMask keep = hand::sources(mask);
    if (user.user) {
        for (int i = 0; i < user.count; ++i) {
            const ReactRoute& r = user.routes[static_cast<size_t>(i)];
            if (keep[static_cast<size_t>(r.source)] && !inMoment(r.source, m)) push(out.routes, out.count, r);
        }
    }
    for (int i = 0; i < momentCount; ++i) push(out.routes, out.count, moment[static_cast<size_t>(i)]);
    return out;
}

}  // namespace

int mergeHand(const HandRoutes& user, const StyleDna& dna, RouteArray& out) {
    out = RouteArray{};
    int n = 0;
    if (!user.user) {
        for (int i = 0; i < dna.count; ++i) push(out, n, dna.routes[static_cast<size_t>(i)]);
        return n;
    }
    const ReactSourceMask mine = hand::sources(user.mask);
    for (int i = 0; i < user.count; ++i) {
        const ReactRoute& r = user.routes[static_cast<size_t>(i)];
        if (mine[static_cast<size_t>(r.source)]) push(out, n, r);
    }
    for (int i = 0; i < dna.count; ++i) {
        const ReactRoute& r = dna.routes[static_cast<size_t>(i)];
        if (!mine[static_cast<size_t>(r.source)]) push(out, n, r);
    }
    return n;
}

MomentState momentState(const HandRoutes& user, const RouteArray& playing, int count, Moment m) {
    MomentState s;
    s.mine = user.user && (user.mask & hand::bit(m)) != 0u;
    for (int i = 0; i < count; ++i) {
        if (inMoment(playing[static_cast<size_t>(i)].source, m)) s.routes += 1;
    }
    s.strength = strengthOf(playing, count, m);
    for (int a = 0; a < kMusicActionCount; ++a) {
        const auto action = static_cast<MusicAction>(a);
        if (isOn(playing, count, m, action)) s.on |= 1u << a;
        if (fits(playing, count, m, action)) s.fits |= 1u << a;
    }
    return s;
}

HandRoutes editMusic(const HandRoutes& user, const StyleDna& dna, MusicEdit op, Moment m, MusicAction action, float value) {
    const std::uint32_t had = user.user ? user.mask : 0u;
    if (op == MusicEdit::ResetToAuto) {
        if ((had & hand::bit(m)) == 0u) return user;
        return commit(user, had & ~hand::bit(m), m, RouteArray{}, 0);
    }

    RouteArray playing;
    const int count = mergeHand(user, dna, playing);
    // The moment's routes as they play now, DNA ones included: the hand takes the moment over from there.
    RouteArray moment{};
    int n = 0;
    for (int i = 0; i < count; ++i) {
        const ReactRoute& r = playing[static_cast<size_t>(i)];
        if (inMoment(r.source, m)) push(moment, n, r);
    }

    RouteArray next{};
    int k = 0;
    if (op == MusicEdit::Toggle) {
        if (isOn(playing, count, m, action)) {
            // Off: every route of the action's family goes, whoever dealt it.
            for (int i = 0; i < n; ++i) {
                if (!inFamily(moment[static_cast<size_t>(i)].target, action)) push(next, k, moment[static_cast<size_t>(i)]);
            }
        } else {
            if (!fits(playing, count, m, action)) return user;
            // On, at the moment's strength, so a moment turned down stays down.
            const ReactRoute added = routeFor(m, action, strengthOf(playing, count, m));
            for (int i = 0; i < n; ++i) {
                if (!sameSlot(moment[static_cast<size_t>(i)], added)) push(next, k, moment[static_cast<size_t>(i)]);
            }
            push(next, k, added);
        }
    } else {
        if (n == 0) return user;  // nothing to scale; the page shows no Strength then
        const float now = strengthOf(playing, count, m);
        const float target = std::clamp(std::isfinite(value) ? value : 1.0f, 0.0f, kMaxMomentStrength);
        for (int i = 0; i < n; ++i) {
            ReactRoute r = moment[static_cast<size_t>(i)];
            // Every amount ~0: a ratio means nothing, so start again from each action's 1× amount.
            const float amount = now > kMinScalable ? r.amount * target / now : unitAmount(r.target) * target;
            r.amount = std::clamp(amount, -2.0f, 2.0f);
            push(next, k, r);
        }
    }
    return commit(user, had | hand::bit(m), m, next, k);
}

LfoTarget musicActionTarget(MusicAction action) { return spec(action).target; }

// ── The ready-made recipes ─────────────────────────────────────────────────────────────────────────

namespace {

// The catalogue scope each target moves, as Kotlin's LfoTarget.scope names it: what a route on it can
// reach. Anything not listed is applied to the finished frame or the camera rig, so every style.
catalogue::Scope targetScope(LfoTarget t) {
    using Sc = catalogue::Scope;
    switch (t) {
        case T::Speed: return Sc::SceneClock;
        case T::Morph:
        case T::PaletteMix: return Sc::ShaderLook;
        case T::Turbulence: return Sc::Turbulence;
        case T::ParticleSize: return Sc::ParticleSprite;
        case T::TrailLength: return Sc::TrailLength;
        case T::FluidCurl:
        case T::FluidGlow:
        case T::FluidFade: return Sc::FluidSim;
        case T::FluidRadius:
        case T::FluidForce: return Sc::Emitters;
        case T::FluidCatchPull:
        case T::FluidCatchRadius: return Sc::Journey;
        default: return Sc::Universal;
    }
}

// Smooth: the target follows the source, rising over `attack` and falling over `release` seconds.
constexpr ReactRoute follow(S source, T target, float amount, float attack, float release) {
    return {true, source, target, ReactMode::Follow, amount, ModCurve::Linear, attack, release};
}

// One jump of `amount` per rising edge, glided over `seconds`.
constexpr ReactRoute step(S source, T target, float amount, float seconds) {
    return {true, source, target, ReactMode::Step, amount, ModCurve::Linear, seconds, seconds};
}

// Keeps turning at `amount` per second while the source lasts.
constexpr ReactRoute spin(S source, T target, float amount) {
    return {true, source, target, ReactMode::Spin, amount, ModCurve::Linear, 0.5f, 0.5f};
}

constexpr RecipeRoutes one(const ReactRoute& a) { return {{a}, 1}; }
constexpr RecipeRoutes two(const ReactRoute& a, const ReactRoute& b) { return {{a, b}, 2}; }
constexpr RecipeRoutes three(const ReactRoute& a, const ReactRoute& b, const ReactRoute& c) { return {{a, b, c}, 3}; }

const std::array<RecipeRoutes, kEventRecipeCount> kRecipes{{
    two(follow(S::KickEnv, T::CamPush, 0.4f, 0.015f, 0.25f), follow(S::KickEnv, T::Zoom, 0.12f, 0.015f, 0.22f)),
    one(follow(S::SnareEnv, T::ChromaAb, 0.5f, 0.01f, 0.18f)),
    two(follow(S::HatEnv, T::ParticleSize, 0.3f, 0.01f, 0.12f), follow(S::HatEnv, T::Bloom, 0.2f, 0.01f, 0.15f)),
    two(follow(S::SubAtt, T::Warp, 0.35f, 0.06f, 0.4f), follow(S::SubAtt, T::FluidForce, 0.5f, 0.05f, 0.4f)),
    two(follow(S::Loudness, T::Brightness, 0.15f, 0.1f, 0.5f), follow(S::Loudness, T::Bloom, 0.25f, 0.1f, 0.5f)),
    two(follow(S::Intensity, T::CamFov, 0.25f, 0.4f, 0.8f), follow(S::Intensity, T::Vignette, -0.3f, 0.4f, 0.8f)),
    three(follow(S::EnergyTrend, T::Speed, 0.5f, 0.5f, 0.5f), follow(S::EnergyTrend, T::CamShake, 0.3f, 0.5f, 0.3f),
          spin(S::EnergyTrend, T::Rotation, 0.3f)),
    two(step(S::HardCut, T::ColorShift, 0.33f, 1.2f), step(S::HardCut, T::PaletteMix, 0.5f, 1.2f)),
    one(step(S::Kick, T::CamRoll, 0.15f, 0.08f)),
    one(step(S::Kick8, T::CamYaw, 0.4f, 0.8f)),
    one(step(S::NoteChange, T::ColorShift, 0.08f, 0.25f)),
    two(follow(S::Flux, T::Ripple, 0.3f, 0.03f, 0.3f), follow(S::Flux, T::Twist, 0.2f, 0.03f, 0.3f)),
    two(follow(S::StereoPan, T::CamYaw, 0.35f, 0.3f, 0.6f), follow(S::StereoWidth, T::CamOrbit, 0.3f, 0.3f, 0.6f)),
    one(step(S::Kick4, T::ColorShift, 0.1f, 0.2f)),
    two(follow(S::Onset, T::Turbulence, 0.3f, 0.01f, 0.3f), follow(S::Onset, T::CamShake, 0.15f, 0.01f, 0.3f)),
    two(follow(S::KickToggle, T::ColorShift, 0.25f, 0.06f, 0.06f), follow(S::KickToggle, T::PaletteMix, 0.6f, 0.06f, 0.06f)),
    two(follow(S::KickRandom, T::CamYaw, 0.35f, 0.15f, 0.15f), follow(S::KickRandom, T::CamPitch, 0.15f, 0.15f, 0.15f)),
    one(step(S::Snare2, T::CamRoll, 0.12f, 0.15f)),
    one(follow(S::PitchHeight, T::CamPitch, 0.3f, 0.3f, 0.6f)),
    one(follow(S::PitchClass, T::ColorShift, 0.5f, 0.15f, 0.4f)),
    one(follow(S::Glide, T::Twist, 0.4f, 0.05f, 0.3f)),
    two(follow(S::Vibrato, T::Ripple, 0.3f, 0.1f, 0.3f), follow(S::Vibrato, T::ChromaAb, 0.2f, 0.1f, 0.3f)),
    two(follow(S::Legato, T::Warp, 0.25f, 0.05f, 0.5f), follow(S::Legato, T::TrailLength, 0.4f, 0.05f, 0.5f)),
    two(follow(S::Silence, T::Brightness, -0.4f, 0.3f, 1.5f), follow(S::Silence, T::Vignette, 0.4f, 0.3f, 1.5f)),
    two(follow(S::Returned, T::Bloom, 0.6f, 0.02f, 1.5f), follow(S::Returned, T::CamPush, 0.5f, 0.02f, 1.5f)),
    one(follow(S::BassPresence, T::Zoom, 0.2f, 1.0f, 2.0f)),
    two(spin(S::MidTime, T::CamOrbit, 0.15f), spin(S::MidTime, T::Rotation, 0.1f)),
    one(step(S::Downbeat, T::CamYaw, 0.25f, 0.6f)),
    three(follow(S::ScaleBrightness, T::Brightness, 0.15f, 1.0f, 1.0f), follow(S::ScaleTension, T::Turbulence, 0.3f, 1.0f, 1.0f),
          follow(S::KeyHue, T::ColorShift, 0.5f, 1.5f, 1.5f)),
    three(follow(S::BuildUp, T::Speed, 0.5f, 0.5f, 0.5f), follow(S::Drop, T::Bloom, 0.7f, 0.02f, 2.0f),
          follow(S::Drop, T::CamPush, 0.6f, 0.02f, 1.5f)),
}};

// Whether `r` is on a slot one of the recipe's routes holds.
bool inRecipe(const RecipeRoutes& mine, const ReactRoute& r) {
    for (int i = 0; i < mine.count; ++i) {
        if (sameSlot(mine.routes[static_cast<size_t>(i)], r)) return true;
    }
    return false;
}

bool inCurrent(const RouteArray& current, int count, const ReactRoute& r) {
    for (int i = 0; i < count; ++i) {
        if (sameSlot(current[static_cast<size_t>(i)], r)) return true;
    }
    return false;
}

int clampedCount(int count) { return std::clamp(count, 0, ReactionEngine::kRoutes); }

}  // namespace

bool targetReaches(LfoTarget target, const std::string& styleId) { return catalogue::reaches(targetScope(target), styleId); }

RecipeRoutes recipeRoutes(EventRecipe r) {
    const int i = static_cast<int>(r);
    return i >= 0 && i < kEventRecipeCount ? kRecipes[static_cast<size_t>(i)] : RecipeRoutes{};
}

RecipeRoutes recipeRoutesFor(EventRecipe r, const std::string& styleId) {
    const RecipeRoutes all = recipeRoutes(r);
    RecipeRoutes out;
    for (int i = 0; i < all.count; ++i) {
        const ReactRoute& route = all.routes[static_cast<size_t>(i)];
        if (targetReaches(route.target, styleId)) out.routes[static_cast<size_t>(out.count++)] = route;
    }
    return out;
}

bool recipeIsOn(EventRecipe r, const RouteArray& current, int count, const std::string& styleId) {
    const RecipeRoutes mine = recipeRoutesFor(r, styleId);
    const int n = clampedCount(count);
    for (int i = 0; i < mine.count; ++i) {
        if (!inCurrent(current, n, mine.routes[static_cast<size_t>(i)])) return false;
    }
    return mine.count > 0;
}

bool recipeFits(EventRecipe r, const RouteArray& current, int count, const std::string& styleId) {
    const RecipeRoutes mine = recipeRoutesFor(r, styleId);
    const int n = clampedCount(count);
    int added = 0;
    for (int i = 0; i < mine.count; ++i) {
        if (!inCurrent(current, n, mine.routes[static_cast<size_t>(i)])) added += 1;
    }
    return n + added <= ReactionEngine::kRoutes;
}

int toggleRecipe(EventRecipe r, const RouteArray& current, int count, const std::string& styleId, RouteArray& out) {
    const int n = clampedCount(count);
    out = RouteArray{};
    int k = 0;
    const bool on = recipeIsOn(r, current, n, styleId);
    if (!on && !recipeFits(r, current, n, styleId)) {
        for (int i = 0; i < n; ++i) push(out, k, current[static_cast<size_t>(i)]);
        return k;
    }
    // Off takes every route on the recipe's slots out; on does the same, then adds the recipe's own.
    const RecipeRoutes mine = recipeRoutesFor(r, styleId);
    for (int i = 0; i < n; ++i) {
        if (!inRecipe(mine, current[static_cast<size_t>(i)])) push(out, k, current[static_cast<size_t>(i)]);
    }
    if (!on) {
        for (int i = 0; i < mine.count; ++i) push(out, k, mine.routes[static_cast<size_t>(i)]);
    }
    return k;
}

}  // namespace geode::viz
