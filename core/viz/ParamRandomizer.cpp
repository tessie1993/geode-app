#include "viz/ParamRandomizer.hpp"

#include <cstring>
#include <iterator>

#include "viz/Catalogue.hpp"
#include "viz/scenes/CymaticsMath.hpp"

namespace geode::viz::randomizer {

namespace {

// splitmix64: tiny, fast, and the same stream on every platform, which the standard
// distributions are not (libc++ and libstdc++ map engine bits to floats differently).
struct Rng {
    uint64_t state;

    uint64_t next() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // 0 <= unit < 1, 24 bits.
    float unit() { return static_cast<float>(next() >> 40) * (1.0f / 16777216.0f); }
    float f(float lo, float hi) { return lo + unit() * (hi - lo); }
    // lo..hi inclusive.
    int n(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<uint64_t>(hi - lo + 1)); }
    int below(int count) { return n(0, count - 1); }
    bool chance(float p) { return unit() < p; }
    float sometimes(float p, float lo, float hi) { return chance(p) ? f(lo, hi) : 0.0f; }
};

using RollFn = void (*)(SceneParams&, Rng&);

struct Roll {
    RolledKey key;
    RollFn roll;
};

// Choice counts the rolls pick from (SceneParams.kt's lists and Renderer's cyclic LUT rows).
constexpr int kPalettes = 21;
constexpr int kCyclicPalettes = 5;
constexpr int kParticleShapes = 7;
constexpr int kFluidPatterns = 4;
constexpr int kFluidPaths = 5;
constexpr int kCymaticsGeometries = 2;
constexpr int kCameraPaths = 4;
constexpr int kFolds[] = {2, 3, 4, 5, 6, 7, 8, 9, 12, 16};
// reactSeed travels as a float; above 2^24 it would not come back as the same int.
constexpr int kMaxReactSeed = (1 << 24) - 1;

const std::vector<Roll>& rolls() {
    using T = Tab;
    // Upper bounds of brightness, contrast, intensity, bloom and glitch are the flash clamp's
    // (1 + kMaxFlashDepth, kMaxFlashDepth): above them a roll is invisible.
    static const std::vector<Roll> kRolls = {
        {{T::Motion, "Speed", {"speed"}}, [](SceneParams& p, Rng& r) { p.speed = r.f(0.2f, 2.5f); }},
        {{T::Motion, "Zoom", {"zoom"}}, [](SceneParams& p, Rng& r) { p.zoom = r.f(0.6f, 2.0f); }},
        {{T::Motion, "Rotation", {"rotation"}}, [](SceneParams& p, Rng& r) { p.rotation = r.f(-1.5f, 1.5f); }},
        {{T::Motion, "Sway", {"sway"}}, [](SceneParams& p, Rng& r) { p.sway = r.sometimes(0.5f, 0.1f, 0.8f); }},
        {{T::Motion, "Drift X", {"driftX"}}, [](SceneParams& p, Rng& r) { p.driftX = r.sometimes(0.3f, -0.5f, 0.5f); }},
        {{T::Motion, "Drift Y", {"driftY"}}, [](SceneParams& p, Rng& r) { p.driftY = r.sometimes(0.3f, -0.5f, 0.5f); }},
        {{T::Motion, "Camera path", {"camPath"}}, [](SceneParams& p, Rng& r) { p.camPath = r.below(kCameraPaths); }},
        {{T::Motion, "Fly speed", {"camFly"}}, [](SceneParams& p, Rng& r) { p.camFly = r.f(0.2f, 1.4f); }},
        {{T::Motion, "Swing", {"camOrbit"}}, [](SceneParams& p, Rng& r) { p.camOrbit = r.f(0.1f, 0.8f); }},
        {{T::Motion, "Field of view", {"camFov"}}, [](SceneParams& p, Rng& r) { p.camFov = r.f(0.8f, 1.4f); }},
        {{T::Motion, "Handheld shake", {"camShake"}}, [](SceneParams& p, Rng& r) { p.camShake = r.sometimes(0.25f, 0.1f, 0.5f); }},
        {{T::Motion, "Endless zoom", {"endlessZoom"}}, [](SceneParams& p, Rng& r) { p.endlessZoom = r.chance(0.2f); }},
        {{T::Motion, "Dive speed", {"endlessZoomSpeed"}}, [](SceneParams& p, Rng& r) { p.endlessZoomSpeed = r.f(0.1f, 0.8f); }},
        {{T::Motion, "Turbulence", {"turbulence"}}, [](SceneParams& p, Rng& r) { p.turbulence = r.sometimes(0.5f, 0.1f, 1.0f); }},

        {{T::Shape, "Domain warp", {"warp"}}, [](SceneParams& p, Rng& r) { p.warp = r.sometimes(0.5f, 0.1f, 0.8f); }},
        {{T::Shape, "Ripple", {"ripple"}}, [](SceneParams& p, Rng& r) { p.ripple = r.sometimes(0.4f, 0.1f, 0.8f); }},
        {{T::Shape, "Morph", {"morph"}}, [](SceneParams& p, Rng& r) { p.morph = r.sometimes(0.5f, 0.1f, 0.8f); }},
        {{T::Shape, "Twist", {"twist"}}, [](SceneParams& p, Rng& r) { p.twist = r.sometimes(0.4f, -0.8f, 0.8f); }},
        {{T::Shape, "Kaleidoscope", {"kaleidoscope", "symmetry"}},
         [](SceneParams& p, Rng& r) {
             p.kaleidoscope = r.chance(0.3f);
             if (p.kaleidoscope) p.symmetry = kFolds[r.below(static_cast<int>(std::size(kFolds)))];
         }},
        {{T::Shape, "Tile", {"tile"}}, [](SceneParams& p, Rng& r) { p.tile = r.chance(0.25f) ? r.f(2.0f, 4.0f) : 1.0f; }},
        {{T::Shape, "Pixelate", {"pixelate"}}, [](SceneParams& p, Rng& r) { p.pixelate = r.sometimes(0.15f, 0.2f, 0.6f); }},
        {{T::Shape, "Mirror", {"mirror"}}, [](SceneParams& p, Rng& r) { p.mirror = r.chance(0.15f); }},
        {{T::Shape, "Particle shape", {"particleShape"}}, [](SceneParams& p, Rng& r) { p.particleShape = r.below(kParticleShapes); }},
        {{T::Shape, "Particle size", {"particleSize"}}, [](SceneParams& p, Rng& r) { p.particleSize = r.f(0.5f, 1.8f); }},
        {{T::Shape, "Density", {"density"}}, [](SceneParams& p, Rng& r) { p.density = r.f(0.4f, 1.0f); }},

        {{T::Reactivity, "Audio drive", {"audioDrive"}}, [](SceneParams& p, Rng& r) { p.audioDrive = r.f(0.6f, 1.8f); }},
        {{T::Reactivity, "Motion amount", {"motionAmount"}}, [](SceneParams& p, Rng& r) { p.motionAmount = r.f(0.3f, 1.0f); }},
        {{T::Reactivity, "Breath", {"motionBreath"}}, [](SceneParams& p, Rng& r) { p.motionBreath = r.f(0.2f, 0.9f); }},
        {{T::Reactivity, "Spin drift", {"motionDrift"}}, [](SceneParams& p, Rng& r) { p.motionDrift = r.f(0.1f, 0.9f); }},
        {{T::Reactivity, "Key colour", {"motionHue"}}, [](SceneParams& p, Rng& r) { p.motionHue = r.f(0.1f, 0.8f); }},
        // A new reaction personality for the style (core/viz/ReactionDna).
        {{T::Reactivity, kReactionDna, {"reactSeed"}}, [](SceneParams& p, Rng& r) { p.reactSeed = r.n(1, kMaxReactSeed); }},
        {{T::Reactivity, "Bass gain", {"bassGain"}}, [](SceneParams& p, Rng& r) { p.bassGain = r.f(0.8f, 1.4f); }},
        {{T::Reactivity, "Mid gain", {"midGain"}}, [](SceneParams& p, Rng& r) { p.midGain = r.f(0.8f, 1.4f); }},
        {{T::Reactivity, "Treble gain", {"trebGain"}}, [](SceneParams& p, Rng& r) { p.trebGain = r.f(0.8f, 1.4f); }},

        {{T::Scene, "Blend preset changes", {"milkdropBlendPresets"}},
         [](SceneParams& p, Rng& r) { p.milkdropBlendPresets = r.chance(0.35f); }},

        // A rolled palette replaces a user-made one: the custom hue overrides go back to unset.
        {{T::Color, "Palette", {"palette", "paletteBaseOverride", "paletteRangeOverride"}},
         [](SceneParams& p, Rng& r) {
             p.palette = r.below(kPalettes);
             p.paletteBaseOverride = SceneParams::kUnsetOverride;
             p.paletteRangeOverride = SceneParams::kUnsetOverride;
         }},
        {{T::Color, "Colour map", {"paletteLut"}},
         [](SceneParams& p, Rng& r) { p.paletteLut = r.chance(0.25f) ? r.below(kCyclicPalettes) : SceneParams::kNoPaletteLut; }},
        {{T::Color, "Palette 2", {"palette2", "palette2BaseOverride", "palette2RangeOverride"}},
         [](SceneParams& p, Rng& r) {
             p.palette2 = r.below(kPalettes);
             p.palette2BaseOverride = SceneParams::kUnsetOverride;
             p.palette2RangeOverride = SceneParams::kUnsetOverride;
         }},
        {{T::Color, "Palette blend", {"paletteMix"}}, [](SceneParams& p, Rng& r) { p.paletteMix = r.sometimes(0.5f, 0.2f, 0.8f); }},
        {{T::Color, "MilkDrop palette tint", {"milkdropPaletteTint"}},
         [](SceneParams& p, Rng& r) { p.milkdropPaletteTint = r.sometimes(0.4f, 0.2f, 0.9f); }},
        {{T::Color, "Hue shift", {"colorShift"}}, [](SceneParams& p, Rng& r) { p.colorShift = r.f(0.0f, 1.0f); }},
        {{T::Color, "Hue range", {"hueRange"}}, [](SceneParams& p, Rng& r) { p.hueRange = r.f(0.5f, 1.5f); }},
        {{T::Color, "Color cycle", {"colorCycle"}}, [](SceneParams& p, Rng& r) { p.colorCycle = r.chance(0.3f); }},
        {{T::Color, "Cycle speed", {"cycleSpeed"}}, [](SceneParams& p, Rng& r) { p.cycleSpeed = r.f(0.02f, 0.3f); }},
        {{T::Color, "Saturation", {"saturation"}}, [](SceneParams& p, Rng& r) { p.saturation = r.f(0.4f, 1.4f); }},
        {{T::Color, "Brightness", {"brightness"}}, [](SceneParams& p, Rng& r) { p.brightness = r.f(0.7f, 1.25f); }},
        {{T::Color, "Contrast", {"contrast"}}, [](SceneParams& p, Rng& r) { p.contrast = r.f(0.8f, 1.25f); }},
        {{T::Color, "Gamma", {"gamma"}}, [](SceneParams& p, Rng& r) { p.gamma = r.f(0.8f, 1.3f); }},
        {{T::Color, "Intensity", {"intensity"}}, [](SceneParams& p, Rng& r) { p.intensity = r.f(0.7f, 1.25f); }},
        {{T::Color, "Temperature", {"temperature"}}, [](SceneParams& p, Rng& r) { p.temperature = r.sometimes(0.4f, -0.6f, 0.6f); }},
        {{T::Color, "Bloom", {"bloom"}}, [](SceneParams& p, Rng& r) { p.bloom = r.sometimes(0.5f, 0.1f, 0.25f); }},
        {{T::Color, "Posterize", {"posterize"}}, [](SceneParams& p, Rng& r) { p.posterize = r.sometimes(0.15f, 0.2f, 0.6f); }},
        {{T::Color, "Duotone", {"duotone"}}, [](SceneParams& p, Rng& r) { p.duotone = r.chance(0.1f); }},
        {{T::Color, "Solarize", {"solarize"}}, [](SceneParams& p, Rng& r) { p.solarize = r.chance(0.05f); }},
        {{T::Color, "Invert", {"invert"}}, [](SceneParams& p, Rng& r) { p.invert = r.chance(0.03f); }},

        {{T::Fx, "Trails", {"trails"}}, [](SceneParams& p, Rng& r) { p.trails = r.chance(0.4f); }},
        {{T::Fx, "Trail length", {"trailLength"}}, [](SceneParams& p, Rng& r) { p.trailLength = r.f(0.3f, 0.9f); }},
        {{T::Fx, "Trail zoom (echo in/out)", {"trailZoom"}}, [](SceneParams& p, Rng& r) { p.trailZoom = r.sometimes(0.3f, -0.3f, 0.3f); }},
        {{T::Fx, "Trail warp (liquid echo)", {"trailWarp"}}, [](SceneParams& p, Rng& r) { p.trailWarp = r.sometimes(0.3f, 0.1f, 0.6f); }},
        {{T::Fx, "Chromatic aberration", {"chromaAb"}}, [](SceneParams& p, Rng& r) { p.chromaAb = r.sometimes(0.4f, 0.1f, 0.5f); }},
        {{T::Fx, "Vignette", {"vignette"}}, [](SceneParams& p, Rng& r) { p.vignette = r.sometimes(0.5f, 0.1f, 0.6f); }},
        {{T::Fx, "Scanlines", {"scanlines"}}, [](SceneParams& p, Rng& r) { p.scanlines = r.sometimes(0.25f, 0.15f, 0.5f); }},
        {{T::Fx, "Film grain", {"grain"}}, [](SceneParams& p, Rng& r) { p.grain = r.sometimes(0.3f, 0.1f, 0.4f); }},
        {{T::Fx, "Glitch", {"glitch"}}, [](SceneParams& p, Rng& r) { p.glitch = r.sometimes(0.2f, 0.1f, 0.25f); }},
        {{T::Fx, "Fisheye", {"fisheye"}}, [](SceneParams& p, Rng& r) { p.fisheye = r.sometimes(0.25f, -0.5f, 0.5f); }},

        {{T::Fluid, "Solver iterations", {"fluidIterations"}}, [](SceneParams& p, Rng& r) { p.fluidIterations = r.n(12, 28); }},
        {{T::Fluid, "Pressure", {"fluidPressure"}}, [](SceneParams& p, Rng& r) { p.fluidPressure = r.f(0.5f, 0.95f); }},
        {{T::Fluid, "Fluid curl", {"fluidCurl"}}, [](SceneParams& p, Rng& r) { p.fluidCurl = r.f(5.0f, 45.0f); }},
        {{T::Fluid, "Motion fade", {"fluidVelocityDissipation"}},
         [](SceneParams& p, Rng& r) { p.fluidVelocityDissipation = r.f(0.02f, 0.6f); }},
        {{T::Fluid, "Fluid fade", {"fluidDensityDissipation"}}, [](SceneParams& p, Rng& r) { p.fluidDensityDissipation = r.f(0.2f, 2.2f); }},
        {{T::Fluid, "Chromatic aging", {"fluidChromaticAging"}}, [](SceneParams& p, Rng& r) { p.fluidChromaticAging = r.f(0.0f, 0.8f); }},
        {{T::Fluid, "Hit pattern", {"fluidBeatPattern"}}, [](SceneParams& p, Rng& r) { p.fluidBeatPattern = r.below(kFluidPatterns); }},
        {{T::Fluid, "Hit splats", {"fluidBeatSplats"}}, [](SceneParams& p, Rng& r) { p.fluidBeatSplats = r.n(1, 6); }},
        {{T::Fluid, "Stirrers", {"fluidStirrers"}}, [](SceneParams& p, Rng& r) { p.fluidStirrers = r.n(0, 3); }},
        {{T::Fluid, "Stirrer speed", {"fluidStirrerSpeed"}}, [](SceneParams& p, Rng& r) { p.fluidStirrerSpeed = r.f(0.3f, 1.6f); }},
        {{T::Fluid, "Fluid splat radius", {"fluidSplatRadius"}}, [](SceneParams& p, Rng& r) { p.fluidSplatRadius = r.f(0.05f, 0.25f); }},
        {{T::Fluid, "Fluid splat force", {"fluidSplatForce"}}, [](SceneParams& p, Rng& r) { p.fluidSplatForce = r.f(0.5f, 2.0f); }},
        {{T::Fluid, "Bass pump", {"fluidBassPump"}}, [](SceneParams& p, Rng& r) { p.fluidBassPump = r.chance(0.25f); }},
        {{T::Fluid, "Treble sparkle", {"fluidSparkle"}}, [](SceneParams& p, Rng& r) { p.fluidSparkle = r.chance(0.7f); }},
        {{T::Fluid, "Palette cycle", {"fluidPaletteCycleSpeed"}}, [](SceneParams& p, Rng& r) { p.fluidPaletteCycleSpeed = r.f(0.0f, 1.2f); }},
        {{T::Fluid, "Path", {"fluidSpawnPath"}}, [](SceneParams& p, Rng& r) { p.fluidSpawnPath = r.below(kFluidPaths); }},
        {{T::Fluid, "Spawn points", {"fluidSpawnPoints"}}, [](SceneParams& p, Rng& r) { p.fluidSpawnPoints = r.n(2, 5); }},
        {{T::Fluid, "Catch points", {"fluidCatchPoints"}}, [](SceneParams& p, Rng& r) { p.fluidCatchPoints = r.n(0, 3); }},
        {{T::Fluid, "Catch pull", {"fluidCatchPull"}}, [](SceneParams& p, Rng& r) { p.fluidCatchPull = r.f(0.4f, 1.8f); }},
        {{T::Fluid, "Catch radius", {"fluidCatchRadius"}}, [](SceneParams& p, Rng& r) { p.fluidCatchRadius = r.f(0.06f, 0.2f); }},
        {{T::Fluid, "Particle life (s)", {"fluidParticleLife"}}, [](SceneParams& p, Rng& r) { p.fluidParticleLife = r.f(3.0f, 12.0f); }},
        {{T::Fluid, "Particle drag", {"fluidParticleDrag"}}, [](SceneParams& p, Rng& r) { p.fluidParticleDrag = r.f(0.15f, 0.9f); }},
        {{T::Fluid, "Particle brightness", {"fluidParticleBrightness"}},
         [](SceneParams& p, Rng& r) { p.fluidParticleBrightness = r.f(0.6f, 1.6f); }},
        {{T::Fluid, "Shading (embossed ink)", {"fluidShading"}}, [](SceneParams& p, Rng& r) { p.fluidShading = r.chance(0.7f); }},
        {{T::Fluid, "Glow (fluid)", {"fluidBloom"}}, [](SceneParams& p, Rng& r) { p.fluidBloom = r.chance(0.8f); }},
        {{T::Fluid, "Fluid glow", {"fluidBloomIntensity"}}, [](SceneParams& p, Rng& r) { p.fluidBloomIntensity = r.f(0.4f, 1.4f); }},
        {{T::Fluid, "Glow threshold", {"fluidBloomThreshold"}}, [](SceneParams& p, Rng& r) { p.fluidBloomThreshold = r.f(0.4f, 0.8f); }},
        {{T::Fluid, "Sunrays", {"fluidSunrays"}}, [](SceneParams& p, Rng& r) { p.fluidSunrays = r.chance(0.7f); }},
        {{T::Fluid, "Sunrays weight", {"fluidSunraysWeight"}}, [](SceneParams& p, Rng& r) { p.fluidSunraysWeight = r.f(0.4f, 1.0f); }},
        {{T::Fluid, "Curl from mids", {"fluidCurlAudio"}}, [](SceneParams& p, Rng& r) { p.fluidCurlAudio = r.f(0.1f, 0.9f); }},
        {{T::Fluid, "Glow from loudness", {"fluidBloomAudio"}}, [](SceneParams& p, Rng& r) { p.fluidBloomAudio = r.f(0.1f, 0.9f); }},
        {{T::Fluid, "Fade when quiet", {"fluidFadeAudio"}}, [](SceneParams& p, Rng& r) { p.fluidFadeAudio = r.f(0.2f, 0.9f); }},
        {{T::Fluid, "Radius on hit", {"fluidRadiusPulse"}}, [](SceneParams& p, Rng& r) { p.fluidRadiusPulse = r.f(0.0f, 0.8f); }},
        {{T::Fluid, "Flow strength", {"flowStrength"}}, [](SceneParams& p, Rng& r) { p.flowStrength = r.f(0.1f, 0.7f); }},
        {{T::Fluid, "Flow force", {"flowForce"}}, [](SceneParams& p, Rng& r) { p.flowForce = r.f(0.4f, 2.0f); }},
        {{T::Fluid, "Flow curl", {"flowCurl"}}, [](SceneParams& p, Rng& r) { p.flowCurl = r.f(5.0f, 40.0f); }},
        {{T::Fluid, "Wave speed", {"waterWaveSpeed"}}, [](SceneParams& p, Rng& r) { p.waterWaveSpeed = r.f(0.5f, 1.6f); }},
        {{T::Fluid, "Damping", {"waterDamping"}}, [](SceneParams& p, Rng& r) { p.waterDamping = r.f(0.96f, 0.995f); }},
        {{T::Fluid, "Ripple strength", {"waterRippleStrength"}}, [](SceneParams& p, Rng& r) { p.waterRippleStrength = r.f(0.5f, 1.6f); }},
        {{T::Fluid, "Depth", {"waterDepth"}}, [](SceneParams& p, Rng& r) { p.waterDepth = r.f(0.3f, 0.9f); }},
        {{T::Fluid, "Specular", {"waterSpecular"}}, [](SceneParams& p, Rng& r) { p.waterSpecular = r.f(0.3f, 0.9f); }},
        {{T::Fluid, "Flow drift", {"waterFlow"}}, [](SceneParams& p, Rng& r) { p.waterFlow = r.f(0.05f, 0.6f); }},
        {{T::Fluid, "Liquid", {"waterLiquid"}}, [](SceneParams& p, Rng& r) { p.waterLiquid = r.f(0.4f, 1.0f); }},
        {{T::Fluid, "Liquid flow", {"waterLiquidFlow"}}, [](SceneParams& p, Rng& r) { p.waterLiquidFlow = r.f(0.6f, 2.6f); }},
        {{T::Fluid, "Liquid fade", {"waterLiquidFade"}}, [](SceneParams& p, Rng& r) { p.waterLiquidFade = r.f(0.1f, 1.2f); }},
        {{T::Fluid, "Ripple overlay strength", {"rippleOverlayStrength"}},
         [](SceneParams& p, Rng& r) { p.rippleOverlayStrength = r.f(0.15f, 0.7f); }},
        {{T::Fluid, "Ripple glint", {"rippleOverlaySpecular"}}, [](SceneParams& p, Rng& r) { p.rippleOverlaySpecular = r.f(0.1f, 0.6f); }},

        {{T::Cymatics, "Geometry", {"cymaticsGeometry"}}, [](SceneParams& p, Rng& r) { p.cymaticsGeometry = r.below(kCymaticsGeometries); }},
        {{T::Cymatics, "Fundamental (Hz)", {"cymaticsFundamental"}}, [](SceneParams& p, Rng& r) { p.cymaticsFundamental = r.f(55.0f, 260.0f); }},
        {{T::Cymatics, "Standing waves", {"cymaticsModes"}},
         [](SceneParams& p, Rng& r) {
             p.cymaticsModes = r.n(2, cymatics::kMaxRenderedModes);
         }},
        {{T::Cymatics, "Tonal focus", {"cymaticsFocus"}}, [](SceneParams& p, Rng& r) { p.cymaticsFocus = r.f(0.35f, 1.0f); }},
        {{T::Cymatics, "Plate ring", {"cymaticsRing"}}, [](SceneParams& p, Rng& r) { p.cymaticsRing = r.f(0.15f, 0.8f); }},
        {{T::Cymatics, "Field scale", {"cymaticsScale"}}, [](SceneParams& p, Rng& r) { p.cymaticsScale = r.f(1.6f, 5.5f); }},
        {{T::Cymatics, "Wave flow", {"cymaticsFlow"}}, [](SceneParams& p, Rng& r) { p.cymaticsFlow = r.f(0.05f, 0.8f); }},
        {{T::Cymatics, "Field swirl", {"cymaticsSwirl"}}, [](SceneParams& p, Rng& r) { p.cymaticsSwirl = r.f(-0.3f, 0.3f); }},
        {{T::Cymatics, "Fill", {"cymaticsFill"}}, [](SceneParams& p, Rng& r) { p.cymaticsFill = r.f(0.05f, 1.0f); }},
        {{T::Cymatics, "Nodal lines", {"cymaticsLine"}}, [](SceneParams& p, Rng& r) { p.cymaticsLine = r.f(0.4f, 1.7f); }},
        {{T::Cymatics, "Nodal glow", {"cymaticsGlow"}}, [](SceneParams& p, Rng& r) { p.cymaticsGlow = r.f(0.3f, 1.7f); }},
        {{T::Cymatics, "Iridescence", {"cymaticsIridescence"}}, [](SceneParams& p, Rng& r) { p.cymaticsIridescence = r.f(0.0f, 1.0f); }},
        {{T::Cymatics, "Caustic sheen", {"cymaticsCaustic"}}, [](SceneParams& p, Rng& r) { p.cymaticsCaustic = r.f(0.2f, 1.3f); }},
    };
    return kRolls;
}

constexpr const char* kTabNames[] = {"SHAPE", "COLOR", "MOTION", "FX", "REACTIVITY", "SCENE", "FLUID", "CYMATICS"};

bool sameWire(const SceneParams& a, const SceneParams& b) {
    float x[SceneParams::kFieldCount];
    float y[SceneParams::kFieldCount];
    a.toWire(x);
    b.toWire(y);
    return std::memcmp(x, y, sizeof x) == 0;
}

}  // namespace

