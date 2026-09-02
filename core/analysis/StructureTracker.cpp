#include "analysis/StructureTracker.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace geode::analysis {

namespace {
constexpr float kFar = std::numeric_limits<float>::max();
}

StructureTracker::StructureTracker(int bandCount, float hopRateHz)
    : bandCount_(bandCount),
      hopRateHz_(hopRateHz),
      dt_(1.0f / hopRateHz),
      fastPole_(poleFor(0.5f)),
      slowPole_(poleFor(8.0f)),
      statsPole_(poleFor(10.0f)),
      fastEnergyPole_(poleFor(1.0f)),
      slowEnergyPole_(poleFor(6.0f)),
      noveltySmoothPole_(poleFor(0.25f)),
      fast_(static_cast<size_t>(bandCount)),
      slow_(static_cast<size_t>(bandCount)),
      sinceSection_(kFar),
      sinceDip_(kFar),
      sinceDrop_(kFar) {}

float StructureTracker::poleFor(float seconds) const {
    return 1.0f - std::exp(-1.0f / (seconds * hopRateHz_));
}

void StructureTracker::step(const float* bands, float rms, float onset) {
    warmupSeconds_ += dt_;
    sinceSection_ += dt_;
    sinceDip_ += dt_;
    sinceDrop_ += dt_;

    double distance = 0.0;
    for (int b = 0; b < bandCount_; b++) {
        const float v = bands[b];
        fast_[b] += (v - fast_[b]) * fastPole_;
        slow_[b] += (v - slow_[b]) * slowPole_;
        const float d = fast_[b] - slow_[b];
        distance += static_cast<double>(d) * d;
    }
    const float raw = static_cast<float>(std::sqrt(distance / bandCount_));
    noveltyPeak_ = std::max(raw, std::max(noveltyPeak_ * kPeakDecay, kNoveltyPeakFloor));
    const float normalized = std::clamp(raw / noveltyPeak_, 0.0f, 1.0f);
    novelty_ += (normalized - novelty_) * noveltySmoothPole_;

    noveltyMean_ += (novelty_ - noveltyMean_) * statsPole_;
    noveltyDev_ += (std::fabs(novelty_ - noveltyMean_) - noveltyDev_) * statsPole_;
    const float threshold = std::max(kSectionFloor, noveltyMean_ + 2.0f * noveltyDev_);
    sectionBoundary_ = false;
    if (sectionArmed_ && warmupSeconds_ > kWarmupSeconds && sinceSection_ > kSectionRefractorySeconds && novelty_ > threshold) {
        sectionBoundary_ = true;
        sectionCount_++;
        sectionArmed_ = false;
        sinceSection_ = 0.0f;
    } else if (!sectionArmed_ && novelty_ < kSectionRearm) {
        sectionArmed_ = true;
    }

    const float energy = 0.5f * rms + 0.5f * onset;
    if (!energySeeded_) {
        energySeeded_ = true;
        fastEnergy_ = energy;
        slowEnergy_ = energy;
    }
    fastEnergy_ += (energy - fastEnergy_) * fastEnergyPole_;
    slowEnergy_ += (energy - slowEnergy_) * slowEnergyPole_;
    buildup_ = std::clamp((fastEnergy_ - slowEnergy_) / kBuildupScale, 0.0f, 1.0f);
    buildupMemory_ = std::max(buildup_, buildupMemory_ * std::exp(-dt_ / kBuildupMemorySeconds));

    if (rms < slowEnergy_ * kDipFraction) {
        dipSeconds_ += dt_;
        if (dipSeconds_ >= kMinDipSeconds) sinceDip_ = 0.0f;
    } else {
        dipSeconds_ = 0.0f;
    }

    drop_ = false;
    if (warmupSeconds_ > kWarmupSeconds && sinceDrop_ > kDropRefractorySeconds && buildupMemory_ > kDropBuildup &&
        sinceDip_ < kDropDipWindowSeconds && rms > kDropSlamLevel && rms > slowEnergy_ + kDropSlamMargin) {
        drop_ = true;
        sinceDrop_ = 0.0f;
        buildupMemory_ = 0.0f;
    }

    arrival_ = false;
    if (rms < kArrivalQuietLevel) {
        quietSeconds_ += dt_;
        if (quietSeconds_ >= kArrivalQuietSeconds) arrivalArmed_ = true;
    } else {
        if (arrivalArmed_ && rms > kArrivalRecoveryLevel && warmupSeconds_ > kWarmupSeconds) {
            arrival_ = true;
            arrivalArmed_ = false;
        }
        quietSeconds_ = 0.0f;
    }
}

void StructureTracker::reset() {
    std::fill(fast_.begin(), fast_.end(), 0.0f);
    std::fill(slow_.begin(), slow_.end(), 0.0f);
    noveltyPeak_ = kNoveltyPeakFloor;
    noveltyMean_ = 0.0f;
    noveltyDev_ = 0.0f;
    sectionArmed_ = true;
    sinceSection_ = kFar;
    fastEnergy_ = 0.0f;
    slowEnergy_ = 0.0f;
    energySeeded_ = false;
    buildupMemory_ = 0.0f;
    sinceDip_ = kFar;
    dipSeconds_ = 0.0f;
    sinceDrop_ = kFar;
    quietSeconds_ = 0.0f;
    arrivalArmed_ = false;
    warmupSeconds_ = 0.0f;
    novelty_ = 0.0f;
    sectionBoundary_ = false;
    sectionCount_ = 0;
    buildup_ = 0.0f;
    drop_ = false;
    arrival_ = false;
}

}  // namespace geode::analysis
