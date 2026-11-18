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

using director::Tune;

// The build's lean at full, from the tuning: faster, closer and rougher, the anticipation before a drop
// (Huron's ITPRA: tension rises and attention narrows). It is the director's own behaviour over its axes,
// decided at Form's state changes, never a reaction to a frame: the drums and the build's level move
// nothing here.
Director::Axes buildLean(const director::Tuning& t) { return {0.0f, t[Tune::LeanSpeed], 0.0f, -t[Tune::LeanCloser], t[Tune::LeanRough]}; }

// A section event Form has not named after this many downbeats, or seconds, is let go unnamed: its look
// lands as made and nothing is remembered. Form names one on the next downbeat, so this is a mechanism's
// fallback, not a taste, and no setting.
constexpr int kNamingBars = 2;
constexpr float kNamingSeconds = 8.0f;
// Past the long stretch, how much longer a look waits for a downbeat a tracker that is not yet locked
// never fires; the first look's own wait past its key.
constexpr float kUnlockedGraceSeconds = 15.0f;
constexpr float kFirstUnlockedSeconds = 16.0f;

// Field by field over a look: every dial a look holds, so growing one and gliding one cover the same set.
template <typename F>
void eachDial(Director::Look& o, const Director::Look& a, const Director::Look& b, F f) {
    for (int i = 0; i < Director::kAxes; ++i) o.axes[i] = f(a.axes[i], b.axes[i]);
    o.energy = f(a.energy, b.energy);
    o.camSmooth = f(a.camSmooth, b.camSmooth);
    o.motionBreath = f(a.motionBreath, b.motionBreath);
    o.moves.orbit = f(a.moves.orbit, b.moves.orbit);
    o.moves.lean = f(a.moves.lean, b.moves.lean);
    o.moves.dolly = f(a.moves.dolly, b.moves.dolly);
    o.moves.breath = f(a.moves.breath, b.moves.breath);
    o.moves.shake = f(a.moves.shake, b.moves.shake);
    o.moves.reframe = f(a.moves.reframe, b.moves.reframe);
    o.moves.cut = f(a.moves.cut, b.moves.cut);
}

// Each dial held to the range make() deals it in.
Director::Look heldLook(Director::Look o) {
    for (float& v : o.axes) v = std::clamp(v, -1.0f, 1.0f);
    o.energy = clamp01(o.energy);
    o.camSmooth = std::clamp(o.camSmooth, 0.1f, 0.9f);
    o.motionBreath = clamp01(o.motionBreath);
    for (float* m : {&o.moves.orbit, &o.moves.lean, &o.moves.dolly, &o.moves.breath, &o.moves.shake, &o.moves.reframe, &o.moves.cut}) {
        *m = std::clamp(*m, 0.0f, 2.5f);
    }
    return o;
}

// How high each of Form's section states sits on the song's arc, 0..1 (GEODE_SECTION_* order): a song
// rests in its intro, breakdown and outro, rises through a verse and a build, and peaks at a drop.
constexpr std::array<float, 6> kArcByState = {0.15f, 0.45f, 0.75f, 1.0f, 0.1f, 0.0f};

// The arc's params, by name: the one seam where the song's arc reaches a param (viz/ParamTable.inc's
// marchDetail, fractal and formula). A name the table does not hold, or a retired row, is skipped.
struct ArcField {
    const char* name;
    Tune share;
};
constexpr std::array<ArcField, 3> kArcFields = {{
    {"marchDetail", Tune::ArcDetail},
    {"fractal", Tune::ArcFractal},
    {"formula", Tune::ArcFormula},
}};

Director::Look lerpLook(const Director::Look& a, const Director::Look& b, float k) {
    // Landed is the look itself, bit for bit (a + (b - a) can miss b by a rounding).
    if (k >= 1.0f) return b;
    Director::Look o = b;
    eachDial(o, a, b, [k](float x, float y) { return lerp(x, y, k); });
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
    std::shared_ptr<const director::Tuning> tuning = std::move(tuning_);
    std::string style = std::move(style_);
    *this = Director();
    config_ = keep;
    configured_ = configured;
    tuning_ = std::move(tuning);
    style_ = std::move(style);
}

