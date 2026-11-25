#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

#include "api/geode_api.h"
#include "viz/modulation/Adsr.hpp"
#include "viz/modulation/Lfo.hpp"
#include "viz/modulation/Reactions.hpp"

namespace geode::viz::responder {

// The Event Responder bank: a Song event row owns an ordered list of responses, and a response is what the
// picture does when that event is heard. There is no patch bay: the row is the wiring.
//
//   event (a Moment) -> row -> responses, each with its own parameters, a strength curve, an envelope
//   (fade in / hold / fade out), a drive mode and every-Nth, chance and cooldown.
//
// A response that moves a parameter (kind Param) compiles into a route of the reaction matrix
// (ReactionEngine, an `external` route) and its envelope runs on the same stages as the waves' envelopes
// (AdsrVoice, sustain as hold); nothing here is a second modulation path. The kinds that need systems the
// engine does not have yet (Things, Fronts, Fields, Clock control) register a ResponseKindHandler under their
// reserved id and are played by the same bank, with the same gates and the same seeded chance.
//
// Pure: no GL, no locks, no allocation after construction. One step a frame on the stepping thread, the same
// frame and dt sequence gives the same fires (chance draws from a seeded generator), so live and export hear
// the same sequence. Time is seconds; a response set in beats plays only while a tempo is heard (the frame's
// bpm > 0) and stays idle otherwise: no tempo is ever assumed.

// How many rows (one per Moment), responses and kinds there can be.
constexpr int kRows = kMomentCount;
constexpr int kMaxResponses = 128;
constexpr int kMaxPerRow = 24;
constexpr int kKindCount = 16;
// Room in a response for what a kind needs besides the common controls.
constexpr int kKindParams = 6;

// A response kind's wire ordinal. Param is built in; the rest are reserved for the systems that will register.
using KindId = int;
constexpr KindId kParam = 0;
constexpr KindId kThing = 1;
constexpr KindId kFront = 2;
constexpr KindId kField = 3;
constexpr KindId kClock = 4;

// Wire ordinals; append only.
enum class DriveMode { Once, While, Follow, Long };
constexpr int kDriveCount = static_cast<int>(DriveMode::Long) + 1;
enum class TimeUnit { Seconds, Beats };
constexpr int kUnitCount = static_cast<int>(TimeUnit::Beats) + 1;
// Where a listed response comes from: the user's own, or a route the style (or the director's DNA) plays.
enum class Origin { User, StyleDefault };

constexpr float kMinAmount = -2.0f;
constexpr float kMaxAmount = 2.0f;
constexpr float kMaxSeconds = 30.0f;
constexpr int kMaxEveryNth = 64;
constexpr float kMaxParam = 10000.0f;

// One response: an instance on a row.
struct Response {
    bool enabled = true;
    int row = 0;                                 // a Moment ordinal
    KindId kind = kParam;
    LfoTarget target = LfoTarget::None;          // Param: the parameter it moves
    float amount = 0.3f;                         // kMinAmount..kMaxAmount, at full strength
    ModCurve strength = ModCurve::Linear;        // how much the event's strength matters
    float fadeIn = 0.05f;                        // seconds (beats when `unit` says so)
    float hold = 0.1f;
    float fadeOut = 0.3f;
    TimeUnit unit = TimeUnit::Seconds;
    DriveMode drive = DriveMode::Once;
    int everyNth = 1;                            // plays on the 1st hit, then every Nth after it
    float chance = 1.0f;                         // 0..1, drawn from the seeded generator
    float cooldown = 0.0f;                       // seconds a played response waits before it may play again
    std::array<float, kKindParams> params{};     // the kind's own parameters (Param uses none)
};

// A response's fields on the wire, in this order, as the edit call's `field` and the list's values.
enum class Field {
    Enabled, Row, Kind, Target, Amount, Strength, FadeIn, Hold, FadeOut, Unit, Drive, EveryNth, Chance, Cooldown,
    P0, P1, P2, P3, P4, P5,
};
constexpr int kFieldCount = static_cast<int>(Field::P5) + 1;
// A listed response: its fields, then its Origin and (for a style default) the route's index in what plays.
constexpr int kListValues = kFieldCount + 2;

// The rows' saved names, in Moment order.
constexpr std::array<std::string_view, kRows> kRowNames = {
    "KICK", "SNARE", "HAT", "BASS", "DROP", "BUILD_UP", "CHORD_CHANGE", "NEW_SECTION", "NOTE", "LOUDNESS",
    "BEAT", "BAR", "ONSET", "LOUDNESS_PEAK", "KEY_CHANGE",
};
static_assert(kRowNames.size() == static_cast<size_t>(kMomentCount), "one name per Moment");

inline int rowNamed(std::string_view name) {
    for (int i = 0; i < kRows; ++i) {
        if (kRowNames[static_cast<size_t>(i)] == name) return i;
    }
    return -1;
}

inline float finiteOr(float v, float fallback) { return std::isfinite(v) ? v : fallback; }

// The response with every field held inside what the bank accepts.
inline void clampResponse(Response& r) {
    r.row = std::clamp(r.row, 0, kRows - 1);
    r.kind = std::clamp(r.kind, 0, kKindCount - 1);
    r.amount = std::clamp(finiteOr(r.amount, 0.3f), kMinAmount, kMaxAmount);
    r.fadeIn = std::clamp(finiteOr(r.fadeIn, 0.05f), 0.0f, kMaxSeconds);
    r.hold = std::clamp(finiteOr(r.hold, 0.1f), 0.0f, kMaxSeconds);
    r.fadeOut = std::clamp(finiteOr(r.fadeOut, 0.3f), 0.0f, kMaxSeconds);
    r.everyNth = std::clamp(r.everyNth, 1, kMaxEveryNth);
    r.chance = std::clamp(finiteOr(r.chance, 1.0f), 0.0f, 1.0f);
    r.cooldown = std::clamp(finiteOr(r.cooldown, 0.0f), 0.0f, kMaxSeconds);
    for (float& p : r.params) p = std::clamp(finiteOr(p, 0.0f), -kMaxParam, kMaxParam);
}

// What the bank holds for one renderer: which rows are on, and every row's responses in order.
struct Config {
    std::array<bool, kRows> rowOn{};
    // True: a row that is off also holds back the style's own routes on its events (the "style default" chips),
    // so nothing plays that its row does not show on. False (a list saved before the rows existed): the style's
    // routes play as they always did.
    bool gate = false;
    std::array<Response, kMaxResponses> responses{};
    int count = 0;
};

// A new style: every row off, the rows gating the style's own routes.
inline Config newStyleConfig() {
    Config c;
    c.gate = true;
    return c;
}

// The row's own routes of the matrix: the route's source belongs to a Moment (hand::momentOf), or is the
// trigger of one of the Responder's. -1 for a source no row claims (a level, an EQ band, the tempo, a phase).
inline int rowOfSource(ReactSource s) {
    const int m = hand::momentOf(s);
    if (m >= 0) return m;
    switch (s) {
        case ReactSource::Beat: return static_cast<int>(Moment::Beat);
        case ReactSource::Downbeat: return static_cast<int>(Moment::Bar);
        case ReactSource::Onset: return static_cast<int>(Moment::Onset);
        default: return -1;
    }
}

// ── Editing a Config (the Customize page's edits; text in, text out in api/geode_responder_api) ───────────────

int rowCount(const Config& c, int row);
// The config's index of the nth response of a row, or -1.
int indexOfNth(const Config& c, int row, int nth);
// Appends `r` (held inside its ranges) to its row; the new response's index in the config, or -1 when the row is
// not one, the row or the bank is full.
int addResponse(Config& c, Response r);
// Removes the nth response of a row, keeping the others in order; false when there is none.
bool removeResponse(Config& c, int row, int nth);
// One field of the nth response of a row. An unknown field or an ordinal outside its enum changes nothing; a
// number lands inside its range. The row field moves the response to the end of that row.
bool setField(Config& c, int row, int nth, int field, float value);
void setRowOn(Config& c, int row, bool on);
// The wire values of a response (kFieldCount of them, the order of Field).
void fieldValues(const Response& r, float* out);

// ── Playing it ───────────────────────────────────────────────────────────────────────────────────────────

// A later system's hook (Things, Fronts, Fields, Clock control): the bank gates and times its responses (rows,
// every Nth, chance, cooldown, strength, seconds against beats) and hands each one that plays to the handler
// registered for its kind. The handler outlives the bank's use of it and is called on the stepping thread.
class ResponseKindHandler {
public:
    struct Play {
        int instance = 0;                 // the response's index in the config (stable until the config changes)
        const Response* response = nullptr;
        float strength = 1.0f;            // the event's strength through the response's strength curve, 0..1
        float fadeInSeconds = 0.0f;       // the envelope in seconds, a beat unit already turned into them
        float holdSeconds = 0.0f;
        float fadeOutSeconds = 0.0f;
        std::uint32_t seed = 1;           // a seeded draw for the kind's own randomness
        float beatSeconds = 0.0f;         // a beat in seconds while a tempo is heard, else 0 (never a stand-in tempo)
    };
    virtual ~ResponseKindHandler() = default;
    // A response of this kind plays (drive Once, Long, or While as its event begins).
    virtual void fire(const Play& play) = 0;
    // Drive Follow: every frame, the event's level through the strength curve.
    virtual void follow(int, const Response&, float) {}
    // Drive While: the event ended.
    virtual void release(int) {}
};

// What the Customize page reads.
struct Status {
    // Each row's event light: 1 when its event came (a beat or a bar only when one is heard), easing to 0.
    std::array<float, kRows> lit{};
    int enabledRoutes = 0;  // routes of the matrix that play, the style's and the compiled responses'
    int compiled = 0;       // parameter responses that got a route
    int dropped = 0;        // parameter responses that found the matrix full
    int active = 0;         // responses playing now
    int cost = 0;           // an estimate of this frame's work, in route evaluations
    int budget = 0;         // what a style may spend before the page warns
    // 0 light, 1 heavy (three quarters of the budget), 2 over it.
    int level = 0;
};

// The per-frame cost budget the page shows: a route costs 1, a playing envelope 2, a playing response of another
// kind 4. A style over it still plays (the page only warns): the hard limits are the matrix's slots.
constexpr int kCostBudget = 512;

class Bank {
public:
    Bank() { reset(); }

