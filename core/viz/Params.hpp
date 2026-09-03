#pragma once
#include <array>
#include <string>
#include <string_view>

namespace geode::viz {

// Port of SceneParams.kt: same fields, same defaults, same order.
struct SceneParams {
    static constexpr float kUnsetOverride = -1.0f;
    static constexpr int kNoPaletteLut = -1;
    static constexpr int kDefaultSymmetryFolds = 6;

    float speed = 1.0f;
    float zoom = 1.0f;
    float rotation = 0.0f;
    bool endlessZoom = false;
    float endlessZoomSpeed = 0.3f;
    float sway = 0.0f;
    float pulse = 0.0f;
    float driftX = 0.0f;
    float driftY = 0.0f;
    float shake = 0.0f;
    float audioDrive = 1.0f;
    float beatResponse = 1.0f;
    float turbulence = 0.0f;
    float density = 1.0f;
    float marchDetail = 1.0f;
    bool trails = false;
    float trailLength = 0.5f;
    float trailZoom = 0.0f;
    float trailWarp = 0.0f;
    bool mirror = false;
    float warp = 0.0f;
    float ripple = 0.0f;
    int symmetry = kDefaultSymmetryFolds;
    bool kaleidoscope = false;
    float morph = 0.0f;
    float pixelate = 0.0f;
    float posterize = 0.0f;
    int particleShape = 0;
    float particleSize = 1.0f;
    float tile = 1.0f;
    float twist = 0.0f;
    int palette = 0;
    int palette2 = 1;
    float paletteMix = 0.0f;
    float paletteBaseOverride = kUnsetOverride;
    float paletteRangeOverride = kUnsetOverride;
    float palette2BaseOverride = kUnsetOverride;
    float palette2RangeOverride = kUnsetOverride;
    int paletteLut = kNoPaletteLut;
    float milkdropPaletteTint = 0.0f;
    bool milkdropBlendPresets = false;
    float colorShift = 0.0f;
    float hueRange = 1.0f;
    float saturation = 1.0f;
    float brightness = 1.0f;
    float contrast = 1.0f;
    float gamma = 1.0f;
    bool colorCycle = false;
    float cycleSpeed = 0.1f;
    bool invert = false;
    float intensity = 1.0f;
    bool duotone = false;
    float bloom = 0.0f;
    float temperature = 0.0f;
    bool solarize = false;
    float bassGain = 1.0f;
    float midGain = 1.0f;
    float trebGain = 1.0f;
    float flash = 0.0f;
    float chromaAb = 0.0f;
    float vignette = 0.0f;
    float scanlines = 0.0f;
    float grain = 0.0f;
    float glitch = 0.0f;
    float fisheye = 0.0f;
    float strobe = 0.0f;
    float paramFadeSec = 0.0f;
    int fluidQuality = 2;
    bool fluidAutoQuality = true;
    int fluidIterations = 20;
    float fluidPressure = 0.8f;
    float fluidCurl = 30.0f;
    float fluidVelocityDissipation = 0.2f;
    float fluidDensityDissipation = 1.0f;
    float fluidChromaticAging = 0.3f;
    float fluidSplatRadius = 0.12f;
    float fluidSplatForce = 1.0f;
    int fluidBeatPattern = 1;
    int fluidBeatSplats = 3;
    int fluidStirrers = 2;
    float fluidStirrerSpeed = 1.0f;
    bool fluidBassPump = false;
    float fluidPaletteCycleSpeed = 0.5f;
    bool fluidSparkle = true;
    int fluidSpawnPath = 1;
    int fluidSpawnPoints = 3;
    float fluidSpawnProgress = 1.0f;
    int fluidCatchPoints = 2;
    float fluidCatchPull = 1.0f;
    float fluidCatchRadius = 0.12f;
    bool fluidParticlesEnabled = true;
    float fluidParticleLife = 6.0f;
    float fluidParticleDrag = 0.5f;
    float fluidParticleBrightness = 1.0f;
    bool fluidDyeEnabled = true;
    bool fluidShading = true;
    bool fluidBloom = true;
    float fluidBloomIntensity = 0.8f;
    float fluidBloomThreshold = 0.6f;
    bool fluidSunrays = true;
    float fluidSunraysWeight = 1.0f;
    float fluidCurlAudio = 0.5f;
    float fluidBloomAudio = 0.5f;
    float fluidFadeAudio = 0.6f;
    float fluidRadiusPulse = 0.4f;
    bool flowEnabled = false;
    float flowStrength = 0.35f;
    float flowForce = 1.0f;
    float flowCurl = 25.0f;
    float waterWaveSpeed = 1.0f;
    float waterDamping = 0.985f;
    float waterRippleStrength = 1.0f;
    float waterDepth = 0.6f;
    float waterSpecular = 0.7f;
    float waterFlow = 0.3f;
    float waterLiquid = 0.85f;
    float waterLiquidFlow = 1.4f;
    float waterLiquidFade = 0.35f;
    int cymaticsGeometry = 0;
    float cymaticsFundamental = 110.0f;
    int cymaticsModes = 5;
    float cymaticsRing = 0.35f;
    float cymaticsFocus = 0.7f;
    float cymaticsScale = 3.2f;
    float cymaticsFill = 0.45f;
    float cymaticsLine = 1.0f;
    float cymaticsGlow = 1.0f;
    float cymaticsIridescence = 0.5f;
    float cymaticsCaustic = 0.8f;
    float cymaticsFlow = 0.35f;
    float cymaticsSwirl = 0.05f;
    bool rippleOverlayEnabled = false;
    float rippleOverlayStrength = 0.4f;
    float rippleOverlaySpecular = 0.3f;

    struct Palette {
        const char* name;
        float base;
        float range;
    };
    static const std::array<Palette, 21>& palettes();

    bool usesCustomPalette() const { return paletteBaseOverride >= 0.0f || paletteRangeOverride >= 0.0f; }
    bool usesCustomPalette2() const { return palette2BaseOverride >= 0.0f || palette2RangeOverride >= 0.0f; }
    float paletteBase() const;
    float paletteRange() const;
    float palette2Base() const;
    float palette2Range() const;

    // Every float field except the NOT_FADED sentinels, in declaration order; what lerp and blend glide.
    struct FloatField {
        const char* name;
        float SceneParams::*member;
    };
    static const std::array<FloatField, 98>& lerpedFloats();

    // Every field in declaration order; the wire order of geode_viz_set_params.
    static constexpr int kFieldCount = 134;
    static const std::array<const char*, kFieldCount>& fieldNames();

    // Sets a field by its Kotlin property name; ints and bools are taken from the float's value.
    bool set(std::string_view name, float value);
};

SceneParams lerpParams(const SceneParams& from, const SceneParams& to, float k);
SceneParams blendParams(const SceneParams& a, const SceneParams& b, float t);

}  // namespace geode::viz
