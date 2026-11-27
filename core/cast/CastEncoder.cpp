#include "cast/CastEncoder.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

#include "util/ThreadNice.hpp"

namespace geode::cast {

namespace sink = output::sink;

PcmBacklog::PcmBacklog(int rateHz, int channels)
    : rateHz_(rateHz), channels_(channels > 0 ? channels : 1), feed_(viz::frame::CastAudioFeed::started(rateHz)) {}

void PcmBacklog::start() {
    std::lock_guard<std::mutex> hold(lock_);
    feed_ = viz::frame::CastAudioFeed::started(rateHz_);
    pcm_.clear();
    pcm_.reserve(static_cast<size_t>(feed_.capacity()) * static_cast<size_t>(channels_));
    active_ = true;
}

void PcmBacklog::stop() {
    std::lock_guard<std::mutex> hold(lock_);
    active_ = false;
    pcm_.clear();
    feed_ = viz::frame::CastAudioFeed::started(rateHz_);
}

bool PcmBacklog::active() const {
    std::lock_guard<std::mutex> hold(lock_);
    return active_;
}

void PcmBacklog::push(const int16_t* pcm, int frames) {
    if (!pcm || frames <= 0) return;
    std::lock_guard<std::mutex> hold(lock_);
    if (!active_) return;
    const auto ch = static_cast<size_t>(channels_);
    const int64_t pending = feed_.pending;
    const int64_t drop = feed_.add(frames);
    // The oldest go first: from what waits, then from the head of this block.
    const int64_t fromBacklog = std::min(drop, pending);
    pcm_.erase(pcm_.begin(), pcm_.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(fromBacklog) * ch));
    const int64_t skip = drop - fromBacklog;
    pcm_.insert(pcm_.end(), pcm + static_cast<size_t>(skip) * ch, pcm + static_cast<size_t>(frames) * ch);
}

int PcmBacklog::take(int16_t* out, int capacityFrames, int64_t* ptsUs) {
    if (!out || !ptsUs || capacityFrames <= 0) return 0;
    std::lock_guard<std::mutex> hold(lock_);
    if (!active_) return 0;
    const int64_t n = feed_.take(capacityFrames, *ptsUs);
    if (n <= 0) return 0;
    const size_t samples = static_cast<size_t>(n) * static_cast<size_t>(channels_);
    std::copy(pcm_.begin(), pcm_.begin() + static_cast<std::ptrdiff_t>(samples), out);
    pcm_.erase(pcm_.begin(), pcm_.begin() + static_cast<std::ptrdiff_t>(samples));
    return static_cast<int>(n);
}

int64_t PcmBacklog::pending() const {
    std::lock_guard<std::mutex> hold(lock_);
    return active_ ? feed_.pending : 0;
}

CastEncoder::CastEncoder(std::unique_ptr<sink::Codecs> codecs, const Format& format, Outputs outputs)
    : codecs_(std::move(codecs)),
      format_(format),
      outputs_(std::move(outputs)),
      writer_({static_cast<uint32_t>(format.width), static_cast<uint32_t>(format.height),
               static_cast<uint32_t>(format.rateHz), static_cast<uint16_t>(format.channels),
               static_cast<uint32_t>(format.audioBitRate)},
              int64_t{format.keyframeSec} * 1'000'000, outputs_.stream),
      aacPcm_(format.rateHz, format.channels),
      opusPcm_(format.rateHz, format.channels) {}

std::unique_ptr<CastEncoder> CastEncoder::open(std::unique_ptr<sink::Codecs> codecs, const Format& format,
                                               Outputs outputs) {
    if (!codecs || format.width <= 0 || format.height <= 0 || format.rateHz <= 0 || format.channels <= 0) return nullptr;
    std::unique_ptr<CastEncoder> e(new CastEncoder(std::move(codecs), format, std::move(outputs)));
    sink::VideoFormat video;
    video.width = format.width;
    video.height = format.height;
    video.bitRate = format.videoBitRate;
    video.frameRate = format.fps;
    video.iFrameIntervalSec = format.keyframeSec;
    e->video_ = e->codecs_->createEncoder(video.mime);
    if (!e->video_ || !e->video_->configure(video) || !e->video_->useSurfaceInput()) return nullptr;
    sink::AudioFormat audio;
    audio.sampleRateHz = format.rateHz;
    audio.channels = format.channels;
    audio.bitRate = format.audioBitRate;
    e->aac_ = e->codecs_->createEncoder(audio.mime);
    if (!e->aac_ || !e->aac_->configure(audio)) return nullptr;
    // Both run before the surface may be drawn into.
    if (!e->video_->start() || !e->aac_->start()) return nullptr;
    e->aacPcm_.start();
    return e;
}

CastEncoder::~CastEncoder() {
    stopping_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    closeOpus();
    // The codecs go with their members, the video one last of the three releasing its surface.
}

void* CastEncoder::inputWindow() const { return video_ ? video_->inputWindow() : nullptr; }

void CastEncoder::queuePcm(const int16_t* pcm, int frames) {
    aacPcm_.push(pcm, frames);
    opusPcm_.push(pcm, frames);   // keeps nothing while no Opus encoder runs
}

void CastEncoder::start() {
    if (thread_.joinable()) return;
    thread_ = std::thread([this] { run(); });
}

