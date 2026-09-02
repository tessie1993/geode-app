#include "viz/scenes/StyleCatalog.hpp"

#include <array>
#include <cstring>

namespace geode::viz::styles {

namespace {

// Field order follows the struct; designated initialisers keep each entry readable.
constexpr std::array<CymaticsStyle, 11> kCymatics = {{
    {.id = "cymatics", .shaderStyle = 0, .geometryOverride = -1},
    {.id = "chladni_sand", .shaderStyle = 1, .geometryOverride = 1, .scale = 1.08f, .fill = 0.2f, .line = 1.28f, .glow = 0.58f,
     .iridescence = 0.18f, .caustic = 0.25f, .flow = 0.35f, .hueOffset = 0.06f, .hueSpan = 0.45f},
    {.id = "bessel_drum", .shaderStyle = 2, .geometryOverride = 0, .scale = 0.92f, .fill = 1.15f, .line = 0.88f, .glow = 0.85f,
     .iridescence = 0.45f, .caustic = 0.9f, .hueOffset = -0.04f, .hueSpan = 0.7f, .modeCap = 2},
    {.id = "harmonograph", .shaderStyle = 3, .geometryOverride = -1, .scale = 0.82f, .fill = 0.42f, .line = 1.3f, .glow = 1.25f,
     .iridescence = 0.8f, .caustic = 0.35f, .flow = 1.35f, .swirl = 1.4f, .hueOffset = 0.18f, .hueSpan = 1.6f},
    {.id = "faraday", .shaderStyle = 4, .geometryOverride = 0, .scale = 1.18f, .fill = 1.1f, .line = 0.78f, .glow = 1.08f,
     .iridescence = 0.65f, .caustic = 1.2f, .flow = 1.55f, .hueOffset = -0.12f, .hueSpan = 1.15f},
    {.id = "harmonic_shell", .shaderStyle = 5, .geometryOverride = 0, .scale = 0.72f, .fill = 1.2f, .line = 0.72f, .glow = 0.95f,
     .iridescence = 1.25f, .caustic = 1.3f, .swirl = 0.45f, .hueOffset = 0.1f, .hueSpan = 1.35f},
    {.id = "caustic_sheet", .shaderStyle = 6, .geometryOverride = -1, .scale = 1.05f, .fill = 1.28f, .line = 0.55f, .glow = 0.88f,
     .iridescence = 1.3f, .caustic = 1.65f, .flow = 1.2f, .hueOffset = -0.22f, .hueSpan = 0.5f, .modeCap = 4},
    {.id = "levitator", .shaderStyle = 7, .geometryOverride = 1, .scale = 0.88f, .fill = 0.45f, .line = 0.92f, .glow = 1.42f,
     .iridescence = 0.6f, .caustic = 0.72f, .flow = 0.55f, .hueOffset = 0.32f, .hueSpan = 0.8f, .modeCap = 3},
    {.id = "standing_chamber", .shaderStyle = 8, .geometryOverride = 1, .scale = 0.78f, .fill = 0.75f, .line = 0.92f, .glow = 1.18f,
     .iridescence = 0.5f, .caustic = 0.8f, .flow = 0.65f, .hueOffset = -0.3f, .hueSpan = 0.9f, .modeCap = 4},
    {.id = "rosensweig", .shaderStyle = 9, .geometryOverride = 0, .scale = 1.15f, .fill = 1.35f, .line = 0.65f, .glow = 1.0f,
     .iridescence = 0.35f, .caustic = 1.45f, .flow = 0.48f, .hueOffset = 0.45f, .hueSpan = 0.35f},
    {.id = "kundt_tube", .shaderStyle = 10, .geometryOverride = 1, .scale = 0.7f, .fill = 0.68f, .line = 1.25f, .glow = 0.9f,
     .iridescence = 0.35f, .caustic = 0.55f, .flow = 1.1f, .hueOffset = 0.03f, .hueSpan = 0.6f},
}};

constexpr std::array<SilkStyle, 10> kSilk = {{
    {.id = "silk_web", .field = 0},
    {.id = "silk_bloom", .field = 1, .flow = 0.85f, .fieldScale = 0.8f, .decay = 0.988f, .swirl = 0.15f, .hueOffset = 0.55f,
     .bBase = 0.16f, .bAmp = 0.05f, .bPeriod = 41.0f},
    {.id = "silk_weave", .field = 2, .flow = 1.15f, .fieldScale = 1.25f, .strokes = 1.3f, .elong = 0.7f, .decay = 0.975f,
     .hueOffset = 0.12f, .bBase = 0.16f, .bAmp = 0.045f, .bPeriod = 47.0f},
    {.id = "silk_shell", .field = 3, .flow = 0.9f, .fieldScale = 0.9f, .strokes = 0.8f, .elong = 1.4f, .decay = 0.987f, .swirl = 0.4f,
     .hueOffset = -0.18f, .bBase = 0.18f, .bAmp = 0.035f, .bPeriod = 38.0f},
    {.id = "silk_spiral", .field = 4, .flow = 1.2f, .fieldScale = 1.05f, .elong = 1.8f, .decay = 0.982f, .swirl = 0.5f,
     .hueOffset = 0.3f, .bBase = 0.17f, .bAmp = 0.045f, .bPeriod = 49.0f},
    {.id = "silk_fold", .field = 5, .fieldScale = 1.15f, .strokes = 1.2f, .decay = 0.98f, .hueOffset = 0.78f, .bBase = 0.2f,
     .bAmp = 0.04f, .bPeriod = 44.0f},
    {.id = "silk_hyper", .field = 6, .flow = 0.8f, .fieldScale = 0.75f, .elong = 2.2f, .decay = 0.99f, .hueOffset = 0.48f,
     .bBase = 0.22f, .bAmp = 0.05f, .bPeriod = 35.0f},
    {.id = "silk_resonance", .field = 7, .flow = 1.05f, .swirl = 0.3f, .hueOffset = 0.06f, .bBase = 0.19f, .bAmp = 0.04f,
     .bPeriod = 43.0f},
    {.id = "silk_curl", .field = 8, .flow = 1.3f, .fieldScale = 1.2f, .strokes = 1.4f, .elong = 0.9f, .decay = 0.978f,
     .hueOffset = 0.62f},
    {.id = "silk_pendulum", .field = 9, .flow = 0.95f, .fieldScale = 0.85f, .elong = 1.5f, .decay = 0.986f, .fold = 3, .swirl = 0.0f,
     .hueOffset = 0.9f},
}};

constexpr std::array<LifeStyle, 10> kLife = {{
    {.id = "life_orbium", .rule = 0, .dt = 0.1f, .mu = 0.15f, .sigma = 0.017f, .look = 0},
    {.id = "life_gyre", .rule = 0, .dt = 0.1f, .mu = 0.156f, .sigma = 0.0224f, .look = 3, .hueOffset = 0.5f},
    {.id = "life_helix", .rule = 0, .dt = 0.1f, .mu = 0.3f, .sigma = 0.0505f, .look = 2, .seedJitter = 7.0f, .hueOffset = 0.12f},
    {.id = "life_pulsar", .rule = 0, .dt = 0.1f, .mu = 0.38f, .sigma = 0.07f, .look = 0, .seedJitter = 6.0f, .hueOffset = 0.07f},
    {.id = "life_hydro", .rule = 0, .dt = 0.1f, .core = 1, .growth = 1, .mu = 0.26f, .sigma = 0.036f, .radius = 18.0f, .rings = 3,
     .b2 = 1.0f, .b3 = 1.0f, .look = 2, .seedJitter = 5.0f, .hueOffset = 0.4f},
    {.id = "life_bug", .rule = 0, .dt = 1.0f, .core = 2, .mu = 0.31f, .sigma = 0.049f, .look = 4, .seedJitter = 8.0f, .hueOffset = 0.85f},
    {.id = "life_mitosis", .rule = 1, .dt = 1.0f, .feed = 0.0367f, .kill = 0.0649f, .substeps = 4, .look = 0, .seedJitter = 11.0f,
     .hueOffset = 0.6f},
    {.id = "life_coral", .rule = 1, .dt = 1.0f, .feed = 0.0545f, .kill = 0.062f, .substeps = 5, .look = 2, .seedJitter = 8.0f,
     .hueOffset = 0.02f},
    {.id = "life_labyrinth", .rule = 1, .dt = 1.0f, .feed = 0.026f, .kill = 0.055f, .substeps = 5, .look = 1, .seedJitter = 7.0f,
     .hueOffset = 0.09f},
    {.id = "life_worms", .rule = 1, .dt = 1.0f, .feed = 0.078f, .kill = 0.061f, .substeps = 4, .look = 5, .seedJitter = 10.0f},
}};

constexpr std::array<AcidStyle, 10> kAcid = {{
    {.id = "acid_tv", .mode = 0, .source = 0, .glitch = 0.3f, .overdrive = 0.8f, .liquid = 0.8f, .scanline = 0.25f, .curve = 0.3f,
     .saturation = 1.15f},
    {.id = "acid_well", .mode = 1, .source = 1, .zoom = 1.035f, .rotate = 0.001f, .hueRate = 0.015f, .feedback = 0.965f,
     .modulate = 0.2f, .scanline = 0.45f, .curve = 0.5f, .saturation = 0.8f, .hueOffset = 0.33f},
    {.id = "acid_kaleid", .mode = 2, .source = 0, .zoom = 1.014f, .rotate = 0.002f, .hueRate = 0.05f, .feedback = 0.95f,
     .modulate = 0.5f, .liquid = 0.5f, .saturation = 1.1f},
    {.id = "acid_droste", .mode = 3, .source = 1, .zoom = 1.0f, .rotate = 0.0012f, .hueRate = 0.03f, .feedback = 0.96f,
     .modulate = 0.3f, .hueOffset = 0.72f},
    {.id = "acid_prism", .mode = 4, .source = 1, .zoom = 1.008f, .rotate = -0.0014f, .hueRate = 0.02f, .feedback = 0.96f,
     .hueOffset = 0.45f},
    {.id = "acid_mosh", .mode = 5, .source = 2, .zoom = 1.004f, .rotate = 0.0f, .hueRate = 0.06f, .feedback = 0.945f, .glitch = 1.0f,
     .overdrive = 0.5f, .hueOffset = 0.18f},
    {.id = "acid_scan", .mode = 6, .source = 2, .zoom = 1.006f, .rotate = 0.0f, .hueRate = 0.03f, .feedback = 0.95f, .glitch = 0.7f,
     .scanline = 0.8f, .curve = 0.6f, .hueOffset = 0.55f},
    {.id = "acid_solar", .mode = 7, .source = 1, .zoom = 1.012f, .rotate = 0.0018f, .hueRate = 0.08f, .feedback = 0.93f,
     .overdrive = 0.3f, .hueOffset = 0.06f},
    {.id = "acid_mirror", .mode = 8, .source = 0, .zoom = 1.009f, .rotate = 0.0008f, .hueRate = 0.035f, .feedback = 0.958f,
     .modulate = 0.45f, .hueOffset = 0.85f},
    {.id = "acid_smear", .mode = 9, .source = 3, .zoom = 1.005f, .rotate = 0.0005f, .hueRate = 0.045f, .feedback = 0.962f,
     .overdrive = 0.6f, .liquid = 0.3f, .hueOffset = 0.6f},
}};

constexpr std::array<MycoStyle, 10> kMyco = {{
    {.id = "myco_polycephalum", .reaim = 0.15f},
    {.id = "myco_rivals", .sensorDist = 11.0f, .speciesMix = 0.5f, .crossAb = -1.0f, .crossBa = -1.0f, .reaim = 0.2f, .look = 1,
     .exposure = 2.6f, .hueSpan = 1.2f},
    {.id = "myco_symbiosis", .sensorDist = 8.0f, .sensorAngle = 0.45f, .speciesMix = 0.5f, .crossAb = 0.35f, .crossBa = 0.35f,
     .reaim = 0.15f, .hueOffset = 0.1f},
    {.id = "myco_predator", .agentRes = 176, .sensorDist = 12.0f, .moveStep = 1.25f, .speciesMix = 0.35f, .selfA = 0.35f,
     .crossAb = 1.6f, .crossBa = -1.3f, .reaim = 0.25f, .look = 4, .hueOffset = 0.03f},
    {.id = "myco_ghosts", .agentRes = 160, .sensorDist = 14.0f, .jitter = 0.12f, .deposit = 0.05f, .decay = 0.955f, .look = 2,
     .exposure = 4.2f, .hueOffset = 0.58f},
    {.id = "myco_circuit", .agentRes = 176, .sensorAngle = 0.6f, .jitter = 0.0f, .deposit = 0.12f, .decay = 0.87f, .snap = 0.7854f,
     .look = 3, .exposure = 3.2f, .hueOffset = 0.35f},
    {.id = "myco_silkroad", .agentRes = 176, .sensorDist = 22.0f, .sensorAngle = 0.18f, .turnAngle = 0.12f, .moveStep = 1.35f,
     .jitter = 0.02f, .deposit = 0.10f, .decay = 0.93f, .look = 1, .hueOffset = -0.2f},
    {.id = "myco_sporestorm", .sensorDist = 8.0f, .deposit = 0.16f, .decay = 0.87f, .reaim = 0.5f, .hueOffset = 0.68f},
    {.id = "myco_capillary", .agentRes = 208, .sensorDist = 5.0f, .sensorAngle = 0.85f, .turnAngle = 1.1f, .moveStep = 0.75f,
     .deposit = 0.14f, .decay = 0.9f, .look = 5, .exposure = 3.8f, .hueOffset = 0.98f},
    {.id = "myco_frostvein", .agentRes = 176, .sensorDist = 10.0f, .sensorAngle = 0.35f, .turnAngle = 0.5f, .moveStep = 0.9f,
     .deposit = 0.12f, .decay = 0.915f, .aniso = 0.8f, .look = 1, .hueOffset = 0.52f},
}};

template <typename Table>
const typename Table::value_type* find(const Table& table, const std::string& id) {
    for (const auto& style : table) {
        if (id == style.id) return &style;
    }
    return nullptr;
}

template <typename Table>
std::vector<std::string> ids(const Table& table) {
    std::vector<std::string> out;
    out.reserve(table.size());
    for (const auto& style : table) out.emplace_back(style.id);
    return out;
}

}  // namespace

const CymaticsStyle* cymatics(const std::string& id) { return find(kCymatics, id); }
const SilkStyle* silk(const std::string& id) { return find(kSilk, id); }
const LifeStyle* life(const std::string& id) { return find(kLife, id); }
const AcidStyle* acid(const std::string& id) { return find(kAcid, id); }
const MycoStyle* myco(const std::string& id) { return find(kMyco, id); }

std::vector<std::string> cymaticsIds() { return ids(kCymatics); }
std::vector<std::string> silkIds() { return ids(kSilk); }
std::vector<std::string> lifeIds() { return ids(kLife); }
std::vector<std::string> acidIds() { return ids(kAcid); }
std::vector<std::string> mycoIds() { return ids(kMyco); }

}  // namespace geode::viz::styles
