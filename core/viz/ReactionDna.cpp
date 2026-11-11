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
        case Personality::Swing: case Personality::Spin: return 3;
        case Personality::Split: return 4;
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
        case Personality::Split:
            b.add(s, LfoTarget::ChromaAb, ReactMode::Follow, d.range(0.5f, 0.9f), 0.01f, 0.2f);
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
    {T::Pulse, {T::Pulse}, 1, 0.3f, 0.015f, 0.25f, false},
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
