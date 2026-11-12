#include "viz/CataloguePresets.hpp"

#include <set>

#include "viz/Catalogue.hpp"
#include "viz/TableNumber.hpp"

namespace geode::viz::catalogue {
namespace {

// One value a look sets, by #346's param name. Ints and flags are written as numbers, as the wire
// carries them; SceneParams::set rounds an int and reads a flag as value > 0.5.
struct Value {
    const char* name;
    float value;
};

struct GenericLook {
    const char* name;
    float attack;
    float decay;
    std::vector<Value> values;
};

struct Variant {
    const char* name;
    const char* sceneId;
    float attack;
    float decay;
    std::vector<Value> values;
};

// The twelve generic looks, in the order each style lists them. Every loose shader style offers all
// twelve, named "<style id> · <name>".
const std::vector<GenericLook>& genericLooks() {
    static const std::vector<GenericLook> kLooks = {
        {"Chill", 0.4f, 0.06f, {{"speed", 0.5f}, {"audioDrive", 0.85f}, {"trails", 1},
            {"trailLength", 0.8f}, {"saturation", 0.8f}, {"brightness", 0.9f}, {"colorCycle", 1}, {"cycleSpeed", 0.025f},
            {"palette", 3}, {"sway", 0.35f}, {"vignette", 0.4f}, {"paramFadeSec", 1.5f}}},
        {"Punchy", 0.85f, 0.24f, {{"speed", 1.4f}, {"audioDrive", 1.6f}, {"bloom", 0.5f},
            {"contrast", 1.2f}, {"intensity", 1.15f}, {"palette", 1}, {"bassGain", 1.4f}}},
        {"Hypno", 0.5f, 0.1f, {{"endlessZoom", 1}, {"endlessZoomSpeed", 0.35f}, {"kaleidoFolds", 6.0f},
            {"colorCycle", 1}, {"cycleSpeed", 0.07f}, {"turbulence", 0.35f}, {"palette", 7}, {"warp", 0.3f}}},
        {"Vivid", 0.6f, 0.12f, {{"saturation", 1.35f}, {"intensity", 1.2f}, {"bloom", 0.4f}, {"palette", 5},
            {"hueRange", 1.25f}, {"warp", 0.25f}, {"chromaAb", 0.3f}}},
        {"Retro", 0.55f, 0.14f, {{"speed", 0.9f}, {"palette", 2}, {"scanlines", 0.6f}, {"grain", 0.35f}, {"vignette", 0.5f},
            {"posterize", 0.4f}, {"contrast", 1.1f}, {"saturation", 1.1f}}},
        {"Glitch", 0.9f, 0.3f, {{"speed", 1.2f}, {"audioDrive", 1.4f}, {"chromaAb", 0.5f},
            {"palette", 8}, {"intensity", 1.1f}, {"trebGain", 1.5f}}},
        {"Dream", 0.35f, 0.05f, {{"speed", 0.45f}, {"trails", 1}, {"trailLength", 0.85f}, {"bloom", 0.55f},
            {"fisheye", 0.4f}, {"saturation", 0.9f}, {"brightness", 1.05f}, {"palette", 9},
            {"driftX", 0.15f}, {"sway", 0.4f}}},
        {"Warp", 0.6f, 0.16f, {{"speed", 1.1f}, {"zoom", 1.1f}, {"twist", 0.5f}, {"warp", 0.5f}, {"morph", 0.4f},
            {"turbulence", 0.6f}, {"palette", 4}}},
        {"Prism", 0.5f, 0.12f, {{"kaleidoFolds", 8.0f}, {"palette", 5},
            {"saturation", 1.2f}, {"bloom", 0.35f}, {"rotation", 0.2f}, {"tile", 2.0f}}},
        {"Noir", 0.55f, 0.1f, {{"palette", 4}, {"saturation", 0.15f}, {"contrast", 1.25f}, {"vignette", 0.6f},
            {"grain", 0.4f}, {"duotone", 1}, {"brightness", 1.0f}}},
        {"Strobe", 0.95f, 0.35f, {{"speed", 1.5f}, {"audioDrive", 1.7f}, {"bloom", 0.45f},
            {"palette", 1}, {"intensity", 1.2f}, {"bassGain", 1.5f}}},
        {"Deep", 0.4f, 0.07f, {{"endlessZoom", 1}, {"endlessZoomSpeed", 0.5f}, {"zoom", 1.05f}, {"palette", 9},
            {"saturation", 1.0f}, {"bloom", 0.4f}, {"vignette", 0.45f}, {"trails", 1}, {"trailLength", 0.6f}}},
    };
    return kLooks;
}

// The hand-tuned variants, each on one style.
const std::vector<Variant>& variants() {
    static const std::vector<Variant> kVariants = {
        {"orb_lattice · Deep Sea", "orb_lattice", 0.45f, 0.1f, {{"palette", 3},
            {"speed", 0.7f}, {"audioDrive", 1.2f}, {"bloom", 0.45f},
            {"chromaAb", 0.2f}, {"vignette", 0.35f}, {"paramFadeSec", 1.0f}}},
        {"rod_tunnel · Bead Rush", "rod_tunnel", 0.7f, 0.18f, {{"palette", 1},
            {"speed", 1.1f}, {"marchDetail", 1.2f}, {"audioDrive", 1.3f}, {"bloom", 0.6f},
            {"contrast", 1.1f}, {"bassGain", 1.3f}}},
        {"neon_tiles · Arcade", "neon_tiles", 0.55f, 0.14f, {{"palette", 12},
            {"speed", 0.8f}, {"audioDrive", 1.15f}, {"kaleidoFolds", 8.0f},
            {"bloom", 0.5f}, {"posterize", 0.15f}, {"scanlines", 0.15f}, {"saturation", 1.2f}}},
        {"chroma_orb · Lens", "chroma_orb", 0.5f, 0.12f, {{"speed", 0.8f}, {"audioDrive", 1.2f},
            {"bloom", 0.3f}, {"contrast", 1.1f}, {"vignette", 0.3f}}},
        {"mandala_dome · Cathedral", "mandala_dome", 0.45f, 0.12f, {{"speed", 0.7f}, {"audioDrive", 1.1f},
            {"bloom", 0.35f}, {"saturation", 1.1f}, {"contrast", 1.05f}}},
        {"curl_bloom · Stirred", "curl_bloom", 0.5f, 0.15f, {{"speed", 0.7f}, {"audioDrive", 1.15f},
            {"marchDetail", 1.0f}, {"bloom", 0.35f}, {"contrast", 1.1f}}},
        {"nectar_flow · Through It", "nectar_flow", 0.6f, 0.18f, {{"speed", 0.85f}, {"audioDrive", 1.25f},
            {"marchDetail", 1.1f}, {"bloom", 0.45f}, {"saturation", 1.15f}}},
        {"merkaba_grid · Sanctum", "merkaba_grid", 0.5f, 0.14f, {{"speed", 0.7f}, {"audioDrive", 1.15f},
            {"bloom", 0.4f}, {"contrast", 1.1f}, {"vignette", 0.35f}}},
        {"spiral_eye · Event Horizon", "spiral_eye", 0.55f, 0.16f, {{"speed", 0.9f}, {"audioDrive", 1.3f},
            {"bloom", 0.45f}, {"contrast", 1.15f}, {"bassGain", 1.2f}}},
        {"blacklight_bloom · UV Ink", "blacklight_bloom", 0.45f, 0.12f, {{"speed", 0.75f}, {"audioDrive", 1.15f},
            {"bloom", 0.35f}, {"saturation", 1.25f}, {"contrast", 1.1f}}},
        {"fractal_temple · Causeway", "fractal_temple", 0.5f, 0.14f, {{"speed", 0.8f}, {"audioDrive", 1.2f},
            {"bloom", 0.4f}, {"saturation", 1.1f}, {"vignette", 0.3f}}},
        {"bead_vortex · Corkscrew", "bead_vortex", 0.6f, 0.15f, {{"speed", 1.1f}, {"audioDrive", 1.25f},
            {"bloom", 0.4f}, {"contrast", 1.1f}, {"bassGain", 1.2f}}},
        {"winter · Flurry", "winter", 0.5f, 0.12f, {{"palette", 9}, {"speed", 0.8f},
            {"sway", 0.4f}, {"audioDrive", 1.1f}, {"saturation", 0.85f}, {"brightness", 1.05f},
            {"bloom", 0.35f}, {"vignette", 0.3f}, {"flowEnabled", 1}, {"flowStrength", 0.55f}, {"flowCurl", 30.0f},
            {"paramFadeSec", 1.0f}}},
        {"lava · Lamp", "lava", 0.3f, 0.08f, {{"palette", 1}, {"speed", 0.55f},
            {"audioDrive", 1.0f}, {"saturation", 1.15f}, {"brightness", 1.05f}, {"bloom", 0.45f},
            {"vignette", 0.45f}, {"trails", 1}, {"trailLength", 0.5f}, {"paramFadeSec", 2.0f}}},
        {"fluid · Inkdrop", "fluid", 0.35f, 0.5f, {{"fluidBeatPattern", 0}, {"fluidBeatSplats", 2}, {"fluidStirrers", 1},
            {"fluidStirrerSpeed", 0.5f}, {"fluidCurl", 20.0f}, {"fluidDensityDissipation", 0.35f},
            {"fluidChromaticAging", 0.45f}, {"fluidSplatRadius", 0.16f}, {"fluidBloomIntensity", 0.5f},
            {"fluidParticlesEnabled", 0}, {"palette", 5}, {"fluidSpawnPath", 4}, {"fluidSpawnPoints", 1},
            {"fluidCatchPoints", 0}}},
        {"fluid · Vortex", "fluid", 0.5f, 0.4f, {{"fluidBeatPattern", 1}, {"fluidBeatSplats", 4}, {"fluidStirrers", 2},
            {"fluidStirrerSpeed", 1.4f}, {"fluidCurl", 45.0f}, {"fluidChromaticAging", 0.5f}, {"fluidParticlesEnabled", 1},
            {"fluidParticleDrag", 0.35f}, {"palette", 2}, {"fluidSpawnPath", 0}, {"fluidSpawnPoints", 3},
            {"fluidCatchPoints", 1}, {"fluidCatchPull", 1.6f}, {"fluidCatchRadius", 0.1f}}},
        {"fluid · Spectrum", "fluid", 0.6f, 0.35f, {{"fluidBeatPattern", 3}, {"fluidBeatSplats", 6}, {"fluidStirrers", 0},
            {"fluidBassPump", 1}, {"fluidSunrays", 1}, {"fluidSunraysWeight", 1.2f}, {"fluidFadeAudio", 1.0f},
            {"fluidPaletteCycleSpeed", 1.2f}, {"colorCycle", 1}, {"cycleSpeed", 0.05f}, {"palette", 3},
            {"fluidSpawnPath", 1}, {"fluidSpawnPoints", 4}, {"fluidCatchPoints", 0}}},
        {"fluid · Nebula", "fluid", 0.4f, 0.55f, {{"fluidDyeEnabled", 0}, {"fluidParticlesEnabled", 1},
            {"fluidParticleDrag", 0.6f}, {"fluidParticleBrightness", 1.6f}, {"fluidCurl", 40.0f}, {"fluidStirrers", 3},
            {"fluidStirrerSpeed", 0.8f}, {"fluidBloomIntensity", 1.1f}, {"palette", 7}, {"fluidSpawnPath", 3},
            {"fluidSpawnPoints", 5}, {"fluidParticleLife", 9.0f}, {"fluidCatchPoints", 2}, {"fluidCatchPull", 1.2f},
            {"fluidCatchRadius", 0.14f}}},
        {"fluid · Lava", "fluid", 0.25f, 0.7f, {{"fluidChromaticAging", 1.0f}, {"fluidCurl", 15.0f},
            {"fluidVelocityDissipation", 0.6f}, {"fluidDensityDissipation", 0.25f}, {"fluidSplatRadius", 0.22f},
            {"fluidSplatForce", 0.7f}, {"fluidBloomIntensity", 1.2f}, {"fluidBloomThreshold", 0.4f},
            {"fluidStirrerSpeed", 0.4f}, {"palette", 1}, {"fluidSpawnPath", 2}, {"fluidSpawnPoints", 2},
            {"fluidCatchPoints", 0}, {"fluidSpawnProgress", 0.6f}}},
        {"fluid · Storm", "fluid", 0.7f, 0.25f, {{"fluidBeatPattern", 2}, {"fluidBeatSplats", 5}, {"fluidStirrers", 4},
            {"fluidStirrerSpeed", 1.8f}, {"fluidCurl", 50.0f}, {"fluidBassPump", 1}, {"fluidParticlesEnabled", 1},
            {"fluidFadeAudio", 0.15f}, {"fluidSunrays", 1}, {"palette", 6}, {"fluidSpawnPath", 4}, {"fluidSpawnPoints", 6},
            {"fluidParticleLife", 3.5f}, {"fluidCatchPoints", 3}, {"fluidCatchPull", 2.2f}, {"fluidCatchRadius", 0.08f}}},
        {"fluid · Journey", "fluid", 0.45f, 0.45f, {{"fluidSpawnPath", 2}, {"fluidSpawnPoints", 4},
            {"fluidSpawnProgress", 1.0f}, {"fluidCatchPoints", 2}, {"fluidCatchPull", 1.4f}, {"fluidCatchRadius", 0.12f},
            {"fluidParticleLife", 7.0f}, {"fluidBeatPattern", 1}, {"fluidBeatSplats", 3}, {"fluidStirrers", 2},
            {"fluidChromaticAging", 0.4f}, {"fluidCurl", 35.0f}, {"colorCycle", 1}, {"cycleSpeed", 0.03f},
            {"palette", 16}}},
        // One preset per styled fluid look. Each names the style's id, so applying it switches to that look;
        // the params only lean on what the look already does (a wash wants slow dye, chrome wants a smooth
        // field, neon wants hits).
        {"fluid_ink · Sumi", "fluid_ink", 0.35f, 0.6f, {{"fluidBeatPattern", 2}, {"fluidBeatSplats", 2},
            {"fluidStirrers", 1}, {"fluidStirrerSpeed", 0.4f}, {"fluidCurl", 12.0f}, {"fluidDensityDissipation", 0.5f},
            {"fluidSplatRadius", 0.2f}, {"fluidParticlesEnabled", 0}, {"fluidSunrays", 0}, {"fluidSpawnPath", 4},
            {"fluidSpawnPoints", 2}, {"fluidCatchPoints", 0}, {"palette", 4}}},
        {"fluid_chrome · Mercury", "fluid_chrome", 0.3f, 0.5f, {{"fluidBeatPattern", 0}, {"fluidBeatSplats", 3},
            {"fluidStirrers", 2}, {"fluidStirrerSpeed", 0.7f}, {"fluidCurl", 18.0f}, {"fluidDensityDissipation", 0.4f},
            {"fluidSplatRadius", 0.22f}, {"fluidParticlesEnabled", 0}, {"fluidShading", 1}, {"fluidSpawnPath", 0},
            {"fluidSpawnPoints", 3}, {"fluidCatchPoints", 1}, {"fluidCatchPull", 1.2f}, {"palette", 3}}},
        {"fluid_neon · Pulse", "fluid_neon", 0.6f, 0.3f, {{"fluidBeatPattern", 1}, {"fluidBeatSplats", 5},
            {"fluidStirrers", 2}, {"fluidStirrerSpeed", 1.2f}, {"fluidCurl", 32.0f}, {"fluidBassPump", 1},
            {"fluidBloomIntensity", 1.2f}, {"fluidParticlesEnabled", 1}, {"fluidParticleBrightness", 1.3f},
            {"fluidSpawnPath", 1}, {"fluidSpawnPoints", 4}, {"fluidCatchPoints", 2}, {"palette", 1}}},
        {"fluid_lava · Magma", "fluid_lava", 0.25f, 0.7f, {{"fluidBeatPattern", 2}, {"fluidBeatSplats", 3},
            {"fluidStirrers", 1}, {"fluidStirrerSpeed", 0.35f}, {"fluidCurl", 10.0f}, {"fluidVelocityDissipation", 0.5f},
            {"fluidDensityDissipation", 0.3f}, {"fluidSplatRadius", 0.24f}, {"fluidBloomIntensity", 1.4f},
            {"fluidBloomThreshold", 0.45f}, {"fluidParticlesEnabled", 0}, {"fluidSpawnPath", 2}, {"fluidSpawnPoints", 2},
            {"fluidCatchPoints", 0}, {"palette", 2}}},
        {"fluid_aurora · Curtains", "fluid_aurora", 0.4f, 0.5f, {{"fluidBeatPattern", 3}, {"fluidBeatSplats", 6},
            {"fluidStirrers", 3}, {"fluidStirrerSpeed", 0.8f}, {"fluidCurl", 38.0f}, {"fluidDensityDissipation", 0.8f},
            {"fluidSunrays", 1}, {"fluidSunraysWeight", 1.1f}, {"fluidParticlesEnabled", 1}, {"fluidSpawnPath", 1},
            {"fluidSpawnPoints", 5}, {"fluidCatchPoints", 1}, {"palette", 0}}},
        {"pool · Lanes", "pool", 0.45f, 0.35f, {{"warp", 0.45f}, {"turbulence", 0.6f}, {"morph", 0.15f}, {"palette", 3}}},
        {"curlflow · Streams", "curlflow", 0.4f, 0.5f, {{"fluidSpawnPath", 1}, {"fluidSpawnPoints", 3},
            {"fluidCatchPoints", 2}, {"fluidCatchPull", 1.2f}, {"fluidCatchRadius", 0.12f}, {"fluidParticleLife", 8.0f},
            {"fluidParticleDrag", 0.4f}, {"turbulence", 0.7f}, {"audioDrive", 1.2f}, {"particleSize", 1.4f}, {"trails", 1},
            {"trailLength", 0.92f}, {"palette", 10}}},
        {"cymatics · CymaScope", "cymatics", 0.35f, 0.4f, {{"cymaticsGeometry", 0}, {"cymaticsFundamental", 110.0f},
            {"cymaticsModes", 5}, {"cymaticsFocus", 0.7f}, {"cymaticsRing", 0.35f}, {"cymaticsScale", 3.4f},
            {"cymaticsFill", 0.12f}, {"cymaticsLine", 1.0f}, {"cymaticsGlow", 1.1f}, {"cymaticsIridescence", 0.35f},
            {"cymaticsCaustic", 0.8f}, {"cymaticsFlow", 0.25f}, {"cymaticsSwirl", 0.04f}, {"palette", 6},
            {"hueRange", 0.5f}}},
        {"cymatics · Psychedelic", "cymatics", 0.3f, 0.5f, {{"cymaticsGeometry", 0}, {"cymaticsFundamental", 90.0f},
            {"cymaticsModes", 3}, {"cymaticsFocus", 0.85f}, {"cymaticsRing", 0.55f}, {"cymaticsScale", 3.0f},
            {"cymaticsFill", 1.0f}, {"cymaticsLine", 1.6f}, {"cymaticsGlow", 1.2f}, {"cymaticsIridescence", 1.0f},
            {"cymaticsCaustic", 1.0f}, {"cymaticsFlow", 0.5f}, {"cymaticsSwirl", 0.06f}, {"palette", 0}}},
        {"cymatics · Chladni plate", "cymatics", 0.35f, 0.45f, {{"cymaticsGeometry", 1}, {"cymaticsFundamental", 82.0f},
            {"cymaticsModes", 4}, {"cymaticsFocus", 0.9f}, {"cymaticsRing", 0.5f}, {"cymaticsScale", 1.15f},
            {"cymaticsFill", 0.3f}, {"cymaticsLine", 1.1f}, {"cymaticsGlow", 0.9f}, {"cymaticsIridescence", 0.3f},
            {"cymaticsCaustic", 0.7f}, {"cymaticsFlow", 0.15f}, {"cymaticsSwirl", 0.0f}, {"palette", 4},
            {"hueRange", 0.6f}}},
    };
    return kVariants;
}

SceneParams paramsOf(const std::vector<Value>& values) {
    SceneParams out{};
    for (const Value& v : values) out.set(v.name, v.value);
    return out;
}

// The loose shader styles in picker order: shader styles outside the style families.
std::vector<std::string> looseShaderIds() {
    std::set<std::string> familyIds;
    for (const auto& family : families()) familyIds.insert(family.ids.begin(), family.ids.end());
    std::vector<std::string> out;
    for (const auto& id : allIds()) {
        if (kindOf(id) == Kind::Shader && !familyIds.count(id)) out.push_back(id);
    }
    return out;
}

std::string lookRow(const char* key, float attack, float decay, const std::vector<Value>& values) {
    std::string out = std::string("look|") + key + "|" + tableNumber(attack) + "|" + tableNumber(decay) + "|";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) out += ',';
        out += std::string(values[i].name) + "=" + tableNumber(values[i].value);
    }
    return out + "\n";
}

