#include "viz/Director.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace geode::viz {

using sound::Emotion;

namespace {

std::atomic<float> gAmount{0.0f};
std::atomic<uint64_t> gSeed{0};

// Where each GEMS feeling sits on the colour wheel (red 0, yellow 1/6, green 1/3, blue 2/3), after the
// colour-emotion studies: joy yellow-orange, power and tension red, sadness blue, peace green-teal,
// tenderness pink, nostalgia amber, wonder violet, transcendence sky blue (Jonauskaite et al. 2020, 30
// nations, r = .88; Palmer et al. 2013 and Whiteford et al. 2018 for music: happy and fast is lighter,
// yellower and more saturated, sad and slow darker, bluer and greyer).
constexpr std::array<float, Emotion::kGems> kAnchor = {
    0.76f,  // wonder: violet
    0.56f,  // transcendence: sky
    0.93f,  // tenderness: pink
    0.08f,  // nostalgia: amber
    0.42f,  // peace: green-teal
    0.00f,  // power: red
    0.13f,  // joy: yellow-orange
    0.97f,  // tension: crimson
    0.63f,  // sadness: blue
};

// What reacts to what, per feeling archetype.
struct Pick {
    ReactSource source;
    LfoTarget target;
    float amount;
    float attack;
    float release;
};
constexpr int kPerPool = 3;
using Pool = std::array<Pick, kPerPool>;
const std::array<Pool, Emotion::kGems> kPools = {{
    // Wonder: slow expansion and sparkle; harmony moves the colour, the melody lifts the eye.
    {{{ReactSource::ChordHue, LfoTarget::ColorShift, 0.25f, 1.0f, 2.0f},
      {ReactSource::PitchHeight, LfoTarget::CamPitch, 0.3f, 0.4f, 1.0f},
      {ReactSource::HarmonicChange, LfoTarget::Bloom, 0.3f, 0.05f, 1.2f}}},
    // Transcendence: vast and upward, the phrase turns the view.
    {{{ReactSource::PhrasePhase, LfoTarget::CamOrbit, 0.4f, 0.5f, 0.5f},
      {ReactSource::CurvedTime, LfoTarget::CamYaw, 0.2f, 1.0f, 1.0f},
      {ReactSource::Intensity, LfoTarget::Bloom, 0.25f, 0.5f, 1.0f}}},
    // Tenderness: round, close, breathing.
    {{{ReactSource::MidAtt, LfoTarget::Sway, 0.3f, 0.3f, 0.8f},
      {ReactSource::Level, LfoTarget::Bloom, 0.2f, 0.4f, 1.0f},
      {ReactSource::NoteChange, LfoTarget::CamPush, 0.15f, 0.2f, 1.0f}}},
    // Nostalgia: warm drift, chords change the light.
    {{{ReactSource::HarmonicChange, LfoTarget::Temperature, 0.3f, 0.2f, 2.0f},
      {ReactSource::DensitySlow, LfoTarget::Saturation, -0.25f, 0.5f, 1.5f},
      {ReactSource::Vibrato, LfoTarget::Sway, 0.3f, 0.3f, 1.0f}}},
    // Peace: horizontal, slow.
    {{{ReactSource::CurvedTime, LfoTarget::DriftX, 0.25f, 1.0f, 1.0f},
      {ReactSource::Level, LfoTarget::Zoom, 0.1f, 0.5f, 1.5f},
      {ReactSource::MidAtt, LfoTarget::CamOrbit, 0.2f, 0.5f, 1.0f}}},
    // Power: heavy, low, impacts.
    {{{ReactSource::Kick, LfoTarget::CamPush, 0.45f, 0.02f, 0.4f},
      {ReactSource::BassAtt, LfoTarget::Zoom, 0.3f, 0.03f, 0.4f},
      {ReactSource::Downbeat, LfoTarget::CamShake, 0.4f, 0.01f, 0.3f}}},
    // Joy: bouncy, bright.
    {{{ReactSource::Beat, LfoTarget::Bloom, 0.3f, 0.01f, 0.25f},
      {ReactSource::Snare, LfoTarget::Twist, 0.3f, 0.02f, 0.3f},
      {ReactSource::Kick, LfoTarget::Zoom, 0.2f, 0.02f, 0.3f}}},
    // Tension: angular, shaking.
    {{{ReactSource::Tension, LfoTarget::ChromaAb, 0.5f, 0.3f, 0.6f},
      {ReactSource::HatEnv, LfoTarget::CamShake, 0.3f, 0.01f, 0.2f},
      {ReactSource::Hfc, LfoTarget::Twist, 0.15f, 0.02f, 0.2f}}},
    // Sadness: sinking, dark, sparse.
    {{{ReactSource::Loudness, LfoTarget::Brightness, 0.2f, 0.8f, 2.0f},
      {ReactSource::PitchHeight, LfoTarget::CamPitch, -0.2f, 0.5f, 1.5f},
      {ReactSource::Level, LfoTarget::Vignette, 0.3f, 0.5f, 1.5f}}},
}};

float wrap01(float h) { return h - std::floor(h); }
// Shortest way round the colour wheel, -0.5..0.5.
float hueDelta(float from, float to) { return wrap01(to - from + 0.5f) - 0.5f; }
float mixHue(float from, float to, float k) { return wrap01(from + hueDelta(from, to) * k); }
float lerp(float a, float b, float k) { return a + (b - a) * k; }
float clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
float smooth(float x) {
    const float t = clamp01(x);
    return t * t * (3.0f - 2.0f * t);
}

// splitmix64, the house RNG (macros::surprise, ReactionDna): one stream per decision.
struct Rng {
    uint64_t x;
    float next() {
        x += 0x9E3779B97F4A7C15ull;
        uint64_t z = x;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return static_cast<float>(z >> 40) / 16777216.0f;
    }
    float signedUnit() { return 2.0f * next() - 1.0f; }
};

// A GEMS factor drawn with probability ∝ weight^power, skipping `not1` and `not2`.
int draw(const Emotion::GemsVec& g, Rng& rng, float power, int not1 = -1, int not2 = -1) {
    std::array<float, Emotion::kGems> w{};
    float sum = 0.0f;
    for (int i = 0; i < Emotion::kGems; ++i) {
        if (i == not1 || i == not2) continue;
        w[i] = std::pow(std::max(g[i], 0.0f), power);
        sum += w[i];
    }
    float u = rng.next() * sum;
    for (int i = 0; i < Emotion::kGems; ++i) {
        if (w[i] <= 0.0f) continue;
        if (u < w[i]) return i;
        u -= w[i];
    }
    for (int i = Emotion::kGems - 1; i >= 0; --i) {
        if (i != not1 && i != not2) return i;
    }
    return 0;
}

// The pal() cosine ramp runs the colour wheel backwards (lib_palette.glsl: red, then blue at a third,
// green at two thirds), so there a look centred on `hue` and `range` wide starts at -hue - range/2. MilkDrop's
// tint and the simulations (SceneCommon's hue::base) read the base as a hue and sweep forward from it.
float palBase(float hue, float range, bool palWheel) { return palWheel ? wrap01(-hue - 0.5f * range) : wrap01(hue - 0.5f * range); }
// The other way: the hue at the centre of a palette.
float palHue(float base, float range, bool palWheel) { return palWheel ? wrap01(-base - 0.5f * range) : wrap01(base + 0.5f * range); }

Director::Look lerpLook(const Director::Look& a, const Director::Look& b, float k) {
    Director::Look o = b;
    o.hue = mixHue(a.hue, b.hue, k);
    o.range = lerp(a.range, b.range, k);
    o.hue2 = mixHue(a.hue2, b.hue2, k);
    o.range2 = lerp(a.range2, b.range2, k);
    o.mix = lerp(a.mix, b.mix, k);
    o.saturation = lerp(a.saturation, b.saturation, k);
    o.brightness = lerp(a.brightness, b.brightness, k);
    o.temperature = lerp(a.temperature, b.temperature, k);
    o.contrast = lerp(a.contrast, b.contrast, k);
    for (int i = 0; i < kMacroCount; ++i) o.knobs[i] = lerp(a.knobs[i], b.knobs[i], k);
    o.camSmooth = lerp(a.camSmooth, b.camSmooth, k);
    o.motionBreath = lerp(a.motionBreath, b.motionBreath, k);
    o.motionOrbit = lerp(a.motionOrbit, b.motionOrbit, k);
    o.motionDrift = lerp(a.motionDrift, b.motionDrift, k);
    o.moves.orbit = lerp(a.moves.orbit, b.moves.orbit, k);
    o.moves.lean = lerp(a.moves.lean, b.moves.lean, k);
    o.moves.dolly = lerp(a.moves.dolly, b.moves.dolly, k);
    o.moves.breath = lerp(a.moves.breath, b.moves.breath, k);
    o.moves.shake = lerp(a.moves.shake, b.moves.shake, k);
    o.moves.reframe = lerp(a.moves.reframe, b.moves.reframe, k);
    o.moves.cut = lerp(a.moves.cut, b.moves.cut, k);
    o.valence = lerp(a.valence, b.valence, k);
    o.energy = lerp(a.energy, b.energy, k);
    o.tension = lerp(a.tension, b.tension, k);
    return o;
}

// The playing set at weight k, with its hands off every source the user set by hand (Customize › Music:
// "the director leaves it alone"). Checked per frame, so a moment set or reset mid-look takes effect at once.
Director::Routes scaled(const Director::Routes& base, float k, const ReactSourceMask& userOwned) {
    Director::Routes out = base;
    for (ReactRoute& r : out) {
        r.amount *= k;
        r.enabled = r.enabled && k > 0.0f && !userOwned[static_cast<size_t>(r.source)];
    }
    return out;
}

// Rejecting a draw: how far two looks are apart, in hue (doubled), width and saturation.
float lookDistance(const Director::Look& a, const Director::Look& b) {
    return 2.0f * std::fabs(hueDelta(a.hue, b.hue)) + std::fabs(a.range - b.range) + 0.5f * std::fabs(a.saturation - b.saturation);
}
constexpr float kMinDistance = 0.18f;
constexpr int kTries = 12;

}  // namespace

