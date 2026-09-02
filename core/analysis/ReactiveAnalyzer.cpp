#include "analysis/ReactiveAnalyzer.hpp"

#include <algorithm>
#include <cmath>

namespace geode::analysis {

ReactiveAnalyzer::ReactiveAnalyzer(int bandCount, int fftSize, int sampleRateHz, float hopRateHz)
    : bandCount_(bandCount),
      fftSize_(fftSize),
      sampleRateHz_(sampleRateHz),
      hopRateHz_(hopRateHz),
      window_(fftSize, WindowShape::Hann),
      windowed_(static_cast<size_t>(fftSize)),
      spectrum_(fftSize),
      logBands_(bandCount, fftSize, sampleRateHz),
      bandPower_(static_cast<size_t>(bandCount)),
      bandDb_(static_cast<size_t>(bandCount)),
      bandNormalized_(static_cast<size_t>(bandCount)),
      whitened_(static_cast<size_t>(bandCount)),
      range_(bandCount),
      whitening_(bandCount),
      flux_(bandCount),
      picker_(hopRateHz),
      tempo_(hopRateHz),
      stability_(hopRateHz),
      drums_(std::make_unique<DrumChannels>(bandCount, hopRateHz, sampleRateHz)),
      structure_(bandCount, hopRateHz),
      harmonic_(fftSize / 2 + 1, hopRateHz),
      bands_(static_cast<size_t>(bandCount)),
      smoothingState_(static_cast<size_t>(bandCount)) {}

void ReactiveAnalyzer::setSampleRateHz(int value) {
    if (value != sampleRateHz_) {
        sampleRateHz_ = value;
        logBands_.setSampleRateHz(value);
        const float sensitivity = picker_.sensitivity();
        drums_ = std::make_unique<DrumChannels>(bandCount_, hopRateHz_, value);
        drums_->setSensitivity(sensitivity);
    }
}

void ReactiveAnalyzer::setSensitivity(float value) {
    picker_.setSensitivity(value);
    drums_->setSensitivity(value);
}

void ReactiveAnalyzer::analyze(const float* samples, float dtSeconds) {
    rms_ = rmsOf(samples);
    if (rms_ < kSilenceRms) {
        lastFrameSilent_ = true;
        silentSeconds_ += dtSeconds;
        if (silentSeconds_ > kStabilitySilenceHoldSeconds) {
            stability_.step(0.0f);
            tempoStability_ = stability_.value();
        }
        silenceOutputs();
        return;
    }

    lastFrameSilent_ = false;
    silentSeconds_ = 0.0f;
    window_.applyInto(samples, fftSize_, 0, windowed_.data());
    spectrum_.compute(windowed_.data());

    logBands_.energy(spectrum_.magnitudes().data(), bandPower_.data());
    toDb(bandPower_.data(), bandDb_.data());
    range_.normalize(bandDb_.data(), dtSeconds, bandNormalized_.data());
    smooth(bandNormalized_.data(), dtSeconds);

    bass_ = mean(bands_, 0, bandCount_ / 8);
    mid_ = mean(bands_, bandCount_ / 8, bandCount_ / 2);
    treble_ = mean(bands_, bandCount_ / 2, bandCount_);
    centroid_ = normalizedCentroid();
    macroEnergy_ = macroEnergyOf(rms_, dtSeconds);

    whitening_.whiten(bandPower_.data(), dtSeconds, whitened_.data());
    fluxValue_ = flux_.next(whitened_.data());
    const bool isOnset = picker_.accept(fluxValue_);
    onset_ = std::clamp(fluxValue_ / std::max(picker_.threshold() * kOnsetHeadroom, 1e-9f), 0.0f, 1.0f);
    transient_ = picker_.strength();

    tempo_.step(fluxValue_);
    bpm_ = tempo_.bpm();
    pulseConfidence_ = tempo_.confidence();
    stability_.step(tempo_.bpm());
    tempoStability_ = stability_.value();
    beat_ = grid_.step(tempo_.periodFrames(), tempo_.confidence(), isOnset);
    beatPhase_ = grid_.phase();
    beatStrength_ = beat_ ? picker_.strength() : 0.0f;

    drums_->step(whitened_.data());
    kick_ = drums_->kick();
    snare_ = drums_->snare();
    hat_ = drums_->hat();

    bar_.step(grid_.phase(), beat_, grid_.locked(), beat_ ? picker_.strength() + kKickAccentWeight * drums_->kick() : 0.0f);
    barPhase_ = bar_.barPhase();
    beatInBar_ = bar_.beatInBar();
    downbeat_ = bar_.downbeat();
    downbeatConfidence_ = bar_.confidence();

    structure_.step(bands_.data(), rms_, onset_);
    novelty_ = structure_.novelty();
    sectionBoundary_ = structure_.sectionBoundary();
    buildup_ = structure_.buildup();
    drop_ = structure_.drop();
    arrival_ = structure_.arrival();
    harmonic_.step(spectrum_.magnitudes().data());
    harmonicity_ = harmonic_.balance();
}

void ReactiveAnalyzer::reset() {
    range_.reset();
    whitening_.reset();
    flux_.reset();
    picker_.reset();
    tempo_.reset();
    grid_.reset();
    stability_.reset();
    bar_.reset();
    structure_.reset();
    harmonic_.reset();
    drums_->reset();
    std::fill(smoothingState_.begin(), smoothingState_.end(), 0.0f);
    std::fill(bands_.begin(), bands_.end(), 0.0f);
    levelPeak_ = 0.0f;
    silentSeconds_ = 0.0f;
    rms_ = 0.0f;
    bass_ = 0.0f;
    mid_ = 0.0f;
    treble_ = 0.0f;
    centroid_ = 0.0f;
    fluxValue_ = 0.0f;
    onset_ = 0.0f;
    beat_ = false;
    beatStrength_ = 0.0f;
    transient_ = 0.0f;
    beatPhase_ = 0.0f;
    pulseConfidence_ = 0.0f;
    bpm_ = 0.0f;
    tempoStability_ = 0.0f;
    barPhase_ = 0.0f;
    beatInBar_ = 0;
    downbeat_ = false;
    downbeatConfidence_ = 0.0f;
    novelty_ = 0.0f;
    sectionBoundary_ = false;
    buildup_ = 0.0f;
    drop_ = false;
    arrival_ = false;
    harmonicity_ = HarmonicBalance::kUndecided;
    macroEnergy_ = 0.0f;
    kick_ = 0.0f;
    snare_ = 0.0f;
    hat_ = 0.0f;
}

void ReactiveAnalyzer::spectrumInto(float* out) const {
    const int bins = fftSize_ / 2;
    if (lastFrameSilent_) {
        std::fill(out, out + bins, 0.0f);
        return;
    }
    const float scale = 2.0f / fftSize_;
    out[0] = 0.0f;
    const auto& magnitudes = spectrum_.magnitudes();
    for (int k = 1; k < bins; k++) out[k] = magnitudes[k] * scale;
}

void ReactiveAnalyzer::silenceOutputs() {
    std::fill(bands_.begin(), bands_.end(), 0.0f);
    std::fill(smoothingState_.begin(), smoothingState_.end(), 0.0f);
    bass_ = 0.0f;
    mid_ = 0.0f;
    treble_ = 0.0f;
    centroid_ = 0.0f;
    fluxValue_ = 0.0f;
    onset_ = 0.0f;
    beat_ = false;
    beatStrength_ = 0.0f;
    transient_ = 0.0f;
    downbeat_ = false;
    sectionBoundary_ = false;
    drop_ = false;
    arrival_ = false;
    macroEnergy_ = 0.0f;
    kick_ = 0.0f;
    snare_ = 0.0f;
    hat_ = 0.0f;
}

void ReactiveAnalyzer::toDb(const float* power, float* out) const {
    for (int b = 0; b < bandCount_; b++) {
        const float p = power[b];
        out[b] = p <= 0.0f ? AdaptiveRange::kSilenceDb : std::max(10.0f * std::log10(p), AdaptiveRange::kSilenceDb);
    }
}

void ReactiveAnalyzer::smooth(const float* source, float dtSeconds) {
    if (dtSeconds <= 0.0f) {
        std::copy(source, source + bandCount_, bands_.begin());
        return;
    }
    const float attack = attackSeconds_ <= 0.0f ? 1.0f : std::clamp(1.0f - std::exp(-dtSeconds / attackSeconds_), 0.0f, 1.0f);
    const float release = releaseSeconds_ <= 0.0f ? 1.0f : std::clamp(1.0f - std::exp(-dtSeconds / releaseSeconds_), 0.0f, 1.0f);
    for (int b = 0; b < bandCount_; b++) {
        const float target = source[b];
        const float k = target > smoothingState_[b] ? attack : release;
        smoothingState_[b] += (target - smoothingState_[b]) * k;
        bands_[b] = smoothingState_[b];
    }
}

float ReactiveAnalyzer::rmsOf(const float* samples) const {
    double acc = 0.0;
    for (int i = 0; i < fftSize_; i++) {
        const double v = samples[i];
        acc += v * v;
    }
    return std::clamp(static_cast<float>(std::sqrt(acc / fftSize_)), 0.0f, 1.0f);
}

float ReactiveAnalyzer::normalizedCentroid() const {
    float weight = 0.0f;
    float weighted = 0.0f;
    for (int b = 0; b < bandCount_; b++) {
        weight += bands_[b];
        weighted += bands_[b] * b;
    }
    return weight <= 1e-6f ? 0.0f : std::clamp(weighted / weight / (bandCount_ - 1), 0.0f, 1.0f);
}

float ReactiveAnalyzer::macroEnergyOf(float level, float dtSeconds) {
    const float decay = dtSeconds <= 0.0f ? 1.0f : std::exp(-dtSeconds / kMacroPeakSeconds);
    levelPeak_ = std::max(level, levelPeak_ * decay);
    return levelPeak_ <= 1e-6f ? 0.0f : std::clamp(level / levelPeak_, 0.0f, 1.0f);
}

float ReactiveAnalyzer::mean(const std::vector<float>& values, int from, int to) const {
    float acc = 0.0f;
    for (int i = from; i < to; i++) acc += values[i];
    return acc / (to - from);
}

}  // namespace geode::analysis
