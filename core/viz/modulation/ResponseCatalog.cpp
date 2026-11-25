#include "viz/modulation/ResponseCatalog.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

#include "util/TableNumber.hpp"
#include "viz/modulation/ModCatalog.hpp"
#include "viz/modulation/ModTable.hpp"
#include "viz/panel/ParamText.hpp"

namespace geode::viz::responder::catalog {

namespace {

constexpr int kMaxChoices = 8;
constexpr int kKinds = 5;

constexpr int p(int index) { return static_cast<int>(Field::P0) + index; }

// One control a kind has of its own: a kind-param (or the amount) with its words and range.
struct Control {
    const char* label;
    int field;
    Unit unit;
    float min;
    float max;
    float step;
    const char* choices[kMaxChoices];
};

#define GEODE_ORIGINS {"Middle", "Random", "Pitch height", "Stereo side", "Left edge", "Right edge", "Bottom edge", "Top edge"}

constexpr Control kParamControls[] = {
    {"Amount", static_cast<int>(Field::Amount), Unit::Amount, kMinAmount, kMaxAmount, 0.05f, {}},
};
constexpr Control kThingControls[] = {
    {"Shape", p(0), Unit::Choice, 0, 3, 1, {"Dot", "Ring", "Shard", "Blob"}},
    {"Behaviour", p(1), Unit::Choice, 0, 5, 1, {"Drift", "Orbit", "Gravity", "Bounce", "Morph", "Melt"}},
    {"Comes from", p(2), Unit::Choice, 0, 7, 1, GEODE_ORIGINS},
    {"Speed", p(3), Unit::Percent, 0, 1, 0.05f, {}},
    {"Size", p(4), Unit::Percent, 0, 0.5f, 0.01f, {}},
    {"Chain reaction", p(5), Unit::CountOff, 0, 3, 1, {}},
};
constexpr Control kFrontControls[] = {
    {"Shape", p(0), Unit::Choice, 0, 2, 1, {"Line", "Ring", "Wedge"}},
    {"What it does", p(1), Unit::Choice, 0, 4, 1, {"Brighten", "Colour shift", "Bend", "Invert", "Reveal"}},
    {"Comes from", p(2), Unit::Choice, 0, 7, 1, GEODE_ORIGINS},
    {"Speed", p(3), Unit::Percent, 0, 2, 0.05f, {}},
    {"Width", p(4), Unit::Percent, 0, 0.5f, 0.01f, {}},
    {"How many", p(5), Unit::Count, 1, 8, 1, {}},
};
constexpr Control kFieldControls[] = {
    {"Kind", p(0), Unit::Choice, 0, 2, 1, {"Attract", "Repel", "Swirl"}},
    {"Swirl turns", p(1), Unit::Choice, 0, 1, 1, {"Anticlockwise", "Clockwise"}},
    {"Comes from", p(2), Unit::Choice, 0, 7, 1, GEODE_ORIGINS},
    {"Drift", p(3), Unit::Percent, 0, 1, 0.05f, {}},
    {"Reach", p(4), Unit::Percent, 0, 1, 0.05f, {}},
    {"How many", p(5), Unit::Count, 1, 8, 1, {}},
};
constexpr Control kClockControls[] = {
    {"Does", p(0), Unit::Choice, 0, 2, 1, {"Freeze", "Stutter", "Reverse"}},
    {"Loop length", p(1), Unit::Seconds, 0, 4, 0.05f, {}},
    {"Repeats", p(2), Unit::Count, 1, 16, 1, {}},
    {"Reverse speed", p(3), Unit::Percent, 0, 4, 0.1f, {}},
    {"Loop counted in", p(4), Unit::Choice, 0, 1, 1, {"Seconds", "Beats"}},
    {"Freeze holds", p(5), Unit::Choice, 0, 1, 1, {"Keeps moving", "Last frame"}},
};

struct KindInfo {
    const char* name;
    const Control* controls;
    int count;
    int titled;  // how many of the first choice controls a chip's title names
};

constexpr KindInfo kKindInfo[kKinds] = {
    {"Parameter", kParamControls, static_cast<int>(std::size(kParamControls)), 0},
    {"Things", kThingControls, static_cast<int>(std::size(kThingControls)), 2},
    {"Fronts", kFrontControls, static_cast<int>(std::size(kFrontControls)), 2},
    {"Fields", kFieldControls, static_cast<int>(std::size(kFieldControls)), 1},
    {"Clock", kClockControls, static_cast<int>(std::size(kClockControls)), 1},
};

// The controls every response has.
struct Common {
    const char* label;
    Field field;
    Unit unit;
    float min;
    float max;
    float step;
};
constexpr Common kCommon[] = {
    {"Fade in", Field::FadeIn, Unit::Seconds, 0.0f, 4.0f, 0.05f},
    {"Hold", Field::Hold, Unit::Seconds, 0.0f, 8.0f, 0.05f},
    {"Fade out", Field::FadeOut, Unit::Seconds, 0.0f, 4.0f, 0.05f},
    {"Every hit", Field::EveryNth, Unit::Count, 1.0f, 16.0f, 1.0f},
    {"Chance", Field::Chance, Unit::Percent, 0.0f, 1.0f, 0.05f},
    {"Cooldown", Field::Cooldown, Unit::Seconds, 0.0f, 5.0f, 0.1f},
};

constexpr const char* kFamilies[kFamilyCount] = {"Appear", "Travel", "Interact", "Transform", "Light", "Camera", "Time"};

constexpr Preset kPresets[kPresetCount] = {
    {"Snap", 0.0f, 0.0f, 0.15f},
    {"Pulse", 0.05f, 0.1f, 0.5f},
    {"Swell", 1.2f, 0.3f, 0.4f},
    {"Stay", 0.1f, 4.0f, 0.4f},
};
constexpr int kSnap = 0;
constexpr int kPulse = 1;
constexpr int kSwell = 2;
constexpr int kStay = 3;

struct Words {
    const char* name;
    const char* sentence;
};
// ModCurve's ordinals: linear, exponential (only strong hits show), logarithmic (gentle hits show), smooth.
constexpr Words kCurves[kCurveCount] = {
    {"Linear", "Follows the hit strength one to one."},
    {"Sharp", "Only strong hits really show."},
    {"Soft", "Even gentle hits show clearly."},
    {"Smooth", "Eases in and out around the middle."},
};
// DriveMode's ordinals.
constexpr Words kDrives[kDriveCount] = {
    {"Once", "Plays once when it happens, then ends on its own."},
    {"While", "Stays on for as long as the moment lasts."},
    {"Follow", "Rises and falls with how strong the sound is."},
    {"Long", "A slow swell that spreads over several seconds."},
};

// The rows, in Moment order, and the groups the page lists them in.
struct RowInfo {
    const char* label;
    int category;
    bool needsBeat;
};
constexpr const char* kCategories[] = {"Instruments", "Rhythm", "Long", "Sound and key", "Picture"};
constexpr RowInfo kRowInfo[kRows] = {
    {"Kick", 0, false},          {"Snare", 0, false},        {"Hi-hat", 0, false},      {"Bass", 0, false},
    {"Drop", 2, false},          {"Build-up", 2, false},     {"Chord change", 0, false}, {"New section", 2, false},
    {"Melody note", 0, false},   {"Loudness", 3, false},     {"Beat", 1, true},         {"Bar", 1, true},
    {"Any new sound", 1, false}, {"Loudness peak", 3, false}, {"Key change", 3, false},  {"Collision", 4, false},
};

// A tile of the Add response sheet. Params are the kind-params; a Parameter tile names its target by its saved name.
struct Entry {
    const char* name;
    const char* line;
    int family;
    int kind;
    const char* target;
    float amount;
    int preset;
    DriveMode drive;
    float params[kKindParams];
};

constexpr Entry kEntries[] = {
    // Appear
    {"Pop burst", "Dots burst out and fade", 0, kThing, nullptr, 0.3f, kPulse, DriveMode::Once, {0, 0, 1, 0.15f, 0, 0}},
    {"Bloom in", "A soft form swells from nothing", 0, kThing, nullptr, 0.3f, kSwell, DriveMode::Long, {3, 0, 0, 0, 0.3f, 0}},
    {"Fade away", "The picture thins out, then returns", 0, kParam, "INTENSITY", -0.6f, kSwell, DriveMode::While, {0, 0, 0, 0, 0, 0}},
    // Travel
    {"Sweep across", "A bright band crosses the screen", 1, kFront, nullptr, 0.3f, kPulse, DriveMode::Once, {0, 0, 4, 0, 0, 1}},
    {"Drift through", "Shapes enter one edge and leave the other", 1, kThing, nullptr, 0.3f, kPulse, DriveMode::Once,
     {0, 0, 4, 0.3f, 0, 0}},
    {"Orbit", "Shapes circle the middle, then fade", 1, kThing, nullptr, 0.3f, kPulse, DriveMode::Once, {1, 1, 0, 0.2f, 0, 0}},
    {"Rain", "Small shapes fall from the top", 1, kThing, nullptr, 0.3f, kStay, DriveMode::Follow, {0, 2, 7, 0.3f, 0, 0}},
    {"Rise", "Shapes lift from the bottom", 1, kThing, nullptr, 0.3f, kStay, DriveMode::Follow, {0, 0, 6, 0.25f, 0, 0}},
    // Interact
    {"Pull in", "Shapes gather toward a point", 2, kField, nullptr, 0.3f, kSwell, DriveMode::While, {0, 0, 0, 0, 0, 1}},
    {"Push away", "A shove from a point scatters nearby shapes", 2, kField, nullptr, 0.3f, kSnap, DriveMode::Once, {1, 0, 0, 0, 0, 1}},
    {"Swirl", "Nearby shapes turn around a point", 2, kField, nullptr, 0.3f, kSwell, DriveMode::While, {2, 0, 0, 0, 0, 1}},
    {"Ripple from the hit", "A ring spreads out from where it hit", 2, kFront, nullptr, 0.3f, kPulse, DriveMode::Once,
     {1, 0, 1, 0, 0, 1}},
    {"Chain reaction", "A burst sets off smaller bursts where it ends", 2, kThing, nullptr, 0.3f, kSnap, DriveMode::Once,
     {0, 0, 1, 0.1f, 0, 2}},
    {"Bounce and spark", "Shapes bounce about", 2, kThing, nullptr, 0.3f, kPulse, DriveMode::Once, {2, 3, 1, 0.4f, 0, 0}},
    {"Lean to the melody", "Shapes gather at the height of the tune", 2, kField, nullptr, 0.3f, kPulse, DriveMode::Follow,
     {0, 0, 2, 0, 0, 1}},
    {"Stir the liquid", "The liquid swirls harder, then settles", 2, kParam, "FLUID_CURL", 0.5f, kPulse, DriveMode::Once,
     {0, 0, 0, 0, 0, 0}},
    // Transform
    {"Warp pulse", "The picture bulges and relaxes", 3, kParam, "WARP", 0.5f, kPulse, DriveMode::Once, {0, 0, 0, 0, 0, 0}},
    {"Twist snap", "The picture winds up and snaps back", 3, kParam, "TWIST", 0.4f, kSnap, DriveMode::Once, {0, 0, 0, 0, 0, 0}},
    {"Fold step", "The picture mirrors into more parts", 3, kParam, "KALEIDO_FOLDS", 0.5f, kSnap, DriveMode::Once,
     {0, 0, 0, 0, 0, 0}},
    {"Morph jump", "The shape changes to a neighbour", 3, kParam, "MORPH", 0.4f, kPulse, DriveMode::Once, {0, 0, 0, 0, 0, 0}},
    {"Melt into", "One shape melts into another", 3, kThing, nullptr, 0.3f, kSwell, DriveMode::Once, {0, 5, 0, 0, 0, 0}},
    {"Crunch", "The picture blocks up into big pixels", 3, kParam, "PIXELATE", 0.4f, kSnap, DriveMode::Once, {0, 0, 0, 0, 0, 0}},
    // Light
    {"Flash", "A bright pop that fades", 4, kParam, "BRIGHTNESS", 0.4f, kPulse, DriveMode::Once, {0, 0, 0, 0, 0, 0}},
    {"Colour shift", "The colours step along", 4, kParam, "COLOR_SHIFT", 0.2f, kPulse, DriveMode::Once, {0, 0, 0, 0, 0, 0}},
    {"Palette step", "The colours step along your palette", 4, kParam, "PALETTE_SHIFT", 0.25f, kSnap, DriveMode::Once,
     {0, 0, 0, 0, 0, 0}},
    {"Glow swell", "A slow glow rises and falls", 4, kParam, "BLOOM", 0.6f, kSwell, DriveMode::Long, {0, 0, 0, 0, 0, 0}},
    {"Colour split", "The colour copies slide apart", 4, kParam, "CHROMA_AB", 0.5f, kPulse, DriveMode::Once, {0, 0, 0, 0, 0, 0}},
    {"Invert sweep", "A band of inverted colour crosses the screen", 4, kFront, nullptr, 0.3f, kSnap, DriveMode::Once,
     {0, 3, 7, 0, 0, 1}},
    // Camera
    {"Punch in", "The camera lunges forward, then eases back", 5, kParam, "CAM_PUSH", 0.5f, kSnap, DriveMode::Once,
     {0, 0, 0, 0, 0, 0}},
    {"Turn", "The view turns to a new angle", 5, kParam, "CAM_YAW", 0.3f, kSnap, DriveMode::Once, {0, 0, 0, 0, 0, 0}},
    {"Drift the camera", "The camera slides and keeps going", 5, kParam, "CAM_FLY", 0.4f, kStay, DriveMode::While,
     {0, 0, 0, 0, 0, 0}},
    {"Roll", "The horizon tilts and levels out", 5, kParam, "CAM_ROLL", 0.3f, kPulse, DriveMode::Once, {0, 0, 0, 0, 0, 0}},
    {"Shake", "A short rattle", 5, kParam, "CAM_SHAKE", 0.5f, kPulse, DriveMode::Once, {0, 0, 0, 0, 0, 0}},
    // Time
    {"Freeze-frame", "The picture stops for a moment", 6, kClock, nullptr, 0.3f, kStay, DriveMode::While, {0, 0, 1, 1, 0, 0}},
    {"Stutter", "A slice of the last moment repeats", 6, kClock, nullptr, 0.3f, kSnap, DriveMode::Once, {1, 0.25f, 4, 1, 0, 0}},
    {"Reverse blip", "Motion runs backwards for a moment", 6, kClock, nullptr, 0.3f, kPulse, DriveMode::Once, {2, 0, 1, 1, 0, 0}},
    {"Speed burst", "Everything speeds up, then settles", 6, kParam, "SPEED", 0.5f, kPulse, DriveMode::Once, {0, 0, 0, 0, 0, 0}},
    {"Echo burst", "Ghost copies trail behind, then clear", 6, kParam, "TRAIL_LENGTH", 0.5f, kPulse, DriveMode::Once,
     {0, 0, 0, 0, 0, 0}},
};
constexpr int kEntryCount = static_cast<int>(std::size(kEntries));

constexpr float kNoValue = std::numeric_limits<float>::quiet_NaN();
// A style's own value smaller than this is no value to take a percentage of: the amount reads as its full reach.
constexpr float kLeastBase = 0.05f;

const Control* controlFor(const Response& r, int field) {
    const KindInfo& kind = kKindInfo[std::clamp(r.kind, 0, kKinds - 1)];
    for (int i = 0; i < kind.count; ++i) {
        if (kind.controls[i].field == field) return &kind.controls[i];
    }
    return nullptr;
}

std::string choiceOf(const Control& c, float value) {
    const int at = std::clamp(static_cast<int>(std::lround(value)), 0, kMaxChoices - 1);
    return c.choices[at] != nullptr ? c.choices[at] : "";
}

std::string secondsText(float v) { return paramText(v, ParamUnit::Seconds, kNoValue, false); }

std::string timeText(const Response& r, float v) {
    if (r.unit == TimeUnit::Seconds) return secondsText(v);
    return tableNumber(v) + (v == 1.0f ? " beat" : " beats");
}

std::string amountText(const Response& r, const SceneParams& look) {
    float shown = r.amount;
    const int slot = modtable::slotOf(static_cast<int>(r.target));
    if (slot >= 0) {
        const float scale = kParamMods[static_cast<size_t>(slot)].scale;
        const float own = std::fabs(look.getSlot(slot));
        if (own >= kLeastBase && scale > 0.0f) shown = r.amount * scale / own;
    }
    return paramText(shown, ParamUnit::Percent, kNoValue, true);
}

std::string ordinalText(int n) {
    const int tens = n % 100;
    const char* suffix = "th";
    if (tens < 11 || tens > 13) {
        if (n % 10 == 1) suffix = "st";
        if (n % 10 == 2) suffix = "nd";
        if (n % 10 == 3) suffix = "rd";
    }
    return std::to_string(n) + suffix;
}

std::string controlText(const Control& c, float value) {
    switch (c.unit) {
        case Unit::Percent: return paramText(value, ParamUnit::Percent, kNoValue, false);
        case Unit::Count: return std::to_string(static_cast<int>(std::lround(value)));
        case Unit::CountOff: {
            const int n = static_cast<int>(std::lround(value));
            return n == 0 ? "Off" : std::to_string(n);
        }
        case Unit::Seconds: return secondsText(value);
        case Unit::Choice: return choiceOf(c, value);
        case Unit::Amount: break;
    }
    return tableNumber(value);
}

std::string line(std::initializer_list<std::string> cells) {
    std::string out;
    for (const std::string& c : cells) {
        if (!out.empty()) out += '|';
        out += c;
    }
    return out + "\n";
}

std::string build() {
    std::string out = "responses|" + std::to_string(kTextVersion) + "\n";
    for (int i = 0; i < kFamilyCount; ++i) out += line({"family", std::to_string(i), kFamilies[i]});
    for (int i = 0; i < kPresetCount; ++i) {
        out += line({"preset", std::to_string(i), kPresets[i].name, tableNumber(kPresets[i].fadeIn), tableNumber(kPresets[i].hold),
                     tableNumber(kPresets[i].fadeOut)});
    }
    for (int i = 0; i < kCurveCount; ++i) out += line({"curve", std::to_string(i), kCurves[i].name, kCurves[i].sentence});
    for (int i = 0; i < kDriveCount; ++i) out += line({"drive", std::to_string(i), kDrives[i].name, kDrives[i].sentence});
    for (int i = 0; i < static_cast<int>(std::size(kCategories)); ++i) out += line({"category", std::to_string(i), kCategories[i]});
    for (int c = 0; c < static_cast<int>(std::size(kCategories)); ++c) {
        for (int row = 0; row < kRows; ++row) {
            const RowInfo& info = kRowInfo[row];
            if (info.category != c) continue;
            out += line({"row", std::to_string(row), std::string(kRowNames[static_cast<size_t>(row)]), info.label, std::to_string(c),
                         info.needsBeat ? "1" : "0"});
        }
    }
    for (int i = 0; i < kEntryCount; ++i) {
        const Entry& e = kEntries[i];
        out += line({"entry", std::to_string(i), std::to_string(e.family), std::to_string(e.kind), e.name, e.line});
    }
    for (int k = 0; k < kKinds; ++k) {
        out += line({"kind", std::to_string(k), kKindInfo[k].name});
        for (int i = 0; i < kKindInfo[k].count; ++i) {
            const Control& c = kKindInfo[k].controls[i];
            std::string row = line({"control", std::to_string(k), std::to_string(c.field), c.label, std::to_string(static_cast<int>(c.unit)),
                                    tableNumber(c.min), tableNumber(c.max), tableNumber(c.step)});
            row.pop_back();
            for (const char* choice : c.choices) {
                if (choice != nullptr) row += std::string("|") + choice;
            }
            out += row + "\n";
        }
    }
    for (const Common& c : kCommon) {
        out += line({"common", std::to_string(static_cast<int>(c.field)), c.label, std::to_string(static_cast<int>(c.unit)),
                     tableNumber(c.min), tableNumber(c.max), tableNumber(c.step)});
    }
    return out;
}

}  // namespace

const Preset& preset(int index) { return kPresets[std::clamp(index, 0, kPresetCount - 1)]; }

int presetOf(const Response& r) {
    constexpr float kTolerance = 0.001f;
    for (int i = 0; i < kPresetCount; ++i) {
        const Preset& s = kPresets[i];
        if (std::fabs(r.fadeIn - s.fadeIn) < kTolerance && std::fabs(r.hold - s.hold) < kTolerance &&
            std::fabs(r.fadeOut - s.fadeOut) < kTolerance) {
            return i;
        }
    }
    return -1;
}

bool applyPreset(Response& r, int index) {
    if (index < 0 || index >= kPresetCount) return false;
    r.fadeIn = kPresets[index].fadeIn;
    r.hold = kPresets[index].hold;
    r.fadeOut = kPresets[index].fadeOut;
    return true;
}

int entryCount() { return kEntryCount; }

bool make(int entry, int row, Response& out) {
    if (entry < 0 || entry >= kEntryCount || row < 0 || row >= kRows) return false;
    const Entry& e = kEntries[entry];
    Response r;
    r.row = row;
    r.kind = e.kind;
    r.amount = e.amount;
    r.drive = e.drive;
    applyPreset(r, e.preset);
    for (int i = 0; i < kKindParams; ++i) r.params[static_cast<size_t>(i)] = e.params[i];
    if (e.target != nullptr) {
        const int target = modcatalog::targetNamed(e.target);
        if (target <= static_cast<int>(LfoTarget::None)) return false;
        r.target = static_cast<LfoTarget>(target);
    }
    clampResponse(r);
    out = r;
    return true;
}

std::string title(const Response& r) {
    const int k = std::clamp(r.kind, 0, kKinds - 1);
    if (k == kParam) {
        const std::string label = modcatalog::targetLabel(r.target);
        return label.empty() ? kKindInfo[kParam].name : label;
    }
    const KindInfo& kind = kKindInfo[k];
    std::string out = kind.name;
    int named = 0;
    for (int i = 0; i < kind.count && named < kind.titled; ++i) {
        const Control& c = kind.controls[i];
        if (c.unit != Unit::Choice) continue;
        out += named == 0 ? ": " : ", ";
        out += choiceOf(c, r.params[static_cast<size_t>(c.field - p(0))]);
        ++named;
    }
    return out;
}

std::string fieldText(const Response& r, int field, const SceneParams& look) {
    if (field < 0 || field >= kFieldCount) return "";
    if (field >= p(0)) {
        const Control* c = controlFor(r, field);
        return c != nullptr ? controlText(*c, r.params[static_cast<size_t>(field - p(0))]) : "";
    }
    switch (static_cast<Field>(field)) {
        case Field::Enabled: return r.enabled ? "On" : "Off";
        case Field::Row: return kRowInfo[std::clamp(r.row, 0, kRows - 1)].label;
        case Field::Kind: return kKindInfo[std::clamp(r.kind, 0, kKinds - 1)].name;
        case Field::Target: return modcatalog::targetLabel(r.target);
        case Field::Amount: return r.kind == kParam ? amountText(r, look) : paramText(r.amount, ParamUnit::Percent, kNoValue, true);
        case Field::Strength: return kCurves[std::clamp(static_cast<int>(r.strength), 0, kCurveCount - 1)].name;
        case Field::FadeIn: return timeText(r, r.fadeIn);
        case Field::Hold: return timeText(r, r.hold);
        case Field::FadeOut: return timeText(r, r.fadeOut);
        case Field::Unit: return r.unit == TimeUnit::Seconds ? "Seconds" : "Beats";
        case Field::Drive: return kDrives[std::clamp(static_cast<int>(r.drive), 0, kDriveCount - 1)].name;
        case Field::EveryNth: return r.everyNth <= 1 ? "Every hit" : "Every " + ordinalText(r.everyNth) + " hit";
        case Field::Chance: return paramText(r.chance, ParamUnit::Percent, kNoValue, false);
        case Field::Cooldown: return secondsText(r.cooldown);
        default: break;
    }
    return "";
}

const std::string& text() {
    static const std::string kText = build();
    return kText;
}

}  // namespace geode::viz::responder::catalog
