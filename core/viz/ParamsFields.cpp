#include "viz/Params.hpp"

namespace geode::viz {

const std::array<const char*, 134>& SceneParams::fieldNames() {
    static const std::array<const char*, 134> kNames = {{
        "speed", "zoom", "rotation", "endlessZoom", "endlessZoomSpeed", "sway", "pulse", "driftX", "driftY", "shake",
        "audioDrive", "beatResponse", "turbulence", "density", "marchDetail", "trails", "trailLength", "trailZoom",
        "trailWarp", "mirror", "warp", "ripple", "symmetry", "kaleidoscope", "morph", "pixelate", "posterize",
        "particleShape", "particleSize", "tile", "twist", "palette", "palette2", "paletteMix", "paletteBaseOverride",
        "paletteRangeOverride", "palette2BaseOverride", "palette2RangeOverride", "paletteLut", "milkdropPaletteTint",
        "milkdropBlendPresets", "colorShift", "hueRange", "saturation", "brightness", "contrast", "gamma",
        "colorCycle", "cycleSpeed", "invert", "intensity", "duotone", "bloom", "temperature", "solarize", "bassGain",
        "midGain", "trebGain", "flash", "chromaAb", "vignette", "scanlines", "grain", "glitch", "fisheye", "strobe",
        "paramFadeSec", "fluidQuality", "fluidAutoQuality", "fluidIterations", "fluidPressure", "fluidCurl",
        "fluidVelocityDissipation", "fluidDensityDissipation", "fluidChromaticAging", "fluidSplatRadius",
        "fluidSplatForce", "fluidBeatPattern", "fluidBeatSplats", "fluidStirrers", "fluidStirrerSpeed",
        "fluidBassPump", "fluidPaletteCycleSpeed", "fluidSparkle", "fluidSpawnPath", "fluidSpawnPoints",
        "fluidSpawnProgress", "fluidCatchPoints", "fluidCatchPull", "fluidCatchRadius", "fluidParticlesEnabled",
        "fluidParticleLife", "fluidParticleDrag", "fluidParticleBrightness", "fluidDyeEnabled", "fluidShading",
        "fluidBloom", "fluidBloomIntensity", "fluidBloomThreshold", "fluidSunrays", "fluidSunraysWeight",
        "fluidCurlAudio", "fluidBloomAudio", "fluidFadeAudio", "fluidRadiusPulse", "flowEnabled", "flowStrength",
        "flowForce", "flowCurl", "waterWaveSpeed", "waterDamping", "waterRippleStrength", "waterDepth",
        "waterSpecular", "waterFlow", "waterLiquid", "waterLiquidFlow", "waterLiquidFade", "cymaticsGeometry",
        "cymaticsFundamental", "cymaticsModes", "cymaticsRing", "cymaticsFocus", "cymaticsScale", "cymaticsFill",
        "cymaticsLine", "cymaticsGlow", "cymaticsIridescence", "cymaticsCaustic", "cymaticsFlow", "cymaticsSwirl",
        "rippleOverlayEnabled", "rippleOverlayStrength", "rippleOverlaySpecular",
    }};
    return kNames;
}

}  // namespace geode::viz
