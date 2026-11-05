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
    // Inert since wave three; removed with the next format bump.
    float pulse = 0.0f;
    float driftX = 0.0f;
    float driftY = 0.0f;
    // Inert since wave three; removed with the next format bump.
    float shake = 0.0f;
    float audioDrive = 1.0f;
    // Inert since wave three; removed with the next format bump.
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
    // Inert since wave three; removed with the next format bump.
    float flash = 0.0f;
    float chromaAb = 0.0f;
    float vignette = 0.0f;
    float scanlines = 0.0f;
    float grain = 0.0f;
    // Retired: the composite Glitch effect is gone. No reader; removed with the next format bump.
    float glitch = 0.0f;
    float fisheye = 0.0f;
    // Inert since wave three; removed with the next format bump.
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
    // How far the superformula driver (viz/FormDrive) moved every family's
    // parameters with the music. Inert since wave three (viz/MotionField
    // replaced it); kept, and 0 still switches nothing on, so a preset saved
    // before wave three still decodes.
    float formDrive = 0.7f;

    // Wave three: the continuous motion system (viz/MotionField), which
    // replaces formDrive/beatResponse/flash/strobe/pulse/shake as the way the
    // music moves a scene. Appended after the older fields so every existing
    // wire index survives.
    float motionAmount = 0.7f;
    float motionBreath = 0.5f;
    float motionOrbit = 0.5f;
    float motionDrift = 0.5f;
    float motionHue = 0.4f;

    // The shared camera rig (viz/CameraRig) and the reaction matrix
    // (viz/Reactions). Appended after wave three so every wire index survives.
    // The cam* offsets (push/yaw/pitch/roll) are what reaction routes and LFOs
    // write; the rig springs toward them, so a route never snaps the view.
    float camFly = 0.5f;      // forward travel speed along the path, 0..2
    int camPath = 0;          // 0 glide, 1 spiral, 2 orbit, 3 hover
    float camOrbit = 0.35f;   // sideways swing / orbit radius, 0..1
    float camPush = 0.0f;     // dolly offset, -1..1
    float camYaw = 0.0f;      // look offsets, -1..1
    float camPitch = 0.0f;
    float camRoll = 0.0f;
    float camFov = 1.0f;      // field-of-view multiplier, 0.5..2
    float camShake = 0.0f;    // handheld wobble, 0..1
    float camSmooth = 0.5f;   // spring softness, 0 tight .. 1 floaty
    bool camTouch = false;    // drag to look around
    bool camGyro = false;     // phone tilt steers the view (never in export)
    float reactAmount = 1.0f; // global scale of every reaction route, 0..2
    int reactSeed = 0;        // which variant of the style's reaction DNA

    // Note & key reactions (viz/NoteResponse), per style like every Customize control, so a preset, a
    // template and an export carry them. Appended so every existing wire index survives. Inert while
    // Settings › Behaviour › Note & key reactions is off: the layer then does nothing at all.
    // Customize › Advanced › Note & key (Pro): how much each source drives, 0 none .. 1 as designed .. 2.
    float noteColor = 1.0f;      // the notes' colours: the whole-picture tint's spread and pull
    float notePitch = 1.0f;      // pitch height: morph
    float noteIntervals = 1.0f;  // leaps swing and twist, steps glide
    float noteTension = 1.0f;    // harmonic tension bends (warp, chromatic split)
    float noteChords = 1.0f;     // a chord change shifts the scene (fold count, a turn)
    float noteDrums = 1.0f;      // the drums' push, on top of how percussive the track is

    // Liquid mode (viz/liquid; Customize › Advanced › Liquid, Pro). The switch is flowEnabled, Liquidity is
    // flowStrength, Push is flowForce and Swirl is flowCurl, the old flow field's fields, so a preset saved
    // with "Flow field on" opens as a liquid. Appended so every existing wire index survives.
    int liquidBorn = 1;     // what forms at the vanishing point: liquid::Born (0 live, 1 re-rolled, 2 next style, 3 ink forms)
    int liquidAge = 0;      // liquid::Age: 0 pure palette, 1 fade deep, 2 ink bleed
    int liquidSurface = 0;  // liquid::Surface: 0 ink, 1 wet gloss, 2 glass

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

    // Every float field except the NOT_FADED sentinels, in declaration order; what lerpParams glides.
    struct FloatField {
        const char* name;
        float SceneParams::*member;
    };
    // MUST equal the number of initialisers in Params.cpp: a std::array with
    // fewer initialisers than its size value-initialises the rest, and a null
    // pointer-to-member is not a compile error. It was declared as 98 over 95
    // entries, and lerpParams (and the blend since removed) wrote through the three null members
    // on every fade. Count again whenever a lerped float is added.
    static constexpr int kLerpedFloatCount = 118;
    static const std::array<FloatField, kLerpedFloatCount>& lerpedFloats();

    // Every field in declaration order; the wire order of geode_viz_set_params.
    static constexpr int kFieldCount = 163;
    static const std::array<const char*, kFieldCount>& fieldNames();

    // Sets a field by its Kotlin property name; ints and bools are taken from the float's value.
    bool set(std::string_view name, float value);
    // Reads a field by its Kotlin property name; ints and bools come back as floats (0 for an unknown name).
    float get(std::string_view name) const;

    // The geode_viz_set_params wire: kFieldCount floats in fieldNames() order.
    static SceneParams fromWire(const float* values);
    void toWire(float* out) const;
};

SceneParams lerpParams(const SceneParams& from, const SceneParams& to, float k);

}  // namespace geode::viz
