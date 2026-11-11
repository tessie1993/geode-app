#include "viz/Director.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

#include "sound/Form.hpp"
#include "viz/Circular.hpp"

namespace geode::viz {

using sound::Emotion;

namespace {

std::atomic<float> gAmount{0.0f};
std::atomic<uint64_t> gSeed{0};
std::atomic<bool> gSwitch{false};

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

// The colour wheel's one set of helpers (viz/Circular.hpp): hueDelta is the shortest way round, -0.5..0.5.
using circular::mixHue;
using circular::wrap01;
float hueDelta(float from, float to) { return circular::arcDelta(from, to); }
float lerp(float a, float b, float k) { return a + (b - a) * k; }
float clamp01(float x) { return std::clamp(x, 0.0f, 1.0f); }
float smooth(float x) {
    const float t = clamp01(x);
    return t * t * (3.0f - 2.0f * t);
}

// splitmix64, the house RNG (NoteResponse, ReactionDna): one stream per decision.
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

using Axis = Director::Axis;

// One field an axis turns, over the range its Customize slider covers. Each field has one axis.
struct AxisField {
    Axis axis;
    float SceneParams::*field;
    float lo;
    float hi;
    bool inverted;
};

// Twist is not here, on purpose. It spirals the frame about the screen centre in the shared view()
// and in the composite, so it draws the same S-curve on every style, and it rests at 0 in the
// middle of its range: an axis turned up pulled it toward +1 and one turned down toward -1, so
// Complexity twisted the picture whichever way it went, on every look ("almost all visuals have a
// yin-yang curve"). Twist stays the user's slider, the style's own reactions and the short kick below.
constexpr std::array<AxisField, 23> kAxisFields = {{
    {Axis::Energy, &SceneParams::reactAmount, 0.0f, 2.0f, false},
    {Axis::Energy, &SceneParams::audioDrive, 0.2f, 2.5f, false},
    {Axis::Energy, &SceneParams::motionAmount, 0.0f, 1.0f, false},
    {Axis::Energy, &SceneParams::bloom, 0.0f, 0.25f, false},
    {Axis::Energy, &SceneParams::intensity, 0.2f, 1.25f, false},
    {Axis::Speed, &SceneParams::speed, 0.05f, 4.0f, false},
    {Axis::Speed, &SceneParams::camFly, 0.0f, 2.0f, false},
    {Axis::Speed, &SceneParams::cycleSpeed, 0.02f, 0.6f, false},
    {Axis::Speed, &SceneParams::motionDrift, 0.0f, 1.0f, false},
    {Axis::Complexity, &SceneParams::tile, 1.0f, 6.0f, false},
    {Axis::Complexity, &SceneParams::warp, 0.0f, 1.0f, false},
    {Axis::Complexity, &SceneParams::morph, 0.0f, 1.0f, false},
    {Axis::Space, &SceneParams::camFov, 0.5f, 2.0f, false},
    {Axis::Space, &SceneParams::zoom, 0.3f, 3.0f, true},
    {Axis::Space, &SceneParams::camOrbit, 0.0f, 1.0f, false},
    {Axis::Space, &SceneParams::motionBreath, 0.0f, 1.0f, false},
    {Axis::Space, &SceneParams::vignette, 0.0f, 1.0f, true},
    {Axis::Space, &SceneParams::fisheye, -1.0f, 1.0f, false},
    {Axis::Chaos, &SceneParams::turbulence, 0.0f, 1.5f, false},
    {Axis::Chaos, &SceneParams::chromaAb, 0.0f, 1.0f, false},
    {Axis::Chaos, &SceneParams::ripple, 0.0f, 1.0f, false},
    {Axis::Chaos, &SceneParams::camShake, 0.0f, 1.0f, false},
    {Axis::Chaos, &SceneParams::grain, 0.0f, 1.0f, false},
}};

// Each axis's routes at a full turn up; the amount scales with the axis, and they are off at or below 0.
struct AxisRoute {
    Axis axis;
    ReactSource source;
    LfoTarget target;
    float amount;
    float attack;
    float release;
};

constexpr std::array<AxisRoute, 6> kAxisRoutes = {{
    {Axis::Energy, ReactSource::BassAtt, LfoTarget::Bloom, 0.3f, 0.02f, 0.3f},
    {Axis::Speed, ReactSource::CurvedTime, LfoTarget::Speed, 0.4f, 0.3f, 0.6f},
    {Axis::Complexity, ReactSource::DensitySlow, LfoTarget::Morph, 0.4f, 0.3f, 0.8f},
    {Axis::Space, ReactSource::StereoPan, LfoTarget::DriftX, 0.3f, 0.3f, 0.6f},
    // KickRandom holds its draw until the next kick, so this is a held twist, not a pulse: at 0.5
    // it kept every style half spiralled between kicks whenever Chaos was up. 0.2 is a nudge.
    {Axis::Chaos, ReactSource::KickRandom, LfoTarget::Twist, 0.2f, 0.05f, 0.1f},
    {Axis::Chaos, ReactSource::SnareToggle, LfoTarget::ChromaAb, 0.4f, 0.03f, 0.03f},
}};
static_assert(kAxisRoutes.size() <= ReactionEngine::kRoutes, "the axis routes fit one reaction engine");

float axisOf(const Director::Axes& x, Axis a) { return x[static_cast<size_t>(a)]; }
float& axisOf(Director::Axes& x, Axis a) { return x[static_cast<size_t>(a)]; }

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
    for (int i = 0; i < Director::kAxes; ++i) o.axes[i] = lerp(a.axes[i], b.axes[i], k);
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
void setGlobal(bool on, float amount, uint64_t seed) {
    gSwitch.store(on, std::memory_order_relaxed);
    gAmount.store(std::isfinite(amount) ? std::clamp(amount, 0.0f, 1.0f) : 0.0f, std::memory_order_relaxed);
    gSeed.store(seed, std::memory_order_relaxed);
}
DirectorConfig global() { return {gAmount.load(std::memory_order_relaxed), gSeed.load(std::memory_order_relaxed)}; }
bool switchOn() { return gSwitch.load(std::memory_order_relaxed); }
DirectorConfig forExport(bool switchOn, uint64_t seed, bool entitled, long long durationMs) {
    const bool directed = switchOn && (entitled || durationMs <= kFreeTasteMs);
    return {directed ? 1.0f : 0.0f, seed};
}
}  // namespace director

void Director::reset() {
    const DirectorConfig keep = config_;
    const bool configured = configured_;
    *this = Director();
    config_ = keep;
    configured_ = configured;
}

void Director::configure(const DirectorConfig& config) {
    // A new seed is a new play: forget the song so far and start deciding afresh. The look on screen
    // stays until the new play's first decision, which glides from it. Dropping it here snapped the
    // palette, the dials and the routes back to the user's own for the four to twelve seconds the
    // first decision waits, at every track change.
    if (configured_ && config.seed != config_.seed) {
        const float amount = amount_;
        const float presence = presence_;
        const Look shown = now_;
        const ReactionEngine routes = incoming_;
        const Routes routesBase = incomingBase_;
        const ReactionEngine keptAxisRoutes = axisReactions_;
        reset();
        amount_ = amount;
        presence_ = presence;
        from_ = to_ = now_ = shown;
        incoming_ = routes;
        incomingBase_ = routesBase;
        axisReactions_ = keptAxisRoutes;
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

    // Behaviour: the axes, the camera, the motion field. Each gets a little seeded variety.
    auto knob = [&](float x) { return std::clamp(0.8f * x + 0.15f * rng.signedUnit(), -1.0f, 1.0f); };
    axisOf(L.axes, Axis::Energy) = knob(2.0f * e - 1.0f + 0.3f * (g[Emotion::kPower] + g[Emotion::kJoy]) - 0.3f * g[Emotion::kPeace]);
    axisOf(L.axes, Axis::Speed) = knob(1.6f * e - 0.8f - 0.4f * (g[Emotion::kPeace] + g[Emotion::kTranscendence]) + 0.3f * g[Emotion::kJoy]);
    axisOf(L.axes, Axis::Complexity) =
        knob(1.5f * (g[Emotion::kWonder] + g[Emotion::kTension] + g[Emotion::kJoy]) - 1.2f * (g[Emotion::kSadness] + g[Emotion::kPeace]) + 0.3f * rng.signedUnit());
    axisOf(L.axes, Axis::Space) =
        knob(1.6f * (g[Emotion::kTranscendence] + g[Emotion::kWonder] + g[Emotion::kPeace]) - 1.2f * hard);
    axisOf(L.axes, Axis::Chaos) =
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
    Look best = make(feeling_, config_.seed, commits_ * kTries, drop);
    float bestDistance = -1.0f;
    for (int i = 0; i < kTries; ++i) {
        const Look candidate = i == 0 ? best : make(feeling_, config_.seed, commits_ * kTries + i, drop);
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

    // Nothing on screen to glide from: the first look of the first play. A look carried over from the
    // last play (configure) is glided from like any other.
    const bool fresh = commits_ == 0 && presence_ <= 0.0f;
    from_ = fresh ? best : now_;
    to_ = best;
    glide_ = fresh ? 1.0f : 0.0f;
    glideSeconds_ = std::max(glideSeconds, 0.05f);
    now_ = fresh ? best : now_;
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

void Director::step(const GeodeSoundFrame& f, float dt, std::uint32_t handMask) {
    if (!(dt > 0.0f)) return;
    emotion_.step(f, dt);
    step(f, emotion_.state(), dt, handMask);
}

void Director::step(const GeodeSoundFrame& f, const Emotion::State& heard, float dt, std::uint32_t handMask) {
    if (!(dt > 0.0f)) return;
    feeling_ = heard;
    amount_ += (config_.amount - amount_) * (1.0f - std::exp(-dt / 0.7f));
    if (config_.amount <= 0.0f && amount_ < 1e-3f) amount_ = 0.0f;
    const Emotion::State& s = feeling_;

    if (f.keyClarity >= 0.3f) {
        key_ = static_cast<int>(std::lround(f.keyRoot * 12.0f)) % 12 + (f.keyMajor >= 0.5f ? 0 : 12);
    }
    if (f.loudness > 0.15f) heard_ += dt;
    sinceCommit_ += dt;
    // A trigger's value is its strength (the beat tracker's confidence), not a level: any downbeat
    // or phrase the tracker fires counts, as with Form's events below.
    if (f.downbeat > 0.0f) barsSinceCommit_ += 1;
    const float bar = f.bpm > 0.0f && f.beatConf > 0.3f ? 240.0f / f.bpm : 2.0f;
    const float barGlide = std::clamp(bar, 1.0f, 4.0f);

    if (commits_ == 0) {
        // The first look waits for a key it can trust, or twelve seconds of music, whichever comes first:
        // in the first seconds the key detector is still settling, and the first look should not be
        // made for a key the song is not in.
        if (heard_ >= 4.0f && (s.confidence > 0.5f || heard_ >= 12.0f)) commit(Reason::First, 2.0f);
    } else if (sound::dropped(f)) {
        // Every event Form fires: it owns the floor (Form::kEventFloor), so a threshold here would only
        // lose the weaker ones.
        commit(Reason::Drop, 0.25f);
    } else if (sound::sectionChanged(f) && sinceCommit_ >= 4.0f) {
        commit(Reason::Section, barGlide);
    } else {
        // A modulation counts once it has held six seconds and lands on a downbeat (or anywhere, when
        // there is no beat to land on).
        const bool moved = key_ >= 0 && key_ != committedKey_ && f.keyClarity >= 0.3f;
        modulationSeconds_ = moved ? modulationSeconds_ + dt : 0.0f;
        const bool onBar = f.downbeat > 0.0f || f.beatConf < 0.3f;
        if (modulationSeconds_ >= 6.0f && onBar && sinceCommit_ >= 4.0f) {
            commit(Reason::Modulation, barGlide);
        } else if ((barsSinceCommit_ >= 32 && f.phrase > 0.0f) || sinceCommit_ >= 75.0f) {
            commit(Reason::Long, barGlide);
        }
    }

    if (commits_ > 0) presence_ += (1.0f - presence_) * (1.0f - std::exp(-dt / 1.0f));
    glide_ = std::min(1.0f, glide_ + dt / glideSeconds_);
    now_ = lerpLook(from_, to_, smooth(glide_));

    const float k = smooth(glide_);
    const float w = weight();
    const ReactSourceMask userOwned = hand::sources(handMask);
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

    // After everything the axes read (the look, buildUp_, dropBoost_).
    axisReactions_.routes = director::axisRoutes(axes());
    axisReactions_.tick(f, dt);
}

SceneParams Director::apply(const SceneParams& p, bool palWheel) const {
    const float a = weight();
    if (a <= 0.0f) return p;
    const Emotion::State& s = feeling_;
    const Look& L = now_;
    SceneParams o = p;

    // Between decisions the look breathes with the live feeling, read against the one it was made for.
    const float sat = L.saturation * (1.0f + 0.3f * (s.energy - L.energy) + 0.2f * dropBoost_);
    const float bright = L.brightness + 0.1f * (s.valence - L.valence) + 0.08f * dropBoost_;
    const float contrast = L.contrast + 0.2f * (s.tension - L.tension);
    // A build contracts the palette toward one hue (Huron's ITPRA: tension narrows), the drop releases it.
    const float range = L.range * (1.0f - 0.5f * buildUp_);

    // The palette, like the dials below, is base plus modulation: the look turns the user's palette
    // by its own distance from the default one, so picking another palette still changes the picture
    // with Auto on. Blending to the look's hue outright made the palette pickers dead at full weight.
    // A palette the user built (a LUT) is theirs whole: the look leaves it alone.
    static const SceneParams kDefaults{};
    if (p.paletteLut == SceneParams::kNoPaletteLut) {
        const auto turn = [](float from, float to) {
            const float d = wrap01(to - from);
            return d > 0.5f ? d - 1.0f : d;
        };
        const auto widen = [a](float user, float look, float base) { return std::max(user + a * (look - base), 0.0f); };
        const float baseHue = palHue(kDefaults.paletteBase(), kDefaults.paletteRange(), palWheel);
        const float baseHue2 = palHue(kDefaults.palette2Base(), kDefaults.palette2Range(), palWheel);
        const float hue = wrap01(palHue(p.paletteBase(), p.paletteRange(), palWheel) + a * turn(baseHue, L.hue));
        const float hue2 = wrap01(palHue(p.palette2Base(), p.palette2Range(), palWheel) + a * turn(baseHue2, L.hue2));
        const float r1 = widen(p.paletteRange(), range, kDefaults.paletteRange());
        const float r2 = widen(p.palette2Range(), L.range2, kDefaults.palette2Range());
        o.paletteBaseOverride = palBase(hue, r1, palWheel);
        o.paletteRangeOverride = r1;
        o.palette2BaseOverride = palBase(hue2, r2, palWheel);
        o.palette2RangeOverride = r2;
    }
    // The dials: base plus modulation. The look is what the director would make of an untouched
    // style, so it is added as its distance from the default; a slider the user moved keeps its
    // whole offset. Blending toward the look instead ignored the slider at full weight, which is
    // every Pro and trial play: Brightness and Saturation stopped answering while Auto was on.
    // Held to the dial's own range (ParamTable.inc): a low slider under a look below the default
    // would otherwise go negative, and a negative saturation inverts the colour.
    const auto over = [a](float user, float look, float base, float lo = 0.0f, float hi = 1.0f) {
        return std::clamp(user + a * (look - base), std::min(lo, user), std::max(hi, user));
    };
    o.paletteMix = over(p.paletteMix, L.mix, kDefaults.paletteMix);
    o.hueRange = over(p.hueRange, 1.0f, kDefaults.hueRange, 0.0f, 1.5f);
    o.saturation = over(p.saturation, std::clamp(sat, 0.2f, 1.5f), kDefaults.saturation, 0.0f, 1.5f);
    o.brightness = over(p.brightness, std::clamp(bright, 0.5f, 1.25f), kDefaults.brightness, 0.2f, 2.0f);
    o.temperature = over(p.temperature, L.temperature, kDefaults.temperature, -1.0f, 1.0f);
    o.contrast = over(p.contrast, std::clamp(contrast, 0.6f, 1.5f), kDefaults.contrast, 0.3f, 1.25f);
    o.milkdropPaletteTint = over(p.milkdropPaletteTint, 0.6f, kDefaults.milkdropPaletteTint);
    o.camSmooth = over(p.camSmooth, L.camSmooth, kDefaults.camSmooth);
    o.motionBreath = over(p.motionBreath, L.motionBreath, kDefaults.motionBreath);
    o.motionOrbit = over(p.motionOrbit, L.motionOrbit, kDefaults.motionOrbit);
    o.motionDrift = over(p.motionDrift, L.motionDrift, kDefaults.motionDrift);
    // The melody's degree: a short tint toward warm for the stable ones, cool for the dark ones, and a
    // little strain on the leaning ones.
    o.colorShift += a * accent_ * accentSign_ * (0.02f + 0.04f * accentTension_);
    o.camShake = std::clamp(o.camShake + a * accent_ * 0.2f * accentTension_, 0.0f, 1.0f);
    return o;
}

Director::Axes Director::axes() const {
    const float a = weight();
    if (a <= 0.0f) return {};
    const Emotion::State& s = feeling_;
    Axes d = now_.axes;
    axisOf(d, Axis::Energy) += 0.4f * (s.energy - now_.energy) + 0.4f * dropBoost_;
    axisOf(d, Axis::Chaos) += 0.4f * (s.tension - now_.tension) + 0.3f * dropBoost_;
    axisOf(d, Axis::Speed) += 0.3f * buildUp_;
    axisOf(d, Axis::Space) += 0.3f * dropBoost_ - 0.4f * buildUp_;
    for (float& v : d) v = std::clamp(a * v, -1.0f, 1.0f);
    return d;
}

SceneParams Director::applyAxes(const SceneParams& p) const { return director::applyAxes(p, axes()); }

CameraRig::Moves Director::moves() const {
    CameraRig::Moves o = director::axisMoves(axes());
    const float a = weight();
    if (a <= 0.0f) return o;
    const CameraRig::Moves& m = now_.moves;
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
    const SceneParams q = axisReactions_.apply(p);
    if (weight() <= 0.0f) return q;
    return incoming_.apply(outgoing_.apply(q));
}

namespace director {

// Base plus modulation, MilkDrop's rule: the value it is given is the base and an axis adds to it. The
// offset is what takes the style's default to the end at a full turn, so an untouched look moves as the
// old knobs moved it, and a slider the user moved carries its whole offset through. An axis never
// carries a field past the end it turns toward, and never drags back a value already beyond it.
SceneParams applyAxes(const SceneParams& p, const Director::Axes& x) {
    static const SceneParams kDefaults{};
    SceneParams out = p;
    for (const AxisField& d : kAxisFields) {
        const float v = axisOf(x, d.axis);
        if (v == 0.0f || !std::isfinite(v)) continue;
        const float up = d.inverted ? d.lo : d.hi;
        const float down = d.inverted ? d.hi : d.lo;
        const float end = v > 0.0f ? up : down;
        const float delta = std::fabs(v) * (end - std::clamp(kDefaults.*d.field, d.lo, d.hi));
        float& f = out.*d.field;
        if (!std::isfinite(f)) continue;
        if (delta > 0.0f) {
            f = std::min(f + delta, std::max(end, f));
        } else if (delta < 0.0f) {
            f = std::max(f + delta, std::min(end, f));
        }
    }
    return out;
}

CameraRig::Moves axisMoves(const Director::Axes& x) {
    const float space = 1.0f + axisOf(x, Axis::Space);
    const float chaos = axisOf(x, Axis::Chaos);
    CameraRig::Moves m;
    m.orbit = space;
    m.lean = space;
    m.dolly = space;
    m.breath = space;
    m.shake = std::max(0.0f, 1.0f + 1.5f * chaos);
    m.reframe = 1.0f + chaos;
    m.cut = 1.0f + chaos;
    return m;
}

Director::Routes axisRoutes(const Director::Axes& x) {
    Director::Routes out{};
    for (size_t i = 0; i < kAxisRoutes.size(); ++i) {
        const AxisRoute& a = kAxisRoutes[i];
        const float v = axisOf(x, a.axis);
        ReactRoute& r = out[i];
        r.enabled = v > 0.0f;
        r.source = a.source;
        r.target = a.target;
        r.mode = ReactMode::Follow;
        r.amount = a.amount * std::max(v, 0.0f);
        r.curve = ModCurve::Linear;
        r.attackSeconds = a.attack;
        r.releaseSeconds = a.release;
    }
    return out;
}

}  // namespace director

}  // namespace geode::viz
