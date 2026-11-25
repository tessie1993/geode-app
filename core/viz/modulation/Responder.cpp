#include "viz/modulation/Responder.hpp"

#include <cmath>

#include "viz/modulation/ModTargets.hpp"

namespace geode::viz::responder {

namespace {

// As ReactionEngine's own step: one long stall must not fling an envelope.
constexpr float kMaxDt = 0.1f;
// A response that plays always rises at least this far, so a weak strength curve never swallows the fire.
constexpr float kMinHit = 0.02f;

std::uint32_t seedFor(int index) {
    const std::uint32_t seed = 0x9E3779B9u * static_cast<std::uint32_t>(index + 1);
    return seed == 0u ? 1u : seed;
}

// xorshift32, 0..1: the chance and a kind's own randomness, the same for the same frames.
float nextUnit(std::uint32_t& x) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return static_cast<float>(x >> 8) / 16777216.0f;
}

// Beat and Bar are events only while a beat is heard.
bool needsTempo(int row) { return row == static_cast<int>(Moment::Beat) || row == static_cast<int>(Moment::Bar); }

// The event's strength on the frame it comes.
float triggerValue(int row, const ReactSignals& signals) {
    if (row == static_cast<int>(Moment::KeyChange)) return signals.keyChange();
    if (row == static_cast<int>(Moment::Collide)) return 0.0f;  // the picture's event: Bank::hear gives its strength
    return signals.value(hand::trigger(static_cast<Moment>(row)));
}

// How strongly the event holds now: what a While response's gate and a Follow response read.
float levelOf(int row, const ReactSignals& signals) {
    using S = ReactSource;
    switch (static_cast<Moment>(row)) {
        case Moment::Kick: return signals.value(S::KickEnv);
        case Moment::Snare: return signals.value(S::SnareEnv);
        case Moment::Hat: return signals.value(S::HatEnv);
        case Moment::Bass: return signals.value(S::BassAtt);
        case Moment::Drop: return signals.value(S::InDrop);
        case Moment::BuildUp: return signals.value(S::BuildUp);
        case Moment::ChordChange: return signals.value(S::HarmonicChange);
        case Moment::NewSection: return signals.value(S::SectionChange);
        case Moment::Note: return signals.value(S::MelodyPresence);
        case Moment::Loudness: return signals.value(S::Loudness);
        case Moment::Beat: return signals.value(S::Beat);
        case Moment::Bar: return signals.value(S::Downbeat);
        case Moment::Onset: return signals.value(S::OnsetEnv);
        case Moment::LoudnessPeak: return signals.value(S::Surge);
        case Moment::KeyChange: return signals.keyChange();
        case Moment::Collide: return 0.0f;
    }
    return 0.0f;
}

// A response's envelope in seconds. A beat unit needs a tempo: with none heard it cannot be played (never a guess).
struct Times {
    float in = 0.0f;
    float hold = 0.0f;
    float out = 0.0f;
    bool ok = false;
};

float beatSeconds(float bpm) { return bpm > 0.0f ? 60.0f / bpm : 0.0f; }

Times timesOf(const Response& r, float bpm) {
    if (r.unit == TimeUnit::Seconds) return {r.fadeIn, r.hold, r.fadeOut, true};
    if (!(bpm > 0.0f)) return {};
    const float beat = beatSeconds(bpm);
    return {r.fadeIn * beat, r.hold * beat, r.fadeOut * beat, true};
}

}  // namespace

// ── Editing a Config ─────────────────────────────────────────────────────────────────────────────────────

int rowCount(const Config& c, int row) {
    int n = 0;
    for (int i = 0; i < c.count; ++i) {
        if (c.responses[static_cast<size_t>(i)].row == row) ++n;
    }
    return n;
}

int indexOfNth(const Config& c, int row, int nth) {
    if (nth < 0) return -1;
    int seen = 0;
    for (int i = 0; i < c.count; ++i) {
        if (c.responses[static_cast<size_t>(i)].row != row) continue;
        if (seen == nth) return i;
        ++seen;
    }
    return -1;
}

