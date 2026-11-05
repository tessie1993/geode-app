#include "output/TakeSession.hpp"

#include <android/log.h>
#include <android/native_window.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaMuxer.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <thread>
#include <vector>

namespace geode::output {

namespace {

constexpr const char* kTag = "GeodeTake";
constexpr int32_t kColorFormatSurface = 0x7F000789;  // MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface
constexpr int32_t kAacLc = 2;                         // MediaCodecInfo.CodecProfileLevel.AACObjectLC
constexpr int32_t kAudioBitrate = 192'000;
constexpr int kAudioChunkFrames = 1024;  // one AAC frame
constexpr int64_t kDrainWaitUs = 5'000;
constexpr int64_t kStopGiveUpNs = 3'000'000'000;
constexpr auto kInFlightWait = std::chrono::milliseconds(500);
// Encoded samples kept while the muxer waits for both tracks' formats; a stage that never draws must not
// grow this without bound (about 40 s of AAC frames).
constexpr size_t kMaxHeld = 2'000;

int64_t bootNs() {
    timespec ts{};
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
}

void freeCodec(AMediaCodec*& codec) {
    if (!codec) return;
    AMediaCodec_stop(codec);
    AMediaCodec_delete(codec);
    codec = nullptr;
}

}  // namespace

struct Take::Impl {
    AMediaCodec* video = nullptr;
    AMediaCodec* audio = nullptr;
    AMediaMuxer* muxer = nullptr;
    ssize_t videoTrack = -1;
    ssize_t audioTrack = -1;
    bool muxing = false;
    bool videoEos = false;
    bool audioEos = false;
    bool audioEosQueued = false;
    ssize_t heldInput = -1;  // an AAC input buffer dequeued and still waiting for sound
    struct Held {
        bool video;
        std::vector<uint8_t> data;
        AMediaCodecBufferInfo info;
    };
    std::vector<Held> held;
    std::unique_ptr<TakeFramePicker> frames;
    std::unique_ptr<TakeAudio> sound;
    std::thread worker;
    std::atomic<bool> stop{false};
    std::atomic<int64_t> stopAtNs{0};

    void write(bool isVideo, const uint8_t* data, const AMediaCodecBufferInfo& info) {
        const ssize_t track = isVideo ? videoTrack : audioTrack;
        if (muxing) {
            AMediaMuxer_writeSampleData(muxer, static_cast<size_t>(track), data, &info);
            return;
        }
        if (held.size() >= kMaxHeld) held.erase(held.begin());
        Held h{isVideo, std::vector<uint8_t>(data + info.offset, data + info.offset + info.size), info};
        h.info.offset = 0;
        held.push_back(std::move(h));
    }

    void maybeStartMuxer() {
        if (muxing || videoTrack < 0 || audioTrack < 0) return;
        if (AMediaMuxer_start(muxer) != AMEDIA_OK) {
            __android_log_print(ANDROID_LOG_WARN, kTag, "muxer refused to start");
            return;
        }
        muxing = true;
        for (const Held& h : held) {
            AMediaMuxer_writeSampleData(muxer, static_cast<size_t>(h.video ? videoTrack : audioTrack), h.data.data(), &h.info);
        }
        held.clear();
    }

