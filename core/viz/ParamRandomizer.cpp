#include "viz/ParamRandomizer.hpp"

#include <cstring>
#include <iterator>

#include "viz/Catalogue.hpp"

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
    Tab tab;
    const char* key;
    RollFn roll;
};

// A choice anywhere in the field's own schema range (ParamTable.inc), so a value added there is rolled.
int choice(Rng& r, ParamSlot slot) {
    const ParamSpec& spec = SceneParams::spec(slot);
    return r.n(static_cast<int>(spec.min), static_cast<int>(spec.max));
}

// The field's roll range, its meta row's rollLo..rollHi (ParamTable.inc): a number anywhere in it, the
// same often but zero otherwise, or a whole count in it.
float drawn(Rng& r, ParamSlot slot) {
    const ParamMeta& m = SceneParams::meta(slot);
    return r.f(m.rollLo, m.rollHi);
}
float maybe(Rng& r, float p, ParamSlot slot) {
    const ParamMeta& m = SceneParams::meta(slot);
    return r.sometimes(p, m.rollLo, m.rollHi);
}
int counted(Rng& r, ParamSlot slot) {
    const ParamMeta& m = SceneParams::meta(slot);
    return r.n(static_cast<int>(m.rollLo), static_cast<int>(m.rollHi));
}