const std::vector<RolledKey>& rolledKeys() {
    static const std::vector<RolledKey> kKeys = [] {
        std::vector<RolledKey> out;
        for (const auto& roll : rolls()) out.push_back(roll.key);
        return out;
    }();
    return kKeys;
}

const std::vector<std::pair<const char*, const char*>>& neverRolled() {
    static const std::vector<std::pair<const char*, const char*>> kNever = {
        {"paramFadeSec", "an automation preference, not a look"},
        {"fluidQuality", "a performance setting, not a look"},
        {"fluidAutoQuality", "a performance setting, not a look"},
        {"fluidSpawnProgress", "how much the song itself drives the look"},
        {"fluidParticlesEnabled", "the fluid particle layer's master switch"},
        {"fluidDyeEnabled", "the fluid ink layer's master switch"},
        {"flowEnabled", "the FlowField's master switch"},
        {"rippleOverlayEnabled", "the water-ripple overlay's master switch"},
        {"marchDetail", "a performance budget, not a look"},
        {"camPush", "a modulation offset the reaction routes drive"},
        {"camYaw", "a modulation offset the reaction routes drive"},
        {"camPitch", "a modulation offset the reaction routes drive"},
        {"camRoll", "a modulation offset the reaction routes drive"},
        {"camSmooth", "a comfort preference, not a look"},
        {"camTouch", "an input preference, not a look"},
        {"camGyro", "an input preference, not a look"},
        {"reactAmount", "how much the user wants the music to move things"},
        {"camCrane", "a modulation offset the reaction routes drive"},
        {"camCut", "a trigger the reaction routes drive"},
        {"fall", "a layer the reaction routes drive"},
        {"fractal", "a layer the reaction routes drive"},
        {"spawn", "a trigger the reaction routes drive"},
        {"melt", "a layer the reaction routes drive"},
        {"freeze", "a trigger the reaction routes drive"},
        {"shatter", "a trigger the reaction routes drive"},
        {"particles", "a layer the reaction routes drive"},
        {"particleFall", "set with the particle layer"},
        // Inert since wave three (see SceneParams): no reader, so no control and no roll.
        {"pulse", "inert since wave three"},
        {"shake", "inert since wave three"},
        {"beatResponse", "inert since wave three"},
        {"flash", "inert since wave three"},
        {"strobe", "inert since wave three"},
        {"formDrive", "inert since wave three"},
        {"motionOrbit", "no native reader"},
    };
    return kNever;
}