    // Drains every finished buffer of one encoder into the muxer; waits up to waitUs for the first.
    void drain(AMediaCodec* codec, bool isVideo, int64_t waitUs) {
        bool& eos = isVideo ? videoEos : audioEos;
        while (!eos) {
            AMediaCodecBufferInfo info{};
            const ssize_t idx = AMediaCodec_dequeueOutputBuffer(codec, &info, waitUs);
            waitUs = 0;
            if (idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                AMediaFormat* format = AMediaCodec_getOutputFormat(codec);
                const ssize_t track = AMediaMuxer_addTrack(muxer, format);
                AMediaFormat_delete(format);
                (isVideo ? videoTrack : audioTrack) = track;
                maybeStartMuxer();
                continue;
            }
            if (idx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) continue;
            if (idx < 0) return;
            size_t capacity = 0;
            const uint8_t* data = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(idx), &capacity);
            // The codec-config buffer is the format's csd, which the muxer already has from addTrack.
            const bool config = (info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) != 0;
            if (data && info.size > 0 && !config) write(isVideo, data, info);
            if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) eos = true;
            AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(idx), false);
        }
    }

    // Hands the AAC encoder what TakeAudio has ready, and its end of stream once the take is stopping and
    // every frame (TakeAudio::padTo included) is in.
    void feedAudio(bool stopping) {
        while (!audioEosQueued) {
            const ssize_t idx = heldInput >= 0 ? heldInput : AMediaCodec_dequeueInputBuffer(audio, 0);
            if (idx < 0) return;
            heldInput = idx;
            size_t capacity = 0;
            uint8_t* buf = AMediaCodec_getInputBuffer(audio, static_cast<size_t>(idx), &capacity);
            if (!buf) return;
            const int room = std::min<int>(kAudioChunkFrames, static_cast<int>(capacity / (TakeAudio::kChannels * sizeof(int16_t))));
            const int64_t pts = sound->nextReadPtsUs();
            const int n = sound->read(reinterpret_cast<int16_t*>(buf), room, bootNs());
            if (n == 0) {
                if (!stopping) return;  // keep the buffer for the next pass
                AMediaCodec_queueInputBuffer(audio, static_cast<size_t>(idx), 0, 0, static_cast<uint64_t>(pts),
                                             AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                audioEosQueued = true;
            } else {
                AMediaCodec_queueInputBuffer(audio, static_cast<size_t>(idx), 0,
                                             static_cast<size_t>(n) * TakeAudio::kChannels * sizeof(int16_t),
                                             static_cast<uint64_t>(pts), 0);
            }
            heldInput = -1;
        }
    }
};

Take::~Take() {
    if (!impl_) return;
    if (impl_->worker.joinable()) {
        impl_->stopAtNs.store(bootNs());
        impl_->stop.store(true);
        impl_->worker.join();
    }
    freeCodec(impl_->video);
    freeCodec(impl_->audio);
    if (impl_->muxer) AMediaMuxer_delete(impl_->muxer);
    if (window_) ANativeWindow_release(window_);
}

bool Take::open(int fd, int screenW, int screenH, float displayFps, uint64_t generation, TakeLimits limits) {
    impl_ = std::make_unique<Impl>();
    generation_ = generation;
    limits_ = std::move(limits);
    const float fps = takeFps(displayFps);
    for (int attempt = 0; attempt < kMaxAttempts && !impl_->video; ++attempt) {
        const TakeSize size = takeSize(screenW, screenH, attempt);
        if (size.width == 0) break;
        AMediaCodec* codec = AMediaCodec_createEncoderByType("video/avc");
        if (!codec) break;
        AMediaFormat* format = AMediaFormat_new();
        AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "video/avc");
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_WIDTH, size.width);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_HEIGHT, size.height);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, kColorFormatSurface);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, takeBitrate(size, fps));
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_FRAME_RATE, static_cast<int32_t>(fps));
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
        const bool configured = AMediaCodec_configure(codec, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) == AMEDIA_OK;
        AMediaFormat_delete(format);
        ANativeWindow* window = nullptr;
        if (configured && AMediaCodec_createInputSurface(codec, &window) == AMEDIA_OK && AMediaCodec_start(codec) == AMEDIA_OK) {
            impl_->video = codec;
            window_ = window;
            size_ = size;
        } else {
            __android_log_print(ANDROID_LOG_INFO, kTag, "AVC encoder refused %dx%d", size.width, size.height);
            if (window) ANativeWindow_release(window);
            AMediaCodec_delete(codec);
        }
    }
    if (!impl_->video) return false;

    impl_->audio = AMediaCodec_createEncoderByType("audio/mp4a-latm");
    if (!impl_->audio) return false;
    AMediaFormat* format = AMediaFormat_new();
    AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, TakeAudio::kRate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, TakeAudio::kChannels);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_BIT_RATE, kAudioBitrate);
    AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_AAC_PROFILE, kAacLc);
    const bool audioOk = AMediaCodec_configure(impl_->audio, format, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) == AMEDIA_OK &&
                         AMediaCodec_start(impl_->audio) == AMEDIA_OK;
    AMediaFormat_delete(format);
    if (!audioOk) return false;

    impl_->muxer = AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
    if (!impl_->muxer) return false;

    // Time zero is now, after the encoders are up, so the picture does not start with their set-up time.
    const int64_t startNs = bootNs();
    const int64_t capUs = limits_.capMs > 0 ? limits_.capMs * 1000 : 0;
    impl_->frames = std::make_unique<TakeFramePicker>(startNs, fps, capUs);
    impl_->sound = std::make_unique<TakeAudio>(startNs, capUs);
    impl_->worker = std::thread([this] { run(); });
    return true;
}