int addResponse(Config& c, Response r) {
    if (r.row < 0 || r.row >= kRows || c.count >= kMaxResponses || rowCount(c, r.row) >= kMaxPerRow) return -1;
    clampResponse(r);
    c.responses[static_cast<size_t>(c.count)] = r;
    return c.count++;
}

bool removeResponse(Config& c, int row, int nth) {
    const int at = indexOfNth(c, row, nth);
    if (at < 0) return false;
    for (int i = at; i + 1 < c.count; ++i) c.responses[static_cast<size_t>(i)] = c.responses[static_cast<size_t>(i + 1)];
    --c.count;
    c.responses[static_cast<size_t>(c.count)] = Response{};
    return true;
}

namespace {

// An ordinal a wire float names, or -1 when it names none of `count`.
int ordinal(float value, int count) {
    if (!std::isfinite(value)) return -1;
    const int i = static_cast<int>(std::lround(value));
    return i >= 0 && i < count ? i : -1;
}

}  // namespace

bool setField(Config& c, int row, int nth, int field, float value) {
    const int at = indexOfNth(c, row, nth);
    if (at < 0 || field < 0 || field >= kFieldCount || !std::isfinite(value)) return false;
    Response r = c.responses[static_cast<size_t>(at)];
    switch (static_cast<Field>(field)) {
        case Field::Enabled:
            r.enabled = value > 0.5f;
            break;
        case Field::Row: {
            const int to = ordinal(value, kRows);
            if (to < 0) return false;
            if (to == r.row) return true;
            if (rowCount(c, to) >= kMaxPerRow) return false;
            // To the end of its new row, the rest keeping their order.
            for (int i = at; i + 1 < c.count; ++i) c.responses[static_cast<size_t>(i)] = c.responses[static_cast<size_t>(i + 1)];
            r.row = to;
            c.responses[static_cast<size_t>(c.count - 1)] = r;
            return true;
        }
        case Field::Kind: {
            const int k = ordinal(value, kKindCount);
            if (k < 0) return false;
            r.kind = k;
            break;
        }
        case Field::Target: {
            const int t = ordinal(value, modtargets::kTargetCount);
            if (t < 0) return false;
            r.target = static_cast<LfoTarget>(t);
            break;
        }
        case Field::Amount: r.amount = value; break;
        case Field::Strength: {
            const int k = ordinal(value, 4);
            if (k < 0) return false;
            r.strength = static_cast<ModCurve>(k);
            break;
        }
        case Field::FadeIn: r.fadeIn = value; break;
        case Field::Hold: r.hold = value; break;
        case Field::FadeOut: r.fadeOut = value; break;
        case Field::Unit: {
            const int u = ordinal(value, kUnitCount);
            if (u < 0) return false;
            r.unit = static_cast<TimeUnit>(u);
            break;
        }
        case Field::Drive: {
            const int d = ordinal(value, kDriveCount);
            if (d < 0) return false;
            r.drive = static_cast<DriveMode>(d);
            break;
        }
        case Field::EveryNth: r.everyNth = static_cast<int>(std::lround(value)); break;
        case Field::Chance: r.chance = value; break;
        case Field::Cooldown: r.cooldown = value; break;
        case Field::P0:
        case Field::P1:
        case Field::P2:
        case Field::P3:
        case Field::P4:
        case Field::P5:
            r.params[static_cast<size_t>(field - static_cast<int>(Field::P0))] = value;
            break;
    }
    clampResponse(r);
    c.responses[static_cast<size_t>(at)] = r;
    return true;
}

void setRowOn(Config& c, int row, bool on) {
    if (row >= 0 && row < kRows) c.rowOn[static_cast<size_t>(row)] = on;
}

