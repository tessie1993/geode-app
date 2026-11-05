#include "scene/SceneDirector.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "viz/Reactions.hpp"

namespace geode::scene {

namespace {

// splitmix64 (Steele, Lea & Flood 2014): a full-period mixer, so (seed, moment) keys draw independently.
constexpr std::uint64_t mix(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// A short stream of uniform draws in [0, 1), all from one key.
struct Draw {
    std::uint64_t state;
    float next() {
        state += 0x9E3779B97F4A7C15ull;
        return static_cast<float>(mix(state) >> 40) * (1.0f / 16777216.0f);
    }
    int below(int n) { return std::min(n - 1, static_cast<int>(next() * static_cast<float>(n))); }
};

constexpr std::uint64_t kPersonaSalt = 0xD1B54A32D192ED03ull;

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

// The feeling's pull on one weight, bent by the depth: f^depth, so 0 leaves every weight at its param.
float bend(float factor, float depth) { return std::pow(std::max(factor, 1e-4f), depth); }

// Picks an index by weight; -1 when every weight is zero.
template <std::size_t N>
int pick(const std::array<float, N>& w, Draw& d) {
    float sum = 0.0f;
    for (float x : w) sum += std::max(x, 0.0f);
    if (sum <= 0.0f) return -1;
    float u = d.next() * sum;
    int last = -1;
    for (std::size_t i = 0; i < N; ++i) {
        if (w[i] <= 0.0f) continue;
        last = static_cast<int>(i);
        if (u < w[i]) return last;
        u -= w[i];
    }
    return last;  // rounding: the last kind with weight
}

std::uint32_t momentBit(MomentKind k) {
    switch (k) {
    case MomentKind::Section: return viz::hand::bit(viz::Moment::NewSection);
    case MomentKind::Drop: return viz::hand::bit(viz::Moment::Drop);
    case MomentKind::BuildCrest: return viz::hand::bit(viz::Moment::BuildUp);
    case MomentKind::TrackStart: return 0;  // no moment of the hand's owns a new track
    }
    return 0;
}

float barSeconds(const GeodeSoundFrame& f) {
    const float bpm = (f.bpm > 0.0f && std::isfinite(f.bpm)) ? std::clamp(f.bpm, 60.0f, 200.0f) : 120.0f;
    return 240.0f / bpm;
}

constexpr int labelOf(ChoiceKind k, ShotKind s) {
    return k == ChoiceKind::Nothing ? 0
           : k == ChoiceKind::CameraShot ? 1 + static_cast<int>(s)
           : k == ChoiceKind::Spawn ? 1 + kShotKinds
                                    : 2 + kShotKinds;
}

}  // namespace

signal::ParamSchema directorSchema() {
    using signal::ParamDef;
    using signal::ParamKind;
    std::vector<ParamDef> rows{
        {"sceneDirector.nothing", ParamKind::Float, 1.0f, 0.0f, 4.0f},
        {"sceneDirector.shot", ParamKind::Float, 1.0f, 0.0f, 4.0f},
        {"sceneDirector.spawn", ParamKind::Float, 0.8f, 0.0f, 4.0f},
        {"sceneDirector.env", ParamKind::Float, 0.8f, 0.0f, 4.0f},
        {"sceneDirector.feelingDepth", ParamKind::Float, 1.0f, 0.0f, 2.0f},
        {"sceneDirector.personality", ParamKind::Float, 0.75f, 0.0f, 1.0f},
        {"sceneDirector.moreChance", ParamKind::Float, 0.5f, 0.0f, 1.0f},
        {"sceneDirector.repeatDamp", ParamKind::Float, 0.5f, 0.0f, 1.0f},
        {"sceneDirector.maxChoices", ParamKind::Int, 3.0f, 1.0f, static_cast<float>(SceneDirector::kMaxChoices)},
    };
    return signal::ParamSchema(std::move(rows));
}

static_assert(kDirectorParamCount == 9, "directorSchema() has one row per DirectorParam");

SceneDirector::SceneDirector() : store_(directorSchema()) { reset(0); }

void SceneDirector::reset(std::uint64_t seed) {
    seed_ = seed;
    // The play's personality: a multiplier per kind and per shot in 0.4..1.6, scaled by the param at use.
    Draw d{mix(seed ^ kPersonaSalt)};
    for (float& p : kindPersona_) p = 0.4f + 1.2f * d.next();
    for (float& p : shotPersona_) p = 0.4f + 1.2f * d.next();
    moments_ = 0;
    trackPending_ = false;
    crestArmed_ = true;
    lastSection_ = false;
    lastDrop_ = false;
    lastLabel_ = -1;
    clock_ = 0.0;
    lastMomentAt_ = -1e9;
    count_ = 0;
}

void SceneDirector::onTrackBoundary(std::uint64_t newSeed) {
    reset(newSeed);
    trackPending_ = true;
}

std::span<const Choice> SceneDirector::step(const GeodeSoundFrame& f, float dt, const Context& ctx) {
    count_ = 0;
    const float stepDt = (std::isfinite(dt) && dt > 0.0f) ? dt : 0.0f;
    store_.advance(stepDt);

    // The edges, read every frame so a trigger is taken once whatever the frame rate.
    const bool sectionNow = f.sectionChange > 0.0f;
    const bool dropNow = f.drop > 0.0f;
    const bool sectionEdge = sectionNow && !lastSection_;
    const bool dropEdge = dropNow && !lastDrop_;
    lastSection_ = sectionNow;
    lastDrop_ = dropNow;
    bool crestEdge = false;
    if (f.buildUp < kRearm) crestArmed_ = true;
    if (crestArmed_ && f.buildUp >= kCrest) {
        crestArmed_ = false;
        crestEdge = true;
    }

    // One moment a frame, the strongest: a new track, a drop, a section start, a crest.
    bool have = true;
    Moment m{MomentKind::Section, 0.0f};
    if (trackPending_) {
        m = {MomentKind::TrackStart, 1.0f};
        trackPending_ = false;
    } else if (dropEdge) {
        m = {MomentKind::Drop, clamp01(f.drop)};
    } else if (sectionEdge) {
        m = {MomentKind::Section, clamp01(f.sectionChange)};
    } else if (crestEdge) {
        m = {MomentKind::BuildCrest, kCrest};  // the crossing, not the frame's value: any frame rate alike
    } else {
        have = false;
    }

    if (have) {
        // The moment's number is used up even when nothing is decided, so every later moment keeps its own draw.
        const int index = moments_++;
        const bool owned = (ctx.handMask & momentBit(m.kind)) != 0;
        if (ctx.enabled && !owned) count_ = decide(m, index, f, ctx);
        lastMomentAt_ = clock_;
    }
    clock_ += stepDt;
    store_.clearModulation();
    return {out_.data(), static_cast<std::size_t>(count_)};
}

int SceneDirector::decide(const Moment& m, int index, const GeodeSoundFrame& f, const Context& ctx) {
    // Keyed on (seed, moment number) alone: never the frame count, so any frame rate draws alike.
    Draw d{mix(seed_ ^ mix(static_cast<std::uint64_t>(index) + 1u))};
    auto param = [this](DirectorParam id) { return store_.effective(id); };
    const float depth = param(kFeelingDepth);
    const float persona = param(kPersonality);
    const float damp = param(kRepeatDamp);
    const int cap = std::clamp(static_cast<int>(std::lround(param(kMaxChoices))), 1, kMaxChoices);
    auto personaOf = [persona](float p) { return 1.0f + persona * (p - 1.0f); };

    const float energy = clamp01(ctx.feeling.energy);
    const float valence = std::clamp(ctx.feeling.valence, -1.0f, 1.0f);
    const float tension = clamp01(ctx.feeling.tension);
    const float calm = 1.0f - energy;
    const bool drop = m.kind == MomentKind::Drop;
    const bool crest = m.kind == MomentKind::BuildCrest;
    const bool track = m.kind == MomentKind::TrackStart;
    const float ds = drop ? m.strength : 0.0f;
    const int state = static_cast<int>(std::lround(f.sectionState));
    const float bar = barSeconds(f);
    // Two moments within two bars: the second keeps its hands off the camera more often.
    const bool crowded = (clock_ - lastMomentAt_) < 2.0 * bar;

    // The feeling's pull on each kind.
    float nothing = 0.5f + 1.5f * calm;
    if (state == GEODE_SECTION_BREAKDOWN || state == GEODE_SECTION_OUTRO) nothing *= 1.5f;
    else if (state == GEODE_SECTION_INTRO) nothing *= 1.2f;
    else if (state == GEODE_SECTION_BUILD) nothing *= 0.8f;
    else if (state == GEODE_SECTION_DROP) nothing *= 0.6f;
    if (drop) nothing *= 1.0f - 0.85f * ds;
    if (track) nothing *= 0.7f;

    float shot = 0.6f + 0.8f * energy + 0.4f * tension;
    if (drop) shot *= 1.0f + ds;
    if (crest) shot *= 1.3f;
    if (crowded) shot *= 0.5f;

    float spawn = 0.0f;
    if (ctx.spawners > 0) {
        spawn = 0.3f + energy + 0.4f * std::max(valence, 0.0f);
        if (drop) spawn *= 1.0f + 1.5f * ds;
        if (state == GEODE_SECTION_DROP) spawn *= 1.3f;
    }

    float env = 0.5f + 0.5f * tension + 0.15f * (1.0f - valence);
    if (track) env *= 2.0f;
    else if (m.kind == MomentKind::Section) env *= 1.2f;
    else if (drop) env *= 1.0f + 0.5f * ds;

    std::array<float, kChoiceKinds> w{
        param(kNothingWeight) * personaOf(kindPersona_[0]) * bend(nothing, depth),
        param(kShotWeight) * personaOf(kindPersona_[1]) * bend(shot, depth),
        spawn > 0.0f ? param(kSpawnWeight) * personaOf(kindPersona_[2]) * bend(spawn, depth) : 0.0f,
        param(kEnvWeight) * personaOf(kindPersona_[3]) * bend(env, depth),
    };

    // The shots this moment allows, by the same pull.
    const bool hasObjects = ctx.objects > 0;
    const bool calmComfort = ctx.comfort == camera::Comfort::Calm;
    const bool wild = ctx.comfort == camera::Comfort::Wild;
    const float push = (drop ? ds : 0.0f) + (crest || state == GEODE_SECTION_BUILD ? 0.6f : 0.0f);
    std::array<float, kShotKinds> sw{
        1.0f + 0.5f * std::max(valence, 0.0f) + 0.5f * calm,          // Orbit
        hasObjects ? 0.8f + 0.6f * tension : 0.0f,                    // LockOn
        hasObjects ? 0.8f + 0.6f * energy : 0.0f,                     // Follow
        calmComfort ? 0.0f : (0.4f + 1.2f * ds + 0.6f * tension) * (wild ? 1.5f : 1.0f),  // Flip
        calmComfort ? 0.0f : 0.5f + 0.6f * energy + (m.kind == MomentKind::Section ? 0.4f : 0.0f),  // TurnRound
        0.6f + 1.2f * push + 0.4f * energy,                            // Push
        0.6f + calm,                                                   // Hold
    };
    for (int i = 0; i < kShotKinds; ++i) {
        if (sw[static_cast<std::size_t>(i)] > 0.0f)
            sw[static_cast<std::size_t>(i)] =
                personaOf(shotPersona_[static_cast<std::size_t>(i)]) * bend(sw[static_cast<std::size_t>(i)], depth);
    }

    // The film does not repeat itself: the last pick is damped (Nothing may repeat: a calm song stays).
    if (lastLabel_ >= 1 && lastLabel_ <= kShotKinds) {
        sw[static_cast<std::size_t>(lastLabel_ - 1)] *= damp;
    } else if (lastLabel_ == 1 + kShotKinds) {
        w[2] *= damp;
    } else if (lastLabel_ == 2 + kShotKinds) {
        w[3] *= damp;
    }
    float shotSum = 0.0f;
    for (float x : sw) shotSum += x;
    if (shotSum <= 0.0f) w[1] = 0.0f;

    const float intensity = drop ? ds : crest ? 0.6f : track ? 0.5f : 0.6f * energy;
    const float more = param(kMoreChance) * (0.25f + 0.75f * intensity);

    int n = 0;
    int firstLabel = -1;
    int k = pick(w, d);
    if (k < 0) k = 0;
    while (n < cap) {
        Choice c{};
        c.kind = static_cast<ChoiceKind>(k);
        c.moment = m.kind;
        c.strength = m.strength;
        switch (c.kind) {
        case ChoiceKind::Nothing: break;
        case ChoiceKind::CameraShot: {
            const int s = std::max(pick(sw, d), 0);
            c.shot.kind = static_cast<ShotKind>(s);
            const bool subject = c.shot.kind == ShotKind::Orbit || c.shot.kind == ShotKind::LockOn ||
                                 c.shot.kind == ShotKind::Follow;
            c.shot.targetHint = (subject && hasObjects) ? d.below(ctx.objects) : -1;
            // Whole bars: calm songs hold a shot longer, a drop cuts it short.
            static constexpr std::array<int, 4> kBars{1, 2, 4, 8};
            std::array<float, 4> bw{0.5f + energy + ds, 1.0f, 0.5f + calm, calm * calm};
            int bars = kBars[static_cast<std::size_t>(std::max(pick(bw, d), 0))];
            if (c.shot.kind == ShotKind::Flip) bars = 1;
            else if (c.shot.kind == ShotKind::TurnRound) bars = std::min(bars, 2);
            c.shot.duration = c.shot.kind == ShotKind::Hold ? 0.0f : static_cast<float>(bars) * bar;
            break;
        }
        case ChoiceKind::Spawn: {
            c.spawn.spawner = d.below(ctx.spawners);
            c.spawn.burst = drop || d.next() < 0.25f * energy;
            const float u = d.next();
            c.spawn.count = c.spawn.burst ? 8 + static_cast<int>(u * 40.0f * (0.4f + 0.6f * intensity))
                                          : 1 + static_cast<int>(u * 4.0f * (0.5f + energy));
            break;
        }
        case ChoiceKind::EnvShift: {
            const float u = d.next();
            c.env.glideSeconds = drop    ? 0.25f + 0.5f * u
                                 : track ? 2.0f + 2.0f * u
                                         : bar * (1.0f + std::floor(u * 2.0f)) * (1.0f + calm);
            c.env.mutation = std::clamp(0.15f + 0.35f * tension + 0.3f * intensity + (track ? 0.3f : 0.0f) +
                                            0.2f * (d.next() - 0.5f),
                                        0.05f, 1.0f);
            break;
        }
        }
        out_[static_cast<std::size_t>(n++)] = c;
        if (firstLabel < 0) firstLabel = labelOf(c.kind, c.shot.kind);
        if (c.kind == ChoiceKind::Nothing) break;  // Nothing is the moment's only choice
        // Another kind at the same moment, at most one of each, never Nothing after something.
        w[0] = 0.0f;
        w[static_cast<std::size_t>(k)] = 0.0f;
        if (n >= cap || d.next() >= more) break;
        k = pick(w, d);
        if (k < 0) break;
    }
    lastLabel_ = firstLabel;
    return n;
}

}  // namespace geode::scene
