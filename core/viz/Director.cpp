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

Director::Look lerpLook(const Director::Look& a, const Director::Look& b, float k) {
    Director::Look o = b;
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
    // Behaviour: the macro knobs, the camera, the motion field. Each gets a little seeded variety.
    auto knob = [&](float x) { return std::clamp(0.8f * x + 0.15f * rng.signedUnit(), -1.0f, 1.0f); };
    L.knobs[static_cast<int>(Macro::Energy)] = knob(2.0f * e - 1.0f + 0.3f * (g[Emotion::kPower] + g[Emotion::kJoy]) - 0.3f * g[Emotion::kPeace]);
    L.knobs[static_cast<int>(Macro::Speed)] = knob(1.6f * e - 0.8f - 0.4f * (g[Emotion::kPeace] + g[Emotion::kTranscendence]) + 0.3f * g[Emotion::kJoy]);
    L.knobs[static_cast<int>(Macro::Complexity)] =
        knob(1.5f * (g[Emotion::kWonder] + g[Emotion::kTension] + g[Emotion::kJoy]) - 1.2f * (g[Emotion::kSadness] + g[Emotion::kPeace]) + 0.3f * rng.signedUnit());
    L.knobs[static_cast<int>(Macro::Mood)] = 0.0f;  // colour is the style's and the user's, never the director's
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
    o.motionOrbit = turned(p.motionOrbit, L.motionOrbit, kDefaults.motionOrbit);
    o.motionDrift = turned(p.motionDrift, L.motionDrift, kDefaults.motionDrift);
    return o;
}

SceneParams Director::direct(const SceneParams& p, const MacroKnobs& user) const {
    if (weight() <= 0.0f) return macros::apply(p, user);
    SceneParams o = macros::apply(apply(p), knobs(user));
    const SceneParams own = macros::apply(p, user);
    for (float SceneParams::*dial : {&SceneParams::motionAmount, &SceneParams::motionBreath, &SceneParams::motionOrbit, &SceneParams::motionDrift}) {
        if (own.*dial <= 0.0f) o.*dial = 0.0f;
    }
    return o;
}

MacroKnobs Director::knobs(const MacroKnobs& user) const {
    const float a = weight();
    if (a <= 0.0f) return user;
    // The look as decided, held until the next decision: nothing here follows the frame, so between
    // decisions the picture moves only by the reaction matrix and the user's motion dials.
    MacroKnobs out = user;
    for (int i = 0; i < kMacroCount; ++i) out[i] = std::clamp(user[i] + a * now_.knobs[i], -1.0f, 1.0f);
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

}  // namespace geode::viz