std::string build() {
    const auto loose = looseShaderIds();
    std::string looks;
    std::string rows;
    size_t lookCount = 0;
    size_t presetCount = 0;
    for (const auto& look : genericLooks()) {
        looks += lookRow(look.name, look.attack, look.decay, look.values);
        ++lookCount;
    }
    for (const auto& id : loose) {
        for (const auto& look : genericLooks()) {
            rows += "preset|" + id + " · " + look.name + "|" + id + "|" + look.name + "\n";
            ++presetCount;
        }
    }
    for (const auto& v : variants()) {
        rows += lookRow(v.name, v.attack, v.decay, v.values);
        rows += std::string("preset|") + v.name + "|" + v.sceneId + "|" + v.name + "\n";
        ++lookCount;
        ++presetCount;
    }
    return "presets|" + std::to_string(kPresetsTextVersion) + "|" + std::to_string(lookCount) + "|" +
           std::to_string(presetCount) + "\n" + looks + rows;
}

}  // namespace

const std::vector<Preset>& presets() {
    static const std::vector<Preset> kPresets = [] {
        std::vector<Preset> out;
        for (const auto& id : looseShaderIds()) {
            for (const auto& look : genericLooks()) {
                out.push_back({id + " · " + look.name, id, look.attack, look.decay, paramsOf(look.values)});
            }
        }
        for (const auto& v : variants()) out.push_back({v.name, v.sceneId, v.attack, v.decay, paramsOf(v.values)});
        return out;
    }();
    return kPresets;
}

const std::string& presetsText() {
    static const std::string kText = build();
    return kText;
}

}  // namespace geode::viz::catalogue
