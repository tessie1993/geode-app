#include "viz/styles/catalogue/Catalogue.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include "viz/Params.hpp"
#include "viz/TouchField.hpp"
#include "viz/styles/renderers/fluid/FluidQuality.hpp"
#include "viz/styles/renderers/cymatics/CymaticsMath.hpp"
#include "viz/styles/catalogue/StyleCatalog.hpp"

namespace geode::viz::catalogue {

namespace {

// Every shader style's row, from styles/StyleTable.inc (see its header). Flags are the per-style facts;
// a family style takes its family's look.
struct Flags {
    unsigned bits = 0;
    float spin = 0.0f;
};
constexpr Flags operator|(Flags a, Flags b) { return {a.bits | b.bits, a.spin != 0.0f ? a.spin : b.spin}; }
constexpr Flags None{};
constexpr Flags March{1u << 0};
constexpr Flags OffRig{1u << 1};
constexpr Flags Form{1u << 2};
constexpr Flags Formula{1u << 3};
constexpr Flags Shapes{1u << 4};
constexpr Flags Shadow{1u << 5};
constexpr Flags Escape{1u << 6};
constexpr Flags Spin(float rate) { return {0, rate}; }

struct StyleRow {
    const char* id;
    Look look;
    Flags flags;
    bool loose;
};

constexpr Look Fractals = Look::Fractals, FlyThrough = Look::FlyThrough, Patterns = Look::Patterns,
               Flowing = Look::Flowing, Classic = Look::Classic, Space = Look::Space, Psychedelic = Look::Psychedelic,
               Glass = Look::Glass;

// Pass 1: each family's id list and look.
#define GEODE_LOOSE(id, look, flags, ...)
#define GEODE_FAMILY_BEGIN(name, prefix, library, look) \
    constexpr Look kLook_##name = look;                 \
    constexpr const char* kIds_##name[] = {
#define GEODE_STYLE(name, id, flags, ...) id,
#define GEODE_FAMILY_END(name) \
    }                          \
    ;
#include "viz/styles/catalogue/StyleTable.inc"
#undef GEODE_LOOSE
#undef GEODE_FAMILY_BEGIN
#undef GEODE_STYLE
#undef GEODE_FAMILY_END

// Pass 2: the rows in picker order.
constexpr StyleRow kRows[] = {
#define GEODE_LOOSE(id, look, flags, ...) {id, look, flags, true},
#define GEODE_FAMILY_BEGIN(name, prefix, library, look)
#define GEODE_STYLE(name, id, flags, ...) {id, kLook_##name, flags, false},
#define GEODE_FAMILY_END(name)
#include "viz/styles/catalogue/StyleTable.inc"
#undef GEODE_LOOSE
#undef GEODE_FAMILY_BEGIN
#undef GEODE_STYLE
#undef GEODE_FAMILY_END
};

// Each family lists its own styles, so it carries its own count; the opener is first.
constexpr Family kFamilies[] = {
#define GEODE_LOOSE(id, look, flags, ...)
#define GEODE_FAMILY_BEGIN(name, prefix, library, look) {#name, prefix, library, look, kIds_##name},
#define GEODE_STYLE(name, id, flags, ...)
#define GEODE_FAMILY_END(name)
#include "viz/styles/catalogue/StyleTable.inc"
#undef GEODE_LOOSE
#undef GEODE_FAMILY_BEGIN
#undef GEODE_STYLE
#undef GEODE_FAMILY_END
};

const StyleRow* rowOf(const std::string& id) {
    for (const StyleRow& row : kRows) {
        if (id == row.id) return &row;
    }
    return nullptr;
}

bool flagged(const std::string& id, Flags flag) {
    const StyleRow* row = rowOf(id);
    return row != nullptr && (row->flags.bits & flag.bits) != 0;
}

constexpr const char* kCurlFlow = "curlflow";
// The Customize control a cymatics style can hold (its Customize key).
constexpr const char* kGeometryKey = "Geometry";

constexpr const char* kKindNames[kKindCount] = {"SHADER", "SILK",     "LIFE",  "MYCELIUM", "ACID",
                                                "FLUID", "CURL_FLOW", "CYMATICS"};
constexpr const char* kScopeNames[kScopeCount] = {
    "UNIVERSAL",    "SCENE_CLOCK",     "AUDIO_DRIVE", "BASS_BAND", "MID_BAND",     "TREBLE_BAND", "BAND_GAINS", "SHADER_LOOK",
    "MARCH_DETAIL", "ENDLESS_ZOOM",    "TURBULENCE",  "DYE_DENSITY", "TRAIL_TOGGLE", "TRAIL_LENGTH", "TRAIL_ECHO",
    "PARTICLE_SPRITE", "FLUID_SIM",   "EMITTERS",  "JOURNEY",      "CYMATICS",
    "FLOW_FIELD_SIM",  "SPIN",            "FAMILY_FORM",   "FORMULA",
    "SHAPES",          "SHADOW",          "ESCAPE",
};

constexpr const char* kLookNames[kLookCount] = {"FLOWING", "LIVING", "PSYCHEDELIC", "FRACTALS", "SPACE",
                                                "FLY_THROUGH", "GLASS", "PATTERNS", "CLASSIC"};

// A dedicated renderer's styles all share one group; shader styles go by family or by their own tag.
Look kindLook(Kind k) {
    switch (k) {
        case Kind::Silk:
        case Kind::Fluid:
        case Kind::CurlFlow: return Look::Flowing;
        case Kind::Life:
        case Kind::Mycelium: return Look::Living;
        case Kind::Acid: return Look::Psychedelic;
        case Kind::Cymatics: return Look::Patterns;
        case Kind::Shader: break;
    }
    return Look::Psychedelic;
}


// The lookups compare a std::string with each entry, which is undefined for a null one. The tables' sizes
// are deduced from their entries (or are an enum's count), and none may hold a null.
constexpr const char* idOf(const char* id) { return id; }
constexpr const char* idOf(const StyleRow& row) { return row.id; }

template <typename Table>
constexpr bool noneNull(const Table& table) {
    for (const auto& entry : table) {
        if (idOf(entry) == nullptr) return false;
    }
    return true;
}

// Every family has a name and at least one id (its opener), none of them null.
constexpr bool familiesNoneNull() {
    for (const Family& family : kFamilies) {
        if (family.name == nullptr || family.ids.empty() || !noneNull(family.ids)) return false;
    }
    return true;
}

static_assert(noneNull(kRows), "StyleTable.inc: no null id");
static_assert(familiesNoneNull(), "kFamilies: a name and ids, no null");
static_assert(noneNull(kKindNames), "kKindNames: one name per Kind");
static_assert(noneNull(kScopeNames), "kScopeNames: one name per Scope");
static_assert(noneNull(kLookNames), "kLookNames: one name per Look");

// A simulation presented in 3D (core/viz/styles/renderers/life/FieldPresent): its present is drawn by a ShaderScene on the
// shared block, so it reads the shader look, the bands and Turbulence as a shader style does, and marches.
bool presentsIn3d(Kind k) { return k == Kind::Life; }

// Per-band gain reaches only a style that reads that band's envelope.
bool readsBass(Kind k) {
    return k == Kind::Shader || k == Kind::Silk || k == Kind::Mycelium || k == Kind::Acid || k == Kind::Fluid || presentsIn3d(k);
}
bool readsMid(Kind k) {
    return k == Kind::Shader || k == Kind::Silk || k == Kind::Acid || k == Kind::Fluid || k == Kind::CurlFlow || presentsIn3d(k);
}
bool readsTreble(Kind) { return true; }

// Whether a scope reaches every style of a kind. MarchDetail, Spin, FamilyForm, Formula, Shapes, Shadow and Escape
// are per style, so no kind has them.
bool reachesKind(Scope s, Kind k) {
    switch (s) {
        case Scope::Universal: return true;
        case Scope::SceneClock:
        case Scope::AudioDrive: return true;
        case Scope::BassBand: return readsBass(k);
        case Scope::MidBand: return readsMid(k);
        case Scope::TrebleBand: return readsTreble(k);
        case Scope::BandGains: return readsBass(k) || readsMid(k) || readsTreble(k);
        case Scope::ShaderLook: return k == Kind::Shader || presentsIn3d(k);
        case Scope::MarchDetail: return false;
        case Scope::EndlessZoom: return true;
        case Scope::Turbulence:
            return k == Kind::Shader || k == Kind::Mycelium || k == Kind::Acid || k == Kind::CurlFlow || presentsIn3d(k);
        case Scope::DyeDensity: return k == Kind::Fluid;
        // A shader style's trails are the feedback pass's sustain, echo zoom and warp (EndlessZoom.hpp).
        case Scope::TrailToggle:
        case Scope::TrailLength: return k == Kind::Shader || k == Kind::Silk || k == Kind::CurlFlow;
        case Scope::TrailEcho: return k == Kind::Shader || k == Kind::CurlFlow;
        case Scope::ParticleSprite: return k == Kind::Fluid || k == Kind::CurlFlow;
        case Scope::FluidSim: return k == Kind::Fluid;
        case Scope::Emitters: return k == Kind::Fluid;
        case Scope::Journey: return k == Kind::Fluid || k == Kind::CurlFlow;
        case Scope::Cymatics: return k == Kind::Cymatics;
        case Scope::FlowFieldSim: return k != Kind::Fluid;
        case Scope::Spin:
        case Scope::FamilyForm:
        case Scope::Formula:
        case Scope::Shapes:
        case Scope::Shadow:
        case Scope::Escape: return false;
    }
    return false;
}

// The Styles page family of a style outside the shader families: its renderer's.
const char* pickerName(Kind k) {
    switch (k) {
        case Kind::Shader: return "shaders";
        case Kind::Silk: return "silk";
        case Kind::Life: return "life";
        case Kind::Mycelium: return "mycelium";
        case Kind::Acid: return "morphing";
        case Kind::Fluid:
        case Kind::CurlFlow: return "fluid";
        case Kind::Cymatics: return "cymatics";
    }
    return "shaders";
}

void append(std::vector<std::string>& out, const std::vector<std::string>& ids) { out.insert(out.end(), ids.begin(), ids.end()); }

std::string number(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", static_cast<double>(v));
    return buf;
}

std::string build() {
    std::string out = "catalogue|" + std::to_string(kTextVersion) + "\n";
    for (int k = 0; k < kKindCount; ++k) {
        out += "kind|";
        out += kKindNames[k];
        out += '|';
        bool first = true;
        for (int s = 0; s < kScopeCount; ++s) {
            if (!reachesKind(static_cast<Scope>(s), static_cast<Kind>(k))) continue;
            if (!first) out += ',';
            out += kScopeNames[s];
            first = false;
        }
        out += '\n';
    }
    for (const auto& id : allIds()) {
        out += "style|" + id + "|" + name(kindOf(id)) + "|" + name(lookOf(id));
        if (isMarched(id)) out += "|MARCH_DETAIL";
        if (spinRate(id) != 0.0f) out += "|SPIN";
        if (drawsFamilyForm(id)) out += "|FAMILY_FORM";
        if (hasFormulas(id)) out += "|FORMULA";
        if (drawsShapes(id)) out += "|SHAPES";
        if (castsShadows(id)) out += "|SHADOW";
        if (colorsByEscape(id)) out += "|ESCAPE";
        out += '\n';
    }
    for (const auto& r : kRetired) out += std::string("retired|") + r.id + "|" + r.now + "\n";
    for (const auto& family : kFamilies) {
        out += std::string("family|") + family.name + "|" + family.prefix + "|" + family.library + "|";
        for (size_t i = 0; i < family.ids.size(); ++i) {
            if (i > 0) out += ',';
            out += family.ids[i];
        }
        out += '\n';
    }
    for (const auto& picker : pickerFamilies()) {
        out += "picker|" + picker.name + "|";
        for (size_t i = 0; i < picker.ids.size(); ++i) {
            if (i > 0) out += ',';
            out += picker.ids[i];
        }
        out += '\n';
    }
    // A cymatics style that draws on one plate geometry overrides the user's Geometry control.
    for (const auto& id : styles::cymaticsIds()) {
        const int geometry = styles::cymatics(id)->geometryOverride;
        if (geometry >= 0) out += "override|" + id + "|" + kGeometryKey + "|" + std::to_string(geometry) + "\n";
    }
    // Every control that is NOT universal, by its key, from the params schema's meta rows in wire order
    // (a control of several fields once): the composite pass applies the rest to the finished frame.
    std::vector<std::string> keyed;
    for (const ParamMeta& meta : SceneParams::metaTable()) {
        if (!meta.live || meta.label[0] == '\0' || meta.scope == Scope::Universal) continue;
        if (std::find(keyed.begin(), keyed.end(), meta.label) != keyed.end()) continue;
        keyed.emplace_back(meta.label);
        out += std::string("key|") + meta.label + "|" + name(meta.scope) + "\n";
    }
    const auto range = [&out](const char* key, float lo, float hi) {
        out += std::string("range|") + key + "|" + number(lo) + "|" + number(hi) + "\n";
    };
    range("Detail", kMarchDetail.min, kMarchDetail.max);
    range("Fundamental (Hz)", cymatics::kMinFundamentalHz, cymatics::kMaxFundamentalHz);
    range("Standing waves", 1.0f, static_cast<float>(cymatics::kMaxRenderedModes));
    range("Zoom", kTouchZoom.min, kTouchZoom.max);
    range("Rotation", kTouchRotation.min, kTouchRotation.max);
    // The Fluid tab's quality chips are labelled by the shell; this says how many tiers they index.
    out += "value|fluidTiers|" + std::to_string(fluid::quality::kTierCount) + "\n";
    out += "value|touchRotationPerDegree|" + number(kTouchRotationPerDegree) + "\n";
    // The fingers the touch field takes (geode_viz_set_touch); the shell collects no more than this.
    out += "value|touchPoints|" + std::to_string(TouchField::kMaxPoints) + "\n";
    return out;
}

}  // namespace

