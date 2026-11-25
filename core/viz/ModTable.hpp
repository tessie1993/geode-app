#pragma once
#include <array>

#include "viz/params/Params.hpp"

// The modulation targets as data. A scene field a wave, an envelope, a route or a knob can move is one with a
// GEODE_PARAM_MOD row in viz/params/ParamTable.inc: its scale (how far one unit of modulation moves it) and its flags (kModLight)
// are that row, so a field becomes a target by gaining one, and no list here names what it does.
//
// A target's ordinal is its wire value (LfoTarget, the lfo_target config row, the routes' and the envelopes' floats). The
// ordinals 0..82 are the targets as they first shipped, frozen in kLegacy with the name a saved wave, envelope, route
// or knob link carries and the label the pickers show, so nothing saved or on the wire changes: None, the retired
// slots (5, 25, 37, 38), the waves' speed and depth (39..44) and Macro (59) write no field. Every other field with a mod
// row follows them, in slot order: saved by its field name (cymaticsRing), labelled with its Customize control's
// label. Pure data, constexpr, so LfoTarget's last ordinal and the target count are known at compile time.
namespace geode::viz::modtable {

inline constexpr int kNoSlot = -1;

struct Legacy {
    const char* name;
    const char* label;
    int slot;
};

#define GEODE_MOD_SLOT(field) static_cast<int>(ParamSlot::field)
inline constexpr std::array<Legacy, 83> kLegacy{{
    {"NONE", "None", kNoSlot},
    {"SPEED", "Speed", GEODE_MOD_SLOT(speed)},
    {"ZOOM", "Zoom", GEODE_MOD_SLOT(zoom)},
    {"ROTATION", "Rotation", GEODE_MOD_SLOT(rotation)},
    {"SWAY", "Sway", GEODE_MOD_SLOT(sway)},
    {"RETIRED_5", "", kNoSlot},  // was PULSE; a saved "PULSE" no longer resolves
    {"DRIFT_X", "Drift X", GEODE_MOD_SLOT(driftX)},
    {"DRIFT_Y", "Drift Y", GEODE_MOD_SLOT(driftY)},
    {"WARP", "Warp", GEODE_MOD_SLOT(warp)},
    {"RIPPLE", "Ripple", GEODE_MOD_SLOT(ripple)},
    {"MORPH", "Morph", GEODE_MOD_SLOT(morph)},
    {"TWIST", "Twist", GEODE_MOD_SLOT(twist)},
    {"TILE", "Tile", GEODE_MOD_SLOT(tile)},
    {"PIXELATE", "Pixelate", GEODE_MOD_SLOT(pixelate)},
    {"POSTERIZE", "Posterize", GEODE_MOD_SLOT(posterize)},
    {"COLOR_SHIFT", "Hue shift", GEODE_MOD_SLOT(colorShift)},
    {"PALETTE_MIX", "Palette 2 amount", GEODE_MOD_SLOT(paletteAmount1)},  // was Palette blend
    {"SATURATION", "Saturation", GEODE_MOD_SLOT(saturation)},
    {"BRIGHTNESS", "Brightness", GEODE_MOD_SLOT(brightness)},
    {"INTENSITY", "Intensity", GEODE_MOD_SLOT(intensity)},
    {"BLOOM", "Bloom", GEODE_MOD_SLOT(bloom)},
    {"TEMPERATURE", "Temperature", GEODE_MOD_SLOT(temperature)},
    {"TURBULENCE", "Turbulence", GEODE_MOD_SLOT(turbulence)},
    {"CHROMA_AB", "Chroma AB", GEODE_MOD_SLOT(chromaAb)},
    {"VIGNETTE", "Vignette", GEODE_MOD_SLOT(vignette)},
    {"RETIRED_25", "", kNoSlot},  // was GLITCH; a saved "GLITCH" no longer resolves
    {"FISHEYE", "Fisheye", GEODE_MOD_SLOT(fisheye)},
    {"PARTICLE_SIZE", "Particle size", GEODE_MOD_SLOT(particleSize)},
    {"TRAIL_LENGTH", "Trail length", GEODE_MOD_SLOT(trailLength)},
    {"FLUID_CURL", "Fluid curl", GEODE_MOD_SLOT(fluidCurl)},
    {"FLUID_RADIUS", "Fluid splat radius", GEODE_MOD_SLOT(fluidSplatRadius)},
    {"FLUID_FORCE", "Fluid splat force", GEODE_MOD_SLOT(fluidSplatForce)},
    {"FLUID_GLOW", "Fluid glow", GEODE_MOD_SLOT(fluidBloomIntensity)},
    {"FLUID_FADE", "Fluid fade", GEODE_MOD_SLOT(fluidDensityDissipation)},
    {"FLUID_CATCH_PULL", "Catch pull", GEODE_MOD_SLOT(fluidCatchPull)},
    {"FLUID_CATCH_RADIUS", "Catch radius", GEODE_MOD_SLOT(fluidCatchRadius)},
    {"FLOW_STRENGTH", "Flow strength", GEODE_MOD_SLOT(flowStrength)},
    {"RETIRED_37", "", kNoSlot},  // was WATER_RIPPLE (Water is gone; Pool replaced it)
    {"RETIRED_38", "", kNoSlot},  // was RIPPLE_OVERLAY
    {"LFO1_RATE", "Wave 1 speed", kNoSlot},
    {"LFO1_DEPTH", "Wave 1 depth", kNoSlot},
    {"LFO2_RATE", "Wave 2 speed", kNoSlot},
    {"LFO2_DEPTH", "Wave 2 depth", kNoSlot},
    {"LFO3_RATE", "Wave 3 speed", kNoSlot},
    {"LFO3_DEPTH", "Wave 3 depth", kNoSlot},
    {"CAM_FLY", "Camera speed", GEODE_MOD_SLOT(camFly)},
    {"CAM_ORBIT", "Camera swing", GEODE_MOD_SLOT(camOrbit)},
    {"CAM_PUSH", "Camera push", GEODE_MOD_SLOT(camPush)},
    {"CAM_YAW", "Camera turn", GEODE_MOD_SLOT(camYaw)},
    {"CAM_PITCH", "Camera tilt", GEODE_MOD_SLOT(camPitch)},
    {"CAM_ROLL", "Camera roll", GEODE_MOD_SLOT(camRoll)},
    {"CAM_FOV", "Camera FOV", GEODE_MOD_SLOT(camFov)},
    {"CAM_SHAKE", "Camera shake", GEODE_MOD_SLOT(camShake)},
    {"FLOW_CURL", "Swirl", GEODE_MOD_SLOT(flowCurl)},
    {"FLOW_FORCE", "Push", GEODE_MOD_SLOT(flowForce)},
    {"DIVE_SPEED", "Dive", GEODE_MOD_SLOT(endlessZoomSpeed)},
    {"DIVE_TWIST", "Dive twist", GEODE_MOD_SLOT(diveTwist)},
    {"DIVE_AIM_X", "Dive aim sideways", GEODE_MOD_SLOT(diveAimX)},
    {"DIVE_AIM_Y", "Dive aim up and down", GEODE_MOD_SLOT(diveAimY)},
    {"MACRO", "Macro knob", kNoSlot},
    {"PALETTE_AMOUNT_1", "Palette 1 amount", GEODE_MOD_SLOT(paletteAmount0)},
    {"PALETTE_AMOUNT_3", "Palette 3 amount", GEODE_MOD_SLOT(paletteAmount2)},
    {"PALETTE_AMOUNT_4", "Palette 4 amount", GEODE_MOD_SLOT(paletteAmount3)},
    {"PALETTE_AMOUNT_5", "Palette 5 amount", GEODE_MOD_SLOT(paletteAmount4)},
    {"PALETTE_AMOUNT_6", "Palette 6 amount", GEODE_MOD_SLOT(paletteAmount5)},
    {"PALETTE_AMOUNT_7", "Palette 7 amount", GEODE_MOD_SLOT(paletteAmount6)},
    {"PALETTE_AMOUNT_8", "Palette 8 amount", GEODE_MOD_SLOT(paletteAmount7)},
    {"PALETTE_SHIFT", "Palette position", GEODE_MOD_SLOT(paletteShift)},
    {"KALEIDO_FOLDS", "Kaleidoscope", GEODE_MOD_SLOT(kaleidoFolds)},
    {"LAYER_MIX", "Layer mix", GEODE_MOD_SLOT(layerMix)},
    {"SIM_FLUID", "Fluid layer", GEODE_MOD_SLOT(simFluid)},
    {"SIM_PARTICLES", "Particle field", GEODE_MOD_SLOT(simParticles)},
    {"SIM_FORCE", "Physics force", GEODE_MOD_SLOT(simForce)},
    {"SIM_LAYER_ORDER", "Fluid in front", GEODE_MOD_SLOT(simLayerOrder)},
    {"SPAWN", "Spawn", GEODE_MOD_SLOT(spawn)},
    {"FRACTAL", "Fractal", GEODE_MOD_SLOT(fractal)},
    {"FRONT_LAYER", "Front layer", GEODE_MOD_SLOT(frontLayer)},
    {"BACK_LAYER", "Back layer", GEODE_MOD_SLOT(backLayer)},
    {"FORMULA", "Formula", GEODE_MOD_SLOT(formula)},
    {"SHAPES", "Shapes", GEODE_MOD_SLOT(shapes)},
    {"SHADOW", "Soft shadows", GEODE_MOD_SLOT(shadow)},
    {"ESCAPE_COLOR", "Escape colour", GEODE_MOD_SLOT(escapeColor)},
    {"LIFE", "Life", GEODE_MOD_SLOT(life)},
}};
#undef GEODE_MOD_SLOT

inline constexpr int kLegacyCount = static_cast<int>(kLegacy.size());

constexpr bool legacyWrites(int slot) {
    for (const Legacy& t : kLegacy) {
        if (t.slot == slot) return true;
    }
    return false;
}

// Every legacy field keeps its mod row: its scale is the one it always had.
constexpr bool everyLegacyFieldHasAModRow() {
    for (const Legacy& t : kLegacy) {
        if (t.slot != kNoSlot && !kParamMods[static_cast<size_t>(t.slot)].on) return false;
    }
    return true;
}
static_assert(everyLegacyFieldHasAModRow(), "a legacy target's field keeps its GEODE_PARAM_MOD row");

constexpr int addedCount() {
    int n = 0;
    for (int slot = 0; slot < static_cast<int>(kParamMods.size()); ++slot) {
        if (kParamMods[static_cast<size_t>(slot)].on && !legacyWrites(slot)) ++n;
    }
    return n;
}

// The fields with a mod row no legacy target writes, in slot order: ordinals kLegacyCount and on.
inline constexpr int kAddedCount = addedCount();
inline constexpr std::array<int, kAddedCount> kAddedSlots = [] {
    std::array<int, kAddedCount> out{};
    int n = 0;
    for (int slot = 0; slot < static_cast<int>(kParamMods.size()); ++slot) {
        if (kParamMods[static_cast<size_t>(slot)].on && !legacyWrites(slot)) out[static_cast<size_t>(n++)] = slot;
    }
    return out;
}();

inline constexpr int kCount = kLegacyCount + kAddedCount;

// The field an ordinal writes; kNoSlot for one that writes none or is out of range.
constexpr int slotOf(int ordinal) {
    if (ordinal < 0 || ordinal >= kCount) return kNoSlot;
    return ordinal < kLegacyCount ? kLegacy[static_cast<size_t>(ordinal)].slot : kAddedSlots[static_cast<size_t>(ordinal - kLegacyCount)];
}

}  // namespace geode::viz::modtable
