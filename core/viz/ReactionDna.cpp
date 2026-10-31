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

    // How often each source already drives something: the lanes prefer sources nobody uses yet, so
    // the kick, the melody, the build-up and the big moments each move different things.
    std::array<int, kReactSourceCount> load{};

    void lane(ReactSource source, LfoTarget target, ReactMode mode, float amount, float attack, float release,
              ModCurve curve = ModCurve::Linear) {
        const int before = dna.count;
        add(source, target, mode, amount, attack, release, curve);
        if (dna.count > before) ++load[static_cast<size_t>(source)];
    }

    // The least-used source of `pool`, ties broken by the draw.
    template <size_t N>
    ReactSource fresh(Draw& d, const std::array<ReactSource, N>& pool) const {
        const int start = d.below(static_cast<int>(N));
        ReactSource best = pool[static_cast<size_t>(start)];
        for (size_t k = 0; k < N; ++k) {
            const ReactSource s = pool[(static_cast<size_t>(start) + k) % N];
            if (load[static_cast<size_t>(s)] < load[static_cast<size_t>(best)]) best = s;
        }
        return best;
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

bool allowedTarget(LfoTarget t, const StyleTraits& traits) {
    if ((t == LfoTarget::Morph || t == LfoTarget::PaletteMix) && !traits.shaderLook) return false;
    if ((t == LfoTarget::Rotation || t == LfoTarget::CamRoll) && traits.spinRate == 0.0f) return false;
    return true;
}

using S = ReactSource;
using T = LfoTarget;
using M = ReactMode;

// A lane: one parameter, the sources that suit it, how it moves and how far.
struct LaneSpec {
    T target;
    std::array<S, 5> pool;
    M mode;
    float lo, hi;  // amount range
    float attack, release;
    ModCurve curve;
};

// Every continuous look parameter gets a lane. Luminance ones (saturation, brightness, temperature,
// vignette) draw only from slow sources, so the picture breathes rather than flickers.
constexpr std::array<LaneSpec, 17> kLookLanes = {{
    {T::Zoom, {S::BassAtt, S::SubAtt, S::Low, S::VolAtt, S::BassPresence}, M::Follow, 0.06f, 0.14f, 0.06f, 0.4f, ModCurve::Smooth},
    {T::Warp, {S::SubAtt, S::Bass, S::MidAtt, S::Flux, S::DensityFast}, M::Follow, 0.25f, 0.5f, 0.05f, 0.45f, ModCurve::Smooth},
    {T::Ripple, {S::KickEnv, S::SnareEnv, S::OnsetEnv, S::BassAtt, S::Kick2}, M::Follow, 0.25f, 0.5f, 0.02f, 0.4f, ModCurve::Linear},
    {T::Morph, {S::MidPresence, S::PitchHeight, S::Intensity, S::DensitySlow, S::Centroid}, M::Follow, 0.3f, 0.5f, 0.5f, 1.2f, ModCurve::Smooth},
    {T::Twist, {S::Flux, S::Interval, S::StereoPan, S::Glide, S::KickRandom}, M::Follow, 0.2f, 0.4f, 0.1f, 0.5f, ModCurve::Smooth},
    {T::ColorShift, {S::PitchClass, S::NoteRandom, S::Centroid, S::PitchHeight, S::Evenness}, M::Follow, 0.3f, 0.6f, 0.4f, 0.9f, ModCurve::Linear},
    {T::PaletteMix, {S::Intensity, S::TreblePresence, S::MidPresence, S::EnergyTrend, S::Rolloff}, M::Follow, 0.4f, 0.8f, 0.8f, 1.6f, ModCurve::Smooth},
    {T::Saturation, {S::Intensity, S::PitchConfidence, S::Loudness, S::MidPresence, S::Evenness}, M::Follow, 0.2f, 0.4f, 0.6f, 1.5f, ModCurve::Smooth},
    {T::Brightness, {S::Level, S::Loudness, S::Presence, S::BassPresence, S::Intensity}, M::Follow, 0.1f, 0.2f, 0.6f, 1.5f, ModCurve::Smooth},
    {T::Bloom, {S::VolAtt, S::PeakMag, S::KickEnv, S::TrebleAtt, S::Level}, M::Follow, 0.2f, 0.45f, 0.08f, 0.5f, ModCurve::Linear},
    {T::Temperature, {S::Centroid, S::PitchHeight, S::EnergyTrend, S::Evenness, S::Rolloff}, M::Follow, 0.3f, 0.5f, 0.8f, 1.8f, ModCurve::Smooth},
    {T::Turbulence, {S::Flux, S::Flatness, S::DensityFast, S::TrebleAtt, S::RunDensity}, M::Follow, 0.3f, 0.6f, 0.1f, 0.6f, ModCurve::Smooth},
    {T::ChromaAb, {S::SnareEnv, S::HatEnv, S::Flatness, S::OnsetEnv, S::High}, M::Follow, 0.3f, 0.6f, 0.01f, 0.25f, ModCurve::Linear},
    {T::Vignette, {S::EnergyTrend, S::Intensity, S::BassPresence, S::Presence, S::DensitySlow}, M::Follow, 0.2f, 0.4f, 0.8f, 1.6f, ModCurve::Smooth},
    {T::Glitch, {S::Snare4, S::Reattack, S::Snare2, S::HardCut, S::Kick8}, M::Follow, 0.15f, 0.3f, 0.08f, 0.35f, ModCurve::Linear},
    {T::Fisheye, {S::SubAtt, S::Kick2, S::KickEnv, S::BassAtt, S::Surge}, M::Follow, 0.2f, 0.4f, 0.03f, 0.4f, ModCurve::Smooth},
    {T::ParticleSize, {S::TrebleAtt, S::HatEnv, S::High, S::Vibrato, S::Flux}, M::Follow, 0.3f, 0.5f, 0.02f, 0.3f, ModCurve::Linear},
}};

void addLane(Builder& b, Draw& d, const LaneSpec& l, const StyleTraits& traits) {
    if (!allowedTarget(l.target, traits) || b.uses(l.target)) return;
    b.lane(b.fresh(d, l.pool), l.target, l.mode, d.range(l.lo, l.hi), l.attack, l.release, l.curve);
}

// The camera, per feel. Each move has its own sound; a route on a channel replaces the rig's own
// move for that channel (CameraRig::stepAside), so this is the camera's whole personality.
void addCamera(Builder& b, Draw& d, CameraFeel feel, const StyleTraits& traits) {
    const auto lane = [&](T t, S s, M m, float amount, float attack, float release, ModCurve c = ModCurve::Linear) {
        if (allowedTarget(t, traits) && !b.uses(t)) b.lane(s, t, m, amount, attack, release, c);
    };
    switch (feel) {
        case CameraFeel::MusicVideo:
            lane(T::CamCut, b.fresh(d, std::array{S::HardCut, S::Kick8, S::Kick8}), M::Follow, 1.0f, 0.0f, 0.3f);
            lane(T::CamPush, b.fresh(d, std::array{S::EnergyTrend, S::Surge, S::Intensity}), M::Follow, d.range(0.4f, 0.6f), 0.6f, 1.5f,
                 ModCurve::Smooth);
            lane(T::CamYaw, b.fresh(d, std::array{S::MidTime, S::NoteTime, S::BassTime}), M::Spin, d.sign() * d.range(0.12f, 0.2f), 0.3f, 0.6f);
            lane(T::CamCrane, b.fresh(d, std::array{S::PitchHeight, S::Centroid}), M::Follow, d.range(0.4f, 0.6f), 0.8f, 1.6f, ModCurve::Smooth);
            lane(T::CamShake, b.fresh(d, std::array{S::KickEnv, S::SnareEnv}), M::Follow, d.range(0.1f, 0.2f), 0.01f, 0.3f);
            lane(T::CamFov, b.fresh(d, std::array{S::BassAtt, S::SubAtt}), M::Follow, -d.range(0.1f, 0.18f), 0.05f, 0.4f);
            break;
        case CameraFeel::Floaty:
            lane(T::CamFly, b.fresh(d, std::array{S::CurvedTime, S::MidTime, S::BassTime}), M::Drive, d.range(0.9f, 1.2f), 0.5f, 1.0f);
            lane(T::CamYaw, b.fresh(d, std::array{S::MidTime, S::BassTime, S::NoteTime}), M::Spin, d.sign() * d.range(0.2f, 0.3f), 0.6f, 1.2f);
            lane(T::CamOrbit, b.fresh(d, std::array{S::MidPresence, S::Intensity, S::BassPresence}), M::Follow, d.range(0.3f, 0.5f), 1.0f, 2.0f,
                 ModCurve::Smooth);
            lane(T::CamCrane, b.fresh(d, std::array{S::PitchHeight, S::Intensity}), M::Follow, d.range(0.5f, 0.7f), 1.0f, 2.0f, ModCurve::Smooth);
            lane(T::CamPitch, b.fresh(d, std::array{S::Centroid, S::Interval}), M::Follow, d.range(0.15f, 0.25f), 0.8f, 1.5f, ModCurve::Smooth);
            lane(T::CamFov, b.fresh(d, std::array{S::Intensity, S::EnergyTrend}), M::Follow, d.range(0.15f, 0.25f), 1.0f, 2.0f, ModCurve::Smooth);
            break;
        case CameraFeel::Club:
            lane(T::CamPush, b.fresh(d, std::array{S::KickEnv, S::Kick}), M::Follow, d.range(0.4f, 0.6f), 0.01f, 0.25f);
            lane(T::CamShake, b.fresh(d, std::array{S::SnareEnv, S::KickEnv}), M::Follow, d.range(0.35f, 0.5f), 0.01f, 0.3f);
            lane(T::CamCut, S::Kick4, M::Follow, 1.0f, 0.0f, 0.3f);
            lane(T::CamRoll, S::SnareToggle, M::Follow, d.sign() * d.range(0.15f, 0.3f), 0.06f, 0.06f);
            lane(T::CamFov, b.fresh(d, std::array{S::BassAtt, S::SubAtt}), M::Follow, -d.range(0.2f, 0.3f), 0.02f, 0.3f);
            lane(T::CamYaw, b.fresh(d, std::array{S::HitTime, S::BassTime}), M::Spin, d.sign() * d.range(0.25f, 0.35f), 0.2f, 0.4f);
            break;
    }
}

// The layers, strong on every style (the user's choice), each on a sound no other layer uses.
void addLayers(Builder& b, Draw& d) {
    // Falling through: a mix drawn per style, so the direction is the style's own.
    switch (d.below(4)) {
        case 0:  // energy falls in, the sound coming back pulls it out
            b.lane(S::EnergyTrend, T::Fall, M::Follow, d.range(0.8f, 1.1f), 0.4f, 0.8f, ModCurve::Smooth);
            b.lane(S::Returned, T::Fall, M::Follow, -d.range(1.0f, 1.4f), 0.05f, 1.5f);
            break;
        case 1:  // two sounds at once: one pushes in, the other pulls back out
            b.lane(b.fresh(d, std::array{S::KickEnv, S::BassAtt, S::SubAtt}), T::Fall, M::Follow, d.range(0.8f, 1.1f), 0.02f, 0.5f);
            b.lane(b.fresh(d, std::array{S::PitchHeight, S::MidPresence, S::Centroid}), T::Fall, M::Follow, -d.range(0.6f, 0.9f), 0.3f, 0.8f,
                   ModCurve::Smooth);
            break;
        case 2:  // a new direction, held, at every big moment
            b.lane(b.fresh(d, std::array{S::HardCut, S::Kick16}), T::Fall, M::Jump, d.range(0.7f, 1.0f), 0.6f, 0.6f);
            break;
        default:  // the melody: a rising line falls in, a falling one climbs back out
            b.lane(S::Interval, T::Fall, M::Follow, d.range(1.0f, 1.4f), 0.2f, 0.8f, ModCurve::Smooth);
            b.lane(b.fresh(d, std::array{S::SubAtt, S::Intensity}), T::Fall, M::Follow, d.range(0.3f, 0.5f), 0.3f, 0.8f);
            break;
    }
    b.lane(b.fresh(d, std::array{S::Intensity, S::EnergyTrend, S::DensitySlow, S::MidPresence, S::BassPresence}), T::Fractal, M::Follow,
           d.range(0.8f, 1.2f), 0.4f, 1.5f, ModCurve::Smooth);
    b.lane(b.fresh(d, std::array{S::Kick, S::Snare, S::Onset, S::NoteChange, S::Kick2}), T::Spawn, M::Follow, 1.0f, 0.0f, 0.2f);
    b.lane(b.fresh(d, std::array{S::SubAtt, S::Glide, S::Legato, S::Vibrato, S::BassPresence}), T::Melt, M::Follow, d.range(0.5f, 0.9f), 0.2f,
           0.9f, ModCurve::Smooth);
    b.lane(b.fresh(d, std::array{S::Silence, S::Kick16, S::Reattack, S::Snare4}), T::Freeze, M::Follow, 1.0f, 0.0f, 0.3f);
    b.lane(b.fresh(d, std::array{S::HardCut, S::Surge, S::Kick16, S::Snare4}), T::Shatter, M::Follow, 1.0f, 0.0f, 0.3f);
    b.lane(b.fresh(d, std::array{S::HatEnv, S::TrebleAtt, S::OnsetEnv, S::Flux, S::High}), T::Particles, M::Follow, d.range(0.7f, 1.0f), 0.02f,
           0.4f);
    // The particles fall or fly up, the way changing at big moments or every sixteen kicks.
    b.lane(b.fresh(d, std::array{S::HardCut, S::Kick16}), T::ParticleFall, M::Jump, d.range(0.6f, 0.9f), 0.8f, 0.8f, ModCurve::Smooth);
}

// The motions the music drives (matrix::drive): which clock sets the pace is the style's own.
void addDrives(Builder& b, Draw& d, const StyleTraits& traits) {
    b.lane(b.fresh(d, std::array{S::MidTime, S::CurvedTime, S::NoteTime, S::BassTime}), T::Speed, M::Drive, d.range(0.9f, 1.2f), 0.3f, 0.6f);
    if (traits.spinRate != 0.0f) {
        b.lane(b.fresh(d, std::array{S::BassTime, S::MidTime, S::HitTime, S::TrebleTime}), T::Rotation, M::Drive, d.range(0.9f, 1.2f), 0.3f,
               0.6f);
    }
    b.lane(b.fresh(d, std::array{S::NoteTime, S::TrebleTime, S::CurvedTime}), T::CycleSpeed, M::Drive, d.range(0.8f, 1.2f), 0.5f, 1.0f);
}

}  // namespace

const char* cameraFeelName(CameraFeel f) {
    switch (f) {
        case CameraFeel::MusicVideo: return "Music video";
        case CameraFeel::Floaty: return "Floaty journey";
        case CameraFeel::Club: return "Club";
    }
    return "Music video";
}

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

    // Everything else listens too (the user: "every parameter"): the motions on their own clocks,
    // the camera in the style's feel, the layers, then a lane for every look parameter still free.
    for (int i = 0; i < b.dna.count; ++i) ++b.load[static_cast<size_t>(b.dna.routes[static_cast<size_t>(i)].source)];
    b.dna.feel = static_cast<CameraFeel>(d.below(kCameraFeelCount));
    addDrives(b, d, traits);
    addCamera(b, d, b.dna.feel, traits);
    addLayers(b, d);
    for (const LaneSpec& l : kLookLanes) addLane(b, d, l, traits);

    return b.dna;
}

}  // namespace geode::viz
