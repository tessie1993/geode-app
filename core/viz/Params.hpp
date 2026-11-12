#pragma once
#include <array>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace geode::viz {

// The scene params schema (#346): every field's name, kind, default, control range and wire slot is
// one row of viz/ParamTable.inc, and everything below expands from those rows: the struct's members
// and defaults, the wire order, get/set by name, what lerpParams glides and the LFO clamps. Kotlin's
// SceneParams.kt mirrors the fields today; geode_params_schema_text is how the shell reads the rows.

// What a row is. Circular is a float that wraps (a hue turn): the LFO adds to it unclamped.
enum class ParamKind { Float, Int, Bool, Circular };
// Whether lerpParams glides a field (Fade) or it snaps to the new value (Hold).
enum class ParamFade { Fade, Hold };

// The C++ type of each kind, for the struct's members.
#define GEODE_PARAM_TYPE_Float float
#define GEODE_PARAM_TYPE_Circular float
#define GEODE_PARAM_TYPE_Int int
#define GEODE_PARAM_TYPE_Bool bool

// Every field's wire slot, by its own name: ParamSlot::flowCurl is the slot of SceneParams::flowCurl.
enum class ParamSlot : int {
#define GEODE_PARAM(kind, name, def, lo, hi, fade) name,
#include "viz/ParamTable.inc"
#undef GEODE_PARAM
    kCount
};

// Rows nothing reads any more. Each keeps its wire slot, so every later slot stays where it is, but it
// is not a param: set() refuses it, the wire reads it as its default, and no look saves it.
inline constexpr ParamSlot kRetiredParams[] = {ParamSlot::pulse,
                                               ParamSlot::shake,
                                               ParamSlot::beatResponse,
                                               ParamSlot::flash,
                                               ParamSlot::glitch,
                                               ParamSlot::strobe,
                                               // Water's knobs and the ripple overlay: Pool replaced Water.
                                               ParamSlot::waterWaveSpeed,
                                               ParamSlot::waterDamping,
                                               ParamSlot::waterRippleStrength,
                                               ParamSlot::waterDepth,
                                               ParamSlot::waterSpecular,
                                               ParamSlot::waterFlow,
                                               ParamSlot::waterLiquid,
                                               ParamSlot::waterLiquidFlow,
                                               ParamSlot::waterLiquidFade,
                                               ParamSlot::rippleOverlayEnabled,
                                               ParamSlot::rippleOverlayStrength,
                                               ParamSlot::rippleOverlaySpecular,
                                               ParamSlot::formDrive,
                                               ParamSlot::motionOrbit,
                                               // The Kaleidoscope switch and its count: kaleidoFolds replaced both.
                                               ParamSlot::symmetry,
                                               ParamSlot::kaleidoscope,
                                               // Palette 2 and Palette blend: the palette stack draws more than one palette.
                                               ParamSlot::palette2,
                                               ParamSlot::paletteMix,
                                               ParamSlot::palette2BaseOverride,
                                               ParamSlot::palette2RangeOverride,
                                               // Color cycle and Cycle speed: the old hue cycle.
                                               ParamSlot::colorCycle,
                                               ParamSlot::cycleSpeed};

// How many labels a picker's GEODE_PARAM_CHOICES row gives it; 0 for a field with no row. Compile time, so
// an owner of the values (CameraRig's paths, the fluid tiers) can assert its count against the labels.
constexpr int choiceCount(ParamSlot slot) {
    int n = 0;
#define GEODE_PARAM_CHOICES(name, ...) \
    if (slot == ParamSlot::name) n = static_cast<int>(std::initializer_list<const char*>{__VA_ARGS__}.size());
#include "viz/ParamTable.inc"
#undef GEODE_PARAM_CHOICES
    return n;
}

namespace catalogue {
enum class Scope;
}

// A meta row's flags (ParamTable.inc): kMod, an LfoTarget writes the field.
inline constexpr unsigned kMod = 1u << 0;

// A live row's meta row, as the engine's tables read it (ParamTable.inc says what each column means).
// A retired row's is empty: no label, Universal, no flags, no roll range, live false.
struct ParamMeta {
    bool live;
    const char* label;
    catalogue::Scope scope;
    unsigned flags;
    float rollLo;
    float rollHi;
};

// One row, as the shell and the clamps read it. Ints and bools travel as floats, like the wire.
struct ParamSpec {
    const char* name;
    ParamKind kind;
    float defaultValue;
    float min;
    float max;
    ParamFade fade;
};