const std::vector<std::string>& allIds() {
    static const std::vector<std::string> kIds = [] {
        std::vector<std::string> out;
        append(out, styles::silkIds());
        append(out, styles::lifeIds());
        append(out, styles::mycoIds());
        append(out, styles::acidIds());
        for (const StyleRow& row : kRows) out.emplace_back(row.id);
        append(out, styles::fluidIds());
        out.emplace_back(kCurlFlow);
        append(out, styles::cymaticsIds());
        return out;
    }();
    return kIds;
}

const std::vector<PickerFamily>& pickerFamilies() {
    static const std::vector<PickerFamily> kPicker = [] {
        std::vector<PickerFamily> out;
        for (const auto& id : allIds()) {
            std::string name = pickerName(kindOf(id));
            for (const auto& family : kFamilies) {
                if (std::find_if(family.ids.begin(), family.ids.end(), [&](const char* f) { return id == f; }) != family.ids.end())
                    name = family.name;
            }
            auto it = std::find_if(out.begin(), out.end(), [&](const PickerFamily& p) { return p.name == name; });
            if (it == out.end()) it = out.insert(out.end(), PickerFamily{name, {}});
            it->ids.push_back(id);
        }
        return out;
    }();
    return kPicker;
}

const std::string& pickerNamesText() {
    static const std::string kNames = [] {
        std::string out;
        for (const auto& picker : pickerFamilies()) out += picker.name + "\n";
        return out;
    }();
    return kNames;
}