void Take::run() {
    Impl& m = *impl_;
    for (;;) {
        const bool stopping = m.stop.load();
        m.feedAudio(stopping);
        m.drain(m.video, true, kDrainWaitUs);
        m.drain(m.audio, false, 0);
        if (m.videoEos && m.audioEos) return;
        // An encoder that never answers its end of stream must not hold the user's take hostage.
        if (stopping && bootNs() - m.stopAtNs.load() > kStopGiveUpNs) {
            __android_log_print(ANDROID_LOG_WARN, kTag, "encoders did not drain; finishing without them");
            return;
        }
    }
}

std::optional<int64_t> Take::beginFrame(int64_t frameNs) {
    if (closing_.load()) return std::nullopt;
    inFlight_.fetch_add(1);
    // Rechecked after counting in, so finish() either sees this frame in flight or this frame sees it closing.
    if (closing_.load()) {
        inFlight_.fetch_sub(1);
        return std::nullopt;
    }
    const std::optional<int64_t> us = impl_->frames->admit(frameNs);
    if (!us) {
        inFlight_.fetch_sub(1);
        return std::nullopt;
    }
    return *us * 1000;
}

void Take::endFrame() { inFlight_.fetch_sub(1); }

int64_t Take::finish() {
    closing_.store(true);
    const auto deadline = std::chrono::steady_clock::now() + kInFlightWait;
    while (inFlight_.load() > 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // From here the GL thread presents nothing more into the window; frames() and lastPtsUs() are settled.
    const int64_t lastVideoUs = impl_->frames->lastPtsUs();
    AMediaCodec_signalEndOfInputStream(impl_->video);
    impl_->sound->padTo(std::max<int64_t>(lastVideoUs, 0));
    impl_->stopAtNs.store(bootNs());
    impl_->stop.store(true);
    impl_->worker.join();
    const bool ok = impl_->muxing && impl_->frames->frames() > 0;
    if (impl_->muxing) AMediaMuxer_stop(impl_->muxer);
    if (!ok) return -1;
    return std::max(lastVideoUs, impl_->sound->nextReadPtsUs()) / 1000;
}

TakeSession& TakeSession::shared() {
    static TakeSession session;
    return session;
}

bool TakeSession::begin(int fd, float displayFps, TakeLimits limits) {
    const int w = stageW_.load();
    const int h = stageH_.load();
    uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> guard(lock_);
        if (take_ || w <= 0 || h <= 0 || fd < 0) return false;
        generation = ++generations_;
    }
    // Encoders are opened outside the lock: writeAudio takes it on the playback thread, which must not
    // wait out a codec set-up.
    std::shared_ptr<Take> take(new Take());
    if (!take->open(fd, w, h, displayFps, generation, std::move(limits))) return false;
    std::lock_guard<std::mutex> guard(lock_);
    if (take_) return false;
    take_ = std::move(take);
    return true;
}

int64_t TakeSession::end() {
    std::shared_ptr<Take> take;
    {
        std::lock_guard<std::mutex> guard(lock_);
        take = std::move(take_);
        take_.reset();
    }
    return take ? take->finish() : -1;
}

bool TakeSession::active() const {
    std::lock_guard<std::mutex> guard(lock_);
    return take_ != nullptr;
}

bool TakeSession::reachedCap() const {
    std::shared_ptr<Take> take = current();
    return take && take->impl_->frames->reachedCap();
}

void TakeSession::writeAudio(const int16_t* pcm, int frames, int channels, int rateHz) {
    std::shared_ptr<Take> take = current();
    if (take) take->impl_->sound->write(pcm, frames, channels, rateHz, bootNs());
}

void TakeSession::noteStageSize(int width, int height) {
    stageW_.store(width, std::memory_order_relaxed);
    stageH_.store(height, std::memory_order_relaxed);
}

std::shared_ptr<Take> TakeSession::current() const {
    std::lock_guard<std::mutex> guard(lock_);
    return take_;
}

}  // namespace geode::output
