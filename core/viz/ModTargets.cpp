#include "viz/ModTargets.hpp"

#include <array>
#include <cstdio>

#include "viz/ReactionDna.hpp"

namespace geode::viz::modtargets {

namespace {

using catalogue::Scope;

struct TargetRow {
    const char* name;
    Scope scope;
};

// One row per LfoTarget, in wire order. A target's scope is the scope of the field it writes: the
// camera rig's and the composite's offsets are Universal (every family is seen through them), the
// shader look only reaches the fragment styles, the fluid and water knobs only their own scenes.
constexpr std::array<TargetRow, kTargetCount> kTargets{{
    {"NONE", Scope::Universal},
    {"SPEED", Scope::SceneClock},
    {"ZOOM", Scope::Universal},
    {"ROTATION", Scope::Universal},
    {"SWAY", Scope::Universal},
    {"PULSE", Scope::Universal},
    {"DRIFT_X", Scope::Universal},
    {"DRIFT_Y", Scope::Universal},
    {"WARP", Scope::Universal},
    {"RIPPLE", Scope::Universal},
    {"MORPH", Scope::ShaderLook},
    {"TWIST", Scope::Universal},
    {"TILE", Scope::Universal},
    {"PIXELATE", Scope::Universal},
    {"POSTERIZE", Scope::Universal},
    {"COLOR_SHIFT", Scope::Universal},
    {"PALETTE_MIX", Scope::ShaderLook},
    {"SATURATION", Scope::Universal},
    {"BRIGHTNESS", Scope::Universal},
    {"INTENSITY", Scope::Universal},
    {"BLOOM", Scope::Universal},
    {"TEMPERATURE", Scope::Universal},
    {"TURBULENCE", Scope::Turbulence},
    {"CHROMA_AB", Scope::Universal},
    {"VIGNETTE", Scope::Universal},
    {"RETIRED_25", Scope::Universal},  // was GLITCH; a saved "GLITCH" no longer resolves
    {"FISHEYE", Scope::Universal},
    {"PARTICLE_SIZE", Scope::ParticleSprite},
    {"TRAIL_LENGTH", Scope::TrailLength},
    {"FLUID_CURL", Scope::FluidSim},
    {"FLUID_RADIUS", Scope::Emitters},
    {"FLUID_FORCE", Scope::Emitters},
    {"FLUID_GLOW", Scope::FluidSim},
    {"FLUID_FADE", Scope::FluidSim},
    {"FLUID_CATCH_PULL", Scope::Journey},
    {"FLUID_CATCH_RADIUS", Scope::Journey},
    {"FLOW_STRENGTH", Scope::Universal},
    {"WATER_RIPPLE", Scope::Water},
    {"RIPPLE_OVERLAY", Scope::RippleOverlay},
    {"LFO1_RATE", Scope::Universal},
    {"LFO1_DEPTH", Scope::Universal},
    {"LFO2_RATE", Scope::Universal},
    {"LFO2_DEPTH", Scope::Universal},
    {"LFO3_RATE", Scope::Universal},
    {"LFO3_DEPTH", Scope::Universal},
    {"CAM_FLY", Scope::Universal},
    {"CAM_ORBIT", Scope::Universal},
    {"CAM_PUSH", Scope::Universal},
    {"CAM_YAW", Scope::Universal},
    {"CAM_PITCH", Scope::Universal},
    {"CAM_ROLL", Scope::Universal},
    {"CAM_FOV", Scope::Universal},
    {"CAM_SHAKE", Scope::Universal},
    {"FLOW_CURL", Scope::Universal},
    {"FLOW_FORCE", Scope::Universal},
    {"DIVE_SPEED", Scope::Universal},  // LfoTarget::EndlessZoomSpeed
}};

constexpr std::array kSourceNames{"LFO", "BASS", "MID", "TREBLE", "LEVEL", "BRIGHTNESS", "TRANSIENT", "STEREO_WIDTH", "STEREO_PAN"};
constexpr std::array kPolarityNames{"BIPOLAR", "POSITIVE", "NEGATIVE"};
constexpr std::array kCurveNames{"LINEAR", "EXPONENTIAL", "LOGARITHMIC", "SMOOTH"};
constexpr std::array kWaveNames{"SINE", "TRIANGLE", "SAW", "SQUARE", "RANDOM", "RANDOM_GLIDE"};
static_assert(kSourceNames.size() == static_cast<size_t>(ModSource::StereoPan) + 1);
static_assert(kPolarityNames.size() == static_cast<size_t>(ModPolarity::Negative) + 1);
static_assert(kCurveNames.size() == static_cast<size_t>(ModCurve::Smooth) + 1);
static_assert(kWaveNames.size() == static_cast<size_t>(LfoWave::RandomGlide) + 1);

bool known(LfoTarget t) { return static_cast<int>(t) >= 0 && static_cast<int>(t) < kTargetCount; }

std::string num(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", static_cast<double>(v));
    return buf;
}

template <typename Names>
void enumRows(std::string& out, const char* tag, const Names& names) {
    for (size_t i = 0; i < names.size(); ++i) out += std::string(tag) + "|" + std::to_string(i) + "|" + names[i] + "\n";
}

std::string build() {
    std::string out = "modulation|" + std::to_string(kTextVersion) + "\n";
    const LfoConfig d{};
    out += "config|" + num(LfoConfig::kDefaultRateSeconds) + "|" + num(LfoConfig::kMinRateSeconds) + "|" +
           num(LfoConfig::kMaxRateSeconds) + "|" + num(d.depth) + "|" + std::to_string(LfoEngine::kSlots) + "\n";
    for (int i = 0; i < kTargetCount; ++i) {
        const auto t = static_cast<LfoTarget>(i);
        out += "target|" + std::to_string(i) + "|" + name(t) + "|" + catalogue::name(scopeOf(t)) + "|" +
               std::to_string(chainSlot(t)) + "|" + (offered(t) ? "1" : "0") + "\n";
    }
    for (size_t i = 0; i < kSourceNames.size(); ++i) {
        const auto s = static_cast<ModSource>(i);
        out += "source|" + std::to_string(i) + "|" + kSourceNames[i] + "|" +
               kPolarityNames[static_cast<size_t>(naturalPolarity(s))] + "\n";
    }
    enumRows(out, "polarity", kPolarityNames);
    enumRows(out, "curve", kCurveNames);
    enumRows(out, "wave", kWaveNames);
    return out;
}

}  // namespace

const char* name(LfoTarget target) { return known(target) ? kTargets[static_cast<size_t>(target)].name : ""; }

Scope scopeOf(LfoTarget target) { return known(target) ? kTargets[static_cast<size_t>(target)].scope : Scope::Universal; }

bool offered(LfoTarget target) { return known(target) && target != LfoTarget::Retired25; }

int chainSlot(LfoTarget target) {
    ModChain c{};
    return chainOf(target, c) ? c.slot : -1;
}

bool reaches(LfoTarget target, const std::string& styleId) {
    if (!offered(target)) return false;
    if (chainSlot(target) >= 0) return true;
    return catalogue::reaches(scopeOf(target), styleId);
}

std::uint32_t musicActionsFor(const std::string& styleId) {
    std::uint32_t bits = 0;
    for (int a = 0; a < kMusicActionCount; ++a) {
        if (reaches(musicActionTarget(static_cast<MusicAction>(a)), styleId)) bits |= 1u << a;
    }
    return bits;
}

ModPolarity naturalPolarity(ModSource source) {
    return source == ModSource::Lfo || source == ModSource::StereoPan ? ModPolarity::Bipolar : ModPolarity::Positive;
}

const std::string& text() {
    static const std::string kText = build();
    return kText;
}

}  // namespace geode::viz::modtargets