std::string pickerIdsText(const std::string& name) {
    std::string out;
    for (const auto& picker : pickerFamilies()) {
        if (picker.name != name) continue;
        for (const auto& id : picker.ids) out += id + "\n";
    }
    return out;
}

bool has(Trait trait, const std::string& id) {
    switch (trait) {
        case Trait::Any: return knows(id);
        case Trait::Shader: return isShaderStyle(id);
        case Trait::FamilyFollower:
            for (const auto& family : kFamilies) {
                if (std::find_if(family.ids.begin(), family.ids.end(), [&](const char* f) { return id == f; }) != family.ids.end())
                    return id != family.opener();
            }
            return false;
        case Trait::AutoDraw:
            return isShaderStyle(id) || styles::silk(id) || styles::life(id) || styles::myco(id) || styles::acid(id);
        case Trait::Fluid: return styles::fluid(id) != nullptr;
        case Trait::Cymatics: return styles::cymatics(id) != nullptr;
    }
    return false;
}

const std::string& idsText(Trait trait) {
    static const std::array<std::string, kTraitCount> kTexts = [] {
        std::array<std::string, kTraitCount> out;
        for (int t = 0; t < kTraitCount; ++t) {
            for (const auto& id : allIds()) {
                if (has(static_cast<Trait>(t), id)) out[static_cast<size_t>(t)] += id + "\n";
            }
        }
        return out;
    }();
    return kTexts[static_cast<size_t>(trait)];
}

