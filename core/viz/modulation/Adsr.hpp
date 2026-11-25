#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "api/geode_api.h"
#include "viz/params/Config.hpp"
#include "viz/modulation/Lfo.hpp"

namespace geode::viz {

enum class EnvBand { Bass, Mid, Treble, Rms, Brightness, Width };

// One envelope. Defaults are the adsr rows of viz/params/ConfigTable.inc
// (#423), the same numbers the stores and the sliders read.
struct AdsrConfig {
    bool enabled = false;
    std::vector<LfoTarget> targets;
    float attack = config::spec(config::Slot::adsr_attack).defaultValue;
    float decay = config::spec(config::Slot::adsr_decay).defaultValue;
    float sustain = config::spec(config::Slot::adsr_sustain).defaultValue;
    float release = config::spec(config::Slot::adsr_release).defaultValue;
    float amount = config::spec(config::Slot::adsr_amount).defaultValue;
    EnvBand band = EnvBand::Bass;
    float gateThreshold = config::spec(config::Slot::adsr_gateThreshold).defaultValue;
    bool sustainTrack = config::spec(config::Slot::adsr_sustainTrack).defaultValue > 0.5f;
    bool retrigger = config::spec(config::Slot::adsr_retrigger).defaultValue > 0.5f;
    // The macro knob's id a Macro target turns; 0 is none.
    int knob = 0;
    // -1 fires on a note start; a Moment ordinal (viz/modulation/Reactions) fires on that moment instead.
    int moment = -1;
};

// Where an envelope is: at rest, rising to its peak, falling to the sustain, holding it, or letting go.
enum class AdsrStage { Idle, Attack, Decay, Sustain, Release };

// The numbers one envelope's stages run on (AdsrConfig's, without the targets and the trigger choice).
struct AdsrShape {
    float attack = 0.0f;
    float decay = 0.0f;
    float sustain = 0.0f;
    float release = 0.0f;
    bool sustainTrack = false;
    bool retrigger = false;
    float gateThreshold = 0.0f;
};

// One envelope's stage machine, apart from what triggers it: the waves' envelopes (AdsrEngine) and the Event
// Responder's responses (viz/modulation/Responder) run the same stages. `hit` is the trigger's strength (0: none),
// `energy` the level a tracking sustain follows, and the gate says whether the envelope may hold (gateHolds) or
// restart its sustain from a release (gateOpen).
struct AdsrVoice {
    AdsrStage stage = AdsrStage::Idle;
    float level = 0.0f;
    float peak = 1.0f;

    void reset() {
        stage = AdsrStage::Idle;
        level = 0.0f;
        peak = 1.0f;
    }
    // One frame; returns the level, 0..1.
    float step(const AdsrShape& c, float dt, float hit, float energy, bool gateOpen, bool gateHolds);
};

class AdsrEngine {
public:
    static constexpr int kCount = 2;

    std::array<AdsrConfig, kCount> configs{};

    // `moments` is the bit per Moment that came this frame (MomentWatch).
    const std::array<float, kCount>& tick(float dt, const GeodeSoundFrame& sound, std::uint32_t moments = 0);
    static void lfoOffsets(const std::array<AdsrConfig, kCount>& configs, const std::array<float, kCount>& envs,
                           std::array<float, LfoEngine::kSlots>& rate, std::array<float, LfoEngine::kSlots>& depth);
    // Adds each enabled envelope, times its amount, to every scene field it targets in `p`, in place.
    static void apply(SceneParams& p, const std::array<AdsrConfig, kCount>& configs, const std::array<float, kCount>& envs);

    // The shortest stage the envelope runs: attack's control floor (the adsr_attack row), which decay
    // and release share although their controls start higher (0.01 s and 0.02 s), so a value saved
    // below a slider's end still plays as it always did.
    static constexpr float kMinStageSeconds = config::spec(config::Slot::adsr_attack).min;
    // The gate a level-tracking sustain divides by, at least the gate control's floor.
    static constexpr float kMinGateThreshold = config::spec(config::Slot::adsr_gateThreshold).min;

private:
    std::array<AdsrVoice, kCount> voice_{};
    std::array<float, kCount> out_{};
};

}  // namespace geode::viz