namespace director {
void setGlobal(float amount, uint64_t seed) {
    gAmount.store(std::isfinite(amount) ? std::clamp(amount, 0.0f, 1.0f) : 0.0f, std::memory_order_relaxed);
    gSeed.store(seed, std::memory_order_relaxed);
}
DirectorConfig global() { return {gAmount.load(std::memory_order_relaxed), gSeed.load(std::memory_order_relaxed)}; }
}  // namespace director

void Director::reset() {
    const DirectorConfig keep = config_;
    const bool configured = configured_;
    *this = Director();
    config_ = keep;
    configured_ = configured;
}

void Director::configure(const DirectorConfig& config) {
    // A new seed is a new play: forget the song so far and start deciding afresh.
    if (configured_ && config.seed != config_.seed) {
        const float amount = amount_;
        reset();
        amount_ = amount;
    }
    config_ = config;
    configured_ = true;
}

Director::Look Director::make(const Emotion::State& s, uint64_t seed, int index, bool drop) const {
    Rng rng{seed ^ (0xD1B54A32D192ED03ull * static_cast<uint64_t>(index + 1))};
    const auto& g = s.gems;
    const float e = drop ? std::max(s.energy, 0.85f) : s.energy;
    const float v = s.valence;
    const float t = s.tension;
    const float sad = g[Emotion::kSadness] + g[Emotion::kNostalgia];
    const float calm = g[Emotion::kPeace] + g[Emotion::kTenderness] + g[Emotion::kTranscendence];
    const float hard = g[Emotion::kTension] + g[Emotion::kPower];

    Look L;
    L.valence = v;
    L.energy = e;
    L.tension = t;
    L.primary = draw(g, rng, 2.0f);
    const int second = draw(g, rng, 1.0f, L.primary);
    const int third = draw(g, rng, 1.0f, L.primary, second);

    // Colour. The key turns the feeling's hue a little around the circle of fifths, so the same feeling
    // in another key is a neighbouring colour, and the sharp side leans warm, the flat side cool.
    const float fifths = static_cast<float>((s.root * 7) % 12) / 12.0f - 0.5f;
    const float keyTurn = s.scale >= 0 ? 0.08f * fifths + 0.03f * s.sharpness : 0.0f;
    L.hue = wrap01(kAnchor[L.primary] + keyTurn + 0.05f * rng.signedUnit());
    if (t > 0.55f) {
        // Strain: split complementary.
        L.range = 0.12f + 0.13f * rng.next();
        L.hue2 = wrap01(L.hue + 0.5f + 0.08f * rng.signedUnit());
        L.mix = 0.25f + 0.3f * t;
    } else if (sad > 0.45f && e < 0.5f) {
        // Melancholy: nearly one colour.
        L.range = 0.03f + 0.05f * rng.next();
        L.hue2 = wrap01(L.hue + 0.04f * rng.signedUnit());
        L.mix = 0.2f;
    } else {
        // Consonance: neighbours, wider the more joyful and energetic.
        const float lift = g[Emotion::kJoy] + g[Emotion::kWonder] + 0.3f;
        L.range = std::clamp(0.1f + 0.35f * e * lift + 0.05f * rng.signedUnit(), 0.06f, 0.6f);
        L.hue2 = wrap01(kAnchor[second] + keyTurn + 0.05f * rng.signedUnit());
        L.mix = 0.15f + 0.25f * rng.next();
    }
    L.range2 = std::clamp(L.range * (0.7f + 0.6f * rng.next()), 0.03f, 0.6f);
    // Valdez & Mehrabian 1994: saturation carries arousal, brightness pleasure.
    L.saturation = std::clamp(0.55f + 0.6f * e - 0.35f * sad + 0.25f * g[Emotion::kJoy] + 0.08f * rng.signedUnit(), 0.3f, 1.4f);
    L.brightness = std::clamp(0.88f + 0.18f * v + 0.1f * g[Emotion::kJoy] - 0.12f * g[Emotion::kSadness] + 0.04f * rng.signedUnit(), 0.6f, 1.2f);
    L.temperature = std::clamp(0.35f * v + 0.5f * g[Emotion::kNostalgia] - 0.35f * g[Emotion::kSadness] + 0.1f * s.sharpness, -0.8f, 0.8f);
    L.contrast = std::clamp(0.95f + 0.35f * t + 0.25f * g[Emotion::kPower] - 0.2f * g[Emotion::kPeace], 0.7f, 1.4f);

    // Behaviour: the macro knobs, the camera, the motion field. Each gets a little seeded variety.
    auto knob = [&](float x) { return std::clamp(0.8f * x + 0.15f * rng.signedUnit(), -1.0f, 1.0f); };
    L.knobs[static_cast<int>(Macro::Energy)] = knob(2.0f * e - 1.0f + 0.3f * (g[Emotion::kPower] + g[Emotion::kJoy]) - 0.3f * g[Emotion::kPeace]);
    L.knobs[static_cast<int>(Macro::Speed)] = knob(1.6f * e - 0.8f - 0.4f * (g[Emotion::kPeace] + g[Emotion::kTranscendence]) + 0.3f * g[Emotion::kJoy]);
    L.knobs[static_cast<int>(Macro::Complexity)] =
        knob(1.5f * (g[Emotion::kWonder] + g[Emotion::kTension] + g[Emotion::kJoy]) - 1.2f * (g[Emotion::kSadness] + g[Emotion::kPeace]) + 0.3f * rng.signedUnit());
    L.knobs[static_cast<int>(Macro::Mood)] = 0.0f;  // the palette above is the director's mood
    L.knobs[static_cast<int>(Macro::Space)] =
        knob(1.6f * (g[Emotion::kTranscendence] + g[Emotion::kWonder] + g[Emotion::kPeace]) - 1.2f * hard);
    L.knobs[static_cast<int>(Macro::Chaos)] =
        knob(1.4f * t - 0.7f + 0.4f * g[Emotion::kPower] - 0.5f * (g[Emotion::kTenderness] + g[Emotion::kPeace]));
    L.camSmooth = std::clamp(0.5f + 0.5f * calm - 0.6f * hard + 0.1f * rng.signedUnit(), 0.1f, 0.9f);
    L.motionBreath = clamp01(0.3f + 0.9f * (g[Emotion::kTenderness] + g[Emotion::kPeace] + g[Emotion::kSadness]));
    L.motionOrbit = clamp01(0.3f + 1.2f * (g[Emotion::kTranscendence] + g[Emotion::kWonder]));
    L.motionDrift = clamp01(0.3f + 0.9f * (g[Emotion::kNostalgia] + g[Emotion::kPeace] + g[Emotion::kTranscendence]));
    auto move = [](float x) { return std::clamp(x, 0.0f, 2.5f); };
    L.moves.orbit = move(0.6f + 1.2f * (g[Emotion::kTranscendence] + g[Emotion::kWonder] + g[Emotion::kPeace]) - 0.4f * g[Emotion::kPower]);
    L.moves.lean = move(0.7f + 1.0f * (g[Emotion::kWonder] + g[Emotion::kTenderness]));
    L.moves.dolly = move(0.6f + 1.2f * (g[Emotion::kPower] + g[Emotion::kJoy]));
    L.moves.breath = move(0.6f + 1.2f * (g[Emotion::kTenderness] + g[Emotion::kPeace] + g[Emotion::kSadness]));
    L.moves.shake = move(0.3f + 1.8f * hard);
    L.moves.reframe = move(0.5f + 1.5f * g[Emotion::kJoy] + g[Emotion::kPower]);
    L.moves.cut = move(0.4f + 1.5f * (g[Emotion::kPower] + g[Emotion::kJoy]) - g[Emotion::kPeace]);

    // What reacts to what: two from the leading feeling, one each from the next two, louder with energy.
    const float gain = 0.6f + 0.6f * e;
    int n = 0;
    auto add = [&](const Pick& p) {
        if (n >= kRoutes) return;
        for (int i = 0; i < n; ++i) {
            if (L.routes[i].target == p.target) return;  // one route per target, so none fights another
        }
        ReactRoute& r = L.routes[n++];
        r.enabled = true;
        r.source = p.source;
        r.target = p.target;
        r.mode = ReactMode::Follow;
        r.amount = p.amount * gain;
        r.curve = ModCurve::Linear;
        r.attackSeconds = p.attack;
        r.releaseSeconds = p.release;
    };
    const int skip = static_cast<int>(rng.next() * kPerPool) % kPerPool;
    for (int i = 0; i < kPerPool; ++i) {
        if (i != skip) add(kPools[L.primary][i]);
    }
    add(kPools[second][static_cast<int>(rng.next() * kPerPool) % kPerPool]);
    add(kPools[third][static_cast<int>(rng.next() * kPerPool) % kPerPool]);
    return L;
}