std::optional<Tab> tabNamed(std::string_view tabName) {
    for (int i = 0; i < static_cast<int>(std::size(kTabNames)); ++i) {
        if (tabName == kTabNames[i]) return static_cast<Tab>(i);
    }
    return std::nullopt;
}

const char* name(Tab tab) { return kTabNames[static_cast<int>(tab)]; }

SceneParams randomize(const SceneParams& current, uint64_t seed, std::optional<Tab> tab, const std::string& styleId,
                      const std::set<std::string>& locked) {
    constexpr int kRerolls = 8;
    Rng rng{seed};
    for (int attempt = 0; attempt < kRerolls; ++attempt) {
        SceneParams out = current;
        for (const auto& roll : rolls()) {
            if (tab && roll.key.tab != *tab) continue;
            if (!styleId.empty() && !catalogue::reaches(catalogue::scopeOf(roll.key.key), styleId)) continue;
            if (locked.count(roll.key.key)) continue;
            roll.roll(out, rng);
        }
        if (!sameWire(out, current)) return out;
    }
    return current;
}

SceneParams resetTab(const SceneParams& current, Tab tab) {
    const SceneParams defaults{};
    SceneParams out = current;
    for (const auto& roll : rolls()) {
        if (roll.key.tab != tab) continue;
        for (const char* field : roll.key.fields) {
            if (field) out.set(field, defaults.get(field));
        }
    }
    return out;
}

}  // namespace geode::viz::randomizer
