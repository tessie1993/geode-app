#include "viz/ModTargets.hpp"

#include <array>

#include "viz/ReactionDna.hpp"
#include "viz/TableNumber.hpp"

namespace geode::viz::modtargets {

namespace {

using catalogue::Scope;

// One name per LfoTarget, in wire order: the name a saved wave, envelope or route carries. Which styles a
// target reaches and whether it is offered are not kept here: they are the meta row of the field it
// writes (viz/ParamTable.inc), so a target follows its Customize control.
constexpr std::array<const char*, kTargetCount> kTargetNames{{
    "NONE",
    "SPEED",
    "ZOOM",
    "ROTATION",
    "SWAY",
    "RETIRED_5",  // was PULSE; a saved "PULSE" no longer resolves
    "DRIFT_X",
    "DRIFT_Y",
    "WARP",
    "RIPPLE",
    "MORPH",
    "TWIST",
    "TILE",
    "PIXELATE",
    "POSTERIZE",
    "COLOR_SHIFT",
    "PALETTE_MIX",
    "SATURATION",
    "BRIGHTNESS",
    "INTENSITY",
    "BLOOM",
    "TEMPERATURE",
    "TURBULENCE",
    "CHROMA_AB",
    "VIGNETTE",
    "RETIRED_25",  // was GLITCH; a saved "GLITCH" no longer resolves
    "FISHEYE",
    "PARTICLE_SIZE",
    "TRAIL_LENGTH",
    "FLUID_CURL",
    "FLUID_RADIUS",
    "FLUID_FORCE",
    "FLUID_GLOW",
    "FLUID_FADE",
    "FLUID_CATCH_PULL",
    "FLUID_CATCH_RADIUS",
    "FLOW_STRENGTH",
    "RETIRED_37",  // was WATER_RIPPLE (Water is gone; Pool replaced it)
    "RETIRED_38",  // was RIPPLE_OVERLAY
    "LFO1_RATE",
    "LFO1_DEPTH",
    "LFO2_RATE",
    "LFO2_DEPTH",
    "LFO3_RATE",
    "LFO3_DEPTH",
    "CAM_FLY",
    "CAM_ORBIT",
    "CAM_PUSH",
    "CAM_YAW",
    "CAM_PITCH",
    "CAM_ROLL",
    "CAM_FOV",
    "CAM_SHAKE",
    "FLOW_CURL",
    "FLOW_FORCE",
    "DIVE_SPEED",  // LfoTarget::EndlessZoomSpeed
    "DIVE_TWIST",
    "DIVE_AIM_X",
    "DIVE_AIM_Y",
    "MACRO",
    "PALETTE_AMOUNT_1",
    "PALETTE_AMOUNT_3",
    "PALETTE_AMOUNT_4",
    "PALETTE_AMOUNT_5",
    "PALETTE_AMOUNT_6",
    "PALETTE_AMOUNT_7",
    "PALETTE_AMOUNT_8",
    "PALETTE_SHIFT",
    "KALEIDO_FOLDS",
    "LAYER_MIX",
    "SIM_FLUID",
    "SIM_PARTICLES",
    "SIM_FORCE",
    "SIM_LAYER_ORDER",
    "SPAWN",
    "FRACTAL",
    "FRONT_LAYER",
    "BACK_LAYER",
    "FORMULA",
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

void reachRow(std::string& out, const std::string& id, const std::string& asId) {
    out += "reach|" + asId;
    for (int picker = kPickerReaction; picker < LfoEngine::kSlots; ++picker) out += "|" + hex(offerMask(id, picker));
    out += "\n";
}

template <typename Names>
void enumRows(std::string& out, const char* tag, const Names& names) {
    for (size_t i = 0; i < names.size(); ++i) out += std::string(tag) + "|" + std::to_string(i) + "|" + names[i] + "\n";
}

std::string build() {
    std::string out = "modulation|" + std::to_string(kTextVersion) + "\n";
    const LfoConfig d{};
    out += "config|" + tableNumber(LfoConfig::kDefaultRateSeconds) + "|" + tableNumber(LfoConfig::kMinRateSeconds) + "|" +
           tableNumber(LfoConfig::kMaxRateSeconds) + "|" + tableNumber(d.depth) + "|" + std::to_string(LfoEngine::kSlots) + "\n";
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
    for (const auto& id : catalogue::allIds()) reachRow(out, id, id);
    reachRow(out, "", "*");
    return out;
}

}  // namespace

const char* name(LfoTarget target) { return known(target) ? kTargetNames[static_cast<size_t>(target)] : ""; }

namespace {

template <typename Names, typename E>
const char* nameIn(const Names& names, E value) {
    const int i = static_cast<int>(value);
    return i >= 0 && i < static_cast<int>(names.size()) ? names[static_cast<size_t>(i)] : "";
}

}  // namespace

const char* sourceName(ModSource source) { return nameIn(kSourceNames, source); }
const char* polarityName(ModPolarity polarity) { return nameIn(kPolarityNames, polarity); }
const char* curveName(ModCurve curve) { return nameIn(kCurveNames, curve); }
const char* waveName(LfoWave wave) { return nameIn(kWaveNames, wave); }

// The scope of the field the target writes; None and the chained targets (a wave's speed or depth)
// write none and are Universal.
Scope scopeOf(LfoTarget target) {
    const int slot = known(target) ? paramSlotOf(target) : -1;
    return slot >= 0 ? SceneParams::meta(static_cast<ParamSlot>(slot)).scope : Scope::Universal;
}

// None and the chained targets always; a scene target while its field's meta row says kMod. A retired
// field has no meta row, so its target (RETIRED_5, RETIRED_25, RETIRED_37, RETIRED_38) is never offered.
bool offered(LfoTarget target) {
    if (!known(target)) return false;
    if (target == LfoTarget::None || target == LfoTarget::Macro) return true;
    ModChain c{};
    if (chainOf(target, c)) return true;
    const int slot = paramSlotOf(target);
    return slot >= 0 && (SceneParams::meta(static_cast<ParamSlot>(slot)).flags & kMod) != 0;
}

int chainSlot(LfoTarget target) {
    ModChain c{};
    return chainOf(target, c) ? c.slot : -1;
}

bool reaches(LfoTarget target, const std::string& styleId) {
    if (!offered(target)) return false;
    if (chainSlot(target) >= 0) return true;
    return catalogue::reaches(scopeOf(target), styleId);
}

bool offeredTo(LfoTarget target, const std::string& styleId, int picker) {
    if (picker < kPickerReaction || picker >= LfoEngine::kSlots || !reaches(target, styleId)) return false;
    // A reaction route moves scene params only; a knob is the waves' and the envelopes'.
    if (target == LfoTarget::Macro) return picker != kPickerReaction;
    const int slot = chainSlot(target);
    if (slot < 0) return true;
    if (picker == kPickerReaction) return false;
    return picker == kPickerEnvelope || picker < slot;
}

Mask offerMask(const std::string& styleId, int picker) {
    Mask bits;
    for (int i = 0; i < kTargetCount; ++i) bits[static_cast<size_t>(i)] = offeredTo(static_cast<LfoTarget>(i), styleId, picker);
    return bits;
}

std::string hex(const Mask& mask) {
    std::string out;
    for (int nibble = (kTargetCount + 3) / 4 - 1; nibble >= 0; --nibble) {
        int digit = 0;
        for (int bit = 3; bit >= 0; --bit) {
            const int i = nibble * 4 + bit;
            digit = digit * 2 + (i < kTargetCount && mask[static_cast<size_t>(i)] ? 1 : 0);
        }
        if (digit != 0 || !out.empty()) out += "0123456789abcdef"[digit];
    }
    return out.empty() ? "0" : out;
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