bool Director::farFromRecent(const Look& candidate) const {
    for (int i = 0; i < recentCount_; ++i) {
        if (lookDistance(candidate, recent_[i]) < kMinDistance) return false;
    }
    return true;
}

void Director::commit(Reason reason, float glideSeconds) {
    const bool drop = reason == Reason::Drop;
    // Same feeling, never the same picture: draw until the look is far from the last six, else keep the
    // farthest of the draws.
    Look best = make(emotion_.state(), config_.seed, commits_ * kTries, drop);
    float bestDistance = -1.0f;
    for (int i = 0; i < kTries; ++i) {
        const Look candidate = i == 0 ? best : make(emotion_.state(), config_.seed, commits_ * kTries + i, drop);
        if (farFromRecent(candidate)) {
            best = candidate;
            break;
        }
        float nearest = 1e9f;
        for (int j = 0; j < recentCount_; ++j) nearest = std::min(nearest, lookDistance(candidate, recent_[j]));
        if (nearest > bestDistance) {
            bestDistance = nearest;
            best = candidate;
        }
    }
    for (int i = kRecent - 1; i > 0; --i) recent_[i] = recent_[i - 1];
    recent_[0] = best;
    recentCount_ = std::min(recentCount_ + 1, kRecent);

    from_ = commits_ == 0 ? best : now_;
    to_ = best;
    glide_ = commits_ == 0 ? 1.0f : 0.0f;
    glideSeconds_ = std::max(glideSeconds, 0.05f);
    now_ = commits_ == 0 ? best : now_;
    // The routes cross over: the playing set fades out while the new one fades in.
    outgoing_ = incoming_;
    outgoingBase_ = incomingBase_;
    incoming_.reset();
    incomingBase_ = best.routes;
    commits_ += 1;
    reason_ = reason;
    sinceCommit_ = 0.0f;
    barsSinceCommit_ = 0;
    modulationSeconds_ = 0.0f;
    committedKey_ = key_;
    if (drop) dropBoost_ = 1.0f;
}