    // Registers the system that plays `kind` (1..kKindCount-1; Param is the bank's own). A null handler removes it.
    // False for a kind the bank cannot hand out.
    bool registerKind(KindId kind, ResponseKindHandler* handler);

    // The rows and responses to play. What was playing carries on for the responses that keep their place.
    void configure(const Config& config);
    const Config& config() const { return config_; }

    // Plays `r` once now, as its row's event would (the picker's live preview): on the next step, whatever the
    // row's switch, the Nth, the chance and the cooldown say. A Follow or While response plays as Once.
    void preview(const Response& r);

    // Holds back the matrix's routes on the rows that are off (only while the config gates), then puts the
    // parameter responses into the slots the matrix leaves free, taking them from its top. Call after the
    // style's routes are in `routes`, and again whenever they, the config or the preview change.
    void compile(std::array<ReactRoute, ReactionEngine::kRoutes>& routes);

    // Restarts: every envelope at rest, every counter and cooldown cleared, the generators reseeded.
    void reset();

    // One frame. `moments` is the bit per Moment that came (MomentWatch), `signals` the frame's sources; the
    // levels of the compiled routes go to `engine`.
    void step(const GeodeSoundFrame& f, float dt, const ReactSignals& signals, std::uint32_t moments, ReactionEngine& engine);

    const Status& status() const { return status_; }

    static constexpr float kLitSeconds = 0.25f;

private:
    struct Runtime {
        AdsrVoice voice;
        float age = 0.0f;
        float holdFor = 0.0f;
        float cooldownLeft = 0.0f;
        int heard = 0;
        std::uint32_t rng = 1;
        bool gate = false;
        bool running = false;
        int slot = -1;
    };

    // The preview is the last of the runtimes.
    static constexpr int kPreview = kMaxResponses;

    void advance(int index, const Response& r, bool live, bool candidate, bool previewing, float strength, float level, float bpm,
                 float dt, ReactionEngine& engine);

    Config config_;
    Response previewed_;
    bool previewPending_ = false;
    bool previewSet_ = false;
    std::array<Runtime, kMaxResponses + 1> runtime_{};
    std::array<ResponseKindHandler*, kKindCount> handlers_{};
    Status status_;
    int activeNow_ = 0;
};

}  // namespace geode::viz::responder