void fieldValues(const Response& r, float* out) {
    out[static_cast<int>(Field::Enabled)] = r.enabled ? 1.0f : 0.0f;
    out[static_cast<int>(Field::Row)] = static_cast<float>(r.row);
    out[static_cast<int>(Field::Kind)] = static_cast<float>(r.kind);
    out[static_cast<int>(Field::Target)] = static_cast<float>(static_cast<int>(r.target));
    out[static_cast<int>(Field::Amount)] = r.amount;
    out[static_cast<int>(Field::Strength)] = static_cast<float>(static_cast<int>(r.strength));
    out[static_cast<int>(Field::FadeIn)] = r.fadeIn;
    out[static_cast<int>(Field::Hold)] = r.hold;
    out[static_cast<int>(Field::FadeOut)] = r.fadeOut;
    out[static_cast<int>(Field::Unit)] = static_cast<float>(static_cast<int>(r.unit));
    out[static_cast<int>(Field::Drive)] = static_cast<float>(static_cast<int>(r.drive));
    out[static_cast<int>(Field::EveryNth)] = static_cast<float>(r.everyNth);
    out[static_cast<int>(Field::Chance)] = r.chance;
    out[static_cast<int>(Field::Cooldown)] = r.cooldown;
    for (int i = 0; i < kKindParams; ++i) out[static_cast<int>(Field::P0) + i] = r.params[static_cast<size_t>(i)];
}

// ── The bank ─────────────────────────────────────────────────────────────────────────────────────────────

bool Bank::registerKind(KindId kind, ResponseKindHandler* handler) {
    if (kind <= kParam || kind >= kKindCount) return false;
    handlers_[static_cast<size_t>(kind)] = handler;
    return true;
}

void Bank::configure(const Config& config) {
    const int kept = std::min(config_.count, config.count);
    config_ = config;
    config_.count = std::clamp(config_.count, 0, kMaxResponses);
    for (int i = 0; i < config_.count; ++i) clampResponse(config_.responses[static_cast<size_t>(i)]);
    // What keeps its place keeps playing; the rest starts from rest.
    for (int i = kept; i < kMaxResponses; ++i) {
        runtime_[static_cast<size_t>(i)] = Runtime{};
        runtime_[static_cast<size_t>(i)].rng = seedFor(i);
    }
}

void Bank::preview(const Response& r) {
    previewed_ = r;
    clampResponse(previewed_);
    previewed_.enabled = true;
    // Follow and While have nothing to follow outside their event: the card plays its envelope once.
    if (previewed_.drive == DriveMode::Follow || previewed_.drive == DriveMode::While) previewed_.drive = DriveMode::Once;
    previewSet_ = true;
    previewPending_ = true;
    runtime_[kPreview] = Runtime{};
    runtime_[kPreview].rng = seedFor(kPreview);
}

void Bank::compile(std::array<ReactRoute, ReactionEngine::kRoutes>& routes) {
    if (config_.gate) {
        for (ReactRoute& r : routes) {
            if (!r.enabled || r.external) continue;
            const int row = rowOfSource(r.source);
            if (row >= 0 && !config_.rowOn[static_cast<size_t>(row)]) r.enabled = false;
        }
    }
    status_.compiled = 0;
    status_.dropped = 0;
    for (Runtime& rs : runtime_) rs.slot = -1;
    int cursor = ReactionEngine::kRoutes - 1;
    const auto place = [&](int index, const Response& r) {
        if (r.kind != kParam || r.target == LfoTarget::None) return;
        while (cursor >= 0 && routes[static_cast<size_t>(cursor)].enabled) --cursor;
        if (cursor < 0) {
            ++status_.dropped;
            return;
        }
        ReactRoute route;
        route.enabled = true;
        route.source = ReactSource::Bass;
        route.target = r.target;
        route.mode = ReactMode::Follow;
        route.amount = r.amount;
        route.curve = ModCurve::Linear;
        route.attackSeconds = 0.0f;
        route.releaseSeconds = 0.0f;
        route.external = true;
        routes[static_cast<size_t>(cursor)] = route;
        runtime_[static_cast<size_t>(index)].slot = cursor;
        ++status_.compiled;
        --cursor;
    };
    for (int i = 0; i < config_.count; ++i) {
        const Response& r = config_.responses[static_cast<size_t>(i)];
        if (r.enabled && config_.rowOn[static_cast<size_t>(r.row)]) place(i, r);
    }
    if (previewSet_) place(kPreview, previewed_);
    status_.enabledRoutes = 0;
    for (const ReactRoute& r : routes) {
        if (r.enabled && r.target != LfoTarget::None) ++status_.enabledRoutes;
    }
}