void Director::step(const GeodeSoundFrame& f, float dt) {
    if (!(dt > 0.0f)) return;
    amount_ += (config_.amount - amount_) * (1.0f - std::exp(-dt / 0.7f));
    if (config_.amount <= 0.0f && amount_ < 1e-3f) amount_ = 0.0f;
    emotion_.step(f, dt);
    const Emotion::State& s = emotion_.state();

    if (f.keyClarity >= 0.3f) {
        key_ = static_cast<int>(std::lround(f.keyRoot * 12.0f)) % 12 + (f.keyMajor >= 0.5f ? 0 : 12);
    }
    if (f.loudness > 0.15f) heard_ += dt;
    sinceCommit_ += dt;
    if (f.downbeat > 0.5f) barsSinceCommit_ += 1;
    const float bar = f.bpm > 0.0f && f.beatConf > 0.3f ? 240.0f / f.bpm : 2.0f;
    const float barGlide = std::clamp(bar, 1.0f, 4.0f);

    if (commits_ == 0) {
        // The first look waits for a key it can trust, or twelve seconds of music, whichever comes first:
        // in the first seconds the key detector is still settling, and the first look should not be
        // made for a key the song is not in.
        if (heard_ >= 4.0f && (s.confidence > 0.5f || heard_ >= 12.0f)) commit(Reason::First, 2.0f);
    } else if (f.drop > 0.5f) {
        commit(Reason::Drop, 0.25f);
    } else if (f.sectionChange > 0.5f && sinceCommit_ >= 4.0f) {
        commit(Reason::Section, barGlide);
    } else {
        // A modulation counts once it has held six seconds and lands on a downbeat (or anywhere, when
        // there is no beat to land on).
        const bool moved = key_ >= 0 && key_ != committedKey_ && f.keyClarity >= 0.3f;
        modulationSeconds_ = moved ? modulationSeconds_ + dt : 0.0f;
        const bool onBar = f.downbeat > 0.5f || f.beatConf < 0.3f;
        if (modulationSeconds_ >= 6.0f && onBar && sinceCommit_ >= 4.0f) {
            commit(Reason::Modulation, barGlide);
        } else if ((barsSinceCommit_ >= 32 && f.phrase > 0.5f) || sinceCommit_ >= 75.0f) {
            commit(Reason::Long, barGlide);
        }
    }

    if (commits_ > 0) presence_ += (1.0f - presence_) * (1.0f - std::exp(-dt / 1.0f));
    glide_ = std::min(1.0f, glide_ + dt / glideSeconds_);
    now_ = lerpLook(from_, to_, smooth(glide_));

    const float k = smooth(glide_);
    const float w = weight();
    const ReactSourceMask userOwned = hand::sources(hand::global());
    outgoing_.routes = scaled(outgoingBase_, (1.0f - k) * w, userOwned);
    incoming_.routes = scaled(incomingBase_, k * w, userOwned);
    outgoing_.tick(f, dt);
    incoming_.tick(f, dt);

    buildUp_ = f.buildUp;
    dropBoost_ *= std::exp(-dt / 3.0f);
    if (s.degreeAccent > 0.5f) {
        accent_ = 1.0f;
        accentSign_ = s.degreeValence >= 0.0f ? 1.0f : -1.0f;
        accentTension_ = s.degreeTension;
    } else {
        accent_ *= std::exp(-dt / 0.5f);
    }
}

