#include "viz/Params.hpp"

#include <algorithm>
#include <cmath>

namespace geode::viz {

const std::array<SceneParams::Palette, 21>& SceneParams::palettes() {
    static const std::array<Palette, 21> kPalettes = {{
        {"Spectrum", 0.0f, 1.0f},   {"Neon", 0.5f, 0.45f},    {"Fire", 0.0f, 0.14f},    {"Ocean", 0.5f, 0.2f},
        {"Mono", 0.6f, 0.02f},      {"Candy", 0.85f, 0.5f},   {"Forest", 0.33f, 0.18f}, {"Aurora", 0.45f, 0.7f},
        {"Sunset", 0.05f, 0.3f},    {"Ice", 0.55f, 0.15f},    {"Vapor", 0.78f, 0.35f},  {"Toxic", 0.25f, 0.25f},
        {"Royal", 0.7f, 0.25f},     {"Blush", 0.93f, 0.12f},  {"Copper", 0.07f, 0.1f},  {"Mint", 0.4f, 0.12f},
        {"Galaxy", 0.65f, 0.5f},    {"Cherry", 0.97f, 0.08f}, {"Cyan", 0.5f, 0.08f},    {"Magenta", 0.833f, 0.08f},
        {"Yellow", 0.167f, 0.08f},
    }};
    return kPalettes;
}

namespace {
const SceneParams::Palette& paletteAt(int index) {
    const auto& table = SceneParams::palettes();
    return table[static_cast<size_t>(std::clamp(index, 0, static_cast<int>(table.size()) - 1))];
}
}  // namespace

float SceneParams::paletteBase() const { return paletteBaseOverride >= 0.0f ? paletteBaseOverride : paletteAt(palette).base; }
float SceneParams::paletteRange() const { return paletteRangeOverride >= 0.0f ? paletteRangeOverride : paletteAt(palette).range; }
float SceneParams::palette2Base() const { return palette2BaseOverride >= 0.0f ? palette2BaseOverride : paletteAt(palette2).base; }
float SceneParams::palette2Range() const { return palette2RangeOverride >= 0.0f ? palette2RangeOverride : paletteAt(palette2).range; }

const std::array<SceneParams::FloatField, 98>& SceneParams::lerpedFloats() {
    static const std::array<FloatField, 98> kFields = {{
        {"speed", &SceneParams::speed},
        {"zoom", &SceneParams::zoom},
        {"rotation", &SceneParams::rotation},
        {"endlessZoomSpeed", &SceneParams::endlessZoomSpeed},
        {"sway", &SceneParams::sway},
        {"pulse", &SceneParams::pulse},
        {"driftX", &SceneParams::driftX},
        {"driftY", &SceneParams::driftY},
        {"shake", &SceneParams::shake},
        {"audioDrive", &SceneParams::audioDrive},
        {"beatResponse", &SceneParams::beatResponse},
        {"turbulence", &SceneParams::turbulence},
        {"density", &SceneParams::density},
        {"marchDetail", &SceneParams::marchDetail},
        {"trailLength", &SceneParams::trailLength},
        {"trailZoom", &SceneParams::trailZoom},
        {"trailWarp", &SceneParams::trailWarp},
        {"warp", &SceneParams::warp},
        {"ripple", &SceneParams::ripple},
        {"morph", &SceneParams::morph},
        {"pixelate", &SceneParams::pixelate},
        {"posterize", &SceneParams::posterize},
        {"particleSize", &SceneParams::particleSize},
        {"tile", &SceneParams::tile},
        {"twist", &SceneParams::twist},
        {"paletteMix", &SceneParams::paletteMix},
        {"milkdropPaletteTint", &SceneParams::milkdropPaletteTint},
        {"colorShift", &SceneParams::colorShift},
        {"hueRange", &SceneParams::hueRange},
        {"saturation", &SceneParams::saturation},
        {"brightness", &SceneParams::brightness},
        {"contrast", &SceneParams::contrast},
        {"gamma", &SceneParams::gamma},
        {"cycleSpeed", &SceneParams::cycleSpeed},
        {"intensity", &SceneParams::intensity},
        {"bloom", &SceneParams::bloom},
        {"temperature", &SceneParams::temperature},
        {"bassGain", &SceneParams::bassGain},
        {"midGain", &SceneParams::midGain},
        {"trebGain", &SceneParams::trebGain},
        {"flash", &SceneParams::flash},
        {"chromaAb", &SceneParams::chromaAb},
        {"vignette", &SceneParams::vignette},
        {"scanlines", &SceneParams::scanlines},
        {"grain", &SceneParams::grain},
        {"glitch", &SceneParams::glitch},
        {"fisheye", &SceneParams::fisheye},
        {"strobe", &SceneParams::strobe},
        {"fluidPressure", &SceneParams::fluidPressure},
        {"fluidCurl", &SceneParams::fluidCurl},
        {"fluidVelocityDissipation", &SceneParams::fluidVelocityDissipation},
        {"fluidDensityDissipation", &SceneParams::fluidDensityDissipation},
        {"fluidChromaticAging", &SceneParams::fluidChromaticAging},
        {"fluidSplatRadius", &SceneParams::fluidSplatRadius},
        {"fluidSplatForce", &SceneParams::fluidSplatForce},
        {"fluidStirrerSpeed", &SceneParams::fluidStirrerSpeed},
        {"fluidPaletteCycleSpeed", &SceneParams::fluidPaletteCycleSpeed},
        {"fluidSpawnProgress", &SceneParams::fluidSpawnProgress},
        {"fluidCatchPull", &SceneParams::fluidCatchPull},
        {"fluidCatchRadius", &SceneParams::fluidCatchRadius},
        {"fluidParticleLife", &SceneParams::fluidParticleLife},
        {"fluidParticleDrag", &SceneParams::fluidParticleDrag},
        {"fluidParticleBrightness", &SceneParams::fluidParticleBrightness},
        {"fluidBloomIntensity", &SceneParams::fluidBloomIntensity},
        {"fluidBloomThreshold", &SceneParams::fluidBloomThreshold},
        {"fluidSunraysWeight", &SceneParams::fluidSunraysWeight},
        {"fluidCurlAudio", &SceneParams::fluidCurlAudio},
        {"fluidBloomAudio", &SceneParams::fluidBloomAudio},
        {"fluidFadeAudio", &SceneParams::fluidFadeAudio},
        {"fluidRadiusPulse", &SceneParams::fluidRadiusPulse},
        {"flowStrength", &SceneParams::flowStrength},
        {"flowForce", &SceneParams::flowForce},
        {"flowCurl", &SceneParams::flowCurl},
        {"waterWaveSpeed", &SceneParams::waterWaveSpeed},
        {"waterDamping", &SceneParams::waterDamping},
        {"waterRippleStrength", &SceneParams::waterRippleStrength},
        {"waterDepth", &SceneParams::waterDepth},
        {"waterSpecular", &SceneParams::waterSpecular},
        {"waterFlow", &SceneParams::waterFlow},
        {"waterLiquid", &SceneParams::waterLiquid},
        {"waterLiquidFlow", &SceneParams::waterLiquidFlow},
        {"waterLiquidFade", &SceneParams::waterLiquidFade},
        {"cymaticsFundamental", &SceneParams::cymaticsFundamental},
        {"cymaticsRing", &SceneParams::cymaticsRing},
        {"cymaticsFocus", &SceneParams::cymaticsFocus},
        {"cymaticsScale", &SceneParams::cymaticsScale},
        {"cymaticsFill", &SceneParams::cymaticsFill},
        {"cymaticsLine", &SceneParams::cymaticsLine},
        {"cymaticsGlow", &SceneParams::cymaticsGlow},
        {"cymaticsIridescence", &SceneParams::cymaticsIridescence},
        {"cymaticsCaustic", &SceneParams::cymaticsCaustic},
        {"cymaticsFlow", &SceneParams::cymaticsFlow},
        {"cymaticsSwirl", &SceneParams::cymaticsSwirl},
        {"rippleOverlayStrength", &SceneParams::rippleOverlayStrength},
        {"rippleOverlaySpecular", &SceneParams::rippleOverlaySpecular},
    }};
    return kFields;
}

bool SceneParams::set(std::string_view name, float value) {
    struct IntField { const char* name; int SceneParams::*member; };
    struct BoolField { const char* name; bool SceneParams::*member; };
    static const IntField kInts[] = {
        {"symmetry", &SceneParams::symmetry},
        {"particleShape", &SceneParams::particleShape},
        {"palette", &SceneParams::palette},
        {"palette2", &SceneParams::palette2},
        {"paletteLut", &SceneParams::paletteLut},
        {"fluidQuality", &SceneParams::fluidQuality},
        {"fluidIterations", &SceneParams::fluidIterations},
        {"fluidBeatPattern", &SceneParams::fluidBeatPattern},
        {"fluidBeatSplats", &SceneParams::fluidBeatSplats},
        {"fluidStirrers", &SceneParams::fluidStirrers},
        {"fluidSpawnPath", &SceneParams::fluidSpawnPath},
        {"fluidSpawnPoints", &SceneParams::fluidSpawnPoints},
        {"fluidCatchPoints", &SceneParams::fluidCatchPoints},
        {"cymaticsGeometry", &SceneParams::cymaticsGeometry},
        {"cymaticsModes", &SceneParams::cymaticsModes},
    };
    static const BoolField kBools[] = {
        {"endlessZoom", &SceneParams::endlessZoom},
        {"trails", &SceneParams::trails},
        {"mirror", &SceneParams::mirror},
        {"kaleidoscope", &SceneParams::kaleidoscope},
        {"milkdropBlendPresets", &SceneParams::milkdropBlendPresets},
        {"colorCycle", &SceneParams::colorCycle},
        {"invert", &SceneParams::invert},
        {"duotone", &SceneParams::duotone},
        {"solarize", &SceneParams::solarize},
        {"fluidAutoQuality", &SceneParams::fluidAutoQuality},
        {"fluidBassPump", &SceneParams::fluidBassPump},
        {"fluidSparkle", &SceneParams::fluidSparkle},
        {"fluidParticlesEnabled", &SceneParams::fluidParticlesEnabled},
        {"fluidDyeEnabled", &SceneParams::fluidDyeEnabled},
        {"fluidShading", &SceneParams::fluidShading},
        {"fluidBloom", &SceneParams::fluidBloom},
        {"fluidSunrays", &SceneParams::fluidSunrays},
        {"flowEnabled", &SceneParams::flowEnabled},
        {"rippleOverlayEnabled", &SceneParams::rippleOverlayEnabled},
    };
    static const FloatField kAllFloats[] = {
        {"speed", &SceneParams::speed},
        {"zoom", &SceneParams::zoom},
        {"rotation", &SceneParams::rotation},
        {"endlessZoomSpeed", &SceneParams::endlessZoomSpeed},
        {"sway", &SceneParams::sway},
        {"pulse", &SceneParams::pulse},
        {"driftX", &SceneParams::driftX},
        {"driftY", &SceneParams::driftY},
        {"shake", &SceneParams::shake},
        {"audioDrive", &SceneParams::audioDrive},
        {"beatResponse", &SceneParams::beatResponse},
        {"turbulence", &SceneParams::turbulence},
        {"density", &SceneParams::density},
        {"marchDetail", &SceneParams::marchDetail},
        {"trailLength", &SceneParams::trailLength},
        {"trailZoom", &SceneParams::trailZoom},
        {"trailWarp", &SceneParams::trailWarp},
        {"warp", &SceneParams::warp},
        {"ripple", &SceneParams::ripple},
        {"morph", &SceneParams::morph},
        {"pixelate", &SceneParams::pixelate},
        {"posterize", &SceneParams::posterize},
        {"particleSize", &SceneParams::particleSize},
        {"tile", &SceneParams::tile},
        {"twist", &SceneParams::twist},
        {"paletteMix", &SceneParams::paletteMix},
        {"paletteBaseOverride", &SceneParams::paletteBaseOverride},
        {"paletteRangeOverride", &SceneParams::paletteRangeOverride},
        {"palette2BaseOverride", &SceneParams::palette2BaseOverride},
        {"palette2RangeOverride", &SceneParams::palette2RangeOverride},
        {"milkdropPaletteTint", &SceneParams::milkdropPaletteTint},
        {"colorShift", &SceneParams::colorShift},
        {"hueRange", &SceneParams::hueRange},
        {"saturation", &SceneParams::saturation},
        {"brightness", &SceneParams::brightness},
        {"contrast", &SceneParams::contrast},
        {"gamma", &SceneParams::gamma},
        {"cycleSpeed", &SceneParams::cycleSpeed},
        {"intensity", &SceneParams::intensity},
        {"bloom", &SceneParams::bloom},
        {"temperature", &SceneParams::temperature},
        {"bassGain", &SceneParams::bassGain},
        {"midGain", &SceneParams::midGain},
        {"trebGain", &SceneParams::trebGain},
        {"flash", &SceneParams::flash},
        {"chromaAb", &SceneParams::chromaAb},
        {"vignette", &SceneParams::vignette},
        {"scanlines", &SceneParams::scanlines},
        {"grain", &SceneParams::grain},
        {"glitch", &SceneParams::glitch},
        {"fisheye", &SceneParams::fisheye},
        {"strobe", &SceneParams::strobe},
        {"paramFadeSec", &SceneParams::paramFadeSec},
        {"fluidPressure", &SceneParams::fluidPressure},
        {"fluidCurl", &SceneParams::fluidCurl},
        {"fluidVelocityDissipation", &SceneParams::fluidVelocityDissipation},
        {"fluidDensityDissipation", &SceneParams::fluidDensityDissipation},
        {"fluidChromaticAging", &SceneParams::fluidChromaticAging},
        {"fluidSplatRadius", &SceneParams::fluidSplatRadius},
        {"fluidSplatForce", &SceneParams::fluidSplatForce},
        {"fluidStirrerSpeed", &SceneParams::fluidStirrerSpeed},
        {"fluidPaletteCycleSpeed", &SceneParams::fluidPaletteCycleSpeed},
        {"fluidSpawnProgress", &SceneParams::fluidSpawnProgress},
        {"fluidCatchPull", &SceneParams::fluidCatchPull},
        {"fluidCatchRadius", &SceneParams::fluidCatchRadius},
        {"fluidParticleLife", &SceneParams::fluidParticleLife},
        {"fluidParticleDrag", &SceneParams::fluidParticleDrag},
        {"fluidParticleBrightness", &SceneParams::fluidParticleBrightness},
        {"fluidBloomIntensity", &SceneParams::fluidBloomIntensity},
        {"fluidBloomThreshold", &SceneParams::fluidBloomThreshold},
        {"fluidSunraysWeight", &SceneParams::fluidSunraysWeight},
        {"fluidCurlAudio", &SceneParams::fluidCurlAudio},
        {"fluidBloomAudio", &SceneParams::fluidBloomAudio},
        {"fluidFadeAudio", &SceneParams::fluidFadeAudio},
        {"fluidRadiusPulse", &SceneParams::fluidRadiusPulse},
        {"flowStrength", &SceneParams::flowStrength},
        {"flowForce", &SceneParams::flowForce},
        {"flowCurl", &SceneParams::flowCurl},
        {"waterWaveSpeed", &SceneParams::waterWaveSpeed},
        {"waterDamping", &SceneParams::waterDamping},
        {"waterRippleStrength", &SceneParams::waterRippleStrength},
        {"waterDepth", &SceneParams::waterDepth},
        {"waterSpecular", &SceneParams::waterSpecular},
        {"waterFlow", &SceneParams::waterFlow},
        {"waterLiquid", &SceneParams::waterLiquid},
        {"waterLiquidFlow", &SceneParams::waterLiquidFlow},
        {"waterLiquidFade", &SceneParams::waterLiquidFade},
        {"cymaticsFundamental", &SceneParams::cymaticsFundamental},
        {"cymaticsRing", &SceneParams::cymaticsRing},
        {"cymaticsFocus", &SceneParams::cymaticsFocus},
        {"cymaticsScale", &SceneParams::cymaticsScale},
        {"cymaticsFill", &SceneParams::cymaticsFill},
        {"cymaticsLine", &SceneParams::cymaticsLine},
        {"cymaticsGlow", &SceneParams::cymaticsGlow},
        {"cymaticsIridescence", &SceneParams::cymaticsIridescence},
        {"cymaticsCaustic", &SceneParams::cymaticsCaustic},
        {"cymaticsFlow", &SceneParams::cymaticsFlow},
        {"cymaticsSwirl", &SceneParams::cymaticsSwirl},
        {"rippleOverlayStrength", &SceneParams::rippleOverlayStrength},
        {"rippleOverlaySpecular", &SceneParams::rippleOverlaySpecular},
    };
    for (const auto& f : kAllFloats) {
        if (name == f.name) { this->*f.member = value; return true; }
    }
    for (const auto& f : kInts) {
        if (name == f.name) { this->*f.member = static_cast<int>(std::lround(value)); return true; }
    }
    for (const auto& f : kBools) {
        if (name == f.name) { this->*f.member = value > 0.5f; return true; }
    }
    return false;
}

SceneParams lerpParams(const SceneParams& from, const SceneParams& to, float k) {
    SceneParams out = to;
    for (const auto& f : SceneParams::lerpedFloats()) {
        const float a = from.*f.member;
        out.*f.member = a + (to.*f.member - a) * k;
    }
    return out;
}

SceneParams blendParams(const SceneParams& a, const SceneParams& b, float t) {
    const float k = std::clamp(t, 0.0f, 1.0f);
    SceneParams out = k < 0.5f ? a : b;
    for (const auto& f : SceneParams::lerpedFloats()) {
        const float from = a.*f.member;
        out.*f.member = from + (b.*f.member - from) * k;
    }
    return out;
}

}  // namespace geode::viz