// What each roll does with its range: how often it moves a field at all, a coin for a switch, the
// palette's overrides cleared. The fields a roll owns are the rows its key labels (rolledKeys).
const std::vector<Roll>& rolls() {
    using T = Tab;
    // The roll ranges of brightness, contrast, intensity and bloom stop at the flash clamp's
    // (1 + kMaxFlashDepth, kMaxBloom): above them a roll is invisible.
    static const std::vector<Roll> kRolls = {
        {T::Motion, "Speed", [](SceneParams& p, Rng& r) { p.speed = drawn(r, ParamSlot::speed); }},
        {T::Motion, "Zoom", [](SceneParams& p, Rng& r) { p.zoom = drawn(r, ParamSlot::zoom); }},
        {T::Motion, "Rotation", [](SceneParams& p, Rng& r) { p.rotation = drawn(r, ParamSlot::rotation); }},
        {T::Motion, "Sway", [](SceneParams& p, Rng& r) { p.sway = maybe(r, 0.5f, ParamSlot::sway); }},
        {T::Motion, "Drift X", [](SceneParams& p, Rng& r) { p.driftX = maybe(r, 0.3f, ParamSlot::driftX); }},
        {T::Motion, "Drift Y", [](SceneParams& p, Rng& r) { p.driftY = maybe(r, 0.3f, ParamSlot::driftY); }},
        {T::Motion, "Camera path", [](SceneParams& p, Rng& r) { p.camPath = choice(r, ParamSlot::camPath); }},
        {T::Motion, "Fly speed", [](SceneParams& p, Rng& r) { p.camFly = drawn(r, ParamSlot::camFly); }},
        {T::Motion, "Swing", [](SceneParams& p, Rng& r) { p.camOrbit = drawn(r, ParamSlot::camOrbit); }},
        {T::Motion, "Field of view", [](SceneParams& p, Rng& r) { p.camFov = drawn(r, ParamSlot::camFov); }},
        {T::Motion, "Handheld shake", [](SceneParams& p, Rng& r) { p.camShake = maybe(r, 0.25f, ParamSlot::camShake); }},
        {T::Motion, "Endless zoom", [](SceneParams& p, Rng& r) { p.endlessZoom = r.chance(0.2f); }},
        {T::Motion, "Dive speed", [](SceneParams& p, Rng& r) { p.endlessZoomSpeed = drawn(r, ParamSlot::endlessZoomSpeed); }},
        {T::Motion, "Turbulence", [](SceneParams& p, Rng& r) { p.turbulence = maybe(r, 0.5f, ParamSlot::turbulence); }},

        {T::Shape, "Domain warp", [](SceneParams& p, Rng& r) { p.warp = maybe(r, 0.5f, ParamSlot::warp); }},
        {T::Shape, "Ripple", [](SceneParams& p, Rng& r) { p.ripple = maybe(r, 0.4f, ParamSlot::ripple); }},
        {T::Shape, "Morph", [](SceneParams& p, Rng& r) { p.morph = maybe(r, 0.5f, ParamSlot::morph); }},
        {T::Shape, "Twist", [](SceneParams& p, Rng& r) { p.twist = maybe(r, 0.4f, ParamSlot::twist); }},
        // On three times in ten, at a count from kSymmetryFolds; else off (1): the old switch's odds and draws.
        {T::Shape, "Kaleidoscope",
         [](SceneParams& p, Rng& r) {
             const auto& folds = SceneParams::kSymmetryFolds;
             p.kaleidoFolds = r.chance(0.3f) ? static_cast<float>(folds[static_cast<size_t>(r.below(static_cast<int>(folds.size())))]) : 1.0f;
         }},
        {T::Shape, "Tile", [](SceneParams& p, Rng& r) { p.tile = r.chance(0.25f) ? drawn(r, ParamSlot::tile) : 1.0f; }},
        {T::Shape, "Pixelate", [](SceneParams& p, Rng& r) { p.pixelate = maybe(r, 0.15f, ParamSlot::pixelate); }},
        {T::Shape, "Mirror", [](SceneParams& p, Rng& r) { p.mirror = r.chance(0.15f); }},
        {T::Shape, "Particle shape", [](SceneParams& p, Rng& r) { p.particleShape = counted(r, ParamSlot::particleShape); }},
        {T::Shape, "Particle size", [](SceneParams& p, Rng& r) { p.particleSize = drawn(r, ParamSlot::particleSize); }},
        {T::Shape, "Density", [](SceneParams& p, Rng& r) { p.density = drawn(r, ParamSlot::density); }},

        {T::Reactivity, "Audio drive", [](SceneParams& p, Rng& r) { p.audioDrive = drawn(r, ParamSlot::audioDrive); }},
        {T::Reactivity, "Motion amount", [](SceneParams& p, Rng& r) { p.motionAmount = drawn(r, ParamSlot::motionAmount); }},
        {T::Reactivity, "Breath", [](SceneParams& p, Rng& r) { p.motionBreath = drawn(r, ParamSlot::motionBreath); }},
        {T::Reactivity, "Key colour", [](SceneParams& p, Rng& r) { p.motionHue = drawn(r, ParamSlot::motionHue); }},
        // A new reaction personality for the style (core/viz/ReactionDna).
        {T::Reactivity, kReactionDna, [](SceneParams& p, Rng& r) { p.reactSeed = counted(r, ParamSlot::reactSeed); }},
        {T::Reactivity, "Bass gain", [](SceneParams& p, Rng& r) { p.bassGain = drawn(r, ParamSlot::bassGain); }},
        {T::Reactivity, "Mid gain", [](SceneParams& p, Rng& r) { p.midGain = drawn(r, ParamSlot::midGain); }},
        {T::Reactivity, "Treble gain", [](SceneParams& p, Rng& r) { p.trebGain = drawn(r, ParamSlot::trebGain); }},

        {T::Scene, "Blend preset changes",
         [](SceneParams& p, Rng& r) { p.milkdropBlendPresets = r.chance(0.35f); }},

        // A rolled palette replaces a user-made one: the custom hue overrides and your colours go back to unset.
        // It deals the listed built-ins only, never a retired row (SceneParams::kListedPalettes).
        {T::Color, "Palette",
         [](SceneParams& p, Rng& r) {
             p.palette = r.below(SceneParams::kListedPalettes);
             p.paletteBaseOverride = SceneParams::kUnsetOverride;
             p.paletteRangeOverride = SceneParams::kUnsetOverride;
             p.paletteStop0 = p.paletteStop1 = p.paletteStop2 = p.paletteStop3 = p.paletteStop4 = SceneParams::kNoStop;
         }},
        {T::Color, "Colour map",
         [](SceneParams& p, Rng& r) { p.paletteLut = r.chance(0.25f) ? counted(r, ParamSlot::paletteLut) : SceneParams::kNoPaletteLut; }},
        {T::Color, "MilkDrop palette tint",
         [](SceneParams& p, Rng& r) { p.milkdropPaletteTint = maybe(r, 0.4f, ParamSlot::milkdropPaletteTint); }},
        {T::Color, "Hue shift", [](SceneParams& p, Rng& r) { p.colorShift = drawn(r, ParamSlot::colorShift); }},
        {T::Color, "Hue range", [](SceneParams& p, Rng& r) { p.hueRange = drawn(r, ParamSlot::hueRange); }},
        {T::Color, "Saturation", [](SceneParams& p, Rng& r) { p.saturation = drawn(r, ParamSlot::saturation); }},
        {T::Color, "Brightness", [](SceneParams& p, Rng& r) { p.brightness = drawn(r, ParamSlot::brightness); }},
        {T::Color, "Contrast", [](SceneParams& p, Rng& r) { p.contrast = drawn(r, ParamSlot::contrast); }},
        {T::Color, "Gamma", [](SceneParams& p, Rng& r) { p.gamma = drawn(r, ParamSlot::gamma); }},
        {T::Color, "Intensity", [](SceneParams& p, Rng& r) { p.intensity = drawn(r, ParamSlot::intensity); }},
        {T::Color, "Temperature", [](SceneParams& p, Rng& r) { p.temperature = maybe(r, 0.4f, ParamSlot::temperature); }},
        {T::Color, "Bloom", [](SceneParams& p, Rng& r) { p.bloom = maybe(r, 0.5f, ParamSlot::bloom); }},
        {T::Color, "Posterize", [](SceneParams& p, Rng& r) { p.posterize = maybe(r, 0.15f, ParamSlot::posterize); }},
        {T::Color, "Duotone", [](SceneParams& p, Rng& r) { p.duotone = r.chance(0.1f); }},
        {T::Color, "Solarize", [](SceneParams& p, Rng& r) { p.solarize = r.chance(0.05f); }},
        {T::Color, "Invert", [](SceneParams& p, Rng& r) { p.invert = r.chance(0.03f); }},

        {T::Fx, "Trails", [](SceneParams& p, Rng& r) { p.trails = r.chance(0.4f); }},
        {T::Fx, "Trail length", [](SceneParams& p, Rng& r) { p.trailLength = drawn(r, ParamSlot::trailLength); }},
        {T::Fx, "Trail zoom (echo in/out)", [](SceneParams& p, Rng& r) { p.trailZoom = maybe(r, 0.3f, ParamSlot::trailZoom); }},
        {T::Fx, "Trail warp (liquid echo)", [](SceneParams& p, Rng& r) { p.trailWarp = maybe(r, 0.3f, ParamSlot::trailWarp); }},
        {T::Fx, "Chromatic aberration", [](SceneParams& p, Rng& r) { p.chromaAb = maybe(r, 0.4f, ParamSlot::chromaAb); }},
        {T::Fx, "Vignette", [](SceneParams& p, Rng& r) { p.vignette = maybe(r, 0.5f, ParamSlot::vignette); }},
        {T::Fx, "Scanlines", [](SceneParams& p, Rng& r) { p.scanlines = maybe(r, 0.25f, ParamSlot::scanlines); }},
        {T::Fx, "Film grain", [](SceneParams& p, Rng& r) { p.grain = maybe(r, 0.3f, ParamSlot::grain); }},
        {T::Fx, "Fisheye", [](SceneParams& p, Rng& r) { p.fisheye = maybe(r, 0.25f, ParamSlot::fisheye); }},

        {T::Fluid, "Solver iterations", [](SceneParams& p, Rng& r) { p.fluidIterations = counted(r, ParamSlot::fluidIterations); }},
        {T::Fluid, "Pressure", [](SceneParams& p, Rng& r) { p.fluidPressure = drawn(r, ParamSlot::fluidPressure); }},
        {T::Fluid, "Fluid curl", [](SceneParams& p, Rng& r) { p.fluidCurl = drawn(r, ParamSlot::fluidCurl); }},
        {T::Fluid, "Motion fade",
         [](SceneParams& p, Rng& r) { p.fluidVelocityDissipation = drawn(r, ParamSlot::fluidVelocityDissipation); }},
        {T::Fluid, "Fluid fade", [](SceneParams& p, Rng& r) { p.fluidDensityDissipation = drawn(r, ParamSlot::fluidDensityDissipation); }},
        {T::Fluid, "Chromatic aging", [](SceneParams& p, Rng& r) { p.fluidChromaticAging = drawn(r, ParamSlot::fluidChromaticAging); }},
        {T::Fluid, "Hit pattern", [](SceneParams& p, Rng& r) { p.fluidBeatPattern = counted(r, ParamSlot::fluidBeatPattern); }},
        {T::Fluid, "Hit splats", [](SceneParams& p, Rng& r) { p.fluidBeatSplats = counted(r, ParamSlot::fluidBeatSplats); }},
        {T::Fluid, "Stirrers", [](SceneParams& p, Rng& r) { p.fluidStirrers = counted(r, ParamSlot::fluidStirrers); }},
        {T::Fluid, "Stirrer speed", [](SceneParams& p, Rng& r) { p.fluidStirrerSpeed = drawn(r, ParamSlot::fluidStirrerSpeed); }},
        {T::Fluid, "Fluid splat radius", [](SceneParams& p, Rng& r) { p.fluidSplatRadius = drawn(r, ParamSlot::fluidSplatRadius); }},
        {T::Fluid, "Fluid splat force", [](SceneParams& p, Rng& r) { p.fluidSplatForce = drawn(r, ParamSlot::fluidSplatForce); }},
        {T::Fluid, "Bass pump", [](SceneParams& p, Rng& r) { p.fluidBassPump = r.chance(0.25f); }},
        {T::Fluid, "Treble sparkle", [](SceneParams& p, Rng& r) { p.fluidSparkle = r.chance(0.7f); }},
        {T::Fluid, "Palette cycle", [](SceneParams& p, Rng& r) { p.fluidPaletteCycleSpeed = drawn(r, ParamSlot::fluidPaletteCycleSpeed); }},
        {T::Fluid, "Path", [](SceneParams& p, Rng& r) { p.fluidSpawnPath = counted(r, ParamSlot::fluidSpawnPath); }},
        {T::Fluid, "Spawn points", [](SceneParams& p, Rng& r) { p.fluidSpawnPoints = counted(r, ParamSlot::fluidSpawnPoints); }},
        {T::Fluid, "Catch points", [](SceneParams& p, Rng& r) { p.fluidCatchPoints = counted(r, ParamSlot::fluidCatchPoints); }},
        {T::Fluid, "Catch pull", [](SceneParams& p, Rng& r) { p.fluidCatchPull = drawn(r, ParamSlot::fluidCatchPull); }},
        {T::Fluid, "Catch radius", [](SceneParams& p, Rng& r) { p.fluidCatchRadius = drawn(r, ParamSlot::fluidCatchRadius); }},
        {T::Fluid, "Particle life (s)", [](SceneParams& p, Rng& r) { p.fluidParticleLife = drawn(r, ParamSlot::fluidParticleLife); }},
        {T::Fluid, "Particle drag", [](SceneParams& p, Rng& r) { p.fluidParticleDrag = drawn(r, ParamSlot::fluidParticleDrag); }},
        {T::Fluid, "Particle brightness",
         [](SceneParams& p, Rng& r) { p.fluidParticleBrightness = drawn(r, ParamSlot::fluidParticleBrightness); }},
        {T::Fluid, "Shading (embossed ink)", [](SceneParams& p, Rng& r) { p.fluidShading = r.chance(0.7f); }},
        {T::Fluid, "Glow (fluid)", [](SceneParams& p, Rng& r) { p.fluidBloom = r.chance(0.8f); }},
        {T::Fluid, "Fluid glow", [](SceneParams& p, Rng& r) { p.fluidBloomIntensity = drawn(r, ParamSlot::fluidBloomIntensity); }},
        {T::Fluid, "Glow threshold", [](SceneParams& p, Rng& r) { p.fluidBloomThreshold = drawn(r, ParamSlot::fluidBloomThreshold); }},
        {T::Fluid, "Sunrays", [](SceneParams& p, Rng& r) { p.fluidSunrays = r.chance(0.7f); }},
        {T::Fluid, "Sunrays weight", [](SceneParams& p, Rng& r) { p.fluidSunraysWeight = drawn(r, ParamSlot::fluidSunraysWeight); }},
        {T::Fluid, "Curl from mids", [](SceneParams& p, Rng& r) { p.fluidCurlAudio = drawn(r, ParamSlot::fluidCurlAudio); }},
        {T::Fluid, "Glow from loudness", [](SceneParams& p, Rng& r) { p.fluidBloomAudio = drawn(r, ParamSlot::fluidBloomAudio); }},
        {T::Fluid, "Fade when quiet", [](SceneParams& p, Rng& r) { p.fluidFadeAudio = drawn(r, ParamSlot::fluidFadeAudio); }},
        {T::Fluid, "Radius on hit", [](SceneParams& p, Rng& r) { p.fluidRadiusPulse = drawn(r, ParamSlot::fluidRadiusPulse); }},
        {T::Fluid, "Flow strength", [](SceneParams& p, Rng& r) { p.flowStrength = drawn(r, ParamSlot::flowStrength); }},
        {T::Fluid, "Flow force", [](SceneParams& p, Rng& r) { p.flowForce = drawn(r, ParamSlot::flowForce); }},
        {T::Fluid, "Flow curl", [](SceneParams& p, Rng& r) { p.flowCurl = drawn(r, ParamSlot::flowCurl); }},

        {T::Cymatics, "Geometry", [](SceneParams& p, Rng& r) { p.cymaticsGeometry = counted(r, ParamSlot::cymaticsGeometry); }},
        {T::Cymatics, "Fundamental (Hz)", [](SceneParams& p, Rng& r) { p.cymaticsFundamental = drawn(r, ParamSlot::cymaticsFundamental); }},
        {T::Cymatics, "Standing waves", [](SceneParams& p, Rng& r) { p.cymaticsModes = counted(r, ParamSlot::cymaticsModes); }},
        {T::Cymatics, "Tonal focus", [](SceneParams& p, Rng& r) { p.cymaticsFocus = drawn(r, ParamSlot::cymaticsFocus); }},
        {T::Cymatics, "Plate ring", [](SceneParams& p, Rng& r) { p.cymaticsRing = drawn(r, ParamSlot::cymaticsRing); }},
        {T::Cymatics, "Field scale", [](SceneParams& p, Rng& r) { p.cymaticsScale = drawn(r, ParamSlot::cymaticsScale); }},
        {T::Cymatics, "Wave flow", [](SceneParams& p, Rng& r) { p.cymaticsFlow = drawn(r, ParamSlot::cymaticsFlow); }},
        {T::Cymatics, "Field swirl", [](SceneParams& p, Rng& r) { p.cymaticsSwirl = drawn(r, ParamSlot::cymaticsSwirl); }},
        {T::Cymatics, "Fill", [](SceneParams& p, Rng& r) { p.cymaticsFill = drawn(r, ParamSlot::cymaticsFill); }},
        {T::Cymatics, "Nodal lines", [](SceneParams& p, Rng& r) { p.cymaticsLine = drawn(r, ParamSlot::cymaticsLine); }},
        {T::Cymatics, "Nodal glow", [](SceneParams& p, Rng& r) { p.cymaticsGlow = drawn(r, ParamSlot::cymaticsGlow); }},
        {T::Cymatics, "Iridescence", [](SceneParams& p, Rng& r) { p.cymaticsIridescence = drawn(r, ParamSlot::cymaticsIridescence); }},
        {T::Cymatics, "Caustic sheen", [](SceneParams& p, Rng& r) { p.cymaticsCaustic = drawn(r, ParamSlot::cymaticsCaustic); }},

        // The dive's tunnel, appended so every roll above draws the numbers it always drew. Blend's roll
        // range stays in its soft half (its meta row): a roll never deals the hard-edged frames.
        {T::Motion, "Ring spacing", [](SceneParams& p, Rng& r) { p.diveSpacing = drawn(r, ParamSlot::diveSpacing); }},
        {T::Motion, "Tunnel shape", [](SceneParams& p, Rng& r) { p.diveShape = drawn(r, ParamSlot::diveShape); }},
        {T::Motion, "Dive blend", [](SceneParams& p, Rng& r) { p.diveBlend = drawn(r, ParamSlot::diveBlend); }},
        {T::Motion, "Depth fade", [](SceneParams& p, Rng& r) { p.diveFog = maybe(r, 0.3f, ParamSlot::diveFog); }},
        {T::Motion, "Dive twist", [](SceneParams& p, Rng& r) { p.diveTwist = maybe(r, 0.35f, ParamSlot::diveTwist); }},
        {T::Motion, "Dive lens", [](SceneParams& p, Rng& r) { p.diveLens = maybe(r, 0.25f, ParamSlot::diveLens); }},
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
        for (const auto& roll : rolls()) {
            RolledKey key{roll.tab, roll.key, {}};
            // The fields are the live rows the key labels, in wire order (ParamTable.inc's meta rows).
            size_t n = 0;
            for (int slot = 0; slot < SceneParams::kFieldCount; ++slot) {
                const ParamMeta& m = SceneParams::meta(static_cast<ParamSlot>(slot));
                if (!m.live || std::strcmp(m.label, roll.key) != 0) continue;
                if (n < key.fields.size()) key.fields[n] = SceneParams::fieldNames()[static_cast<size_t>(slot)];
                ++n;
            }
            out.push_back(key);
        }
        return out;
    }();
    return kKeys;
}