SceneParams Director::apply(const SceneParams& p, bool palWheel) const {
    const float a = weight();
    if (a <= 0.0f) return p;
    const Emotion::State& s = emotion_.state();
    const Look& L = now_;
    SceneParams o = p;

    // Between decisions the look breathes with the live feeling, read against the one it was made for.
    const float sat = L.saturation * (1.0f + 0.3f * (s.energy - L.energy) + 0.2f * dropBoost_);
    const float bright = L.brightness + 0.1f * (s.valence - L.valence) + 0.08f * dropBoost_;
    const float contrast = L.contrast + 0.2f * (s.tension - L.tension);
    // A build contracts the palette toward one hue (Huron's ITPRA: tension narrows), the drop releases it.
    const float range = L.range * (1.0f - 0.5f * buildUp_);

    const float userHue = palHue(p.paletteBase(), p.paletteRange(), palWheel);
    const float userHue2 = palHue(p.palette2Base(), p.palette2Range(), palWheel);
    const float hue = mixHue(userHue, L.hue, a);
    const float r1 = lerp(p.paletteRange(), range, a);
    const float hue2 = mixHue(userHue2, L.hue2, a);
    const float r2 = lerp(p.palette2Range(), L.range2, a);
    o.paletteBaseOverride = palBase(hue, r1, palWheel);
    o.paletteRangeOverride = std::max(r1, 0.0f);
    o.palette2BaseOverride = palBase(hue2, r2, palWheel);
    o.palette2RangeOverride = std::max(r2, 0.0f);
    o.paletteMix = lerp(p.paletteMix, L.mix, a);
    if (a > 0.5f) o.paletteLut = SceneParams::kNoPaletteLut;
    o.hueRange = lerp(p.hueRange, 1.0f, a);
    o.saturation = lerp(p.saturation, std::clamp(sat, 0.2f, 1.5f), a);
    o.brightness = lerp(p.brightness, std::clamp(bright, 0.5f, 1.25f), a);
    o.temperature = lerp(p.temperature, L.temperature, a);
    o.contrast = lerp(p.contrast, std::clamp(contrast, 0.6f, 1.5f), a);
    o.milkdropPaletteTint = lerp(p.milkdropPaletteTint, 0.6f, a);
    o.camSmooth = lerp(p.camSmooth, L.camSmooth, a);
    o.motionBreath = lerp(p.motionBreath, L.motionBreath, a);
    o.motionOrbit = lerp(p.motionOrbit, L.motionOrbit, a);
    o.motionDrift = lerp(p.motionDrift, L.motionDrift, a);
    // The melody's degree: a short tint toward warm for the stable ones, cool for the dark ones, and a
    // little strain on the leaning ones.
    o.colorShift += a * accent_ * accentSign_ * (0.02f + 0.04f * accentTension_);
    o.camShake = std::clamp(o.camShake + a * accent_ * 0.2f * accentTension_, 0.0f, 1.0f);
    return o;
}

