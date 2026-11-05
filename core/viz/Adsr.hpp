#pragma once
#include <array>
#include <vector>

#include "api/geode_api.h"
#include "viz/Lfo.hpp"

namespace geode::viz {

enum class EnvBand { Bass, Mid, Treble, Rms, Brightness, Width };

struct AdsrConfig {
    bool enabled = false;
    std::vector<LfoTarget> targets;
    float attack = 0.05f;
    float decay = 0.25f;
    float sustain = 0.5f;
    float release = 0.35f;
    float amount = 0.5f;
    EnvBand band = EnvBand::Bass;
    float gateThreshold = 0.25f;
    bool sustainTrack = false;
    bool retrigger = true;
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

private:
    static constexpr float kChainRateHz = 4.0f;

    std::array<float, kCount> level_{};
    std::array<int, kCount> stage_{};
    std::array<float, kCount> out_{};
    std::array<float, kCount> peak_{1.0f, 1.0f};
};

}  // namespace geode::viz