void Bank::reset() {
    for (int i = 0; i <= kMaxResponses; ++i) {
        Runtime& rs = runtime_[static_cast<size_t>(i)];
        const int slot = rs.slot;
        rs = Runtime{};
        rs.rng = seedFor(i);
        rs.slot = slot;
    }
    previewPending_ = false;
    status_.lit.fill(0.0f);
    status_.active = 0;
    activeNow_ = 0;
    heardMask_ = 0;
    heardStrength_.fill(0.0f);
}

void Bank::hear(Moment moment, float strength) {
    const int row = static_cast<int>(moment);
    if (row < 0 || row >= kRows) return;
    const size_t k = static_cast<size_t>(row);
    heardMask_ |= 1u << row;
    heardStrength_[k] = std::max(heardStrength_[k], std::clamp(finiteOr(strength, 0.0f), 0.0f, 1.0f));
}

void Bank::step(const GeodeSoundFrame& f, float dt, const ReactSignals& signals, std::uint32_t moments, ReactionEngine& engine) {
    dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, kMaxDt) : 0.0f;
    const float bpm = std::isfinite(f.bpm) && f.bpm > 0.0f ? f.bpm : 0.0f;
    const float fall = std::exp(-dt / kLitSeconds);
    std::array<bool, kRows> came{};
    std::array<float, kRows> strength{};
    std::array<float, kRows> level{};
    for (int row = 0; row < kRows; ++row) {
        const size_t k = static_cast<size_t>(row);
        bool c = ((moments >> row) & 1u) != 0u;
        if (c && needsTempo(row) && bpm <= 0.0f) c = false;
        came[k] = c;
        strength[k] = std::clamp(finiteOr(triggerValue(row, signals), 0.0f), 0.0f, 1.0f);
        level[k] = std::clamp(finiteOr(levelOf(row, signals), 0.0f), 0.0f, 1.0f);
        // An event the picture made since the last step comes like a moment of the music; its level is its strength.
        if (((heardMask_ >> row) & 1u) != 0u) {
            c = true;
            strength[k] = std::max(strength[k], heardStrength_[k]);
            level[k] = std::max(level[k], heardStrength_[k]);
            came[k] = true;
        }
        status_.lit[k] *= fall;
        if (c) status_.lit[k] = 1.0f;
    }
    heardMask_ = 0;
    heardStrength_.fill(0.0f);
    // Where the sound puts things: the pitch heard as a height, the pan as a place across (the middle when none).
    const float pitch = signals.value(ReactSource::PitchConfidence) > 0.0f
                            ? std::clamp(finiteOr(signals.value(ReactSource::PitchHeight), 0.5f), 0.0f, 1.0f)
                            : 0.5f;
    const float pan = std::clamp(0.5f + 0.5f * finiteOr(signals.value(ReactSource::StereoPan), 0.0f), 0.0f, 1.0f);
    for (ResponseKindHandler* handler : handlers_) {
        if (handler != nullptr) handler->tick(dt, pitch, pan, beatSeconds(bpm));
    }
    activeNow_ = 0;
    for (int i = 0; i < config_.count; ++i) {
        const Response& r = config_.responses[static_cast<size_t>(i)];
        const size_t row = static_cast<size_t>(r.row);
        const bool live = r.enabled && config_.rowOn[row];
        advance(i, r, live, came[row], false, strength[row], level[row], bpm, dt, engine);
    }
    if (previewSet_) {
        advance(kPreview, previewed_, true, previewPending_, true, 1.0f, 1.0f, bpm, dt, engine);
        previewPending_ = false;
    }
    status_.active = activeNow_;
    status_.cost = status_.enabledRoutes + 2 * activeNow_;
    status_.budget = kCostBudget;
    status_.level = status_.cost >= kCostBudget ? 2 : (status_.cost * 4 >= kCostBudget * 3 ? 1 : 0);
}

