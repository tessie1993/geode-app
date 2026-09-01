#include "analysis/AnalysisSession.hpp"

#include <algorithm>
#include <cstring>

#include "analysis/StereoField.hpp"

namespace geode::analysis {

namespace {
int hopFor(int sampleRateHz, float hopRateHz) {
    return std::max(static_cast<int>(sampleRateHz / hopRateHz), 1);
}
}  // namespace

AnalysisSession::AnalysisSession(int sampleRateHz, int fftSize, float hopRateHz)
    : sampleRateHz_(sampleRateHz),
      fftSize_(fftSize),
      hopRateHz_(hopRateHz),
      hopSamples_(hopFor(sampleRateHz, hopRateHz)),
      analyzer_(GEODE_BAND_COUNT, fftSize, sampleRateHz, hopRateHz),
      chroma_(hopRateHz),
      spectrum_(static_cast<size_t>(fftSize / 2)),
      buffer_(static_cast<size_t>(fftSize) * 4),
      sideBuffer_(static_cast<size_t>(fftSize) * 4) {}

void AnalysisSession::setSampleRateHz(int value) {
    if (value <= 0 || value == sampleRateHz_) return;
    sampleRateHz_ = value;
    hopSamples_ = hopFor(value, hopRateHz_);
    analyzer_.setSampleRateHz(value);
}

void AnalysisSession::setTuning(float sensitivity, float refractoryMs, float attackSeconds, float releaseSeconds) {
    analyzer_.setSensitivity(sensitivity);
    analyzer_.setRefractoryMs(refractoryMs);
    analyzer_.setAttackSeconds(attackSeconds);
    analyzer_.setReleaseSeconds(releaseSeconds);
}

void AnalysisSession::reset() {
    analyzer_.reset();
    chroma_.reset();
    buffered_ = 0;
}

void AnalysisSession::analyze(const float* mid, const float* side, size_t frames, float dtSeconds, GeodeFeatureFrame& out) {
    if (frames < static_cast<size_t>(fftSize_)) return;
    analyzer_.analyze(mid, dtSeconds);
    analyzer_.spectrumInto(spectrum_.data());
    chroma_.step(spectrum_.data(), static_cast<int>(spectrum_.size()), sampleRateHz_, fftSize_);
    fill(mid, side, out);
    averagedWaveform(mid, out.waveform);
}

void AnalysisSession::push(const float* interleaved, size_t frames, int channels) {
    if (channels <= 0 || frames == 0) return;
    if (buffered_ + frames > buffer_.size()) {
        const size_t grown = std::max(buffered_ + frames, buffer_.size() * 2);
        buffer_.resize(grown);
        sideBuffer_.resize(grown);
    }
    size_t s = 0;
    for (size_t f = 0; f < frames; f++) {
        const float left = interleaved[s];
        const float right = channels >= 2 ? interleaved[s + 1] : left;
        buffer_[buffered_ + f] = (left + right) * 0.5f;
        sideBuffer_[buffered_ + f] = (left - right) * 0.5f;
        s += static_cast<size_t>(channels);
    }
    buffered_ += frames;
}

bool AnalysisSession::pull(GeodeFeatureFrame& out) {
    if (buffered_ < static_cast<size_t>(fftSize_)) return false;
    const float* mid = buffer_.data();
    const float* side = sideBuffer_.data();
    analyzer_.analyze(mid, 1.0f / hopRateHz_);
    analyzer_.spectrumInto(spectrum_.data());
    key_.accumulate(spectrum_.data(), static_cast<int>(spectrum_.size()), sampleRateHz_, fftSize_);
    chroma_.step(spectrum_.data(), static_cast<int>(spectrum_.size()), sampleRateHz_, fftSize_);
    fill(mid, side, out);
    decimatedWaveform(mid, out.waveform);

    const size_t hop = static_cast<size_t>(hopSamples_);
    const size_t remaining = buffered_ - hop;
    std::memmove(buffer_.data(), buffer_.data() + hop, remaining * sizeof(float));
    std::memmove(sideBuffer_.data(), sideBuffer_.data() + hop, remaining * sizeof(float));
    buffered_ = remaining;
    return true;
}

void AnalysisSession::fill(const float* mid, const float* side, GeodeFeatureFrame& out) const {
    const auto& a = analyzer_;
    out.rms = a.rms();
    out.bass = a.bass();
    out.mid = a.mid();
    out.treble = a.treble();
    out.centroid = a.centroid();
    out.flux = a.fluxValue();
    out.onset = a.onset();
    out.beat = a.beat() ? 1.0f : 0.0f;
    out.beatStrength = a.beatStrength();
    out.transient = a.transient();
    out.beatPhase = a.beatPhase();
    out.pulseConfidence = a.pulseConfidence();
    out.bpm = a.bpm();
    out.tempoStability = a.tempoStability();
    out.barPhase = a.barPhase();
    out.beatInBar = static_cast<float>(a.beatInBar());
    out.downbeat = a.downbeat() ? 1.0f : 0.0f;
    out.downbeatConfidence = a.downbeatConfidence();
    out.macroEnergy = a.macroEnergy();
    out.kick = a.kick();
    out.snare = a.snare();
    out.hat = a.hat();
    out.novelty = a.novelty();
    out.sectionBoundary = a.sectionBoundary() ? 1.0f : 0.0f;
    out.buildup = a.buildup();
    out.drop = a.drop() ? 1.0f : 0.0f;
    out.arrival = a.arrival() ? 1.0f : 0.0f;
    out.harmonicity = a.harmonicity();
    out.warmup = a.warmup();
    const stereo::Reading reading = side ? stereo::of(mid, side, fftSize_) : stereo::kMono;
    out.stereoWidth = reading.width;
    out.stereoCorrelation = reading.correlation;
    out.stereoPan = reading.pan;
    out.chromaConfidence = chroma_.confidence();
    std::copy(a.bands().begin(), a.bands().end(), out.bands);
    std::copy(chroma_.bins().begin(), chroma_.bins().end(), out.chroma);
}

void AnalysisSession::averagedWaveform(const float* mid, float* out) const {
    const int step = fftSize_ / GEODE_WAVEFORM_POINTS;
    for (int i = 0; i < GEODE_WAVEFORM_POINTS; i++) {
        float acc = 0.0f;
        const int base = i * step;
        for (int j = 0; j < step; j++) acc += mid[base + j];
        out[i] = acc / step;
    }
}

void AnalysisSession::decimatedWaveform(const float* mid, float* out) const {
    const int step = fftSize_ / GEODE_WAVEFORM_POINTS;
    for (int i = 0; i < GEODE_WAVEFORM_POINTS; i++) out[i] = mid[i * step];
}

}  // namespace geode::analysis
