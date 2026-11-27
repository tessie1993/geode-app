#pragma once
#include <cstdint>

namespace geode::viz {

// The uniform names the per-frame uploads of ShaderScene and CompositePass write, interned as ids at compile
// time. UniformCache::loc(Uniform) resolves each name against its program once and then reads a slot, so a
// frame never hashes a name. A new per-frame uniform in those uploads is one X() line here.
#define GEODE_FRAME_UNIFORMS(X) \
    X(uBackLayer) \
    X(uBassRel) \
    X(uBirths) \
    X(uBlendMode) \
    X(uBloom) \
    X(uBright) \
    X(uCamBasis) \
    X(uCamDolly) \
    X(uCamFlat) \
    X(uCamFocal) \
    X(uCamPos) \
    X(uCamRoll) \
    X(uCamTiltCS) \
    X(uCamTravel) \
    X(uColorShift) \
    X(uContrast) \
    X(uDither) \
    X(uDriftX) \
    X(uDriftY) \
    X(uDuotone) \
    X(uEscapeColor) \
    X(uField) \
    X(uFieldGeom) \
    X(uFieldLook) \
    X(uFieldRes) \
    X(uFieldShape) \
    X(uFlow) \
    X(uFlowPhase) \
    X(uFlowTime) \
    X(uFormula) \
    X(uFxA) \
    X(uFxB) \
    X(uFxFrame) \
    X(uFxL) \
    X(uFractal) \
    X(uFrontLayer) \
    X(uGamma) \
    X(uGateA) \
    X(uGateB) \
    X(uGateL) \
    X(uGrain) \
    X(uHarmony) \
    X(uHasLayer) \
    X(uHueRange) \
    X(uHue) \
    X(uIntensity) \
    X(uInvert) \
    X(uKaleido) \
    X(uLayerMix) \
    X(uLife) \
    X(uMidRel) \
    X(uMirrorX) \
    X(uMorph) \
    X(uNoise) \
    X(uPalBase) \
    X(uPalLut) \
    X(uPalLutMix) \
    X(uPalLutRow) \
    X(uPalRange) \
    X(uPalSpan) \
    X(uPalTint) \
    X(uPalStopCount) \
    X(uPalStops) \
    X(uPixelate) \
    X(uPoolLook) \
    X(uPoolRings) \
    X(uPoolView) \
    X(uPoolWakes) \
    X(uPoolWaves) \
    X(uPostBirths) \
    X(uPostBloom) \
    X(uPostBright) \
    X(uPostContrast) \
    X(uPostGeo) \
    X(uPostGeo2) \
    X(uPostHueCS) \
    X(uPostLens) \
    X(uPostLens2) \
    X(uPostSat) \
    X(uPostShade) \
    X(uPostTile) \
    X(uPostTwistCentre) \
    X(uPosterize) \
    X(uProgress) \
    X(uRatio) \
    X(uResolution) \
    X(uRipple) \
    X(uRotAB) \
    X(uRotL) \
    X(uRotation) \
    X(uSat) \
    X(uScanline) \
    X(uShadow) \
    X(uShapeCount) \
    X(uShapeFold) \
    X(uShapeMelt) \
    X(uShapeMix) \
    X(uShapes) \
    X(uSolarize) \
    X(uSound) \
    X(uSoundCounts) \
    X(uSoundDrive) \
    X(uSoundEvents) \
    X(uSoundTex) \
    X(uSoundTimeSped) \
    X(uSpawn) \
    X(uSteps) \
    X(uStyle) \
    X(uSway) \
    X(uSymmetry) \
    X(uTemperature) \
    X(uTexA) \
    X(uTexB) \
    X(uTex) \
    X(uTexL) \
    X(uTile) \
    X(uTime) \
    X(uTouchAnchor) \
    X(uTouchAxis) \
    X(uTouchCount) \
    X(uTouchGesture) \
    X(uTouchPoints) \
    X(uTouchSpin) \
    X(uTrebRel) \
    X(uTurbulence) \
    X(uTwist) \
    X(uTwistBend) \
    X(uTwistCentre) \
    X(uUnderlay) \
    X(uUnderlayAmount) \
    X(uUnderlayBlend) \
    X(uWarp) \
    X(uWorld) \
    X(uWorldShapes) \
    X(uWorldTex) \
    X(uZoom)

enum class Uniform : uint16_t {
#define GEODE_UNIFORM_ENUM(name) name,
    GEODE_FRAME_UNIFORMS(GEODE_UNIFORM_ENUM)
#undef GEODE_UNIFORM_ENUM
    kCount
};

inline constexpr const char* kUniformNames[] = {
#define GEODE_UNIFORM_NAME(name) #name,
    GEODE_FRAME_UNIFORMS(GEODE_UNIFORM_NAME)
#undef GEODE_UNIFORM_NAME
};

}  // namespace geode::viz
