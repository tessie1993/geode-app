#pragma once
#include <string>
#include <vector>

namespace geode::viz::styles {

// Port of VisualStyleCatalog.kt: the per-family style tables the ids resolve through.
struct CymaticsStyle {
    const char* id;
    int shaderStyle;
    int geometryOverride;  // -1 = the user's cymaticsGeometry
    float scale = 1.0f;
    float fill = 1.0f;
    float line = 1.0f;
    float glow = 1.0f;
    float iridescence = 1.0f;
    float caustic = 1.0f;
    float flow = 1.0f;
    float swirl = 1.0f;
    float hueOffset = 0.0f;
    float hueSpan = 1.0f;
    int modeCap = 8;
};

struct SilkStyle {
    const char* id;
    int field;
    float flow = 1.0f;
    float fieldScale = 1.0f;
    float strokes = 1.0f;
    float elong = 1.0f;
    float decay = 0.985f;
    int fold = 0;
    float swirl = 0.25f;
    float exposure = 1.35f;
    float hueOffset = 0.0f;
    float hueSpan = 1.0f;
    float bBase = 0.17f;
    float bAmp = 0.05f;
    float bPeriod = 37.0f;
    float slabRate = 0.02f;
};

struct LifeStyle {
    const char* id;
    int rule;
    float dt;
    int core = 0;
    int growth = 0;
    float mu = 0.15f;
    float sigma = 0.017f;
    float radius = 13.0f;
    int rings = 1;
    float b1 = 1.0f;
    float b2 = 0.0f;
    float b3 = 0.0f;
    float feed = 0.0f;
    float kill = 0.0f;
    float aniso = 0.0f;
    int substeps = 1;
    int look = 0;
    float seedJitter = 9.0f;
    float hueOffset = 0.0f;
    float hueSpan = 1.0f;
};

struct AcidStyle {
    const char* id;
    int mode;
    int source;
    float zoom = 1.010f;
    float rotate = 0.0015f;
    float hueRate = 0.04f;
    float feedback = 0.955f;
    float modulate = 0.35f;
    float glitch = 0.0f;
    float overdrive = 0.0f;
    float liquid = 0.0f;
    float scanline = 0.0f;
    float curve = 0.0f;
    float saturation = 1.05f;
    float hueOffset = 0.0f;
    float hueSpan = 1.0f;
};

struct MycoStyle {
    const char* id;
    int agentRes = 192;
    float sensorDist = 9.0f;
    float sensorAngle = 0.3927f;
    float turnAngle = 0.7854f;
    float moveStep = 1.0f;
    float jitter = 0.06f;
    float deposit = 0.12f;
    float decay = 0.905f;
    float speciesMix = 0.0f;
    float selfA = 1.0f;
    float crossAb = 0.0f;
    float crossBa = 0.0f;
    float selfB = 1.0f;
    float snap = 0.0f;
    float reaim = 0.0f;
    float aniso = 0.0f;
    int look = 0;
    float exposure = 3.4f;
    float hueOffset = 0.0f;
    float hueSpan = 1.0f;
};

const CymaticsStyle* cymatics(const std::string& id);
const SilkStyle* silk(const std::string& id);
const LifeStyle* life(const std::string& id);
const AcidStyle* acid(const std::string& id);
const MycoStyle* myco(const std::string& id);

std::vector<std::string> cymaticsIds();
std::vector<std::string> silkIds();
std::vector<std::string> lifeIds();
std::vector<std::string> acidIds();
std::vector<std::string> mycoIds();

}  // namespace geode::viz::styles
