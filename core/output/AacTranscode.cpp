#include "output/AacTranscode.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <utility>

#include "audio/dsp/PcmConvert.hpp"
#include "audio/dsp/PcmTiming.hpp"

namespace geode::output::aac {

namespace {

constexpr int kMaxChannels = 2;
constexpr int64_t kUsPerMs = 1000;

sink::FormatEntry intEntry(const char* key, int value) {
    sink::FormatEntry entry;
    entry.key = key;
    entry.type = sink::kFormatInt32;
    entry.whole = value;
    return entry;
}

}  // namespace

std::vector<sink::FormatEntry> formatEntries(int sampleRateHz, int channels, int bitRate, const uint8_t* csd,
                                             size_t csdBytes) {
    std::vector<sink::FormatEntry> entries;
    sink::FormatEntry mime;
    mime.key = "mime";
    mime.type = sink::kFormatString;
    mime.text = sink::kMimeAac;
    entries.push_back(std::move(mime));
    entries.push_back(intEntry("sample-rate", sampleRateHz));
    entries.push_back(intEntry("channel-count", channels));
    entries.push_back(intEntry("bitrate", bitRate));
    if (csd && csdBytes > 0) {
        sink::FormatEntry config;
        config.key = "csd-0";
        config.type = sink::kFormatBuffer;
        config.bytes.assign(csd, csd + csdBytes);
        entries.push_back(std::move(config));
    }
    return entries;
}

Transcode::Transcode(const Request& request, std::unique_ptr<audio::ingest::AiffReader> aiff, Host& host)
    : request_(request), host_(host), aiff_(std::move(aiff)) {
    if (!aiff_ || aiff_->channels() <= 0) {
        fail(Failure::NoAudio, "No audio track in source file");
        return;
    }
    const int sourceChannels = aiff_->channels();
    read_.resize(static_cast<size_t>(kAiffReadSamples - kAiffReadSamples % sourceChannels));
    if (!openEncoder(aiff_->sampleRate(), sourceChannels)) return;
    int64_t skip = audio::samplesBeforeUs(request_.startMs * kUsPerMs, sourceRate_, sourceChannels);
    while (skip > 0) {
        const int want = static_cast<int>(std::min<int64_t>(skip, static_cast<int64_t>(read_.size())));
        const int n = aiff_->readS16(read_.data(), want);
        if (n <= 0) break;
        skip -= n;
    }
}

Transcode::Transcode(const Request& request, std::unique_ptr<audio::ingest::DecodePort> port, Host& host,
                     bool unreadable)
    : request_(request), host_(host), port_(std::move(port)) {
    if (!port_) {
        if (unreadable) {
            fail(Failure::Unreadable, "The track could not be read");
        } else {
            fail(Failure::NoAudio, "No audio track in source file");
        }
        return;
    }
    const int64_t maxUs = request_.maxDurationMs * kUsPerMs;
    const int64_t declaredUs = port_->declared().durationUs;
    if (maxUs > 0) {
        estimatedUs_ = maxUs;
    } else if (declaredUs > 0) {
        estimatedUs_ = std::max<int64_t>(declaredUs - request_.startMs * kUsPerMs, 0);
    }
}

Transcode::~Transcode() = default;

int64_t Transcode::durationUs() const {
    return samples_.empty() ? 0 : audio::aacEndUs(samples_.back().ptsUs, encoderRate_);
}

bool Transcode::fail(Failure failure, const char* detail) {
    if (state_ == State::Running) {
        state_ = State::Failed;
        failure_ = failure;
        detail_ = detail ? detail : "";
    }
    return false;
}

int Transcode::failStep(Failure failure, const char* detail) {
    fail(failure, detail);
    return -1;
}

bool Transcode::openEncoder(int sourceRate, int sourceChannels) {
    sourceRate_ = sourceRate;
    channels_ = std::min(sourceChannels, kMaxChannels);
    encoderRate_ = request_.outputRateHz > 0 ? request_.outputRateHz : audio::aacRateFor(sourceRate);
    if (sourceRate <= 0 || channels_ <= 0 || encoderRate_ <= 0) return fail(Failure::Codec, "Audio track has no rate or channels");
    if (encoderRate_ != sourceRate) resampler_ = std::make_unique<audio::PcmResampler>(sourceRate, encoderRate_, channels_);
    encoder_ = host_.createEncoder();
    if (!encoder_) return fail(Failure::Codec, "No AAC encoder on this device");
    sink::AudioFormat format;
    format.sampleRateHz = encoderRate_;
    format.channels = channels_;
    format.bitRate = kBitRate;
    if (!encoder_->configure(format)) return fail(Failure::Codec, "AAC encoder refused the format");
    if (!encoder_->start()) return fail(Failure::Codec, "AAC encoder did not start");
    return true;
}

void Transcode::resample(const int16_t* in, size_t frames) {
    carryAt_ = 0;
    const size_t samples = frames * static_cast<size_t>(channels_);
    if (!resampler_) {
        carry_.assign(in, in + samples);
        return;
    }
    carry_.resize(resampler_->maxOutFrames(frames) * static_cast<size_t>(channels_));
    const size_t out = resampler_->process(in, frames, carry_.data());
    carry_.resize(out * static_cast<size_t>(channels_));
}

bool Transcode::step() {
    if (state_ != State::Running) return false;
    bool moved = false;
    bool waited = false;
    try {
        if (aiff_) {
            stepAiff(&moved);
        } else {
            stepDecoder(&moved, &waited);
        }
        if (state_ != State::Running) return false;
        // A decoder that put out no audio at all still ends in a (silent) track, at the rate it declared.
        if (!encoder_ && port_ && sourceDone_) {
            const audio::ingest::DeclaredTrack declared = port_->declared();
            const int rate = declared.sampleRate > 0 ? declared.sampleRate
                                                     : (request_.outputRateHz > 0 ? request_.outputRateHz : audio::kMixRateHz);
            if (!openEncoder(rate, declared.channels > 0 ? declared.channels : kMaxChannels)) return false;
        }
        if (encoder_) {
            int fed = 0;
            if (carryAt_ < carry_.size()) {
                fed = feedEncoder();
            } else if (sourceDone_ && !eosSent_) {
                const int index = encoder_->dequeueInput();
                if (index == sink::Encoder::kInputError) return fail(Failure::Codec, "AAC encoder input failed");
                if (index >= 0) {
                    if (!encoder_->queueInput(index, 0, carryTimeUs_, sink::kFlagEndOfStream)) {
                        return fail(Failure::Codec, "AAC encoder refused its end of stream");
                    }
                    eosSent_ = true;
                    fed = 1;
                }
            }
            if (fed < 0) return false;
            const int drained = drainEncoder();
            if (drained < 0) return false;
            moved = moved || fed > 0 || drained > 0;
        }
    } catch (const std::exception& e) {
        return fail(Failure::Codec, e.what());
    }
    if (encoderDone_) {
        if (!format_) return fail(Failure::NoFormat, "AAC encoder produced no format");
        if (!host_.finish()) return fail(Failure::Write, "The audio could not be written to the app's cache");
        progress_ = 1.0f;
        state_ = State::Done;
        return false;
    }
    if (moved) {
        idleSteps_ = 0;
    } else {
        if (++idleSteps_ > kMaxIdleSteps) return fail(Failure::Stalled, "Audio transcode stalled (codec made no progress)");
        if (!waited) host_.idleWait();
    }
    return true;
}

void Transcode::stepAiff(bool* moved) {
    if (carryAt_ < carry_.size() || sourceDone_) return;
    *moved = true;
    const int64_t maxUs = request_.maxDurationMs * kUsPerMs;
    if (maxUs > 0 && carryTimeUs_ > maxUs) {
        sourceDone_ = true;
        return;
    }
    const int n = aiff_->readS16(read_.data(), static_cast<int>(read_.size()));
    if (n <= 0) {
        sourceDone_ = true;
        return;
    }
    const int sourceChannels = aiff_->channels();
    const auto frames = static_cast<size_t>(n / sourceChannels);
    if (sourceChannels <= kMaxChannels) {
        resample(read_.data(), frames);
    } else {
        folded_.resize(frames * static_cast<size_t>(channels_));
        audio::downmixS16(read_.data(), frames, sourceChannels, channels_, folded_.data());
        resample(folded_.data(), frames);
    }
    progress_ = aiff_->progress();
}

void Transcode::stepDecoder(bool* moved, bool* waited) {
    if (!inputDone_ && !sourceDone_) {
        inputDone_ = port_->feedInput();
        *waited = true;
    }
    if (sourceDone_ || carryAt_ < carry_.size()) return;
    audio::ingest::DecodedChunk chunk;
    *waited = true;
    if (!port_->takeOutput(chunk)) return;
    *moved = true;
    const int64_t startUs = request_.startMs * kUsPerMs;
    const int64_t maxUs = request_.maxDurationMs * kUsPerMs;
    // A buffer past the range's end is one the range never asked the decoder for.
    if (maxUs > 0 && chunk.ptsUs > startUs + maxUs) {
        port_->releaseOutput();
        sourceDone_ = true;
        return;
    }
    if (chunk.bytes > 0) takeChunk(chunk);
    port_->releaseOutput();
    if (chunk.endOfStream) sourceDone_ = true;
    if (estimatedUs_ > 0 && chunk.bytes > 0) {
        const float done = static_cast<float>(chunk.ptsUs - startUs) / static_cast<float>(estimatedUs_);
        progress_ = std::clamp(done, 0.0f, 1.0f);
    }
}

bool Transcode::takeChunk(const audio::ingest::DecodedChunk& chunk) {
    if (!encoder_ && !openEncoder(chunk.sampleRate, chunk.channels)) return false;
    const size_t samples = chunk.isFloat ? chunk.bytes / sizeof(float) : chunk.bytes / sizeof(int16_t);
    pcm_.resize(samples);
    if (chunk.isFloat) {
        floats_.resize(samples);
        std::memcpy(floats_.data(), chunk.data, samples * sizeof(float));
        audio::floatToS16(floats_.data(), samples, pcm_.data());
    } else {
        std::memcpy(pcm_.data(), chunk.data, samples * sizeof(int16_t));
    }
    const int16_t* pcm = pcm_.data();
    size_t count = samples;
    if (chunk.channels > channels_) {
        const size_t frames = samples / static_cast<size_t>(chunk.channels);
        folded_.resize(frames * static_cast<size_t>(channels_));
        audio::downmixS16(pcm_.data(), frames, chunk.channels, channels_, folded_.data());
        pcm = folded_.data();
        count = folded_.size();
    }
    const auto bytes = static_cast<int64_t>(count * sizeof(int16_t));
    const audio::PcmTrim trim =
        audio::trimS16(request_.startMs * kUsPerMs, chunk.ptsUs, bytes, sourceRate_, channels_);
    if (trim.skipBytes >= bytes) return false;
    const auto skip = static_cast<size_t>(trim.skipBytes) / sizeof(int16_t);
    resample(pcm + skip, (count - skip) / static_cast<size_t>(channels_));
    carryTimeUs_ = trim.ptsUs;
    return true;
}

int Transcode::feedEncoder() {
    const int index = encoder_->dequeueInput();
    if (index == sink::Encoder::kInputError) return failStep(Failure::Codec, "AAC encoder input failed");
    if (index < 0) return 0;
    size_t capacity = 0;
    uint8_t* buffer = encoder_->inputBuffer(index, &capacity);
    if (!buffer) return failStep(Failure::Codec, "encoder input buffer null (codec error state)");
    const size_t frameBytes = static_cast<size_t>(channels_) * sizeof(int16_t);
    const size_t room = capacity - capacity % frameBytes;
    if (room == 0) return failStep(Failure::Codec, "AAC encoder input buffer holds no frame");
    const size_t bytes = std::min(room, (carry_.size() - carryAt_) * sizeof(int16_t));
    std::memcpy(buffer, carry_.data() + carryAt_, bytes);
    if (!encoder_->queueInput(index, bytes, carryTimeUs_, 0)) return failStep(Failure::Codec, "AAC encoder refused its input");
    // The carry is at the encoder's rate already. An AIFF is stamped by everything it fed; a decoder's buffer
    // from its own PTS on.
    if (aiff_) {
        fedBytes_ += static_cast<int64_t>(bytes);
        carryTimeUs_ = audio::s16BytesToUs(fedBytes_, encoderRate_, channels_);
    } else {
        carryTimeUs_ += audio::s16BytesToUs(static_cast<int64_t>(bytes), encoderRate_, channels_);
    }
    carryAt_ += bytes / sizeof(int16_t);
    if (carryAt_ >= carry_.size()) {
        carry_.clear();
        carryAt_ = 0;
    }
    return 1;
}

int Transcode::drainEncoder() {
    int moved = 0;
    while (!encoderDone_) {
        int index = -1;
        sink::BufferInfo info;
        switch (encoder_->dequeueOutput(&index, &info)) {
            case sink::Out::None:
                return moved;
            case sink::Out::Error:
                return failStep(Failure::Codec, "AAC encoder output failed");
            case sink::Out::FormatChanged:
                format_ = encoder_->outputFormat();
                if (!format_) return failStep(Failure::Codec, "AAC encoder output format null");
                moved = 1;
                continue;
            case sink::Out::Sample:
                break;
        }
        moved = 1;
        size_t capacity = 0;
        const uint8_t* data = encoder_->outputBuffer(index, &capacity);
        if (info.size < 0 || (info.size > 0 && (!data || static_cast<size_t>(info.size) > capacity))) {
            encoder_->releaseOutput(index);
            return failStep(Failure::Codec, "encoder output buffer null (codec error state)");
        }
        if (info.size > 0 && (info.flags & sink::kFlagCodecConfig) == 0) {
            if (!host_.write(data, static_cast<size_t>(info.size))) {
                encoder_->releaseOutput(index);
                return failStep(Failure::Write, "The audio could not be written to the app's cache");
            }
            samples_.push_back({outBytes_, info.size, info.ptsUs, info.flags});
            outBytes_ += info.size;
        }
        const bool eos = (info.flags & sink::kFlagEndOfStream) != 0;
        if (!encoder_->releaseOutput(index)) return failStep(Failure::Codec, "AAC encoder output not released");
        if (eos) encoderDone_ = true;
    }
    return moved;
}

}  // namespace geode::output::aac
