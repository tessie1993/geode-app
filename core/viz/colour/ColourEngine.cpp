#include "viz/colour/ColourEngine.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "sound/Stages.hpp"

namespace geode::viz::colour {

namespace {

using sound::clamp01;
using sound::follow;

constexpr float kPi = 3.14159265f;
constexpr float kFadeSeconds = 2.5f;     // a palette change crossfades in OKLab over this long
constexpr float kMinSection = 12.0f;     // at most one palette change this often, however busy the song
constexpr float kBigMomentTau = 8.0f;    // a hard cut or a drop keeps the colours away from home this long
constexpr float kAwayTau = 1.2f;
constexpr float kDriftTau = 0.4f;
constexpr float kLightTau = 0.3f;
constexpr float kHighlightTau = 0.15f;

float lag(float y, float x, float rise, float fall, float dt) { return follow(y, x, x > y ? rise : fall, dt); }

// A turn (0..1) as the short way round from 0: -0.5..0.5.
float wrapTurns(float t) { return t - std::round(t); }

// Pitch class (0..11) on the circle of fifths, in twelfths.
int fifthsOf(int pc) { return (((pc % 12) + 12) % 12 * 7) % 12; }

// Distance from `from` to `to` round the circle of fifths, in fifths: -6..5.
int fifthsDistance(int from, int to) { return ((fifthsOf(to) - fifthsOf(from)) % 12 + 18) % 12 - 6; }

int pitchClassOf(float turns) { return static_cast<int>(std::lround(turns * 12.0f)) % 12; }

// The CSS hue-rotate() matrix (Filter Effects 1): turns display RGB round the grey axis while keeping
// its luminance, row-major. Cheap enough to apply on every pal() call, unlike an OKLab round trip.
std::array<float, 9> hueMatrix(float degrees) {
    const float r = degrees * kPi / 180.0f;
    const float c = std::cos(r);
    const float s = std::sin(r);
    return {0.213f + c * 0.787f - s * 0.213f, 0.715f - c * 0.715f - s * 0.715f, 0.072f - c * 0.072f + s * 0.928f,
            0.213f - c * 0.213f + s * 0.143f, 0.715f + c * 0.285f + s * 0.140f, 0.072f - c * 0.072f - s * 0.283f,
            0.213f - c * 0.213f - s * 0.787f, 0.715f - c * 0.715f + s * 0.715f, 0.072f + c * 0.928f + s * 0.072f};
}

std::array<Lab, 5> sampleStops(const std::vector<Lab>& stops) {
    std::array<Lab, 5> out{};
    const int n = static_cast<int>(stops.size());
    for (int i = 0; i < 5; i++) {
        const float x = static_cast<float>(i) / 4.0f * static_cast<float>(n - 1);
        const int a = std::min(static_cast<int>(x), n - 1);
        const int b = std::min(a + 1, n - 1);
        out[static_cast<size_t>(i)] = mixLab(stops[static_cast<size_t>(a)], stops[static_cast<size_t>(b)], x - static_cast<float>(a));
    }
    return out;
}

Lab sampleRamp(const std::array<Lab, 5>& stops, float t) {
    const float x = std::clamp(t, 0.0f, 1.0f) * 4.0f;
    const int a = std::min(static_cast<int>(x), 3);
    return mixLab(stops[static_cast<size_t>(a)], stops[static_cast<size_t>(a + 1)], x - static_cast<float>(a));
}

// Tension's bands along a palette: where the complement comes in. Two soft bands per ramp, so a tense
// chord tints some colours of every style without striping it; pal() uses the same function.
float clashBand(float t) { return 0.5f + 0.5f * std::sin(2.0f * kPi * 2.0f * t); }

}  // namespace

ColourSettings ColourSettings::fromWire(const float* v, int count) {
    ColourSettings s;
    if (v == nullptr) return s;
    auto at = [&](int i, float fallback) { return i < count ? v[i] : fallback; };
    s.palette = static_cast<int>(std::lround(at(0, static_cast<float>(s.palette))));
    s.follow = std::clamp(at(1, s.follow), 0.0f, 1.0f);
    s.hueDriver = static_cast<HueDriver>(std::clamp(static_cast<int>(std::lround(at(2, 0.0f))), 0, 4));
    s.variety = std::clamp(at(3, s.variety), 0.0f, 2.0f);
    s.moods = static_cast<uint32_t>(std::lround(at(4, static_cast<float>(kAllMoods)))) & kAllMoods;
    if (s.moods == 0) s.moods = kAllMoods;
    s.customCount = std::clamp(static_cast<int>(std::lround(at(5, 0.0f))), 0, kMaxCustomStops);
    for (int i = 0; i < kMaxCustomStops; i++) {
        for (int c = 0; c < 3; c++) s.custom[static_cast<size_t>(i)][static_cast<size_t>(c)] = std::clamp(at(6 + 3 * i + c, 0.0f), 0.0f, 1.0f);
    }
    if (s.palette >= static_cast<int>(palettes().size()) || s.palette < kCustom) s.palette = kStyleOwn;
    if (s.palette == kCustom && s.customCount == 0) s.palette = kStyleOwn;
    return s;
}

void ColourSettings::toWire(float* out) const {
    out[0] = static_cast<float>(palette);
    out[1] = follow;
    out[2] = static_cast<float>(static_cast<int>(hueDriver));
    out[3] = variety;
    out[4] = static_cast<float>(moods);
    out[5] = static_cast<float>(customCount);
    for (int i = 0; i < kMaxCustomStops; i++) {
        for (int c = 0; c < 3; c++) out[6 + 3 * i + c] = custom[static_cast<size_t>(i)][static_cast<size_t>(c)];
    }
}

void ColourEngine::setSettings(const ColourSettings& settings) {
    const bool restart = settings.palette != settings_.palette || settings.moods != settings_.moods ||
                         (settings.palette == ColourSettings::kCustom &&
                          (settings.customCount != settings_.customCount || settings.custom != settings_.custom));
    settings_ = settings;
    if (restart) started_ = false;
}

void ColourEngine::setSeed(uint32_t seed) {
    if (seed == seed_) return;
    seed_ = seed;
    started_ = false;
    sections_ = 0;
}

void ColourEngine::reset() {
    const ColourSettings settings = settings_;
    const uint32_t seed = seed_;
    *this = ColourEngine();
    settings_ = settings;
    seed_ = seed;
}

uint32_t ColourEngine::hash(uint32_t a, uint32_t b) const {
    // splitmix32-style: the walk's dice, a pure function of the track's seed and the section number.
    uint32_t x = a * 0x9E3779B9u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    x ^= x >> 16;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    x *= 0xC2B2AE35u;
    x ^= x >> 16;
    return x;
}

std::array<Lab, 5> ColourEngine::stopsOf(int index) const {
    if (index == ColourSettings::kCustom) {
        const int n = settings_.customCount;
        std::vector<Lab> stops;
        for (int i = 0; i < n; i++) stops.push_back(srgbToOklab(settings_.custom[static_cast<size_t>(i)]));
        if (n == 1) {
            // One colour: a ramp through it, from its own deep shade to its own pale tint.
            const Lab c = stops[0];
            stops = {{0.12f, c[1] * 0.6f, c[2] * 0.6f}, c, {0.93f, c[1] * 0.35f, c[2] * 0.35f}};
        }
        return sampleStops(stops);
    }
    return paletteLab(std::max(index, 0));
}

void ColourEngine::choosePalette(float arousal, float valence) {
    const ColourSettings& s = settings_;
    const auto& all = palettes();
    // Which moods this change may land in. A chosen palette walks its own mood up to Variety 1.5; the
    // style's own and Auto go to the mood nearest the music's energy and brightness; past 1.5 the whole
    // library (within the allowed moods) is open.
    uint32_t moods = s.moods;
    if (s.variety < 1.5f) {
        if (s.palette >= 0) {
            moods = 1u << static_cast<int>(all[static_cast<size_t>(s.palette)].mood);
        } else {
            int nearest = -1;
            float best = 1e9f;
            for (int m = 0; m < kMoodCount; m++) {
                if ((s.moods & (1u << m)) == 0) continue;
                const MoodInfo& info = moodInfo(static_cast<Mood>(m));
                const float d = std::hypot(info.arousal - arousal, info.valence - valence);
                if (d < best) {
                    best = d;
                    nearest = m;
                }
            }
            if (nearest >= 0) moods = 1u << nearest;
        }
    }
    std::vector<int> candidates;
    for (size_t i = 0; i < all.size(); i++) {
        const int idx = static_cast<int>(i);
        if ((moods & (1u << static_cast<int>(all[i].mood))) == 0) continue;
        if (idx == to_ || idx == from_) continue;  // never the one showing, never the one before it
        candidates.push_back(idx);
    }
    if (candidates.empty()) {
        for (size_t i = 0; i < all.size(); i++) {
            if (static_cast<int>(i) != to_) candidates.push_back(static_cast<int>(i));
        }
    }
    const int next = candidates[hash(seed_, static_cast<uint32_t>(sections_)) % candidates.size()];
    from_ = to_;
    to_ = next;
    fade_ = 0.0f;
    sinceSwitch_ = 0.0f;
}

float ColourEngine::tonicRotation(const sound::GeodeHarmony& h) const {
    // The key on the circle of fifths, measured from C/Am the short way: G +30°, D +60° … F −30°, so a
    // song in C keeps the palette as drawn and a tritone away turns it to its complement. Variety sets
    // how far round a key may take a palette; clarity fades it in as the key becomes clear.
    const float reach = 0.25f + 0.75f * clamp01(settings_.variety / 2.0f);
    const float key = wrapTurns(h.fifths) * 360.0f * reach * clamp01(h.clarity * 1.4f);
    // Mode: major warms toward gold, minor cools toward blue (Palmer et al. 2013).
    const float major = (h.mode - 0.5f) * 2.0f * clamp01(h.clarity * 1.4f);
    const float tint = major >= 0.0f ? 12.0f * major : 20.0f * major;
    return key + tint;
}

float ColourEngine::driverTarget(const GeodeSoundFrame& f, const sound::GeodeHarmony& h) const {
    const int tonic = pitchClassOf(h.tonic);
    const float melody = f.pitchConfidence > 0.2f
                             ? static_cast<float>(fifthsDistance(tonic, pitchClassOf(f.pitchClass))) / 6.0f * clamp01(f.pitchConfidence * 1.5f)
                             : drift_;
    const float chord = std::clamp(wrapTurns(h.chordFifths - h.fifths) * 2.0f * h.chordStrength, -1.0f, 1.0f);
    const float timbre = std::clamp((f.centroid - 0.45f) * 2.5f, -1.0f, 1.0f);
    switch (settings_.hueDriver) {
        case ColourSettings::HueDriver::Melody: return melody;
        case ColourSettings::HueDriver::Chord: return chord;
        case ColourSettings::HueDriver::Key: return 0.0f;
        case ColourSettings::HueDriver::Timbre: return timbre;
        case ColourSettings::HueDriver::Blend: return 0.4f * melody + 0.4f * chord + 0.2f * timbre;
    }
    return 0.0f;
}

void ColourEngine::step(const GeodeSoundFrame& f, const sound::GeodeHarmony& h, float dt) {
    const ColourSettings& s = settings_;
    const float amount = clamp01(s.follow);
    frame_.active = amount > 0.001f || s.palette != ColourSettings::kStyleOwn;
    if (!frame_.active) {
        frame_ = ColourFrame{};
        awayEase_ = 0.0f;
        return;
    }

    // Where the music sits on the mood axes: arousal from level, section energy and hits; valence from
    // major/minor (as sure as the key is) and timbre brightness.
    presence_ = follow(presence_, f.presence, 0.5f, dt);
    const float arousal = clamp01(0.45f * f.level + 0.35f * f.intensity + 0.2f * clamp01(f.onsetEnv));
    const float modeValence = 0.5f + (h.mode - 0.5f) * clamp01(h.clarity * 1.5f);
    const float valence = clamp01(0.6f * modeValence + 0.4f * clamp01(f.centroid * 1.3f));
    arousal_ = lag(arousal_, arousal, 0.1f, 0.5f, dt);
    valence_ = follow(valence_, valence, 3.0f, dt);
    const bool bigMoment = f.hardCut > 0.0f || f.surge > 0.0f || f.returned > 0.0f;
    big_ = bigMoment ? 1.0f : big_ * std::exp(-dt / kBigMomentTau);
    sinceSwitch_ += dt;

    // The palette walk: a new palette on a section change, seeded by the track.
    const bool custom = s.palette == ColourSettings::kCustom;
    const bool walks = !custom && (s.palette == ColourSettings::kStyleOwn ? s.variety > 0.01f
                                   : s.palette == ColourSettings::kAuto   ? true
                                                                          : s.variety > 1.0f);
    if (!started_) {
        if (custom) {
            to_ = from_ = ColourSettings::kCustom;
        } else if (s.palette >= 0) {
            to_ = from_ = s.palette;
        } else {
            to_ = -1;
            from_ = -1;
            choosePalette(0.5f, 0.5f);
            from_ = to_;
        }
        fade_ = 1.0f;
        sinceSwitch_ = 0.0f;
        sections_ = 0;
        started_ = true;
    } else if (walks && bigMoment && sinceSwitch_ >= kMinSection && fade_ >= 1.0f) {
        sections_++;
        choosePalette(arousal_, valence_);
    }
    fade_ = std::min(1.0f, fade_ + dt / kFadeSeconds);

    const float live = amount * presence_;
    const float rotation = live * tonicRotation(h);
    drift_ = follow(drift_, driverTarget(f, h), kDriftTau, dt);
    frame_.drift = amount * drift_ / 6.0f * (0.5f + 0.5f * clamp01(s.variety));
    tension_ = h.tension;
    const float tension = live * tension_;
    const float minor = clamp01((0.5f - h.mode) * 2.0f) * clamp01(h.clarity * 1.4f);
    const float major = clamp01((h.mode - 0.5f) * 2.0f) * clamp01(h.clarity * 1.4f);
    const float lightTarget = 0.6f * (f.pitchHeight - 0.5f) * 2.0f * clamp01(f.pitchConfidence * 1.5f) + 0.4f * (f.centroid - 0.45f) * 2.0f;
    light_ = follow(light_, std::clamp(lightTarget, -1.0f, 1.0f), kLightTau, dt);
    const float lift = live * (0.05f * light_ + 0.03f * major - 0.05f * minor);
    const float chroma = 1.0f - live * 0.15f * minor;

    // How far pal() leaves the style's own palette. A chosen palette, Auto or the picker's colours
    // replace it outright (the user asked for them); the style's own leaves home on loud sections and
    // big moments and comes back in quiet ones.
    float awayTarget = 1.0f;
    if (s.palette == ColourSettings::kStyleOwn) {
        awayTarget = amount * clamp01(s.variety) * presence_ * clamp01(0.2f + 0.45f * f.intensity + 0.55f * big_);
    }
    awayEase_ = follow(awayEase_, awayTarget, kAwayTau, dt);
    frame_.away = awayEase_;

    // The clash: alternate bands of the palette turn toward the complement, split-complementary (150°)
    // through to the full complement (180°) at peak tension (Itten's contrast of hue).
    const float clashAngle = 150.0f + 30.0f * clamp01((tension_ - 0.7f) / 0.3f);
    buildRamp(rotation, chroma, lift);
    frame_.hue = hueMatrix(rotation);
    frame_.clash = hueMatrix(rotation + clashAngle);
    frame_.clashAmount = 0.8f * tension;

    // The highlight: the palette's lightest colour turned toward the melody note's place on the wheel,
    // brighter on hits, stronger for bright timbres. grade() lays it over the brightest pixels only.
    hit_ = std::max(clamp01(f.kickEnv), clamp01(f.onsetEnv));
    const float turnTarget = f.pitchConfidence > 0.2f ? 15.0f * static_cast<float>(fifthsDistance(pitchClassOf(h.tonic), pitchClassOf(f.pitchClass))) : highlightTurn_;
    highlightTurn_ = follow(highlightTurn_, turnTarget, kHighlightTau, dt);
    Lab top = rotateHue(srgbToOklab(frame_.ramp[ColourFrame::kRamp - 1]), amount * highlightTurn_);
    top[0] = std::max(top[0], 0.82f);
    top[1] *= 1.2f;
    top[2] *= 1.2f;
    const Rgb hl = oklabToSrgb(top);
    const float hlAmount = std::min(0.5f, live * (0.1f + 0.35f * hit_) * (0.6f + 0.6f * f.centroid) * (0.3f + 0.7f * frame_.away));
    frame_.highlight = {hl[0], hl[1], hl[2], hlAmount};

    // grade(): tension deepens the shadows and drains a little colour, energy raises saturation and
    // contrast; loudness never sets lightness (Whiteford et al. 2018: across genres loud reads darker,
    // not lighter, so lightness stays with pitch and timbre above).
    frame_.shade = 0.55f * tension;
    frame_.contrast = live * (f.intensity - 0.5f) * 0.3f;
    frame_.saturation = live * ((arousal_ - 0.5f) * 0.5f - 0.15f * tension_ - 0.1f * (1.0f - valence_));
}

void ColourEngine::buildRamp(float rotation, float chromaGain, float lift) {
    const std::array<Lab, 5> a = stopsOf(from_);
    const std::array<Lab, 5> b = stopsOf(to_);
    // Curated palettes turn less than the style's own: enough to follow the key, not so far that a
    // sunset becomes a forest.
    const float reach = 0.4f + 0.6f * clamp01(settings_.variety / 2.0f);
    const float tension = clamp01(settings_.follow) * presence_ * tension_;
    const float clashAngle = 150.0f + 30.0f * clamp01((tension_ - 0.7f) / 0.3f);
    // The crossfade eases in and out, so a change starts and lands softly.
    const float k = fade_ * fade_ * (3.0f - 2.0f * fade_);
    for (int i = 0; i < ColourFrame::kRamp; i++) {
        const float t = static_cast<float>(i) / static_cast<float>(ColourFrame::kRamp - 1);
        Lab c = mixLab(sampleRamp(a, t), sampleRamp(b, t), k);
        c = rotateHue(c, rotation * reach + 0.8f * tension * clashAngle * clashBand(t));
        c[0] = std::clamp(c[0] + lift, 0.0f, 1.0f);
        c[1] *= chromaGain;
        c[2] *= chromaGain;
        frame_.ramp[static_cast<size_t>(i)] = oklabToSrgb(c);
    }
}

}  // namespace geode::viz::colour
