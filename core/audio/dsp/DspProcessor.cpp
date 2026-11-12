#include "audio/dsp/DspProcessor.hpp"

#include <algorithm>
#include <cmath>

#include "audio/dsp/FxRanges.hpp"

namespace geode::audio {

bool DspProcessor::runs(int encoding, int channels) {
    return (encoding == kEncodingPcm16 || encoding == kEncodingFloat) && channels >= 1 && channels <= Equalizer::kMaxChannels;
}

bool DspProcessor::configure(int sampleRate, int channels) {
    const bool runs = sampleRate > 0 && channels >= 1 && channels <= Equalizer::kMaxChannels;
    DspChain* target = nullptr;
    if (runs) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& chain : chains_) {
            if (chain->sampleRate() == sampleRate && chain->channels() == channels) target = chain.get();
        }
        if (!target) {
            // Built here, on the playback thread at configure time, never in process().
            chains_.push_back(std::make_unique<DspChain>(sampleRate, channels));
            target = chains_.back().get();
            applyTo(*target, settings_);
        }
    }
    // The old chain stays live: the old format may still drain through process() until flush().
    pending_ = target;
    staged_ = true;
    return runs;
}

void DspProcessor::flush() {
    bitPerfectLive_.store(bitPerfectWanted_.load(std::memory_order_acquire), std::memory_order_release);
    if (staged_) {
        live_.store(pending_, std::memory_order_release);
        pending_ = nullptr;
        staged_ = false;
    }
    if (DspChain* chain = live_.load(std::memory_order_acquire)) chain->reset();
}

DspChain* DspProcessor::runnable(int channels) const {
    // Bit-perfect skips the chain whole: gain, ramps, equalizer, crossfeed and limiter alike. The chain's own
    // state stands still meanwhile and is reset at the flush that ends bit-perfect.
    if (bitPerfectLive_.load(std::memory_order_acquire)) return nullptr;
    DspChain* chain = live_.load(std::memory_order_acquire);
    if (!chain || channels <= 0 || chain->channels() != channels) return nullptr;
    return chain;
}

void DspProcessor::process(float* interleaved, size_t capacityFloats, size_t frames, int channels) {
    DspChain* chain = runnable(channels);
    if (!chain || !interleaved) return;
    frames = std::min(frames, capacityFloats / static_cast<size_t>(channels));
    if (frames == 0) return;
    gain_.process(interleaved, frames, channels, chain->sampleRate());
    chain->process(interleaved, frames);
}

void DspProcessor::processPcm16(int16_t* interleaved, size_t capacitySamples, size_t frames, int channels) {
    DspChain* chain = runnable(channels);
    if (!chain || !interleaved) return;
    const auto ch = static_cast<size_t>(channels);
    frames = std::min(frames, capacitySamples / ch);
    // A stack chunk instead of a heap scratch: the playback thread never allocates. The chain keeps its state
    // across chunks, so chunking does not change the output.
    constexpr size_t kChunkFloats = 1024;
    constexpr float kFullScale = 32768.0f;
    float chunk[kChunkFloats];
    const size_t chunkFrames = kChunkFloats / ch;
    for (size_t done = 0; done < frames;) {
        const size_t n = std::min(chunkFrames, frames - done);
        int16_t* pcm = interleaved + done * ch;
        for (size_t i = 0; i < n * ch; ++i) chunk[i] = static_cast<float>(pcm[i]) / kFullScale;
        gain_.process(chunk, n, channels, chain->sampleRate());
        chain->process(chunk, n);
        for (size_t i = 0; i < n * ch; ++i) {
            // Clamped to [-1, 1] first (a NaN to silence), so the scaled value always fits an int32; +1.0 is
            // the one value past int16 and clips to 32767.
            const float v = chunk[i];
            const float c = std::isnan(v) ? 0.0f : std::clamp(v, -1.0f, 1.0f);
            const auto s = static_cast<int32_t>(c * kFullScale);
            pcm[i] = static_cast<int16_t>(std::clamp<int32_t>(s, INT16_MIN, INT16_MAX));
        }
        done += n;
    }
}

void DspProcessor::setBitPerfect(bool on) {
    bitPerfectWanted_.store(on, std::memory_order_release);
}

bool DspProcessor::bitPerfectLive() const {
    return bitPerfectLive_.load(std::memory_order_acquire);
}

int DspProcessor::liveChannels() const {
    const DspChain* chain = live_.load(std::memory_order_acquire);
    return chain ? chain->channels() : 0;
}

int DspProcessor::liveSampleRate() const {
    const DspChain* chain = live_.load(std::memory_order_acquire);
    return chain ? chain->sampleRate() : 0;
}

void DspProcessor::applyTo(DspChain& chain, const Settings& s) {
    chain.equalizer().setEnabled(s.enabled);
    for (int band = 0; band < Equalizer::kBands; ++band) chain.equalizer().setBand(band, s.bandsMb[static_cast<size_t>(band)]);
    chain.equalizer().setBassBoost(s.bassBoost);
    chain.loudness().setTargetDb(static_cast<float>(s.loudnessMb) / 100.0f);
    chain.crossfeed().setEnabled(s.crossfeed);
    chain.limiter().setEnabled(s.limiter);
}

// Every chain hears every setting, live, staged or idle, so a chain that goes live at a later flush()
// already plays with the newest settings.
template <typename F>
void DspProcessor::forEachChain(F&& f) {
    for (auto& chain : chains_) f(*chain);
}

void DspProcessor::setEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_.enabled = enabled;
    forEachChain([&](DspChain& c) { c.equalizer().setEnabled(enabled); });
}

void DspProcessor::setBand(int band, int millibels) {
    if (band < 0 || band >= Equalizer::kBands) return;
    std::lock_guard<std::mutex> lock(mutex_);
    settings_.bandsMb[static_cast<size_t>(band)] = millibels;
    forEachChain([&](DspChain& c) { c.equalizer().setBand(band, millibels); });
}

void DspProcessor::setBassBoost(int permille) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_.bassBoost = permille;
    forEachChain([&](DspChain& c) { c.equalizer().setBassBoost(permille); });
}

void DspProcessor::setLoudnessMb(int millibels) {
    const int clamped = fx::clamp(fx::kLoudness, millibels);
    std::lock_guard<std::mutex> lock(mutex_);
    settings_.loudnessMb = clamped;
    forEachChain([&](DspChain& c) { c.loudness().setTargetDb(static_cast<float>(clamped) / 100.0f); });
}

void DspProcessor::setCrossfeed(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_.crossfeed = enabled;
    forEachChain([&](DspChain& c) { c.crossfeed().setEnabled(enabled); });
}

void DspProcessor::setLimiter(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_.limiter = enabled;
    forEachChain([&](DspChain& c) { c.limiter().setEnabled(enabled); });
}

}  // namespace geode::audio