MacroKnobs Director::knobs(const MacroKnobs& user) const {
    const float a = weight();
    if (a <= 0.0f) return user;
    const Emotion::State& s = emotion_.state();
    MacroKnobs d = now_.knobs;
    d[static_cast<int>(Macro::Energy)] += 0.4f * (s.energy - now_.energy) + 0.4f * dropBoost_;
    d[static_cast<int>(Macro::Chaos)] += 0.4f * (s.tension - now_.tension) + 0.3f * dropBoost_;
    d[static_cast<int>(Macro::Speed)] += 0.3f * buildUp_;
    d[static_cast<int>(Macro::Space)] += 0.3f * dropBoost_ - 0.4f * buildUp_;
    MacroKnobs out = user;
    for (int i = 0; i < kMacroCount; ++i) out[i] = std::clamp(user[i] + a * d[i], -1.0f, 1.0f);
    return out;
}

CameraRig::Moves Director::moves(const CameraRig::Moves& base) const {
    const float a = weight();
    if (a <= 0.0f) return base;
    const CameraRig::Moves& m = now_.moves;
    CameraRig::Moves o = base;
    o.orbit *= lerp(1.0f, m.orbit, a);
    o.lean *= lerp(1.0f, m.lean, a);
    o.dolly *= lerp(1.0f, m.dolly, a);
    o.breath *= lerp(1.0f, m.breath, a);
    o.shake *= lerp(1.0f, m.shake, a);
    o.reframe *= lerp(1.0f, m.reframe, a);
    o.cut *= lerp(1.0f, m.cut, a);
    return o;
}

SceneParams Director::applyRoutes(const SceneParams& p) const {
    if (weight() <= 0.0f) return p;
    return incoming_.apply(outgoing_.apply(p));
}

}  // namespace geode::viz
