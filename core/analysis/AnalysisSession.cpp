#include "analysis/AnalysisSession.hpp"

#include <algorithm>
#include <cstring>

#include "analysis/StereoField.hpp"

namespace geode::analysis {

namespace {
// push()'s buffer_/sideBuffer_ are capped at this many FFT windows so a producer that pushes without ever
// pulling cannot grow them without bound.
constexpr size_t kMaxBufferedWindows = 8;
}  // namespace

AnalysisSession::AnalysisSession(int sampleRateHz, int fftSize)
    : sampleRateHz_(sampleRateHz),
      fftSize_(fftSize),
      hopSamples_(std::max(fftSize / kHopDivisor, 1)),
      hopRateHz_(static_cast<float>(sampleRateHz) / static_cast<float>(hopSamples_)),
      analyzer_(GEODE_BAND_COUNT, fftSize, sampleRateHz, hopRateHz_),
      chroma_(hopRateHz_),
      spectrum_(static_cast<size_t>(fftSize / 2)),
      buffer_(static_cast<size_t>(fftSize) * 4),
      sideBuffer_(static_cast<size_t>(fftSize) * 4) {}

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

void AnalysisSession::push(const float* interleaved, size_t frames, int channels) {
    if (channels <= 0 || frames == 0) return;
    // Bound how far this can grow: a producer that pushes without ever pulling must not grow the buffer
    // without bound. Cap at a few FFT windows and drop the oldest samples once at the cap, mirroring
    // Renderer::pushPcm's fixed-capacity ring (core/viz/Renderer.cpp).
    const size_t cap = static_cast<size_t>(fftSize_) * kMaxBufferedWindows;
    if (buffer_.size() < cap && buffered_ + frames > buffer_.size()) {
        const size_t grown = std::min(std::max(buffered_ + frames, buffer_.size() * 2), cap);
        buffer_.resize(grown);
        sideBuffer_.resize(grown);
    }
    // A single push larger than the cap: keep only its most recent samples.
    if (frames > buffer_.size()) {
        const size_t skip = frames - buffer_.size();
        interleaved += skip * static_cast<size_t>(channels);
        frames = buffer_.size();
    }
    // Still no room (buffer already at the cap): drop the oldest buffered samples.
    if (buffered_ + frames > buffer_.size()) {
        const size_t keep = buffer_.size() - frames;
        const size_t drop = buffered_ - keep;
        std::memmove(buffer_.data(), buffer_.data() + drop, keep * sizeof(float));
        std::memmove(sideBuffer_.data(), sideBuffer_.data() + drop, keep * sizeof(float));
        buffered_ = keep;
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
    averagedWaveform(mid, out.waveform);

    // hopSamples_ <= fftSize_ by construction and buffered_ >= fftSize_ here (the guard above), so
    // hop <= buffered_ always; the extra clamp is defense in depth.
    const size_t hop = std::min(static_cast<size_t>(hopSamples_), buffered_);
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
    const int step = std::max(fftSize_ / GEODE_WAVEFORM_POINTS, 1);
    for (int i = 0; i < GEODE_WAVEFORM_POINTS; i++) {
        float acc = 0.0f;
        const int base = i * step;
        for (int j = 0; j < step; j++) acc += mid[base + j];
        out[i] = acc / step;
    }
}

}  // namespace geode::analysis
