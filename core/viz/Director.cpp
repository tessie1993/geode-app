#include "viz/Director.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <initializer_list>

#include "sound/Form.hpp"

namespace geode::viz {

using sound::Emotion;

namespace {

std::atomic<float> gAmount{0.0f};
std::atomic<uint64_t> gSeed{0};
std::atomic<bool> gSwitch{false};

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
// Spin (motionDrift) is not here either: it is the user's turn of a turning style and the director never
// reaches it (#1181); a Speed axis scaling it set every turning style spinning faster under Auto.
// Nor is the reaction matrix's amount (reactAmount): a calm look turned it toward 0, so Auto muted every
// route the user sees, and its Energy reading once leaned on the tempo. The matrix is the user's to turn.
constexpr std::array<AxisField, 20> kAxisFields = {{
    {Axis::Energy, &SceneParams::audioDrive, 0.2f, 2.5f, false},
    {Axis::Energy, &SceneParams::motionAmount, 0.0f, 1.0f, false},
    {Axis::Energy, &SceneParams::bloom, 0.0f, 0.25f, false},
    {Axis::Energy, &SceneParams::intensity, 0.2f, 1.25f, false},
    {Axis::Speed, &SceneParams::speed, 0.05f, 4.0f, false},
    {Axis::Speed, &SceneParams::camFly, 0.0f, 2.0f, false},
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

// The build's lean at full: faster, closer and rougher, the anticipation before a drop (Huron's ITPRA:
// tension rises and attention narrows). It is the director's own behaviour over its axes, decided at
// Form's state changes, never a reaction to a frame: the drums and the build's level move nothing here.
constexpr Director::Axes kBuildLean = {0.0f, 0.3f, 0.0f, -0.4f, 0.25f};  // Energy, Speed, Complexity, Space, Chaos
// How many bars the lean takes to come in (builds run 4–8 bars, and Form names one some way in), and to
// let go when a build fades without a drop.
constexpr float kLeanInBars = 4.0f;
constexpr float kLeanOutBars = 1.0f;
// A section event Form has not named after this many downbeats, or seconds, is let go unnamed: its look
// lands as made and nothing is remembered. Form names one on the next downbeat, so this is a fallback.
constexpr int kNamingBars = 2;
constexpr float kNamingSeconds = 8.0f;

Director::Look lerpLook(const Director::Look& a, const Director::Look& b, float k) {
    // Landed is the look itself, bit for bit (a + (b - a) can miss b by a rounding), so a remembered
    // section's look comes back exactly as it was.
    if (k >= 1.0f) return b;
    Director::Look o = b;
    for (int i = 0; i < Director::kAxes; ++i) o.axes[i] = lerp(a.axes[i], b.axes[i], k);
    o.energy = lerp(a.energy, b.energy, k);
    o.camSmooth = lerp(a.camSmooth, b.camSmooth, k);
    o.motionBreath = lerp(a.motionBreath, b.motionBreath, k);
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
        const float lean = lean_;
        reset();
        amount_ = amount;
        presence_ = presence;
        from_ = to_ = now_ = shown;
        // The lean lets go on the new play's frames, not in one jump. The remembered sections are not
        // carried: they were the last song's.
        lean_ = lean;
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
    // No Spin (motionDrift): the look turned it up for nostalgia, peace and transcendence, which set every
    // turning style spinning faster under Auto after #1181 made Spin the user's alone.
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

float Director::changeBars(const Emotion::State& s) {
    // make() lets the feeling choose what a look is; this lets it choose how the look arrives, the way a
    // VJ fades a ballad's change and cuts a banger's. The GEMS sum to 1, so the balance is -1..1.
    const auto& g = s.gems;
    const float soft = g[Emotion::kPeace] + g[Emotion::kTenderness] + g[Emotion::kTranscendence] +
                       0.5f * (g[Emotion::kNostalgia] + g[Emotion::kSadness]);
    const float driven = g[Emotion::kPower] + g[Emotion::kJoy] + 0.5f * g[Emotion::kTension];
    const float k = soft - driven;
    if (!std::isfinite(k)) return 1.0f;
    const float c = std::clamp(k, -1.0f, 1.0f);
    return c >= 0.0f ? 1.0f + c : 1.0f + 0.75f * c;
}

void Director::commit(Reason reason, float glideSeconds, const Look* chosen) {
    const Look best = chosen != nullptr ? *chosen : make(feeling_, config_.seed, commits_, reason == Reason::Drop);

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

void Director::named(int section, float glideSeconds) {
    naming_ = false;
    if (section < 0 || section >= kSectionSlots) return;
    const auto i = static_cast<size_t>(section);
    if (namingDrop_) lastDropSection_ = section;
    if (known_[i]) {
        // A section heard before: its look comes back. A drop already cut to the look the last drop
        // opened; when this is that section, nothing moves.
        returned_ = true;
        if (std::memcmp(&remembered_[i], &to_, sizeof(Look)) == 0) return;
        from_ = now_;
        to_ = remembered_[i];
        glide_ = 0.0f;
        glideSeconds_ = std::max(glideSeconds, 0.05f);
        return;
    }
    // A new section keeps the look it was given: the one held for it, or the drop's.
    returned_ = false;
    remembered_[i] = to_;
    known_[i] = true;
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
    const bool downbeat = f.downbeat > 0.0f;
    if (downbeat) barsSinceCommit_ += 1;
    // The bar the tracker hears, held to 1..4 s. With no tempo known there is no bar: it is a plain time
    // in seconds, the geometric middle of that range. A look lands over changeBars() of it.
    constexpr float kBarMin = 1.0f;
    constexpr float kBarMax = 4.0f;
    const bool barKnown = f.bpm > 0.0f && f.beatConf > 0.3f;
    const float bar = barKnown ? std::clamp(240.0f / f.bpm, kBarMin, kBarMax) : std::sqrt(kBarMin * kBarMax);
    const float change = std::max(0.25f, changeBars(s) * bar);
    // Where a look may land: on a downbeat, or anywhere when there is no beat to land on.
    const bool onBar = downbeat || f.beatConf < 0.3f;
    const int section = std::isfinite(f.sectionId) ? static_cast<int>(std::lround(f.sectionId)) : -1;

    // A modulation counts once it has held six seconds.
    const bool moved = key_ >= 0 && key_ != committedKey_ && f.keyClarity >= 0.3f;
    modulationSeconds_ = moved ? modulationSeconds_ + dt : 0.0f;

    // Form names a section on the downbeat after it reports one (once a whole bar of it is heard): the
    // frame's sectionId moves off the one the event carried. Checked before the events below, so the
    // event's own downbeat is not counted.
    if (naming_) {
        namingSeconds_ += dt;
        if (downbeat) namingBars_ += 1;
        if (section != namingFrom_) {
            named(section, change);
        } else if (namingBars_ >= kNamingBars || namingSeconds_ >= kNamingSeconds) {
            naming_ = false;  // unnamed: the held look lands as made, and nothing is remembered
        }
    }
    const auto awaitName = [&](bool drop) {
        naming_ = true;
        namingDrop_ = drop;
        namingFrom_ = section;
        namingBars_ = 0;
        namingSeconds_ = 0.0f;
    };

    if (commits_ == 0) {
        // The first look waits for a key it can trust, or twelve seconds of music, whichever comes first:
        // in the first seconds the key detector is still settling, and the first look should not be
        // made for a key the song is not in. Then it lands on a downbeat; past sixteen seconds anywhere (a
        // tracker that is not yet locked fires no downbeat while its confidence may read above 0.3).
        if (heard_ >= 4.0f && (s.confidence > 0.5f || heard_ >= 12.0f) && (onBar || heard_ >= 16.0f)) {
            commit(Reason::First, 2.0f);
            // The section playing now (Form names the first from its first bars) keeps this look.
            if (section >= 0 && section < kSectionSlots) {
                remembered_[static_cast<size_t>(section)] = to_;
                known_[static_cast<size_t>(section)] = true;
            }
        }
    } else if (sound::dropped(f)) {
        // Every event Form fires: it owns the floor (Form::kEventFloor), so a threshold here would only
        // lose the weaker ones. A drop cuts in at once. When it also opens a section (Form fires both),
        // the best guess for that section is the one the last drop opened: a song's drops mostly return
        // to one drop section. Form's name a bar later settles it.
        const bool opens = sound::sectionChanged(f);
        const bool guess = opens && lastDropSection_ >= 0 && known_[static_cast<size_t>(lastDropSection_)];
        commit(Reason::Drop, 0.25f, guess ? &remembered_[static_cast<size_t>(lastDropSection_)] : nullptr);
        returned_ = false;
        naming_ = false;
        if (opens) awaitName(true);
    } else if (sound::sectionChanged(f) && sinceCommit_ >= 4.0f) {
        // The moment is now (Form fires it on a downbeat): the style steps and the reach rolls here. The
        // look is made now but held still until Form names the section, a bar on; then it lands, or the
        // section's remembered look lands in its place. Gliding toward one and then turning to the other
        // would change the picture twice for one section.
        commit(Reason::Section, change);
        returned_ = false;
        awaitName(false);
    } else if (!naming_) {
        if (modulationSeconds_ >= 6.0f && onBar && sinceCommit_ >= 4.0f) {
            commit(Reason::Modulation, change);
        } else if ((barsSinceCommit_ >= 32 && f.phrase > 0.0f) || (sinceCommit_ >= 75.0f && onBar) || sinceCommit_ >= 90.0f) {
            // At a phrase's start; past 75 s (32 bars of a slow song are longer) on a downbeat; past 90 s
            // anywhere, for a beat the tracker has not locked.
            commit(Reason::Long, change);
        }
    }

    // The build's lean: in over four bars while Form's state is a build, out over one when it fades. A
    // drop's cut takes it away with the cut. Form's state changes on a downbeat, so the lean starts there.
    const bool building = std::isfinite(f.sectionState) && std::lround(f.sectionState) == GEODE_SECTION_BUILD;
    if (sound::dropped(f)) {
        lean_ = 0.0f;
    } else if (building) {
        lean_ = std::min(1.0f, lean_ + dt / (kLeanInBars * bar));
    } else {
        lean_ = std::max(0.0f, lean_ - dt / (kLeanOutBars * bar));
    }

    if (commits_ > 0) presence_ += (1.0f - presence_) * (1.0f - std::exp(-dt / 1.0f));
    // A section's look holds still while it waits for the section's name.
    if (!(naming_ && !namingDrop_)) glide_ = std::min(1.0f, glide_ + dt / glideSeconds_);
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
    // Spin (motionDrift) is not here, on purpose: whether a turning style spins, and how fast, is the
    // user's (#1181).
    return o;
}

Director::Axes Director::axes() const {
    const float a = weight();
    if (a <= 0.0f) return {};
    // The look as decided, held until the next decision: nothing here follows the frame, so between
    // decisions the picture moves only by the reaction matrix and the user's motion dials. The build's
    // lean is the one exception, and it follows Form's section state, never a level or a hit.
    Axes d = now_.axes;
    const float lean = smooth(lean_);
    for (int i = 0; i < kAxes; ++i) d[i] = std::clamp(a * (d[i] + lean * kBuildLean[i]), -1.0f, 1.0f);
    return d;
}

SceneParams Director::applyAxes(const SceneParams& p) const {
    SceneParams o = director::applyAxes(p, axes());
    // Energy and Space turn the Motion dials too, but a dial held at 0 is the music's push switched
    // off, and stays off: the director never switches on a reaction the user switched off.
    for (float SceneParams::*dial : {&SceneParams::motionAmount, &SceneParams::motionBreath}) {
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
// old knobs moved it, and a slider the user moved carries its whole offset through. A slider already
// between the default and the end has less room than that offset, so there the offset shrinks with the
// room left (the same share of the way to the end the default would go): added whole and clipped at the
// end, as it was, Auto pinned the param there and the top of the slider changed nothing (at half a turn,
// half of Intensity's travel above its default). An axis never carries a field past the end it turns
// toward, and never drags back a value already beyond it.
SceneParams applyAxes(const SceneParams& p, const Director::Axes& x) {
    static const SceneParams kDefaults{};
    SceneParams out = p;
    for (const AxisField& d : kAxisFields) {
        const float v = axisOf(x, d.axis);
        if (v == 0.0f || !std::isfinite(v)) continue;
        const float up = d.inverted ? d.lo : d.hi;
        const float down = d.inverted ? d.hi : d.lo;
        const float end = v > 0.0f ? up : down;
        const float room = end - std::clamp(kDefaults.*d.field, d.lo, d.hi);
        float& f = out.*d.field;
        if (room == 0.0f || !std::isfinite(f)) continue;
        // The share of the default's room this slider still has: 0 or less at or past the end.
        const float share = (end - f) / room;
        if (share > 0.0f) f += std::fabs(v) * room * std::min(1.0f, share);
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
