#include "viz/DirectorReach.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <optional>
#include <vector>

#include "viz/CustomizeMap.hpp"
#include "viz/ModTargets.hpp"
#include "viz/ParamRandomizer.hpp"
#include "viz/VisualSafety.hpp"

namespace geode::viz {

namespace {

using customize::Category;
using Reason = Director::Reason;

std::atomic<int> gReach{0};
std::mutex gKeptLock;
std::shared_ptr<const director::Kept> gKept;

const std::string kNoStyle;

// The categories a moment may roll. Quick knobs has no roll; Music is how much the user wants the music
// to move things, and the director deals its own routes; Expert tunes an engine, not a look.
constexpr std::array<Category, 6> kShuffled{Category::Look,   Category::Shape,   Category::Motion,
                                            Category::Camera, Category::Effects, Category::Liquid};

// Controls inside those categories the shuffle leaves alone: the palette and the dials the director's
// look already decides from the feeling (a roll there would undo it), and the two full-frame luminance
// flips, which a section change must not fire.
constexpr const char* kLooksOwn[] = {
    "Palette",    "Palette 2",  "Palette blend", "Colour map",  "Hue shift", "Hue range", "Temperature",
    "Color cycle", "Cycle speed", "Saturation",    "Brightness",  "Contrast",  "Gamma",     "Breath",
    "Spin drift", "Invert",      "Solarize",
};

// What a modulator may move: sliders with a visible, continuous effect. Colour is the look's.
constexpr LfoTarget kModTargets[] = {
    LfoTarget::Zoom,        LfoTarget::Rotation,   LfoTarget::Sway,         LfoTarget::Warp,         LfoTarget::Ripple,
    LfoTarget::Morph,       LfoTarget::Twist,      LfoTarget::Turbulence,   LfoTarget::Bloom,        LfoTarget::ChromaAb,
    LfoTarget::Vignette,    LfoTarget::Fisheye,    LfoTarget::TrailLength,  LfoTarget::ParticleSize, LfoTarget::FluidCurl,
    LfoTarget::FluidForce,  LfoTarget::FlowStrength, LfoTarget::FlowCurl,   LfoTarget::WaterRipple,  LfoTarget::RippleOverlay,
    LfoTarget::CamOrbit,    LfoTarget::CamFov,     LfoTarget::CamRoll,      LfoTarget::EndlessZoomSpeed,
};
constexpr int kModTargetCount = static_cast<int>(std::size(kModTargets));

// What a follower hears: the slow side of the sound frame, never a single hit.
constexpr ReactSource kFollowSources[] = {
    ReactSource::Intensity,   ReactSource::BassPresence, ReactSource::MidPresence, ReactSource::TreblePresence,
    ReactSource::DensitySlow, ReactSource::Centroid,     ReactSource::BuildUp,     ReactSource::Tension,
};
constexpr int kFollowSourceCount = static_cast<int>(std::size(kFollowSources));
constexpr LfoWave kWaveShapes[] = {LfoWave::Sine, LfoWave::Triangle, LfoWave::RandomGlide};

struct Rng {
    std::uint64_t x;
    std::uint64_t bits() {
        x += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = x;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    float unit() { return static_cast<float>(bits() >> 40) / 16777216.0f; }
    float between(float lo, float hi) { return lo + unit() * (hi - lo); }
    int below(int count) { return static_cast<int>(bits() % static_cast<std::uint64_t>(count)); }
};

Rng rngFor(std::uint64_t seed, int commit, std::uint64_t salt) {
    return Rng{seed ^ salt ^ (0xD1B54A32D192ED03ull * static_cast<std::uint64_t>(commit + 1))};
}
constexpr std::uint64_t kShuffleSalt = 0x5348554646ull;
constexpr std::uint64_t kModulationSalt = 0x4D4F44ull;

template <typename T, size_t N>
void shuffle(std::array<T, N>& items, int count, Rng& rng) {
    for (int i = count - 1; i > 0; --i) std::swap(items[static_cast<size_t>(i)], items[static_cast<size_t>(rng.below(i + 1))]);
}

float smooth(float x) {
    const float t = std::clamp(x, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

bool glides(int slot) {
    const ParamSpec& spec = SceneParams::schema()[static_cast<size_t>(slot)];
    return spec.fade == ParamFade::Fade && (spec.kind == ParamKind::Float || spec.kind == ParamKind::Circular);
}

struct Control {
    const char* key;
    std::optional<Category> category;
    std::array<int, 3> slots;
    bool shuffled;
};

struct Table {
    std::vector<Control> controls;
    std::array<int, SceneParams::kFieldCount> ofSlot;
};

// Every rolled control with its category and wire slots, built once.
const Table& table() {
    static const Table kTable = [] {
        Table t;
        t.ofSlot.fill(-1);
        for (const auto& roll : randomizer::rolledKeys()) {
            Control c{roll.key, customize::categoryOf(roll.key), {-1, -1, -1}, false};
            c.shuffled = c.category && std::find(kShuffled.begin(), kShuffled.end(), *c.category) != kShuffled.end();
            for (const char* own : kLooksOwn) c.shuffled = c.shuffled && std::string_view(own) != roll.key;
            for (size_t i = 0; i < roll.fields.size(); ++i) {
                if (!roll.fields[i]) continue;
                c.slots[i] = SceneParams::slotOf(roll.fields[i]);
                if (c.slots[i] >= 0 && t.ofSlot[static_cast<size_t>(c.slots[i])] < 0) {
                    t.ofSlot[static_cast<size_t>(c.slots[i])] = static_cast<int>(t.controls.size());
                }
            }
            t.controls.push_back(c);
        }
        return t;
    }();
    return kTable;
}

// How much a moment changes: a drop everything, a section more the more energy the new feeling has.
float strengthOf(Reason reason, float energy) {
    if (reason == Reason::Drop) return 1.0f;
    return std::clamp(0.35f + 0.45f * (std::isfinite(energy) ? energy : 0.5f), 0.35f, 0.8f);
}

bool shuffleMoment(Reason reason, std::uint32_t handMask) {
    switch (reason) {
        case Reason::Drop: return (handMask & hand::bit(Moment::Drop)) == 0;
        case Reason::Section: return (handMask & hand::bit(Moment::NewSection)) == 0;
        case Reason::Long: return true;
        default: return false;
    }
}

}  // namespace

namespace director {

void setReach(int level) {
    const bool known = level >= 0 && level <= static_cast<int>(Reach::Modulate);
    gReach.store(known ? level : 0, std::memory_order_relaxed);
}

Reach reach() { return static_cast<Reach>(gReach.load(std::memory_order_relaxed)); }

void setKept(std::string_view lines) {
    Kept keys;
    size_t start = 0;
    while (start <= lines.size()) {
        const size_t end = std::min(lines.find('\n', start), lines.size());
        if (end > start) keys.emplace(lines.substr(start, end - start));
        start = end + 1;
    }
    auto next = std::make_shared<const Kept>(customize::expandLocks(keys));
    std::lock_guard<std::mutex> guard(gKeptLock);
    gKept = std::move(next);
}

std::shared_ptr<const Kept> kept() {
    {
        std::lock_guard<std::mutex> guard(gKeptLock);
        if (gKept) return gKept;
    }
    static const std::shared_ptr<const Kept> kNone = std::make_shared<const Kept>();
    return kNone;
}

}  // namespace director

void DirectorReach::reset() { *this = DirectorReach(); }

void DirectorReach::glideTo(int slot, float to, float seconds) {
    const auto s = static_cast<size_t>(slot);
    from_[s] = offset_[s];
    to_[s] = to;
    progress_[s] = 0.0f;
    seconds_[s] = seconds;
    home_.reset(s);
}

void DirectorReach::letGo(int slot) {
    const auto s = static_cast<size_t>(slot);
    overridden_.reset(s);
    if (to_[s] != 0.0f) glideTo(slot, 0.0f, kLetGoSeconds);
    home_.set(s);
}

void DirectorReach::letGoAll() {
    for (int s = 0; s < kFields; ++s) letGo(s);
    hasMoment_ = false;
}

void DirectorReach::roll(const SceneParams& requested, const Wire& wire, float seconds) {
    Rng rng = rngFor(seed_, momentCommit_, kShuffleSalt);
    std::array<Category, 6> order = kShuffled;
    shuffle(order, static_cast<int>(order.size()), rng);
    const int count = std::clamp(static_cast<int>(std::lround(momentStrength_ * static_cast<float>(order.size()))), 1,
                                 static_cast<int>(order.size()));
    const auto chosen = [&](Category c) { return std::find(order.begin(), order.begin() + count, c) != order.begin() + count; };

    // The roll itself is Customize's: everything this moment may not touch is held, as a lock holds it.
    const Table& t = table();
    std::set<std::string> held = kept_ ? *kept_ : director::Kept{};
    std::vector<char> rolls(t.controls.size(), 0);
    for (size_t i = 0; i < t.controls.size(); ++i) {
        const Control& c = t.controls[i];
        bool free = c.shuffled && chosen(*c.category) && !held.count(c.key);
        for (int slot : c.slots) free = free && (slot < 0 || !left_[static_cast<size_t>(slot)]);
        rolls[i] = free ? 1 : 0;
        if (!free) held.insert(c.key);
    }
    Wire rolled;
    randomizer::randomize(requested, rng.bits(), std::nullopt, style_, held).toWire(rolled.data());

    for (size_t i = 0; i < t.controls.size(); ++i) {
        const Control& c = t.controls[i];
        const bool flip = rng.unit() < momentStrength_;
        // A category this moment did not draw keeps what it had; a control it may not touch is let go.
        if (!rolls[i]) {
            if (!c.shuffled || !chosen(*c.category)) continue;
            for (int slot : c.slots) {
                if (slot >= 0) letGo(slot);
            }
            continue;
        }
        for (int slot : c.slots) {
            if (slot < 0) continue;
            const auto s = static_cast<size_t>(slot);
            if (glides(slot)) {
                glideTo(slot, momentStrength_ * (rolled[s] - wire[s]), seconds);
            } else {
                override_[s] = rolled[s];
                overridden_.set(s, flip && rolled[s] != wire[s]);
            }
        }
    }
    shuffles_ += 1;
}

DirectorReach::Deal DirectorReach::deal(int commit, float energy) const {
    Deal d;
    Rng rng = rngFor(seed_, commit, kModulationSalt);
    const Table& t = table();
    std::array<LfoTarget, kModTargetCount> usable{};
    int n = 0;
    for (LfoTarget target : kModTargets) {
        const int slot = paramSlotOf(target);
        if (slot < 0 || !modtargets::reaches(target, style_)) continue;
        const int control = t.ofSlot[static_cast<size_t>(slot)];
        if (keptSlots_[static_cast<size_t>(slot)] || left_[static_cast<size_t>(slot)]) continue;
        if (control >= 0 && !customize::reaches(t.controls[static_cast<size_t>(control)].key, style_)) continue;
        usable[static_cast<size_t>(n++)] = target;
    }
    shuffle(usable, n, rng);
    const float depth = kMaxDepth * (0.5f + 0.5f * std::clamp(std::isfinite(energy) ? energy : 0.5f, 0.0f, 1.0f));
    int next = 0;
    for (LfoConfig& w : d.waves) {
        // Drawn whether or not a target is left, so a deal's later draws do not depend on the style's reach.
        const LfoWave wave = kWaveShapes[rng.below(static_cast<int>(std::size(kWaveShapes)))];
        const float seconds = rng.between(kMinWaveSeconds, kMaxWaveSeconds);
        const float share = rng.between(0.6f, 1.0f);
        if (next >= n) continue;
        w.enabled = true;
        w.source = ModSource::Lfo;
        w.target = usable[static_cast<size_t>(next++)];
        w.wave = wave;
        w.rateSeconds = seconds;
        w.depth = depth * share;
        w.polarity = ModPolarity::Bipolar;
        w.curve = ModCurve::Linear;
        w.beats = 0.0f;
    }
    for (ReactRoute& r : d.followers) {
        const ReactSource source = kFollowSources[rng.below(kFollowSourceCount)];
        const float share = rng.between(0.6f, 1.0f) * (rng.unit() < 0.3f ? -1.0f : 1.0f);
        const float attack = rng.between(0.5f, 1.5f);
        const float release = rng.between(2.0f, 4.0f);
        if (next >= n) continue;
        r.enabled = true;
        r.source = source;
        r.target = usable[static_cast<size_t>(next++)];
        r.mode = ReactMode::Follow;
        r.amount = depth * share;
        r.curve = ModCurve::Linear;
        r.attackSeconds = attack;
        r.releaseSeconds = release;
    }
    return d;
}

void DirectorReach::stepModulation(const GeodeSoundFrame& sound, float dt, const Inputs& in) {
    if (!dealt_ && !pending_ && cross_ >= 1.0f) {
        anyModulation_ = false;
        return;
    }
    if (pending_ && cross_ >= 1.0f) {
        out_ = in_;
        wavesOut_ = wavesIn_;
        followOut_ = followIn_;
        gainOut_ = gainIn_;
        in_ = pendingEmpty_ ? Deal{} : deal(pendingCommit_, pendingEnergy_);
        wavesIn_ = LfoEngine();
        followIn_ = ReactionEngine();
        wavesIn_.configs = in_.waves;
        std::copy(in_.followers.begin(), in_.followers.end(), followIn_.routes.begin());
        gainIn_.fill(1.0f);
        dealt_ = !pendingEmpty_;
        cross_ = 0.0f;
        pending_ = false;
        deals_ += 1;
    }
    cross_ = std::min(1.0f, cross_ + dt / kCrossSeconds);
    const float k = smooth(cross_);
    const float scale = weight_ * (in.reducedMotion ? safety::kReducedMotionScale : 1.0f);
    const ReactSourceMask userOwned = hand::sources(in.handMask);
    const float ease = std::clamp(dt / kLetGoSeconds, 0.0f, 1.0f);
    // A modulator eases out when its control is kept or the user moved it, and a follower when the hand
    // mask gives its source to the user. The engines run unscaled and the result is mixed (applyModulation):
    // a follower's own smoothing would lag a fade of its amount and snap when the route switched off.
    const auto silenced = [&](const Deal& d, int i) {
        const bool wave = i < kWaves;
        const LfoTarget target = wave ? d.waves[static_cast<size_t>(i)].target : d.followers[static_cast<size_t>(i - kWaves)].target;
        const int slot = paramSlotOf(target);
        if (slot >= 0 && (keptSlots_[static_cast<size_t>(slot)] || left_[static_cast<size_t>(slot)])) return true;
        return !wave && userOwned[static_cast<size_t>(d.followers[static_cast<size_t>(i - kWaves)].source)];
    };
    for (int i = 0; i < kWaves + kFollowers; ++i) {
        const auto m = static_cast<size_t>(i);
        gainIn_[m] = silenced(in_, i) ? std::max(0.0f, gainIn_[m] - ease) : std::min(1.0f, gainIn_[m] + ease);
        // One already fading out never comes back.
        if (silenced(out_, i)) gainOut_[m] = std::max(0.0f, gainOut_[m] - ease);
        mixIn_[m] = k * scale * gainIn_[m];
        mixOut_[m] = (1.0f - k) * scale * gainOut_[m];
    }
    waveOutValues_ = wavesOut_.tick(dt, sound);
    waveInValues_ = wavesIn_.tick(dt, sound);
    followOut_.tick(sound, dt);
    followIn_.tick(sound, dt);
    anyModulation_ = true;
}

void DirectorReach::step(const SceneParams& requested, const GeodeSoundFrame& sound, float dt, const Inputs& in) {
    Wire wire;
    requested.toWire(wire.data());
    const std::string& style = in.styleId != nullptr ? *in.styleId : kNoStyle;
    const bool newStyle = primed_ && style != style_;
    const bool newPlay = primed_ && in.seed != seed_;
    const bool shuffleOn = in.reach != director::Reach::Look;
    const bool modulateOn = in.reach == director::Reach::Modulate;
    const Table& t = table();

    if (in.kept != kept_) {
        kept_ = in.kept;
        keptSlots_.reset();
        for (const Control& c : t.controls) {
            if (!kept_ || !kept_->count(c.key)) continue;
            for (int slot : c.slots) {
                if (slot < 0) continue;
                keptSlots_.set(static_cast<size_t>(slot));
                letGo(slot);
            }
        }
    }
    // A control the user moved is theirs until the next moment. A new style is not an edit: Auto's own
    // style change arrives a few frames after the moment that asked for it.
    if (primed_ && !newStyle) {
        for (int s = 0; s < kFields; ++s) {
            if (wire[static_cast<size_t>(s)] == last_[static_cast<size_t>(s)]) continue;
            const int control = t.ofSlot[static_cast<size_t>(s)];
            if (control < 0) {
                left_.set(static_cast<size_t>(s));
                continue;
            }
            for (int slot : t.controls[static_cast<size_t>(control)].slots) {
                if (slot < 0) continue;
                left_.set(static_cast<size_t>(slot));
                letGo(slot);
            }
        }
    }
    last_ = wire;
    style_ = style;
    seed_ = in.seed;
    primed_ = true;
    weight_ = std::isfinite(in.weight) ? std::clamp(in.weight, 0.0f, 1.0f) : 0.0f;

    // A new play starts from the user's own look, as an export of it does.
    if (newPlay || !shuffleOn) {
        if (hasMoment_ || anyShuffle_) letGoAll();
        left_.reset();
    }
    if (newPlay || !modulateOn) {
        if (dealt_ || pending_) {
            pending_ = dealt_;
            pendingEmpty_ = true;
        }
    }
    const bool driving = weight_ > 0.0f && dt > 0.0f;
    if (in.committed && driving) {
        if (shuffleOn && shuffleMoment(in.reason, in.handMask)) {
            left_.reset();
            hasMoment_ = true;
            momentCommit_ = in.commit;
            momentStrength_ = strengthOf(in.reason, in.energy);
            roll(requested, wire, in.reason == Reason::Drop ? kDropGlideSeconds : kSectionGlideSeconds);
        }
        if (modulateOn) {
            pending_ = true;
            pendingEmpty_ = false;
            pendingCommit_ = in.commit;
            pendingEnergy_ = in.energy;
        }
    } else if (newStyle && driving) {
        // The same moment, rolled for what the new style shows.
        if (shuffleOn && hasMoment_) roll(requested, wire, kSectionGlideSeconds);
        if (modulateOn && dealt_) {
            pending_ = true;
            pendingEmpty_ = false;
        }
    }

    // The way home keeps wall time, like the glide to a slider the user moved: it must arrive in silence too.
    const float wall = in.wallDt >= 0.0f ? in.wallDt : dt;
    bool any = false;
    for (int s = 0; s < kFields; ++s) {
        const auto i = static_cast<size_t>(s);
        const float by = home_[i] ? wall : dt;
        if (progress_[i] < 1.0f && seconds_[i] > 0.0f && by > 0.0f) {
            progress_[i] = std::min(1.0f, progress_[i] + by / seconds_[i]);
            offset_[i] = from_[i] + (to_[i] - from_[i]) * smooth(progress_[i]);
        }
        any = any || offset_[i] != 0.0f || overridden_[i];
    }
    anyShuffle_ = any;
    if (dt > 0.0f) stepModulation(sound, dt, in);
}

SceneParams DirectorReach::applyShuffle(const SceneParams& p) const {
    if (!anyShuffle_ || weight_ <= 0.0f) return p;
    SceneParams o = p;
    const auto& schema = SceneParams::schema();
    for (int s = 0; s < kFields; ++s) {
        const auto i = static_cast<size_t>(s);
        if (overridden_[i]) {
            // A toggle cannot fade: it follows the director in and out at half its weight.
            if (weight_ >= 0.5f) o.setSlot(s, override_[i]);
        } else if (offset_[i] != 0.0f) {
            const ParamSpec& spec = schema[i];
            const float base = p.getSlot(s);
            const float moved = base + weight_ * offset_[i];
            // Held to the control's range, and never pulled back from a value the user set beyond it.
            o.setSlot(s, spec.kind == ParamKind::Circular ? moved
                                                          : std::clamp(moved, std::min(spec.min, base), std::max(spec.max, base)));
        }
    }
    return o;
}

SceneParams DirectorReach::applyModulation(const SceneParams& p) const {
    if (!anyModulation_) return p;
    Wire base, sum, moved;
    p.toWire(base.data());
    sum = base;
    Slots touched;
    const auto mix = [&](const Deal& d, const Gains& gains, const SceneParams& modulated) {
        modulated.toWire(moved.data());
        for (int i = 0; i < kWaves + kFollowers; ++i) {
            const bool wave = i < kWaves;
            const bool on = wave ? d.waves[static_cast<size_t>(i)].enabled : d.followers[static_cast<size_t>(i - kWaves)].enabled;
            const int slot = paramSlotOf(wave ? d.waves[static_cast<size_t>(i)].target : d.followers[static_cast<size_t>(i - kWaves)].target);
            if (!on || slot < 0) continue;
            const auto s = static_cast<size_t>(slot);
            sum[s] += gains[static_cast<size_t>(i)] * (moved[s] - base[s]);
            touched.set(s);
        }
    };
    // Each engine adds to the same base, so a deal fading out and the one fading in never compound.
    Deal wavesOnly = out_, followOnly = out_;
    for (auto& r : wavesOnly.followers) r.enabled = false;
    for (auto& w : followOnly.waves) w.enabled = false;
    mix(wavesOnly, mixOut_, wavesOut_.apply(p, waveOutValues_));
    mix(followOnly, mixOut_, followOut_.apply(p));
    wavesOnly = followOnly = in_;
    for (auto& r : wavesOnly.followers) r.enabled = false;
    for (auto& w : followOnly.waves) w.enabled = false;
    mix(wavesOnly, mixIn_, wavesIn_.apply(p, waveInValues_));
    mix(followOnly, mixIn_, followIn_.apply(p));

    SceneParams o = p;
    const auto& schema = SceneParams::schema();
    for (int s = 0; s < kFields; ++s) {
        const auto i = static_cast<size_t>(s);
        if (!touched[i] || sum[i] == base[i]) continue;
        const ParamSpec& spec = schema[i];
        o.setSlot(s, spec.kind == ParamKind::Circular ? sum[i]
                                                      : std::clamp(sum[i], std::min(spec.min, base[i]), std::max(spec.max, base[i])));
    }
    return o;
}

}  // namespace geode::viz
