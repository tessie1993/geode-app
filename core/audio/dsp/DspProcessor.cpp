#include "audio/dsp/DspProcessor.hpp"

#include <algorithm>

namespace geode::audio {

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
    if (staged_) {
        live_.store(pending_, std::memory_order_release);
        pending_ = nullptr;
        staged_ = false;
    }
    if (DspChain* chain = live_.load(std::memory_order_acquire)) chain->reset();
}

void DspProcessor::process(float* interleaved, size_t capacityFloats, size_t frames, int channels) {
    DspChain* chain = live_.load(std::memory_order_acquire);
    if (!chain || !interleaved || channels <= 0 || chain->channels() != channels) return;
    frames = std::min(frames, capacityFloats / static_cast<size_t>(channels));
    if (frames == 0) return;
    gain_.process(interleaved, frames, channels, chain->sampleRate());
    chain->process(interleaved, frames);
}

void DspProcessor::processPcm16(int16_t* interleaved, size_t capacitySamples, size_t frames, int channels) {
    DspChain* chain = live_.load(std::memory_order_acquire);
    if (!chain || !interleaved || channels <= 0 || chain->channels() != channels) return;
    const size_t ch = static_cast<size_t>(channels);
    frames = std::min(frames, capacitySamples / ch);
    // In chunks through a float buffer this processor owns, so the audio thread never allocates. Each chunk
    // goes through the same gain and chain as a float block; every stage is a running state, so chunking
    // changes nothing in the result.
    const size_t chunkFrames = scratch_.size() / ch;
    for (size_t done = 0; done < frames; done += chunkFrames) {
        const size_t n = std::min(chunkFrames, frames - done);
        int16_t* block = interleaved + done * ch;
        for (size_t i = 0; i < n * ch; ++i) scratch_[i] = static_cast<float>(block[i]) / kShortFullScale;
        gain_.process(scratch_.data(), n, channels, chain->sampleRate());
        chain->process(scratch_.data(), n);
        // Truncated toward zero, as the Kotlin conversion this replaced did, so a block nothing changed comes
        // back bit-identical (s / 32768 * 32768 == s for every 16-bit s).
        for (size_t i = 0; i < n * ch; ++i) {
            const float v = std::clamp(scratch_[i], -1.0f, 1.0f) * kShortFullScale;
            block[i] = static_cast<int16_t>(std::clamp(static_cast<int>(v), -32768, 32767));
        }
    }
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
    const int clamped = std::clamp(millibels, 0, 1000);
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
