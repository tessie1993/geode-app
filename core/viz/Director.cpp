#include "viz/Director.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <initializer_list>

#include "sound/Form.hpp"

namespace geode::viz {

using sound::Emotion;

namespace {

std::atomic<float> gAmount{0.0f};
std::atomic<uint64_t> gSeed{0};
std::atomic<bool> gSwitch{false};

constexpr float kUntimedGlideSeconds = 2.0f;

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
// yin-yang curve"). Twist stays the user's slider and the style's own reactions.
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

// No axis routes a sound to a param, on purpose (owner's rule: no hardcoded reactions). Each axis used
// to play fixed routes on every style while it was up (bass to Bloom, curved time to Speed, density to
// Morph, stereo pan to Drift, the kick's random to Twist, the snare toggle to Chroma), which no one could
// see or edit. What reacts to what is the reaction matrix alone: the user's routes, else the style's DNA.

float axisOf(const Director::Axes& x, Axis a) { return x[static_cast<size_t>(a)]; }
float& axisOf(Director::Axes& x, Axis a) { return x[static_cast<size_t>(a)]; }

Director::Look lerpLook(const Director::Look& a, const Director::Look& b, float k) {
    Director::Look o = b;
    for (int i = 0; i < Director::kAxes; ++i) o.axes[i] = lerp(a.axes[i], b.axes[i], k);
    o.energy = lerp(a.energy, b.energy, k);
    o.camSmooth = lerp(a.camSmooth, b.camSmooth, k);
    o.motionBreath = lerp(a.motionBreath, b.motionBreath, k);
    o.motionDrift = lerp(a.motionDrift, b.motionDrift, k);
    o.moves.orbit = lerp(a.moves.orbit, b.moves.orbit, k);
    o.moves.lean = lerp(a.moves.lean, b.moves.lean, k);
    o.moves.dolly = lerp(a.moves.dolly, b.moves.dolly, k);
    o.moves.breath = lerp(a.moves.breath, b.moves.breath, k);
    o.moves.shake = lerp(a.moves.shake, b.moves.shake, k);
    o.moves.reframe = lerp(a.moves.reframe, b.moves.reframe, k);
    o.moves.cut = lerp(a.moves.cut, b.moves.cut, k);
    return o;
}

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
    // dials back to the user's own for the four to twelve seconds the first decision waits, at every
    // track change.
    if (configured_ && config.seed != config_.seed) {
        const float amount = amount_;
        const float presence = presence_;
        const Look shown = now_;
        reset();
        amount_ = amount;
        presence_ = presence;
        from_ = to_ = now_ = shown;
    }
    config_ = config;
    configured_ = true;
}

Director::Look Director::make(const Emotion::State& s, uint64_t seed, int index, bool drop) const {
    Rng rng{seed ^ (0xD1B54A32D192ED03ull * static_cast<uint64_t>(index + 1))};
    const auto& g = s.gems;
    const float e = drop ? std::max(s.energy, 0.85f) : s.energy;
    const float t = s.tension;
    const float calm = g[Emotion::kPeace] + g[Emotion::kTenderness] + g[Emotion::kTranscendence];
    const float hard = g[Emotion::kTension] + g[Emotion::kPower];

    Look L;
    L.energy = e;
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
    L.motionDrift = clamp01(0.3f + 0.9f * (g[Emotion::kNostalgia] + g[Emotion::kPeace] + g[Emotion::kTranscendence]));
    auto move = [](float x) { return std::clamp(x, 0.0f, 2.5f); };
    L.moves.orbit = move(0.6f + 1.2f * (g[Emotion::kTranscendence] + g[Emotion::kWonder] + g[Emotion::kPeace]) - 0.4f * g[Emotion::kPower]);
    L.moves.lean = move(0.7f + 1.0f * (g[Emotion::kWonder] + g[Emotion::kTenderness]));
    L.moves.dolly = move(0.6f + 1.2f * (g[Emotion::kPower] + g[Emotion::kJoy]));
    L.moves.breath = move(0.6f + 1.2f * (g[Emotion::kTenderness] + g[Emotion::kPeace] + g[Emotion::kSadness]));
    L.moves.shake = move(0.3f + 1.8f * hard);
    L.moves.reframe = move(0.5f + 1.5f * g[Emotion::kJoy] + g[Emotion::kPower]);
    L.moves.cut = move(0.4f + 1.5f * (g[Emotion::kPower] + g[Emotion::kJoy]) - g[Emotion::kPeace]);
    return L;
}

void Director::commit(Reason reason, float glideSeconds) {
    const Look best = make(feeling_, config_.seed, commits_, reason == Reason::Drop);

    // Nothing on screen to glide from: the first look of the first play. A look carried over from the
    // last play (configure) is glided from like any other.
    const bool fresh = commits_ == 0 && presence_ <= 0.0f;
    from_ = fresh ? best : now_;
    to_ = best;
    glide_ = fresh ? 1.0f : 0.0f;
    glideSeconds_ = std::max(glideSeconds, 0.05f);
    now_ = fresh ? best : now_;
    commits_ += 1;
    reason_ = reason;
    sinceCommit_ = 0.0f;
    barsSinceCommit_ = 0;
    modulationSeconds_ = 0.0f;
    committedKey_ = key_;
}

void Director::step(const GeodeSoundFrame& f, float dt) {
    if (!(dt > 0.0f)) return;
    emotion_.step(f, dt);
    step(f, emotion_.state(), dt);
}

void Director::step(const GeodeSoundFrame& f, const Emotion::State& heard, float dt) {
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
    // A look glides in over a bar of the tempo the tracker hears. With no tempo heard none is assumed:
    // the glide is a plain two seconds.
    const float barGlide = f.bpm > 0.0f && f.beatConf > 0.3f ? std::clamp(240.0f / f.bpm, 1.0f, 4.0f) : kUntimedGlideSeconds;

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
}

SceneParams Director::apply(const SceneParams& p) const {
    const float a = weight();
    if (a <= 0.0f) return p;
    const Look& L = now_;
    SceneParams o = p;
    // Colour is not here, on purpose: the palette, saturation, brightness, temperature and contrast
    // are the style's and the user's. The looks drew them all from nine hue anchors and one
    // saturation curve, so under Auto every style wore the same few colours.
    static const SceneParams kDefaults{};
    // The camera's spring softness is base plus modulation: the look is what the director would make
    // of an untouched style, added as its distance from the default, so a slider the user moved keeps
    // its whole offset.
    o.camSmooth = std::clamp(p.camSmooth + a * (L.camSmooth - kDefaults.camSmooth), std::min(0.0f, p.camSmooth), std::max(1.0f, p.camSmooth));
    // The motion dials scale how far the music moves the picture (viz/MotionField), so the look turns
    // them by a factor, never an offset: a dial the user set to 0 stays off, and the director cannot
    // switch on a reaction to the sound the user switched off.
    const auto turned = [a](float user, float look, float base) {
        return std::clamp(user * (1.0f + 2.0f * a * (look - base)), 0.0f, std::max(1.0f, user));
    };
    o.motionBreath = turned(p.motionBreath, L.motionBreath, kDefaults.motionBreath);
    o.motionDrift = turned(p.motionDrift, L.motionDrift, kDefaults.motionDrift);
    return o;
}

Director::Axes Director::axes() const {
    const float a = weight();
    if (a <= 0.0f) return {};
    // The look as decided, held until the next decision: nothing here follows the frame, so between
    // decisions the picture moves only by the reaction matrix and the user's motion dials.
    Axes d = now_.axes;
    for (float& v : d) v = std::clamp(a * v, -1.0f, 1.0f);
    return d;
}

SceneParams Director::applyAxes(const SceneParams& p) const {
    SceneParams o = director::applyAxes(p, axes());
    // Energy, Speed and Space turn the Motion dials too, but a dial held at 0 is the music's push
    // switched off, and stays off: the director never switches on a reaction the user switched off.
    for (float SceneParams::*dial : {&SceneParams::motionAmount, &SceneParams::motionBreath, &SceneParams::motionDrift}) {
        if (p.*dial <= 0.0f) o.*dial = 0.0f;
    }
    return o;
}

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

}  // namespace director

}  // namespace geode::viz
