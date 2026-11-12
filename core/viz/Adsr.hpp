#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "api/geode_api.h"
#include "viz/Config.hpp"
#include "viz/Lfo.hpp"

namespace geode::viz {

enum class EnvBand { Bass, Mid, Treble, Rms, Brightness, Width };

// One envelope. Defaults are the adsr rows of viz/ConfigTable.inc
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
    // -1 fires on a note start; a Moment ordinal (viz/Reactions) fires on that moment instead.
    int moment = -1;
};

class AdsrEngine {
public:
    static constexpr int kCount = 2;

    std::array<AdsrConfig, kCount> configs{};

    // `moments` is the bit per Moment that came this frame (MomentWatch).
    const std::array<float, kCount>& tick(float dt, const GeodeSoundFrame& sound, std::uint32_t moments = 0);
    static void lfoOffsets(const std::array<AdsrConfig, kCount>& configs, const std::array<float, kCount>& envs,
                           std::array<float, LfoEngine::kSlots>& rate, std::array<float, LfoEngine::kSlots>& depth);
    static SceneParams apply(const SceneParams& p, const std::array<AdsrConfig, kCount>& configs, const std::array<float, kCount>& envs);

    // The shortest stage the envelope runs: attack's control floor (the adsr_attack row), which decay
    // and release share although their controls start higher (0.01 s and 0.02 s), so a value saved
    // below a slider's end still plays as it always did.
    static constexpr float kMinStageSeconds = config::spec(config::Slot::adsr_attack).min;
    // The gate a level-tracking sustain divides by, at least the gate control's floor.
    static constexpr float kMinGateThreshold = config::spec(config::Slot::adsr_gateThreshold).min;

private:
    static constexpr float kChainRateHz = 4.0f;

    std::array<float, kCount> level_{};
    std::array<int, kCount> stage_{};
    std::array<float, kCount> out_{};
    std::array<float, kCount> peak_{1.0f, 1.0f};
};

}  // namespace geode::viz