struct SceneParams {
    static constexpr float kUnsetOverride = -1.0f;
    static constexpr int kNoPaletteLut = -1;
    static constexpr int kNoStop = -1;
    // The fold count a style designed around a count of its own reads (uSymmetry) while the kaleidoscope is off,
    // and the retired symmetry row's default.
    static constexpr int kDefaultSymmetryFolds = 6;
    // The kaleidoscope's folds, fewest first: what a roll deals.
    static constexpr std::array<int, 10> kSymmetryFolds = {2, 3, 4, 5, 6, 7, 8, 9, 12, 16};
    // kaleidoFolds as the shaders fold it: the nearest whole count, a half up, held to 1..16 (NaN is 1, off).
    // Rounded here, not in the shaders, so every style and the composite step together: a slow wave or a glide
    // goes from one whole count to the next and never draws a fractional wedge, whose last wedge would seam.
    static constexpr int kaleidoFoldCount(float folds) {
        if (!(folds >= 1.0f)) return 1;
        return folds >= 16.0f ? 16 : static_cast<int>(folds + 0.5f);
    }
    // What the shaders read for kaleidoFolds (ShaderScene's uKaleido/uSymmetry, the composite's uPostKaleido/
    // uPostSymmetry): on from 2 whole folds, at that count. Off, the count is kDefaultSymmetryFolds, which the
    // styles that build on a count of their own (Chrysanthemum's mirrors and glass) read as it always was.
    struct KaleidoUniforms {
        float on;
        float folds;
    };
    static constexpr KaleidoUniforms kaleidoUniforms(float folds) {
        const int n = kaleidoFoldCount(folds);
        return n >= 2 ? KaleidoUniforms{1.0f, static_cast<float>(n)} : KaleidoUniforms{0.0f, static_cast<float>(kDefaultSymmetryFolds)};
    }
    // kaleidoFolds for a look saved with the retired switch and count: off is 1; on, its count held to 2..16.
    static constexpr float kaleidoFoldsOf(bool on, int symmetry) {
        if (!on) return 1.0f;
        return static_cast<float>(symmetry < 2 ? 2 : (symmetry > 16 ? 16 : symmetry));
    }

#define GEODE_PARAM(kind, name, def, lo, hi, fade) GEODE_PARAM_TYPE_##kind name = def;
#include "viz/ParamTable.inc"
#undef GEODE_PARAM

    struct Palette {
        const char* name;
        float base;
        float range;
    };
    static const std::array<Palette, 21>& palettes();
    // The built-in palettes a picker lists and a roll deals: rows 0..11. Rows 12..20 (Royal .. Yellow) are
    // retired: they stay in the table so a look saved on one still draws it, but nothing offers them again.
    static constexpr int kListedPalettes = 12;

    float paletteBase() const;
    float paletteRange() const;
    // Your colours: paletteStop0..4 in order, and how many are set from the first (0 = the slot's own ramp).
    std::array<int, 5> paletteStops() const { return {paletteStop0, paletteStop1, paletteStop2, paletteStop3, paletteStop4}; }
    int paletteStopCount() const;

    // Every field, in wire order: the float count of geode_viz_set_params (Kotlin's SceneParamsCodec.FIELDS).
    static constexpr int kFieldCount = static_cast<int>(ParamSlot::kCount);
    static const std::array<const char*, kFieldCount>& fieldNames();
    // The table, one ParamSpec per wire slot. Built once; the reference lives for the process.
    static const std::array<ParamSpec, kFieldCount>& schema();
    static const ParamSpec& spec(ParamSlot slot) { return schema()[static_cast<size_t>(slot)]; }
    // The meta rows, one per wire slot (a retired slot's empty). Built once; the reference lives for the process.
    static const std::array<ParamMeta, kFieldCount>& metaTable();
    static const ParamMeta& meta(ParamSlot slot) { return metaTable()[static_cast<size_t>(slot)]; }
    // A picker's labels in index order (its GEODE_PARAM_CHOICES row); empty for a field that is no picker.
    static const std::vector<std::string_view>& choices(ParamSlot slot);
    // The wire slot of a field name, -1 for an unknown one.
    static int slotOf(std::string_view name);
    static constexpr bool retired(ParamSlot slot) {
        for (ParamSlot r : kRetiredParams) {
            if (r == slot) return true;
        }
        return false;
    }
    // The table as the shell reads it (geode_params_schema_text). Rows, '|'-separated, one per line:
    //   params|<kSchemaTextVersion>|<kFieldCount>
    //   param|<slot>|<name>|<FLOAT|INT|BOOL|CIRCULAR>|<default>|<min>|<max>|<FADE|HOLD>|<LIVE|RETIRED>
    // one param row per slot, in slot order; numbers in the shortest form that reads back to the same float.
    // Then one row per built-in palette, in `palette` index order: palette|<index>|<name> (version 3).
    // Then one row per picker label, by field in slot order, each in index order: choice|<name>|<index>|<label> (version 4).
    static constexpr int kSchemaTextVersion = 4;
    static const std::string& schemaText();

    // Sets a field by its name; ints round and bools are value > 0.5. False for an unknown or a retired
    // name, which is how an old look's dead keys are dropped.
    bool set(std::string_view name, float value);
    // Reads a field by its name; ints and bools come back as floats (0 for an unknown name).
    float get(std::string_view name) const;
    // The same by wire slot; a slot outside 0..kFieldCount-1 or a retired one is ignored (false, 0 or its default).
    bool setSlot(int slot, float value);
    float getSlot(int slot) const;

    // The geode_viz_set_params wire: kFieldCount floats in slot order. A retired slot is read as its default.
    static SceneParams fromWire(const float* values);
    // fromWire with every Float row also held to its [min, max] (a Circular one wraps where it is drawn, so
    // it is not): what a value read from a saved look or a shared link may become (geode_params_sanitize).
    static SceneParams sanitized(const float* values);
    void toWire(float* out) const;
};

// Glides every Fade field from `from` toward `to` by k; every Hold field takes `to`'s value.
SceneParams lerpParams(const SceneParams& from, const SceneParams& to, float k);

}  // namespace geode::viz