const std::string& lockableKeysText() {
    static const std::string kText = [] {
        std::string out;
        std::set<std::string_view> seen;
        for (const auto& roll : rolledKeys()) {
            if (seen.insert(roll.key).second) out += std::string(roll.key) + "\n";
        }
        return out;
    }();
    return kText;
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
        {"marchDetail", "a performance budget, not a look"},
        {"camFlat", "how much the user wants flat styles to travel with the camera"},
        {"diveAimX", "where the user points the dive"},
        {"diveAimY", "where the user points the dive"},
        {"diveFollow", "how much the user wants the music to move things"},
        // The palette stack's amounts and position: the stack's own roll deals them with its palettes.
        {"paletteAmount0", "a palette stack amount, rolled with the stack"},
        {"paletteAmount1", "a palette stack amount, rolled with the stack"},
        {"paletteAmount2", "a palette stack amount, rolled with the stack"},
        {"paletteAmount3", "a palette stack amount, rolled with the stack"},
        {"paletteAmount4", "a palette stack amount, rolled with the stack"},
        {"paletteAmount5", "a palette stack amount, rolled with the stack"},
        {"paletteAmount6", "a palette stack amount, rolled with the stack"},
        {"paletteAmount7", "a palette stack amount, rolled with the stack"},
        {"paletteShift", "where along the palette stack, rolled with the stack"},
        {"camPush", "a modulation offset the reaction routes drive"},
        {"camYaw", "a modulation offset the reaction routes drive"},
        {"camPitch", "a modulation offset the reaction routes drive"},
        {"camRoll", "a modulation offset the reaction routes drive"},
        {"camSmooth", "a comfort preference, not a look"},
        {"camTouch", "an input preference, not a look"},
        {"camGyro", "an input preference, not a look"},
        {"reactAmount", "how much the user wants the music to move things"},
        {"motionDrift", "Spin: whether a turning style spins is the user's choice"},
        // Note & key reactions: how much each source drives, the user's, never a roll's.
        {"noteColor", "a Note & key amount: how much the user wants the notes to move things"},
        {"notePitch", "a Note & key amount: how much the user wants the notes to move things"},
        {"noteIntervals", "a Note & key amount: how much the user wants the notes to move things"},
        {"noteTension", "a Note & key amount: how much the user wants the notes to move things"},
        {"noteChords", "a Note & key amount: how much the user wants the notes to move things"},
        {"noteDrums", "a Note & key amount: how much the user wants the notes to move things"},
        // Liquid mode's pickers: the user's choice of what the liquid is, not a look to roll.
        {"liquidBorn", "a Liquid mode pick"},
        {"liquidAge", "a Liquid mode pick"},
        {"liquidSurface", "a Liquid mode pick"},
        // SceneParams::retired: no reader, so no control and no roll.
        {"pulse", "retired"},
        {"shake", "retired"},
        {"beatResponse", "retired"},
        {"flash", "retired"},
        {"glitch", "retired"},
        {"strobe", "retired"},
        // Water's knobs and the ripple overlay, retired when Pool replaced Water.
        {"waterWaveSpeed", "retired"},
        {"waterDamping", "retired"},
        {"waterRippleStrength", "retired"},
        {"waterDepth", "retired"},
        {"waterSpecular", "retired"},
        {"waterFlow", "retired"},
        {"waterLiquid", "retired"},
        {"waterLiquidFlow", "retired"},
        {"waterLiquidFade", "retired"},
        {"rippleOverlayEnabled", "retired"},
        {"rippleOverlayStrength", "retired"},
        {"rippleOverlaySpecular", "retired"},
        {"formDrive", "retired"},
        {"motionOrbit", "retired"},
        // The Kaleidoscope switch and its count, retired for kaleidoFolds (rolled under Kaleidoscope).
        {"symmetry", "retired"},
        {"kaleidoscope", "retired"},
        // Palette 2 and Palette blend, retired: the palette stack draws more than one palette.
        {"palette2", "retired"},
        {"paletteMix", "retired"},
        {"palette2BaseOverride", "retired"},
        {"palette2RangeOverride", "retired"},
        // Color cycle and Cycle speed, retired: the old hue cycle.
        {"colorCycle", "retired"},
        {"cycleSpeed", "retired"},
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
            if (tab && roll.tab != *tab) continue;
            if (!styleId.empty() && !catalogue::reaches(catalogue::scopeOf(roll.key), styleId)) continue;
            if (locked.count(roll.key)) continue;
            roll.roll(out, rng);
        }
        if (!sameWire(out, current)) return out;
    }
    return current;
}

SceneParams resetTab(const SceneParams& current, Tab tab) {
    const SceneParams defaults{};
    SceneParams out = current;
    for (const auto& roll : rolledKeys()) {
        if (roll.tab != tab) continue;
        for (const char* field : roll.fields) {
            if (field) out.set(field, defaults.get(field));
        }
    }
    return out;
}

}  // namespace geode::viz::randomizer
