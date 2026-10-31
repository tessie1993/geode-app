#include "viz/ReactionDna.hpp"

#include <cstdint>

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
        case Personality::Swing: case Personality::Spin: return 3;
        case Personality::Glitch: return 4;
    }
    return 4;
}

bool allowed(Personality p, const StyleTraits& t) {
    if (p == Personality::Spin) return t.spinRate != 0.0f;
    if (p == Personality::Morph) return t.shaderLook;
    return true;
}

class Builder {
public:
    StyleDna dna;

    void add(ReactSource source, LfoTarget target, ReactMode mode, float amount, float attack, float release,
             ModCurve curve = ModCurve::Linear) {
        if (dna.count >= StyleDna::kMaxRoutes) return;
        for (int i = 0; i < dna.count; ++i) {
            const ReactRoute& r = dna.routes[static_cast<size_t>(i)];
            if (r.source == source && r.target == target) return;
        }
        dna.routes[static_cast<size_t>(dna.count++)] = {true, source, target, mode, amount, curve, attack, release};
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

// What the picture does on the beat, for one personality.
void addBeat(Builder& b, Draw& d, Personality p, ReactSource s, const StyleTraits& t) {
    switch (p) {
        case Personality::Pulse:
            b.add(s, LfoTarget::CamPush, ReactMode::Follow, d.range(0.35f, 0.6f), 0.02f, 0.28f);
            break;
        case Personality::Chroma:
            b.add(s, LfoTarget::ColorShift, ReactMode::Jump, d.range(0.25f, 0.45f), 0.08f, 0.35f);
            break;
        case Personality::Flash:
            b.add(s, LfoTarget::Bloom, ReactMode::Follow, d.range(0.45f, 0.75f), 0.08f, 0.4f);
            b.add(s, LfoTarget::Brightness, ReactMode::Follow, d.range(0.15f, 0.3f), 0.08f, 0.45f);
            break;
        case Personality::Twist:
            b.add(s, LfoTarget::Twist, ReactMode::Jump, d.range(0.3f, 0.6f), 0.04f, 0.4f);
            break;
        case Personality::Warp:
            b.add(s, LfoTarget::Warp, ReactMode::Follow, d.range(0.45f, 0.8f), 0.03f, 0.45f, ModCurve::Smooth);
            break;
        case Personality::Ripple:
            b.add(s, LfoTarget::Ripple, ReactMode::Follow, d.range(0.6f, 1.0f), 0.03f, 0.5f);
            break;
        case Personality::Glitch:
            b.add(s, LfoTarget::ChromaAb, ReactMode::Follow, d.range(0.5f, 0.9f), 0.01f, 0.2f);
            b.add(s, LfoTarget::Glitch, ReactMode::Follow, d.range(0.15f, 0.3f), 0.08f, 0.35f);
            break;
        case Personality::Swing:
            b.add(s, LfoTarget::CamYaw, ReactMode::Jump, d.range(0.3f, 0.6f), 0.15f, 0.5f);
            break;
        case Personality::Lens:
            b.add(s, LfoTarget::Fisheye, ReactMode::Follow, d.range(0.3f, 0.6f), 0.03f, 0.35f, ModCurve::Smooth);
            break;
        case Personality::Spin:
            b.add(s, LfoTarget::Rotation, ReactMode::Step, (t.spinRate < 0.0f ? -1.0f : 1.0f) * d.range(0.12f, 0.25f), 0.12f, 0.12f);
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

}  // namespace

StyleDna buildStyleDna(std::string_view styleId, int seed, const StyleTraits& traits) {
    Draw d(mix(fnv1a(styleId) ^ (static_cast<std::uint32_t>(seed) * 0x9E3779B9u)));
    Builder b;

    // The pulse: one personality on the kick side, one from another group on the snare side, so
    // the two halves of a groove look different. Which kick and which snare is the style's own:
    // every hit, every other one, or the note starts.
    b.dna.lead = pickPersonality(d, traits, -1);
    b.dna.second = pickPersonality(d, traits, groupOf(b.dna.lead));
    addBeat(b, d, b.dna.lead, pick(d, std::array{ReactSource::Kick, ReactSource::Kick, ReactSource::Kick2, ReactSource::Onset}), traits);
    addBeat(b, d, b.dna.second, pick(d, std::array{ReactSource::Snare, ReactSource::Snare2, ReactSource::Hat, ReactSource::NoteChange}),
            traits);

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

    // Styles that turn keep turning at a speed the music sets: a clock's rate, spun.
    if (traits.spinRate != 0.0f) {
        const ReactSource clock = pick(d, std::array{ReactSource::MidTime, ReactSource::BassTime, ReactSource::CurvedTime,
                                                     ReactSource::TrebleTime, ReactSource::HitTime});
        b.add(clock, LfoTarget::Rotation, ReactMode::Spin, (traits.spinRate < 0.0f ? -1.0f : 1.0f) * d.range(0.15f, 0.35f), 0.1f, 0.6f);
    }

    // The body: a sustained level moves something continuous, not another hit thing.
    {
        const ReactSource body = pick(d, std::array{ReactSource::BassAtt, ReactSource::Low, ReactSource::SubAtt, ReactSource::VolAtt,
                                                    ReactSource::Level, ReactSource::BassPresence, ReactSource::MidAtt});
        const std::array bodies = {LfoTarget::Zoom, LfoTarget::Saturation, LfoTarget::Ripple, LfoTarget::Bloom,
                                   LfoTarget::Fisheye, LfoTarget::CamOrbit};
        switch (b.freeTarget(d, bodies, traits)) {
            case LfoTarget::Zoom: b.add(body, LfoTarget::Zoom, ReactMode::Follow, d.range(0.06f, 0.12f), 0.08f, 0.45f, ModCurve::Smooth); break;
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

    // Some cruise faster the longer the song stays loud, lean with the stereo field, or rise with the melody.
    if (d.coin()) b.add(ReactSource::Intensity, LfoTarget::CamFly, ReactMode::Follow, d.range(0.3f, 0.6f), 1.5f, 3.0f, ModCurve::Smooth);
    if (d.coin() && !b.uses(LfoTarget::CamYaw)) {
        b.add(ReactSource::StereoPan, LfoTarget::CamYaw, ReactMode::Follow, d.range(0.2f, 0.45f), 0.3f, 0.6f);
    }
    if (d.coin() && !b.uses(LfoTarget::CamPitch)) {
        b.add(ReactSource::PitchHeight, LfoTarget::CamPitch, ReactMode::Follow, d.range(0.1f, 0.25f), 0.6f, 1.2f, ModCurve::Smooth);
    }

    return b.dna;
}

}  // namespace geode::viz
