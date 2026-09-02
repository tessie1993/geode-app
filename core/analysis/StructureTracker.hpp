#pragma once
#include <vector>

namespace geode::analysis {

class StructureTracker {
public:
    static constexpr float kWarmupSeconds = 5.0f;
    static constexpr float kSectionFloor = 0.5f;
    static constexpr float kSectionRearm = 0.35f;
    static constexpr float kSectionRefractorySeconds = 8.0f;
    static constexpr float kPeakDecay = 0.9997f;
    static constexpr float kNoveltyPeakFloor = 0.05f;
    static constexpr float kBuildupScale = 0.3f;
    static constexpr float kBuildupMemorySeconds = 3.0f;
    static constexpr float kDropBuildup = 0.4f;
    static constexpr float kDropDipWindowSeconds = 1.0f;
    static constexpr float kDropSlamLevel = 0.75f;
    static constexpr float kDropSlamMargin = 0.15f;
    static constexpr float kDropRefractorySeconds = 4.0f;
    static constexpr float kDipFraction = 0.35f;
    static constexpr float kMinDipSeconds = 0.1f;
    static constexpr float kArrivalQuietLevel = 0.15f;
    static constexpr float kArrivalQuietSeconds = 2.0f;
    static constexpr float kArrivalRecoveryLevel = 0.4f;

    StructureTracker(int bandCount, float hopRateHz);

    float novelty() const { return novelty_; }
    bool sectionBoundary() const { return sectionBoundary_; }
    int sectionCount() const { return sectionCount_; }
    float buildup() const { return buildup_; }
    bool drop() const { return drop_; }
    bool arrival() const { return arrival_; }
    void step(const float* bands, float rms, float onset);
    void reset();

private:
    float poleFor(float seconds) const;

    int bandCount_;
    float hopRateHz_;
    float dt_;
    float fastPole_;
    float slowPole_;
    float statsPole_;
    float fastEnergyPole_;
    float slowEnergyPole_;
    float noveltySmoothPole_;
    std::vector<float> fast_;
    std::vector<float> slow_;
    float noveltyPeak_ = kNoveltyPeakFloor;
    float noveltyMean_ = 0.0f;
    float noveltyDev_ = 0.0f;
    bool sectionArmed_ = true;
    float sinceSection_;
    float fastEnergy_ = 0.0f;
    float slowEnergy_ = 0.0f;
    bool energySeeded_ = false;
    float buildupMemory_ = 0.0f;
    float sinceDip_;
    float dipSeconds_ = 0.0f;
    float sinceDrop_;
    float quietSeconds_ = 0.0f;
    bool arrivalArmed_ = false;
    float warmupSeconds_ = 0.0f;
    float novelty_ = 0.0f;
    bool sectionBoundary_ = false;
    int sectionCount_ = 0;
    float buildup_ = 0.0f;
    bool drop_ = false;
    bool arrival_ = false;
};

}  // namespace geode::analysis