void Director::configure(const DirectorConfig& config, std::shared_ptr<const director::Tuning> tuning, const std::string& style) {
    configure(config);
    tuning_ = std::move(tuning);
    if (style_ != style) style_ = style;
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
        const float arc = arc_;
        reset();
        amount_ = amount;
        presence_ = presence;
        from_ = to_ = now_ = shown;
        // The lean and the arc settle on the new play's frames, not in one jump; the arc climbs again from
        // the new song's first phrase. The remembered sections are not carried: they were the last song's.
        lean_ = lean;
        arc_ = arc;
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

float Director::changeBars(const Emotion::State& s, const director::Tuning& tuning) {
    // make() lets the feeling choose what a look is; this lets it choose how the look arrives, the way a
    // VJ fades a ballad's change and cuts a banger's. The GEMS sum to 1, so the balance is -1..1.
    const auto& g = s.gems;
    const float soft = g[Emotion::kPeace] + g[Emotion::kTenderness] + g[Emotion::kTranscendence] +
                       0.5f * (g[Emotion::kNostalgia] + g[Emotion::kSadness]);
    const float driven = g[Emotion::kPower] + g[Emotion::kJoy] + 0.5f * g[Emotion::kTension];
    const float k = soft - driven;
    const float lo = tuning[Tune::GlideDrivenBars];
    const float hi = std::max(lo, tuning[Tune::GlideCalmBars]);
    // One bar between, unless the two ends leave it out.
    const float mid = std::clamp(1.0f, lo, hi);
    if (!std::isfinite(k)) return mid;
    const float c = std::clamp(k, -1.0f, 1.0f);
    return c >= 0.0f ? mid + c * (hi - mid) : mid + c * (mid - lo);
}

Director::Look Director::grow(const Look& look, const Look& heading, int returns, float growth, uint64_t seed, int section) {
    if (returns <= 0 || !(growth > 0.0f)) return look;
    const float g = growth * static_cast<float>(returns);
    // Further the way the look was heading when it was chosen...
    Look o = look;
    eachDial(o, look, heading, [g](float v, float h) { return v + g * h; });
    // ... and a turn of its own, seeded per section and return, so a look that was hardly heading anywhere
    // still grows, and each return grows its own way. The energy is the feeling's: it does not grow.
    Rng rng{seed ^ (0x8CB92BA72F3D8DD7ull * static_cast<uint64_t>(section + 1)) ^ (0x2545F4914F6CDD1Dull * static_cast<uint64_t>(returns))};
    for (float& v : o.axes) v += 0.25f * g * rng.signedUnit();
    o.camSmooth += 0.1f * g * rng.signedUnit();
    o.energy = look.energy;
    return heldLook(o);
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

void Director::remember(int section) {
    if (section < 0 || section >= kSectionSlots) return;
    Section& s = sections_[static_cast<size_t>(section)];
    s.known = true;
    s.look = to_;
    // The way it was heading: from the look it replaced (none on screen yet: from rest, a default look).
    const Look& was = commits_ <= 1 && presence_ <= 0.0f ? Look{} : from_;
    eachDial(s.heading, to_, was, [](float a, float b) { return a - b; });
    s.style = style_;
    s.returns = 0;
    current_ = section;
    firstVisit_ = true;
}

void Director::intend(director::StyleIntent::Kind kind, int section) {
    intent_.kind = kind;
    intent_.from = style_;
    intent_.progress = tune()[Tune::StylesProgress] > 0.5f;
    if (kind == director::StyleIntent::Kind::Return && section >= 0 && section < kSectionSlots) {
        const Section& s = sections_[static_cast<size_t>(section)];
        intent_.remembered = s.style;
        intent_.step = s.returns;
    } else {
        intent_.remembered.clear();
        intent_.step = 0;
    }
}

void Director::named(int section, float change, float morph) {
    naming_ = false;
    const bool valid = section >= 0 && section < kSectionSlots;
    const bool known = valid && sections_[static_cast<size_t>(section)].known;
    if (namingDrop_) {
        // The drop is in; Form says where it is.
        if (!valid) return;
        lastDropSection_ = section;
        if (section == guessed_) {
            // The guess was right: the look it cut to was this section's, grown. Nothing moves.
            sections_[static_cast<size_t>(section)].returns += 1;
            returned_ = true;
            current_ = section;
            firstVisit_ = false;
            return;
        }
        if (known) {
            // Another section heard before: morph on into its grown look (no cut: the drop's was the cut).
            Section& s = sections_[static_cast<size_t>(section)];
            s.returns += 1;
            const Look grown = grow(s.look, s.heading, s.returns, tune()[Tune::ReturnGrowth], config_.seed, section);
            from_ = now_;
            to_ = grown;
            glide_ = 0.0f;
            glideSeconds_ = std::max(morph, 0.05f);
            returned_ = true;
            current_ = section;
            firstVisit_ = false;
            return;
        }
        // A new section: it keeps the drop's look, heading the way the drop went.
        Section& s = sections_[static_cast<size_t>(section)];
        s.known = true;
        s.look = to_;
        eachDial(s.heading, to_, beforeDrop_, [](float a, float b) { return a - b; });
        s.style = style_;
        s.returns = 0;
        current_ = section;
        firstVisit_ = true;
        returned_ = false;
        return;
    }
    // A section event, decided now that Form has named it: the look and the style change together.
    if (known) {
        Section& s = sections_[static_cast<size_t>(section)];
        s.returns += 1;
        const Look grown = grow(s.look, s.heading, s.returns, tune()[Tune::ReturnGrowth], config_.seed, section);
        commit(Reason::Section, std::max(change, morph), &grown);
        returned_ = true;
        current_ = section;
        firstVisit_ = false;
        intend(director::StyleIntent::Kind::Return, section);
        return;
    }
    commit(Reason::Section, change);
    returned_ = false;
    intend(director::StyleIntent::Kind::Progress, -1);
    remember(section);
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
    const director::Tuning& T = tune();

    if (f.keyClarity >= 0.3f) {
        key_ = static_cast<int>(std::lround(f.keyRoot * 12.0f)) % 12 + (f.keyMajor >= 0.5f ? 0 : 12);
    }
    if (f.loudness > 0.15f) heard_ += dt;
    sinceCommit_ += dt;
    // A trigger's value is its strength (the beat tracker's confidence), not a level: any downbeat
    // or phrase the tracker fires counts, as with Form's events below.
    const bool downbeat = f.downbeat > 0.0f;
    if (downbeat) barsSinceCommit_ += 1;
    if (f.phrase > 0.0f) phrases_ += 1;
    // The bar the tracker hears, held to 1..4 s. With no tempo known there is no bar: it is a plain time
    // in seconds, the geometric middle of that range. A look lands over changeBars() of it.
    constexpr float kBarMin = 1.0f;
    constexpr float kBarMax = 4.0f;
    const bool barKnown = f.bpm > 0.0f && f.beatConf > 0.3f;
    const float bar = barKnown ? std::clamp(240.0f / f.bpm, kBarMin, kBarMax) : std::sqrt(kBarMin * kBarMax);
    const float change = std::max(0.25f, changeBars(s, T) * bar);
    // A return morphs, never cuts: at least the tuning's morph.
    const float morph = std::max(change, T[Tune::ReturnMorphBars] * bar);
    // Where a look may land: on a downbeat, or anywhere when there is no beat to land on.
    const bool onBar = downbeat || f.beatConf < 0.3f;
    const int section = std::isfinite(f.sectionId) ? static_cast<int>(std::lround(f.sectionId)) : -1;
    const int state = std::isfinite(f.sectionState) ? static_cast<int>(std::lround(f.sectionState)) : -1;

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
            named(section, change, morph);
        } else if (namingBars_ >= kNamingBars || namingSeconds_ >= kNamingSeconds) {
            // Unnamed: a section's look lands as made, remembered nowhere; a drop's stays.
            naming_ = false;
            if (!namingDrop_) {
                commit(Reason::Section, change);
                returned_ = false;
                intend(director::StyleIntent::Kind::Progress, -1);
                current_ = -1;
                firstVisit_ = false;
            }
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
        if (heard_ >= 4.0f && (s.confidence > 0.5f || heard_ >= 12.0f) && (onBar || heard_ >= kFirstUnlockedSeconds)) {
            commit(Reason::First, 2.0f);
            // The section playing now (Form names the first from its first bars) keeps this look.
            remember(section);
        }
    } else if (sound::dropped(f)) {
        // Every event Form fires: it owns the floor (Form::kEventFloor), so a threshold here would only
        // lose the weaker ones. A drop cuts in at once. When it also opens a section (Form fires both),
        // the best guess for that section is the one the last drop opened, grown for its next return: a
        // song's drops mostly return to one drop section. Form's name a bar later settles it.
        const bool opens = sound::sectionChanged(f);
        const bool guess = opens && lastDropSection_ >= 0 && sections_[static_cast<size_t>(lastDropSection_)].known;
        beforeDrop_ = now_;
        if (guess) {
            const Section& g = sections_[static_cast<size_t>(lastDropSection_)];
            const Look grown = grow(g.look, g.heading, g.returns + 1, T[Tune::ReturnGrowth], config_.seed, lastDropSection_);
            commit(Reason::Drop, 0.25f, &grown);
            guessed_ = lastDropSection_;
            intend(director::StyleIntent::Kind::Return, lastDropSection_);
            intent_.step = g.returns + 1;
        } else {
            commit(Reason::Drop, 0.25f);
            guessed_ = -1;
            intend(opens ? director::StyleIntent::Kind::Leap : director::StyleIntent::Kind::Progress, -1);
        }
        returned_ = false;
        naming_ = false;
        if (opens) awaitName(true);
    } else if (sound::sectionChanged(f) && sinceCommit_ >= 4.0f) {
        // Decided when Form names the section, a bar on (Form fires the event on a downbeat and names it
        // on the next): only then is it known whether the section is new or back, and the look and the
        // style change together. Until then the look on screen holds.
        awaitName(false);
    } else if (!naming_) {
        const float longSeconds = T[Tune::LongSeconds];
        if (modulationSeconds_ >= 6.0f && onBar && sinceCommit_ >= 4.0f) {
            commit(Reason::Modulation, change);
        } else if ((barsSinceCommit_ >= 32 && f.phrase > 0.0f) || (sinceCommit_ >= longSeconds && onBar) ||
                   sinceCommit_ >= longSeconds + kUnlockedGraceSeconds) {
            // At a phrase's start; past the long stretch (32 bars of a slow song are longer) on a downbeat;
            // past it and a grace anywhere, for a beat the tracker has not locked.
            commit(Reason::Long, change);
            intend(director::StyleIntent::Kind::Progress, -1);
        }
    }

    // A section on its first visit learns the style it is shown on: the switch a style moment asks for
    // lands a few frames after the moment, and the user may pick one by hand.
    if (firstVisit_ && current_ >= 0 && current_ < kSectionSlots) sections_[static_cast<size_t>(current_)].style = style_;

    // The build's lean: in while Form's state is a build, out when it fades. A drop's cut takes it away
    // with the cut. Form's state changes on a downbeat, so the lean starts there.
    if (sound::dropped(f)) {
        lean_ = 0.0f;
    } else if (state == GEODE_SECTION_BUILD) {
        lean_ = std::min(1.0f, lean_ + dt / (T[Tune::LeanInBars] * bar));
    } else {
        lean_ = std::max(0.0f, lean_ - dt / (T[Tune::LeanOutBars] * bar));
    }

    // The song's arc: toward the level of Form's section state and the phrases heard, over the tuning's
    // bars. Form's state and the phrase count change on downbeats; nothing else moves it.
    const float arcTo = director::arcLevel(state, phrases_, T);
    const float arcStep = dt / (T[Tune::ArcBars] * bar);
    arc_ = arc_ < arcTo ? std::min(arcTo, arc_ + arcStep) : std::max(arcTo, arc_ - arcStep);

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
    // Spin (motionDrift) is not here, on purpose: whether a turning style spins, and how fast, is the
    // user's (#1181).
    // The song's arc on Detail, Fractal and Formula (applyArc, the one seam that names them).
    return director::applyArc(o, a * arcTurn(), tune());
}

float Director::arcTurn() const {
    // -1 at rest .. 1 at the song's peak, times the arc's depth: a song rests below the look it was given
    // in its intro and outro, and climbs above it toward its drops.
    return std::clamp(2.0f * arc_ - 1.0f, -1.0f, 1.0f) * tune()[Tune::ArcDepth];
}

Director::Axes Director::axes() const {
    const float a = weight();
    if (a <= 0.0f) return {};
    // The look as decided, held until the next decision: nothing here follows the frame, so between
    // decisions the picture moves only by the reaction matrix and the user's motion dials. The build's
    // lean and the song's arc are the exceptions, and they follow Form's section state and the phrases
    // heard, never a level or a hit.
    const director::Tuning& T = tune();
    Axes d = now_.axes;
    const float lean = smooth(lean_);
    const Axes leanAt = buildLean(T);
    axisOf(d, Axis::Complexity) += arcTurn() * T[Tune::ArcComplexity];
    for (int i = 0; i < kAxes; ++i) d[i] = std::clamp(a * (d[i] + lean * leanAt[i]), -1.0f, 1.0f);
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

SceneParams applyArc(const SceneParams& p, float turn, const Tuning& tuning) {
    if (turn == 0.0f || !std::isfinite(turn)) return p;
    // Resolved by name once: a param the table does not hold yet (or a retired one) is -1 and skipped.
    static const std::array<int, kArcFields.size()> kSlots = [] {
        std::array<int, kArcFields.size()> out{};
        for (size_t i = 0; i < kArcFields.size(); ++i) {
            const int slot = SceneParams::slotOf(kArcFields[i].name);
            out[i] = slot >= 0 && !SceneParams::retired(static_cast<ParamSlot>(slot)) ? slot : -1;
        }
        return out;
    }();
    SceneParams o = p;
    for (size_t i = 0; i < kArcFields.size(); ++i) {
        const ArcField& a = kArcFields[i];
        const int slot = kSlots[i];
        if (slot < 0) continue;
        const ParamSpec& spec = SceneParams::schema()[static_cast<size_t>(slot)];
        const float v = turn * tuning[a.share];
        const float f = o.getSlot(slot);
        if (v == 0.0f || !std::isfinite(f)) continue;
        // Base plus modulation: that share of the room left toward the end it turns to, so it never
        // carries a value past its range and a slider the user moved keeps its whole step.
        const float end = v > 0.0f ? spec.max : spec.min;
        if ((v > 0.0f && f >= end) || (v < 0.0f && f <= end)) continue;
        o.setSlot(slot, f + std::fabs(v) * (end - f));
    }
    return o;
}

float arcLevel(int sectionState, int phrases, const Tuning& tuning) {
    const bool known = sectionState >= 0 && sectionState < static_cast<int>(kArcByState.size());
    const float state = known ? kArcByState[static_cast<size_t>(sectionState)] : 0.0f;
    const float half = std::max(1.0f, tuning[Tune::ArcPhrases]);
    const float heard = static_cast<float>(std::max(0, phrases));
    // Half the climb at the song's start, all of it once many phrases are behind.
    const float climbed = 0.5f + 0.5f * heard / (heard + half);
    return clamp01(state * climbed);
}

}  // namespace director

}  // namespace geode::viz
