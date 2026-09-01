#pragma once
#include <array>

namespace geode::analysis {

class BarTracker {
public:
    static constexpr int kBeatsPerBar = 4;
    static constexpr float kWrapThreshold = 0.5f;
    static constexpr float kScoreLeak = 0.969f;
    static constexpr float kSwitchMargin = 1.25f;

    float barPhase() const { return barPhase_; }
    int beatInBar() const { return beatInBar_; }
    bool downbeat() const { return downbeat_; }
    float confidence() const { return confidence_; }
    void step(float phase, bool beat, bool locked, float accent);
    void reset();

private:
    void elect();
    float clarity() const;

    float barPhase_ = 0.0f;
    int beatInBar_ = 0;
    bool downbeat_ = false;
    float confidence_ = 0.0f;
    float prevPhase_ = 0.0f;
    int beatIndex_ = 0;
    int downbeatPos_ = 0;
    int beatsSeen_ = 0;
    std::array<float, kBeatsPerBar> scores_{};
};

}  // namespace geode::analysis