std::string presetStyle(const std::string& id) {
    if (styles::fluid(id)) return styles::fluidIds().front();
    if (styles::cymatics(id)) return styles::cymaticsIds().front();
    return id;
}

std::optional<float> overrideOf(const std::string& styleId, const std::string& paramKey) {
    if (paramKey != kGeometryKey) return std::nullopt;
    const auto* style = styles::cymatics(styleId);
    if (style == nullptr || style->geometryOverride < 0) return std::nullopt;
    return static_cast<float>(style->geometryOverride);
}

std::optional<float> valueOf(const std::string& name) {
    if (name == "fluidTiers") return static_cast<float>(fluid::quality::kTierCount);
    if (name == "touchRotationPerDegree") return kTouchRotationPerDegree;
    if (name == "touchPoints") return static_cast<float>(TouchField::kMaxPoints);
    return std::nullopt;
}

bool isShaderStyle(const std::string& id) { return rowOf(id) != nullptr; }

bool knows(const std::string& id) {
    return isShaderStyle(id) || styles::cymatics(id) || styles::silk(id) || styles::life(id) || styles::acid(id) ||
           styles::myco(id) || styles::fluid(id) || id == kCurlFlow;
}

const std::string& resolveId(const std::string& id) {
    static const std::array<std::string, kRetired.size()> now = [] {
        std::array<std::string, kRetired.size()> out;
        for (size_t i = 0; i < kRetired.size(); ++i) out[i] = kRetired[i].now;
        return out;
    }();
    for (size_t i = 0; i < kRetired.size(); ++i) {
        if (id == kRetired[i].id) return now[i];
    }
    return id;
}

