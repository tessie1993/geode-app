#pragma once
#include <array>
#include <string>
#include <string_view>

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
                                               ParamSlot::motionOrbit};

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
    static constexpr int kDefaultSymmetryFolds = 6;

#define GEODE_PARAM(kind, name, def, lo, hi, fade) GEODE_PARAM_TYPE_##kind name = def;
#include "viz/ParamTable.inc"
#undef GEODE_PARAM

    struct Palette {
        const char* name;
        float base;
        float range;
    };
    static const std::array<Palette, 21>& palettes();

    float paletteBase() const;
    float paletteRange() const;
    float palette2Base() const;
    float palette2Range() const;
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
    static constexpr int kSchemaTextVersion = 3;
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
    void toWire(float* out) const;
};

// Glides every Fade field from `from` toward `to` by k; every Hold field takes `to`'s value.
SceneParams lerpParams(const SceneParams& from, const SceneParams& to, float k);

}  // namespace geode::viz
