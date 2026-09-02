#pragma once
#include <memory>
#include <vector>

#include "analysis/AdaptiveRange.hpp"
#include "analysis/AdaptiveWhitening.hpp"
#include "analysis/BarTracker.hpp"
#include "analysis/BeatGrid.hpp"
#include "analysis/DrumChannels.hpp"
#include "analysis/HarmonicBalance.hpp"
#include "analysis/LogBands.hpp"
#include "analysis/OnsetPeakPicker.hpp"
#include "analysis/Spectrum.hpp"
#include "analysis/StructureTracker.hpp"
#include "analysis/SuperFlux.hpp"
#include "analysis/TempoStability.hpp"
#include "analysis/TempoTracker.hpp"
#include "analysis/WindowTable.hpp"

namespace geode::analysis {

class ReactiveAnalyzer {
public:
    ReactiveAnalyzer(int bandCount = 64, int fftSize = 2048, int sampleRateHz = 48000, float hopRateHz = 62.5f);

    int bandCount() const { return bandCount_; }
    int fftSize() const { return fftSize_; }
    int sampleRateHz() const { return sampleRateHz_; }
    void setSampleRateHz(int value);
    float attackSeconds() const { return attackSeconds_; }
    void setAttackSeconds(float value) { attackSeconds_ = value; }
    float releaseSeconds() const { return releaseSeconds_; }
    void setReleaseSeconds(float value) { releaseSeconds_ = value; }
    float sensitivity() const { return picker_.sensitivity(); }
    void setSensitivity(float value);
    float refractoryMs() const { return picker_.refractorySeconds() * 1000.0f; }
    void setRefractoryMs(float value) { picker_.setRefractorySeconds(value / 1000.0f); }

    const std::vector<float>& bands() const { return bands_; }
    float rms() const { return rms_; }
    float bass() const { return bass_; }
    float mid() const { return mid_; }
    float treble() const { return treble_; }
    float centroid() const { return centroid_; }
    float fluxValue() const { return fluxValue_; }
    float onset() const { return onset_; }
    bool beat() const { return beat_; }
    float beatStrength() const { return beatStrength_; }
    float transient() const { return transient_; }
    float beatPhase() const { return beatPhase_; }
    float pulseConfidence() const { return pulseConfidence_; }
    float bpm() const { return bpm_; }
    float tempoStability() const { return tempoStability_; }
    float barPhase() const { return barPhase_; }
    int beatInBar() const { return beatInBar_; }
    bool downbeat() const { return downbeat_; }
    float downbeatConfidence() const { return downbeatConfidence_; }
    float macroEnergy() const { return macroEnergy_; }
    float kick() const { return kick_; }
    float snare() const { return snare_; }
    float hat() const { return hat_; }
    float novelty() const { return novelty_; }
    bool sectionBoundary() const { return sectionBoundary_; }
    float buildup() const { return buildup_; }
    bool drop() const { return drop_; }
    bool arrival() const { return arrival_; }
    float harmonicity() const { return harmonicity_; }
    float warmup() const { return range_.warmup(); }

    void analyze(const float* samples, float dtSeconds);
    void reset();
    void spectrumInto(float* out) const;

private:
    static constexpr float kOnsetHeadroom = 2.0f;
    static constexpr float kMacroPeakSeconds = 20.0f;
    static constexpr float kKickAccentWeight = 0.5f;
    static constexpr float kStabilitySilenceHoldSeconds = 2.0f;
    static constexpr float kSilenceRms = 1e-5f;

    void silenceOutputs();
    void toDb(const float* power, float* out) const;
    void smooth(const float* source, float dtSeconds);
    float rmsOf(const float* samples) const;
    float normalizedCentroid() const;
    float macroEnergyOf(float level, float dtSeconds);
    float mean(const std::vector<float>& values, int from, int to) const;

    int bandCount_;
    int fftSize_;
    int sampleRateHz_;
    float hopRateHz_;
    WindowTable window_;
    std::vector<float> windowed_;
    Spectrum spectrum_;
    LogBands logBands_;
    std::vector<float> bandPower_;
    std::vector<float> bandDb_;
    std::vector<float> bandNormalized_;
    std::vector<float> whitened_;
    AdaptiveRange range_;
    AdaptiveWhitening whitening_;
    SuperFlux flux_;
    OnsetPeakPicker picker_;
    TempoTracker tempo_;
    BeatGrid grid_;
    TempoStability stability_;
    BarTracker bar_;
    std::unique_ptr<DrumChannels> drums_;
    StructureTracker structure_;
    HarmonicBalance harmonic_;
    std::vector<float> bands_;
    std::vector<float> smoothingState_;
    float attackSeconds_ = 0.02f;
    float releaseSeconds_ = 0.15f;
    float rms_ = 0.0f;
    float bass_ = 0.0f;
    float mid_ = 0.0f;
    float treble_ = 0.0f;
    float centroid_ = 0.0f;
    float fluxValue_ = 0.0f;
    float onset_ = 0.0f;
    bool beat_ = false;
    float beatStrength_ = 0.0f;
    float transient_ = 0.0f;
    float beatPhase_ = 0.0f;
    float pulseConfidence_ = 0.0f;
    float bpm_ = 0.0f;
    float tempoStability_ = 0.0f;
    float barPhase_ = 0.0f;
    int beatInBar_ = 0;
    bool downbeat_ = false;
    float downbeatConfidence_ = 0.0f;
    float macroEnergy_ = 0.0f;
    float kick_ = 0.0f;
    float snare_ = 0.0f;
    float hat_ = 0.0f;
    float novelty_ = 0.0f;
    bool sectionBoundary_ = false;
    float buildup_ = 0.0f;
    bool drop_ = false;
    bool arrival_ = false;
    float harmonicity_ = HarmonicBalance::kUndecided;
    float levelPeak_ = 0.0f;
    bool lastFrameSilent_ = true;
    float silentSeconds_ = 0.0f;
};

}  // namespace geode::analysis