void CastEncoder::run() {
    // Opened on the cast render thread: below it, so the drain never takes the frame's CPU, yet above app threads,
    // so a codec's full output never stalls the swap that feeds it.
    util::setThreadNice(util::kFeedNice);
    using Clock = std::chrono::steady_clock;
    while (!stopping_.load(std::memory_order_acquire)) {
        const int64_t now =
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
        bool moved = false;
        try {
            moved = step(now);
        } catch (...) {
            // Out of memory in the writer or a backlog: an exception must not leave the thread.
            failed_.store(true, std::memory_order_release);
        }
        if (failed()) return;
        if (!moved) std::this_thread::sleep_for(std::chrono::milliseconds(kIdleMs));
    }
}

int CastEncoder::feed(sink::Encoder& codec, PcmBacklog& backlog) {
    int moved = 0;
    while (backlog.pending() > 0) {
        const int index = codec.dequeueInput();
        if (index == sink::Encoder::kInputError) return -1;
        if (index < 0) break;
        size_t capacity = 0;
        uint8_t* buffer = codec.inputBuffer(index, &capacity);
        if (!buffer) return -1;
        const size_t frameBytes = static_cast<size_t>(format_.channels) * sizeof(int16_t);
        const int room = static_cast<int>(std::min<size_t>(sink::EncodeSink::kPcmFrames, capacity / frameBytes));
        scratch_.resize(static_cast<size_t>(sink::EncodeSink::kPcmFrames) * static_cast<size_t>(format_.channels));
        int64_t ptsUs = 0;
        const int frames = backlog.take(scratch_.data(), room, &ptsUs);
        const size_t bytes = static_cast<size_t>(std::max(frames, 0)) * frameBytes;
        if (bytes > 0) std::memcpy(buffer, scratch_.data(), bytes);
        if (!codec.queueInput(index, bytes, ptsUs, 0)) return -1;
        moved = 1;
        if (frames <= 0) break;
    }
    return moved;
}

int CastEncoder::drain(sink::Encoder& codec, int which) {
    int moved = 0;
    for (int i = 0; i < kDrainPerStep; ++i) {
        int index = -1;
        sink::BufferInfo info;
        const sink::Out out = codec.dequeueOutput(&index, &info);
        if (out == sink::Out::None) break;
        if (out == sink::Out::Error) return -1;
        moved = 1;
        if (out == sink::Out::FormatChanged) continue;   // the codec config buffers carry what is needed
        size_t capacity = 0;
        const uint8_t* data = codec.outputBuffer(index, &capacity);
        if (info.size < 0 || (info.size > 0 && (!data || static_cast<size_t>(info.size) > capacity))) {
            codec.releaseOutput(index);
            return -1;
        }
        const auto size = static_cast<size_t>(info.size);
        const bool config = (info.flags & sink::kFlagCodecConfig) != 0;
        const bool key = (info.flags & sink::kFlagKeyFrame) != 0;
        if (size > 0) {
            if (which == kVideo) {
                if (config) {
                    writer_.videoConfig(data, size);
                    if (outputs_.videoConfig) outputs_.videoConfig(data, size);
                } else {
                    writer_.video(data, size, info.ptsUs, key);
                    if (outputs_.video) outputs_.video(data, size, info.ptsUs, key);
                }
            } else if (which == kAac) {
                if (config) {
                    writer_.audioConfig(data, size);
                } else {
                    writer_.audio(data, size, info.ptsUs);
                }
            } else if (!config && outputs_.opus) {
                // The Opus header is not sent: WebRTC carries no such thing.
                outputs_.opus(data, size, info.ptsUs);
            }
        }
        if (!codec.releaseOutput(index)) return -1;
    }
    return moved;
}

void CastEncoder::closeOpus() {
    opusPcm_.stop();
    opus_.reset();
}

void CastEncoder::runOpus(int64_t nowNs, bool& moved) {
    const int peers = outputs_.peers ? outputs_.peers() : 0;
    if (peers > 0) {
        opusIdleSinceNs_ = 0;
        if (!opus_ && !opusMissing_) {
            sink::AudioFormat audio;
            audio.mime = kMimeOpus;
            audio.sampleRateHz = format_.rateHz;
            audio.channels = format_.channels;
            audio.bitRate = format_.audioBitRate;
            opus_ = codecs_->createEncoder(audio.mime);
            if (opus_ && opus_->configure(audio) && opus_->start()) {
                opusPcm_.start();
            } else {
                opus_.reset();
                opusMissing_ = true;
            }
        }
    } else if (opus_) {
        if (opusIdleSinceNs_ == 0) opusIdleSinceNs_ = nowNs;
        if (nowNs - opusIdleSinceNs_ > kOpusIdleNs) {
            closeOpus();
            return;
        }
    }
    if (!opus_) return;
    const int fed = feed(*opus_, opusPcm_);
    const int drained = fed < 0 ? -1 : drain(*opus_, kOpus);
    if (fed < 0 || drained < 0) {
        // Only the browser's sound is lost; the stream goes on.
        closeOpus();
        opusMissing_ = true;
        return;
    }
    moved = moved || fed > 0 || drained > 0;
}

bool CastEncoder::step(int64_t nowNs) {
    if (failed() || !video_ || !aac_) return false;
    const int fed = feed(*aac_, aacPcm_);
    const int video = fed < 0 ? -1 : drain(*video_, kVideo);
    const int sound = video < 0 ? -1 : drain(*aac_, kAac);
    if (fed < 0 || video < 0 || sound < 0) {
        failed_.store(true, std::memory_order_release);
        return false;
    }
    bool moved = fed > 0 || video > 0 || sound > 0;
    runOpus(nowNs, moved);
    return moved;
}

}  // namespace geode::cast
