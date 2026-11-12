#include "viz/ModDocument.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <vector>

#include "util/Json.hpp"
#include "viz/ConfigMath.hpp"
#include "viz/ModCatalog.hpp"
#include "viz/ModTargets.hpp"
#include "viz/ReactionText.hpp"
#include "viz/TableNumber.hpp"

namespace geode::viz {

namespace {

using json::Value;

constexpr int kRouteFloats = ReactionEngine::kConfigFloats;
static_assert(kRouteFieldCount == kRouteFloats, "a route's fields are viz/ReactionText's floats, in its order");
using RouteFloats = std::array<float, static_cast<size_t>(ReactionEngine::kRoutes * kRouteFloats)>;

// A saved number as a float; NaN for one that is not finite, so the caller's range check refuses it.
float narrow(double d) {
    if (!std::isfinite(d)) return std::nanf("");
    return static_cast<float>(std::clamp(d, -1.0e30, 1.0e30));
}

// A number, or text that is one from end to end: a file edited by hand may quote its numbers.
std::optional<double> numberOf(const Value* v) {
    if (!v) return std::nullopt;
    if (v->kind() == Value::Kind::Number) return v->number();
    if (v->kind() != Value::Kind::String || v->string().empty()) return std::nullopt;
    const char* begin = v->string().c_str();
    char* end = nullptr;
    const double d = std::strtod(begin, &end);
    if (end == begin || *end != '\0') return std::nullopt;
    return d;
}

float floatOf(const Value& o, const char* key, config::Slot slot) {
    const std::optional<double> d = numberOf(o.get(key));
    return d ? config::sanitized(slot, narrow(*d)) : config::spec(slot).defaultValue;
}

int intOf(const Value& o, const char* key, int fallback) {
    const std::optional<double> d = numberOf(o.get(key));
    if (!d || !std::isfinite(*d)) return fallback;
    return static_cast<int>(std::clamp(*d, -1.0e9, 1.0e9));
}

bool boolOf(const Value& o, const char* key, bool fallback) {
    const Value* v = o.get(key);
    if (!v) return fallback;
    if (v->kind() == Value::Kind::Bool) return v->boolValue();
    if (v->kind() != Value::Kind::String) return fallback;
    std::string s = v->string();
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (s == "true") return true;
    if (s == "false") return false;
    return fallback;
}

std::string_view nameOf(const Value& o, const char* key) {
    const Value* v = o.get(key);
    return v && v->kind() == Value::Kind::String ? std::string_view(v->string()) : std::string_view();
}

template <typename E, typename Named>
E enumOf(const Value& o, const char* key, E fallback, Named named) {
    const int i = named(nameOf(o, key));
    return i >= 0 ? static_cast<E>(i) : fallback;
}

// Only the periods the editor offers; anything else is free timing.
float beatsOf(float v) {
    for (int i = 1; i < modcatalog::kBeatChoices; ++i) {
        if (v == modcatalog::beatChoice(i)) return v;
    }
    return 0.0f;
}

// A knob id the macro bank can hold; anything else is none.
int knobOf(int v) { return v >= 1 && v <= 999999 ? v : 0; }

// A Moment ordinal; anything else is none.
int momentOf(int v) { return v >= 0 && v < kMomentCount ? v : -1; }

// The ordinal an edit's value names, or -1 outside 0..count-1.
int ordinalOf(float value, int count) {
    if (!std::isfinite(value)) return -1;
    const float r = std::round(value);
    return r >= 0.0f && r < static_cast<float>(count) ? static_cast<int>(r) : -1;
}

int wholeOf(float value, int fallback) {
    if (!std::isfinite(value)) return fallback;
    return static_cast<int>(std::clamp(std::round(value), -1.0e9f, 1.0e9f));
}

// The parsed text's slots: empty unless the text is a JSON array.
std::vector<Value> slotsOf(std::string_view text) {
    const std::optional<Value> doc = json::parse(std::string(text));
    if (!doc || doc->kind() != Value::Kind::Array) return {};
    return doc->array();
}

LfoConfig lfoOf(const Value& o) {
    LfoConfig c;
    c.enabled = boolOf(o, "enabled", c.enabled);
    c.source = enumOf(o, "source", c.source, modcatalog::sourceNamed);
    c.target = enumOf(o, "target", c.target, modcatalog::targetNamed);
    c.wave = enumOf(o, "wave", c.wave, modcatalog::waveNamed);
    if (o.get("rateSeconds")) {
        c.rateSeconds = floatOf(o, "rateSeconds", config::Slot::lfo_rateSeconds);
    } else {
        // A slot from before the period was saved holds a frequency; none at all reads as the default period.
        const std::optional<double> hz = numberOf(o.get("rateHz"));
        c.rateSeconds = config::lfoRateSecondsFromHz(hz ? narrow(*hz) : 0.0f);
    }
    c.depth = floatOf(o, "depth", config::Slot::lfo_depth);
    c.polarity = enumOf(o, "polarity", c.polarity, modcatalog::polarityNamed);
    c.curve = enumOf(o, "curve", c.curve, modcatalog::curveNamed);
    c.beats = beatsOf(static_cast<float>(intOf(o, "beats", 0)));
    c.knob = knobOf(intOf(o, "knob", 0));
    c.moment = momentOf(intOf(o, "moment", -1));
    return c;
}

bool moves(const AdsrConfig& c, LfoTarget target) {
    return std::find(c.targets.begin(), c.targets.end(), target) != c.targets.end();
}

AdsrConfig adsrOf(const Value& o) {
    AdsrConfig c;
    c.enabled = boolOf(o, "enabled", c.enabled);
    if (const Value* targets = o.get("targets"); targets && targets->kind() == Value::Kind::Array) {
        for (const Value& t : targets->array()) {
            if (t.kind() != Value::Kind::String) continue;
            const int ordinal = modcatalog::targetNamed(t.string());
            if (ordinal <= 0) continue;
            const auto target = static_cast<LfoTarget>(ordinal);
            if (!moves(c, target)) c.targets.push_back(target);
        }
    }
    c.attack = floatOf(o, "attack", config::Slot::adsr_attack);
    c.decay = floatOf(o, "decay", config::Slot::adsr_decay);
    c.sustain = floatOf(o, "sustain", config::Slot::adsr_sustain);
    c.release = floatOf(o, "release", config::Slot::adsr_release);
    c.amount = floatOf(o, "amount", config::Slot::adsr_amount);
    c.band = enumOf(o, "band", c.band, modcatalog::bandNamed);
    c.gateThreshold = floatOf(o, "gateThreshold", config::Slot::adsr_gateThreshold);
    c.sustainTrack = boolOf(o, "sustainTrack", c.sustainTrack);
    c.retrigger = boolOf(o, "retrigger", c.retrigger);
    c.knob = knobOf(intOf(o, "knob", 0));
    c.moment = momentOf(intOf(o, "moment", -1));
    return c;
}

// ── writing ─────────────────────────────────────────────────────────────────────────────────────────

std::string number(float v) { return std::isfinite(v) ? tableNumber(v) : std::string("0"); }

std::string quoted(const char* name) { return std::string("\"") + name + "\""; }

const char* flagText(bool on) { return on ? "true" : "false"; }

// One more "key":value in the object `out` ends with.
void put(std::string& out, const char* key, const std::string& value) {
    if (out.back() != '{') out += ',';
    out += '"';
    out += key;
    out += "\":";
    out += value;
}

// ── values ──────────────────────────────────────────────────────────────────────────────────────────

template <typename T>
int copyOut(const T* values, int count, T* out, int capacity) {
    if (!out || capacity <= 0) return 0;
    const int n = std::min(count, capacity);
    std::copy_n(values, n, out);
    return n;
}

float flagValue(bool on) { return on ? 1.0f : 0.0f; }

void writeRoute(const ReactRoute& r, float* o) {
    o[0] = flagValue(r.enabled);
    o[1] = static_cast<float>(r.source);
    o[2] = static_cast<float>(r.target);
    o[3] = static_cast<float>(r.mode);
    o[4] = r.amount;
    o[5] = static_cast<float>(r.curve);
    o[6] = r.attackSeconds;
    o[7] = r.releaseSeconds;
}

// A route viz/ReactionText read: its ordinals are inside their enums and its numbers inside the matrix's range.
ReactRoute routeOf(const float* f) {
    ReactRoute r;
    r.enabled = f[0] > 0.5f;
    r.source = static_cast<ReactSource>(static_cast<int>(f[1]));
    r.target = static_cast<LfoTarget>(static_cast<int>(f[2]));
    r.mode = static_cast<ReactMode>(static_cast<int>(f[3]));
    r.amount = f[4];
    r.curve = static_cast<ModCurve>(static_cast<int>(f[5]));
    r.attackSeconds = f[6];
    r.releaseSeconds = f[7];
    return r;
}

// One field of a route; false when the value names nothing the field can hold.
bool setRoute(ReactRoute& r, RouteField field, float value) {
    switch (field) {
        case RouteField::Enabled:
            r.enabled = value > 0.5f;
            return true;
        case RouteField::Source: {
            const int i = ordinalOf(value, kReactSourceCount);
            if (i >= 0) r.source = static_cast<ReactSource>(i);
            return i >= 0;
        }
        case RouteField::Target: {
            // Never None: a route that moves nothing is not saved (viz/ReactionText).
            const int i = ordinalOf(value, modtargets::kTargetCount);
            if (i > 0) r.target = static_cast<LfoTarget>(i);
            return i > 0;
        }
        case RouteField::Mode: {
            const int i = ordinalOf(value, kReactModeCount);
            if (i >= 0) r.mode = static_cast<ReactMode>(i);
            return i >= 0;
        }
        case RouteField::Amount:
            if (std::isfinite(value)) r.amount = std::clamp(value, reactiontext::kMinAmount, reactiontext::kMaxAmount);
            return std::isfinite(value);
        case RouteField::Curve: {
            const int i = ordinalOf(value, modcatalog::kCurveCount);
            if (i >= 0) r.curve = static_cast<ModCurve>(i);
            return i >= 0;
        }
        case RouteField::AttackSeconds:
            if (std::isfinite(value)) r.attackSeconds = std::clamp(value, 0.0f, reactiontext::kMaxAttackSeconds);
            return std::isfinite(value);
        case RouteField::ReleaseSeconds:
            if (std::isfinite(value)) r.releaseSeconds = std::clamp(value, 0.0f, reactiontext::kMaxReleaseSeconds);
            return std::isfinite(value);
    }
    return false;
}

// The route "Add" starts from: a hue step on every onset, small enough to be safe on any style.
ReactRoute addedRoute() {
    ReactRoute r;
    r.enabled = true;
    r.source = ReactSource::Onset;
    r.target = LfoTarget::ColorShift;
    r.mode = ReactMode::Step;
    r.amount = 0.15f;
    return r;
}

}  // namespace

void ModDocument::readLfos(std::string_view text) {
    lfos = waves::Set{};
    const std::vector<Value> slots = slotsOf(text);
    for (size_t i = 0; i < slots.size() && i < lfos.size(); ++i) {
        if (slots[i].kind() == Value::Kind::Object) lfos[i] = lfoOf(slots[i]);
    }
}

void ModDocument::readAdsrs(std::string_view text) {
    adsrs = Envelopes{};
    const std::vector<Value> slots = slotsOf(text);
    for (size_t i = 0; i < slots.size() && i < adsrs.size(); ++i) {
        if (slots[i].kind() == Value::Kind::Object) adsrs[i] = adsrOf(slots[i]);
    }
}

void ModDocument::readReactions(std::string_view text, std::uint32_t mask) {
    hand = HandRoutes{};
    RouteFloats floats{};
    const int count = reactiontext::decode(text, floats.data());
    if (count < 0) return;
    hand.user = true;
    hand.count = std::min(count, ReactionEngine::kRoutes);
    hand.mask = mask & hand::kAll;
    for (int i = 0; i < hand.count; ++i) {
        hand.routes[static_cast<size_t>(i)] = routeOf(floats.data() + i * kRouteFloats);
    }
}

std::string ModDocument::lfosText() const {
    std::string out = "[";
    for (size_t i = 0; i < lfos.size(); ++i) {
        const LfoConfig& c = lfos[i];
        out += i == 0 ? "{" : ",{";
        put(out, "enabled", flagText(c.enabled));
        put(out, "source", quoted(modtargets::sourceName(c.source)));
        put(out, "target", quoted(modtargets::name(c.target)));
        put(out, "wave", quoted(modtargets::waveName(c.wave)));
        put(out, "rateSeconds", number(c.rateSeconds));
        put(out, "depth", number(c.depth));
        put(out, "polarity", quoted(modtargets::polarityName(c.polarity)));
        put(out, "curve", quoted(modtargets::curveName(c.curve)));
        put(out, "beats", std::to_string(static_cast<int>(c.beats)));
        put(out, "knob", std::to_string(c.knob));
        put(out, "moment", std::to_string(c.moment));
        out += '}';
    }
    return out + "]";
}

std::string ModDocument::adsrsText() const {
    std::string out = "[";
    for (size_t i = 0; i < adsrs.size(); ++i) {
        const AdsrConfig& c = adsrs[i];
        std::string targets = "[";
        for (size_t t = 0; t < c.targets.size(); ++t) {
            if (t > 0) targets += ',';
            targets += quoted(modtargets::name(c.targets[t]));
        }
        targets += ']';
        out += i == 0 ? "{" : ",{";
        put(out, "enabled", flagText(c.enabled));
        put(out, "targets", targets);
        put(out, "attack", number(c.attack));
        put(out, "decay", number(c.decay));
        put(out, "sustain", number(c.sustain));
        put(out, "release", number(c.release));
        put(out, "amount", number(c.amount));
        put(out, "band", quoted(modcatalog::bandName(c.band)));
        put(out, "gateThreshold", number(c.gateThreshold));
        put(out, "sustainTrack", flagText(c.sustainTrack));
        put(out, "retrigger", flagText(c.retrigger));
        put(out, "knob", std::to_string(c.knob));
        put(out, "moment", std::to_string(c.moment));
        out += '}';
    }
    return out + "]";
}

std::string ModDocument::reactionsText() const {
    if (!hand.user) return {};
    RouteFloats floats{};
    const int count = std::clamp(hand.count, 0, ReactionEngine::kRoutes);
    for (int i = 0; i < count; ++i) writeRoute(hand.routes[static_cast<size_t>(i)], floats.data() + i * kRouteFloats);
    return reactiontext::encode(floats.data(), count);
}

void ModDocument::setLfo(int slot, int field, float value) {
    if (slot < 0 || slot >= LfoEngine::kSlots || field < 0 || field >= kLfoFieldCount) return;
    LfoConfig& c = lfos[static_cast<size_t>(slot)];
    switch (static_cast<LfoField>(field)) {
        case LfoField::Enabled:
            c.enabled = value > 0.5f;
            break;
        case LfoField::Source:
            if (const int i = ordinalOf(value, modcatalog::kSourceCount); i >= 0) {
                c.source = static_cast<ModSource>(i);
                c.polarity = modtargets::naturalPolarity(c.source);
            }
            break;
        case LfoField::Target:
            if (const int i = ordinalOf(value, modtargets::kTargetCount); i >= 0) c.target = static_cast<LfoTarget>(i);
            break;
        case LfoField::Wave:
            if (const int i = ordinalOf(value, modcatalog::kWaveCount); i >= 0) c.wave = static_cast<LfoWave>(i);
            break;
        case LfoField::RateSeconds:
            if (std::isfinite(value)) c.rateSeconds = config::clampTo(config::Slot::lfo_rateSeconds, value);
            break;
        case LfoField::Depth:
            if (std::isfinite(value)) c.depth = config::clampTo(config::Slot::lfo_depth, value);
            break;
        case LfoField::Polarity:
            if (const int i = ordinalOf(value, modcatalog::kPolarityCount); i >= 0) c.polarity = static_cast<ModPolarity>(i);
            break;
        case LfoField::Curve:
            if (const int i = ordinalOf(value, modcatalog::kCurveCount); i >= 0) c.curve = static_cast<ModCurve>(i);
            break;
        case LfoField::Beats:
            c.beats = beatsOf(value);
            break;
        case LfoField::Knob:
            c.knob = knobOf(wholeOf(value, 0));
            break;
        case LfoField::Moment:
            c.moment = momentOf(wholeOf(value, -1));
            break;
    }
}

void ModDocument::setAdsr(int slot, int field, float value) {
    if (slot < 0 || slot >= AdsrEngine::kCount || field < 0 || field > static_cast<int>(AdsrField::Target)) return;
    AdsrConfig& c = adsrs[static_cast<size_t>(slot)];
    const bool finite = std::isfinite(value);
    switch (static_cast<AdsrField>(field)) {
        case AdsrField::Enabled:
            c.enabled = value > 0.5f;
            break;
        case AdsrField::Attack:
            if (finite) c.attack = config::clampTo(config::Slot::adsr_attack, value);
            break;
        case AdsrField::Decay:
            if (finite) c.decay = config::clampTo(config::Slot::adsr_decay, value);
            break;
        case AdsrField::Sustain:
            if (finite) c.sustain = config::clampTo(config::Slot::adsr_sustain, value);
            break;
        case AdsrField::Release:
            if (finite) c.release = config::clampTo(config::Slot::adsr_release, value);
            break;
        case AdsrField::Amount:
            if (finite) c.amount = config::clampTo(config::Slot::adsr_amount, value);
            break;
        case AdsrField::Band:
            if (const int i = ordinalOf(value, modcatalog::kBandCount); i >= 0) c.band = static_cast<EnvBand>(i);
            break;
        case AdsrField::GateThreshold:
            if (finite) c.gateThreshold = config::clampTo(config::Slot::adsr_gateThreshold, value);
            break;
        case AdsrField::SustainTrack:
            c.sustainTrack = value > 0.5f;
            break;
        case AdsrField::Retrigger:
            c.retrigger = value > 0.5f;
            break;
        case AdsrField::Knob:
            c.knob = knobOf(wholeOf(value, 0));
            break;
        case AdsrField::Moment:
            c.moment = momentOf(wholeOf(value, -1));
            break;
        case AdsrField::Target: {
            const int i = ordinalOf(value, modtargets::kTargetCount);
            if (i <= 0) break;
            const auto target = static_cast<LfoTarget>(i);
            const auto at = std::find(c.targets.begin(), c.targets.end(), target);
            if (at == c.targets.end()) {
                c.targets.push_back(target);
            } else {
                c.targets.erase(at);
            }
            break;
        }
    }
}

void ModDocument::editReactions(const StyleDna& dna, const std::string& styleId, int op, int a, int b, float value) {
    if (op < 0 || op >= kReactionEditCount) return;
    const auto edit = static_cast<ReactionEdit>(op);
    if (edit == ReactionEdit::Clear) {
        hand = HandRoutes{};
        return;
    }
    if (edit == ReactionEdit::MusicToggle || edit == ReactionEdit::MusicStrength || edit == ReactionEdit::MusicReset) {
        if (a < 0 || a >= kMomentCount) return;
        const auto moment = static_cast<Moment>(a);
        if (edit == ReactionEdit::MusicToggle) {
            if (b < 0 || b >= kMusicActionCount) return;
            hand = editMusic(hand, dna, MusicEdit::Toggle, moment, static_cast<MusicAction>(b), 0.0f);
        } else if (edit == ReactionEdit::MusicStrength) {
            if (std::isfinite(value)) hand = editMusic(hand, dna, MusicEdit::Strength, moment, MusicAction::Flash, value);
        } else {
            hand = editMusic(hand, dna, MusicEdit::ResetToAuto, moment, MusicAction::Flash, 0.0f);
        }
        return;
    }

    // The whole-list edits start from what plays, so the routes the style dealt stay beside the change.
    RouteArray playing{};
    int count = mergeHand(hand, dna, playing);
    if (edit == ReactionEdit::RouteSet) {
        if (a < 0 || a >= count || b < 0 || b >= kRouteFieldCount) return;
        if (!setRoute(playing[static_cast<size_t>(a)], static_cast<RouteField>(b), value)) return;
    } else if (edit == ReactionEdit::RouteAdd) {
        if (count >= ReactionEngine::kRoutes) return;
        playing[static_cast<size_t>(count++)] = addedRoute();
    } else if (edit == ReactionEdit::RouteRemove) {
        if (a < 0 || a >= count) return;
        for (int i = a; i + 1 < count; ++i) playing[static_cast<size_t>(i)] = playing[static_cast<size_t>(i + 1)];
        playing[static_cast<size_t>(--count)] = ReactRoute{};
    } else {
        if (a < 0 || a >= kEventRecipeCount) return;
        RouteArray next{};
        count = toggleRecipe(static_cast<EventRecipe>(a), playing, count, styleId, next);
        playing = next;
    }
    hand = HandRoutes{};
    hand.routes = playing;
    hand.count = count;
    hand.user = true;
    hand.mask = hand::kAll;
}

int ModDocument::lfoValues(int slot, const std::string& styleId, float* out, int capacity) const {
    if (slot < 0 || slot >= LfoEngine::kSlots) return 0;
    const LfoConfig& c = lfos[static_cast<size_t>(slot)];
    const bool reaches = c.target == LfoTarget::None || modtargets::offeredTo(c.target, styleId, slot);
    const std::array<float, kLfoValues> values{{
        flagValue(c.enabled),
        static_cast<float>(c.source),
        static_cast<float>(c.target),
        static_cast<float>(c.wave),
        c.rateSeconds,
        c.depth,
        static_cast<float>(c.polarity),
        static_cast<float>(c.curve),
        c.beats,
        static_cast<float>(c.knob),
        static_cast<float>(c.moment),
        flagValue(reaches),
    }};
    return copyOut(values.data(), kLfoValues, out, capacity);
}

int ModDocument::adsrValues(int slot, float* out, int capacity) const {
    if (slot < 0 || slot >= AdsrEngine::kCount) return 0;
    const AdsrConfig& c = adsrs[static_cast<size_t>(slot)];
    std::vector<float> values{
        flagValue(c.enabled),
        c.attack,
        c.decay,
        c.sustain,
        c.release,
        c.amount,
        static_cast<float>(c.band),
        c.gateThreshold,
        flagValue(c.sustainTrack),
        flagValue(c.retrigger),
        static_cast<float>(c.knob),
        static_cast<float>(c.moment),
    };
    for (LfoTarget t : c.targets) values.push_back(static_cast<float>(t));
    return copyOut(values.data(), static_cast<int>(values.size()), out, capacity);
}

int ModDocument::playingValues(const StyleDna& dna, const std::string& styleId, float* out, int capacity) const {
    RouteArray playing{};
    const int count = mergeHand(hand, dna, playing);
    std::vector<float> values(static_cast<size_t>(count * kRouteValues));
    for (int i = 0; i < count; ++i) {
        const ReactRoute& r = playing[static_cast<size_t>(i)];
        float* o = values.data() + i * kRouteValues;
        writeRoute(r, o);
        o[kRouteFieldCount] = flagValue(modtargets::offeredTo(r.target, styleId, modtargets::kPickerReaction));
    }
    return copyOut(values.data(), static_cast<int>(values.size()), out, capacity);
}

int ModDocument::musicValues(const StyleDna& dna, float* out, int capacity) const {
    RouteArray playing{};
    const int count = mergeHand(hand, dna, playing);
    std::array<float, static_cast<size_t>(kMomentCount * kMusicValues)> values{};
    for (int m = 0; m < kMomentCount; ++m) {
        const MomentState s = momentState(hand, playing, count, static_cast<Moment>(m));
        float* o = values.data() + m * kMusicValues;
        o[0] = flagValue(s.mine);
        o[1] = static_cast<float>(s.routes);
        o[2] = s.strength;
        o[3] = static_cast<float>(s.on);
        o[4] = static_cast<float>(s.fits);
    }
    return copyOut(values.data(), static_cast<int>(values.size()), out, capacity);
}

unsigned ModDocument::recipeState(const StyleDna& dna, const std::string& styleId, int recipe) const {
    if (recipe < 0 || recipe >= kEventRecipeCount) return 0u;
    const auto r = static_cast<EventRecipe>(recipe);
    RouteArray playing{};
    const int count = mergeHand(hand, dna, playing);
    return (recipeIsOn(r, playing, count, styleId) ? kRecipeOn : 0u) |
           (recipeFits(r, playing, count, styleId) ? kRecipeFits : 0u) |
           (recipeRoutesFor(r, styleId).count > 0 ? kRecipeOffered : 0u);
}

int ModDocument::targetsOffered(const std::string& styleId, int picker, int* out, int capacity) {
    std::vector<int> offered;
    // A wave may be pointed at nothing; a route always moves something.
    if (picker >= 0 && picker < LfoEngine::kSlots) offered.push_back(static_cast<int>(LfoTarget::None));
    for (int i = 1; i < modtargets::kTargetCount; ++i) {
        if (modtargets::offeredTo(static_cast<LfoTarget>(i), styleId, picker)) offered.push_back(i);
    }
    return copyOut(offered.data(), static_cast<int>(offered.size()), out, capacity);
}

int ModDocument::adsrTargetsOffered(int slot, const std::string& styleId, int* out, int capacity) const {
    if (slot < 0 || slot >= AdsrEngine::kCount) return 0;
    const AdsrConfig& c = adsrs[static_cast<size_t>(slot)];
    std::vector<int> offered;
    for (int i = 1; i < modtargets::kTargetCount; ++i) {
        const auto t = static_cast<LfoTarget>(i);
        if (modtargets::offered(t) && (moves(c, t) || modtargets::reaches(t, styleId))) offered.push_back(i);
    }
    return copyOut(offered.data(), static_cast<int>(offered.size()), out, capacity);
}

}  // namespace geode::viz