void Bank::advance(int index, const Response& r, bool live, bool candidate, bool previewing, float strength, float level, float bpm,
                   float dt, ReactionEngine& engine) {
    Runtime& s = runtime_[static_cast<size_t>(index)];
    s.cooldownLeft = std::max(0.0f, s.cooldownLeft - dt);
    const Times t = timesOf(r, bpm);
    const bool playable = live && t.ok;
    const bool follows = r.drive == DriveMode::Follow;
    const float shaped = std::clamp(shape(strength, r.strength), 0.0f, 1.0f);

    // Whether the event plays it: a cooldown that has not run out passes the hit over (it is not counted), the
    // Nth counts the hits that get past it, and the chance is drawn on every counted hit so the generator's
    // place never depends on the odds.
    bool fire = false;
    if (playable && !follows && candidate) {
        if (previewing) {
            fire = true;
        } else if (s.cooldownLeft <= 0.0f) {
            const int n = std::max(1, r.everyNth);
            const bool nth = s.heard % n == 0;
            s.heard = (s.heard + 1) % n;
            const float u = nextUnit(s.rng);
            fire = nth && (r.chance >= 1.0f || u < r.chance);
        }
    }
    if (fire) {
        s.cooldownLeft = previewing ? 0.0f : r.cooldown;
        s.age = 0.0f;
        s.holdFor = t.in + t.hold;
        s.running = true;
        s.gate = true;
    }
    // A While response stays on as long as its event holds (a rise through 0.5, let go below 0.3).
    if (playable && !fire && r.drive == DriveMode::While) {
        s.gate = level >= MomentWatch::kRise || (s.gate && level >= MomentWatch::kRearm);
    }

    if (r.kind == kParam) {
        float out = 0.0f;
        if (!playable) {
            s.voice.reset();
            s.running = false;
            s.gate = false;
        } else if (follows) {
            out = std::clamp(shape(level, r.strength), 0.0f, 1.0f);
            if (s.slot >= 0) {
                ReactRoute& route = engine.routes[static_cast<size_t>(s.slot)];
                route.attackSeconds = t.in;
                route.releaseSeconds = t.out;
            }
            s.running = out > 0.001f;
        } else {
            const bool open = r.drive == DriveMode::While ? s.gate : s.age < s.holdFor;
            const AdsrShape stages{t.in, 0.0f, 1.0f, t.out, false, r.drive == DriveMode::Once, 0.0f};
            out = s.voice.step(stages, dt, fire ? std::max(shaped, kMinHit) : 0.0f, 0.0f, open, open);
            s.age += dt;
            s.running = s.voice.stage != AdsrStage::Idle;
        }
        if (s.slot >= 0) engine.setExternal(s.slot, out);
    } else {
        ResponseKindHandler* handler = handlers_[static_cast<size_t>(r.kind)];
        if (handler == nullptr || !playable) {
            if (handler != nullptr && s.running && r.drive == DriveMode::While) handler->release(index);
            s.running = false;
            s.gate = false;
        } else {
            if (fire) handler->fire(ResponseKindHandler::Play{index, &r, shaped, t.in, t.hold, t.out, s.rng, beatSeconds(bpm)});
            if (follows) {
                handler->follow(index, r, std::clamp(shape(level, r.strength), 0.0f, 1.0f));
                s.running = true;
            } else if (r.drive == DriveMode::While) {
                if (s.running && !s.gate) {
                    handler->release(index);
                    s.running = false;
                }
            } else {
                // Once and Long: the kind owns what it made, the bank has nothing running.
                s.running = false;
            }
        }
    }
    if (s.running) ++activeNow_;
}

}  // namespace geode::viz::responder