Kind kindOf(const std::string& id) {
    if (isShaderStyle(id)) return Kind::Shader;
    if (styles::fluid(id)) return Kind::Fluid;
    if (id == kCurlFlow) return Kind::CurlFlow;
    if (styles::cymatics(id)) return Kind::Cymatics;
    if (styles::life(id)) return Kind::Life;
    if (styles::myco(id)) return Kind::Mycelium;
    if (styles::acid(id)) return Kind::Acid;
    return Kind::Silk;
}

bool isMarched(const std::string& id) { return flagged(id, March) || presentsIn3d(kindOf(id)); }

bool fliesCamera(const std::string& id) { return isMarched(id) && !flagged(id, OffRig); }

float spinRate(const std::string& id) {
    const StyleRow* row = rowOf(id);
    return row != nullptr ? row->flags.spin : 0.0f;
}

bool drawsFamilyForm(const std::string& id) { return flagged(id, Form); }

bool hasFormulas(const std::string& id) { return flagged(id, Formula); }

bool drawsShapes(const std::string& id) { return flagged(id, Shapes); }

bool castsShadows(const std::string& id) { return flagged(id, Shadow); }

bool colorsByEscape(const std::string& id) { return flagged(id, Escape); }

std::span<const Family> families() { return kFamilies; }

Look lookOf(const std::string& id) {
    if (const StyleRow* row = rowOf(id)) return row->look;
    const Kind kind = kindOf(id);
    if (kind != Kind::Shader) return kindLook(kind);
    // Unreachable: every shader style has a row. Unknown ids are Silk's.
    return Look::Psychedelic;
}

Scope scopeOf(const std::string& paramKey) {
    for (const ParamMeta& meta : SceneParams::metaTable()) {
        if (meta.live && paramKey == meta.label) return meta.scope;
    }
    return Scope::Universal;
}

bool reaches(Scope scope, const std::string& styleId) {
    if (scope == Scope::MarchDetail) return isMarched(styleId);
    if (scope == Scope::Spin) return spinRate(styleId) != 0.0f;
    if (scope == Scope::FamilyForm) return drawsFamilyForm(styleId);
    if (scope == Scope::Formula) return hasFormulas(styleId);
    if (scope == Scope::Shapes) return drawsShapes(styleId);
    if (scope == Scope::Shadow) return castsShadows(styleId);
    if (scope == Scope::Escape) return colorsByEscape(styleId);
    return reachesKind(scope, kindOf(styleId));
}

const char* name(Kind kind) { return kKindNames[static_cast<int>(kind)]; }
const char* name(Scope scope) { return kScopeNames[static_cast<int>(scope)]; }
const char* name(Look look) { return kLookNames[static_cast<int>(look)]; }

std::optional<Scope> scopeNamed(const std::string& name) {
    for (int s = 0; s < kScopeCount; ++s) {
        if (name == kScopeNames[s]) return static_cast<Scope>(s);
    }
    return std::nullopt;
}

const std::string& text() {
    static const std::string kText = build();
    return kText;
}

}  // namespace geode::viz::catalogue
