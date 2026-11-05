#pragma once
#include <array>
#include <vector>

#include "api/geode_api.h"
#include "viz/Config.hpp"
#include "viz/Lfo.hpp"

namespace geode::viz {

enum class EnvBand { Bass, Mid, Treble, Rms, Brightness, Width };

// One envelope as geode_viz_set_adsr hands it over. Defaults are the adsr rows of viz/ConfigTable.inc
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
};

class AdsrEngine {
public:
    static constexpr int kCount = 2;

    std::array<AdsrConfig, kCount> configs{};

    const std::array<float, kCount>& tick(float dt, const GeodeSoundFrame& sound);
    static void lfoOffsets(const std::array<AdsrConfig, kCount>& configs, const std::array<float, kCount>& envs,
                           std::array<float, LfoEngine::kSlots>& rate, std::array<float, LfoEngine::kSlots>& depth);
    static SceneParams apply(const SceneParams& p, const std::array<AdsrConfig, kCount>& configs, const std::array<float, kCount>& envs);
    // Adds what apply() adds to each luminance target this frame, for the flash limiter (#363): an
    // envelope's attack goes down to 5 ms and it retriggers on every onset, so it is a strobe source.
    static void addLuminanceDrive(const std::array<AdsrConfig, kCount>& configs, const std::array<float, kCount>& envs,
                                  LuminanceDrive& out);

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
