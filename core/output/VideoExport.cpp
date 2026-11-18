#include "output/VideoExport.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "audio/dsp/PcmTiming.hpp"
#include "output/EncodePlan.hpp"
#include "output/FrameClock.hpp"

namespace geode::output::videoexport {

namespace {

// MediaFormat.KEY_SAMPLE_RATE.
constexpr char kSampleRateKey[] = "sample-rate";

// What a drain's failure is shown with.
const char* drainDetail(Failure failure) {
    switch (failure) {
        case Failure::EncoderFailed:
            return "the video encoder failed";
        case Failure::Stopped:
            return "the encoded video could not be written";
        default:
            return nullptr;
    }
}

// A keyframe every second (VideoFormat's own default), as the export always asked for.
sink::VideoFormat videoFormat(const std::string& mime, int width, int height, const EncoderRung& rung) {
    sink::VideoFormat format;
    format.mime = mime.c_str();
    format.width = width;
    format.height = height;
    format.bitRate = rung.bitRate;
    format.frameRate = rung.fps;
    return format;
}

}  // namespace

float leadInProgress(float done) { return std::clamp(done, 0.0f, 1.0f) * kLeadInSpan; }

float frameProgress(int64_t frame, int64_t total) {
    if (total <= 0) return kLeadInSpan;
    return kLeadInSpan + static_cast<float>(frame) / static_cast<float>(total) * kFrameSpan;
}

int64_t audioDurationUs(const std::vector<AudioSample>& samples, const std::vector<sink::FormatEntry>& format) {
    if (samples.empty()) return 0;
    int rate = 0;
    for (const sink::FormatEntry& entry : format) {
        if (entry.key == kSampleRateKey && (entry.type == sink::kFormatInt32 || entry.type == sink::kFormatInt64)) {
            rate = static_cast<int>(entry.whole);
        }
    }
    return audio::aacEndUs(samples.back().ptsUs, rate);
}

// The file: a surface-fed video encoder and the already-encoded audio, drained into one muxer. The muxer starts
// when the encoder announces its format, with the video track first; codec-config buffers never reach it (the
// format carries them), and a video sample out before it started is dropped, as the platform's own order makes
// impossible. The audio goes in after each frame, every sample stamped before that frame's time.
class Job::Writer {
public:
    Writer(Host& host, const Request& request, int64_t limitUs) : host_(host), request_(request), limitUs_(limitUs) {}

    Failure openMuxer(const char** why) {
        codecs_ = host_.codecs();
        muxer_ = codecs_ ? codecs_->createMuxer() : nullptr;
        if (!muxer_) {
            *why = "the output file could not be opened";
            return Failure::Stopped;
        }
        audioFormat_ = codecs_->trackFormat(request_.audioFormat);
        if (!audioFormat_) {
            *why = "AAC encoder produced no format";
            return Failure::Stopped;
        }
        return Failure::None;
    }

    // A refused rung's codec is released here, before the next is made.
    bool openEncoder(const sink::VideoFormat& format) {
        std::unique_ptr<sink::Encoder> codec = codecs_->createEncoder(format.mime);
        if (!codec || !codec->configure(format)) return false;
        encoder_ = std::move(codec);
        return true;
    }

    bool start() { return encoder_->useSurfaceInput() && encoder_->start(); }
    void* window() const { return encoder_ ? encoder_->inputWindow() : nullptr; }
    bool endInput() { return encoder_->endSurfaceInput(); }
    bool muxing() const { return muxing_; }
    bool sawEndOfStream() const { return sawEndOfStream_; }

    // Moves what the encoder has ready into the file. Between frames it stops at the first empty poll; at the
    // end it keeps polling, waiting kIdleWaitUs on each empty one, until the end of stream or kMaxIdlePolls.
    Failure drain(bool untilEndOfStream) {
        int idlePolls = 0;
        bool done = false;
        while (!done && idlePolls < kMaxIdlePolls) {
            int index = -1;
            sink::BufferInfo info{};
            switch (encoder_->dequeueOutput(&index, &info)) {
                case sink::Out::FormatChanged:
                    // A change after the muxer started keeps the track it has: MP4 names its tracks once.
                    if (!muxing_ && !startMuxer()) return Failure::Stopped;
                    break;
                case sink::Out::Sample: {
                    size_t capacity = 0;
                    const uint8_t* data = encoder_->outputBuffer(index, &capacity);
                    // Bytes with no buffer, or a size the buffer cannot hold, is a broken codec: stop, never read
                    // past the buffer. A null buffer holding nothing (the end-of-stream marker) is no failure.
                    if (sink::brokenSample(info, data, capacity)) {
                        encoder_->releaseOutput(index);
                        return data ? Failure::Stopped : Failure::EmptyBuffer;
                    }
                    bool written = true;
                    if (muxing_ && info.size > 0 && (info.flags & sink::kFlagCodecConfig) == 0) {
                        written = muxer_->writeSample(videoTrack_, data, info);
                        if (written) wroteSample_ = true;
                    }
                    if (!encoder_->releaseOutput(index)) return Failure::EncoderFailed;
                    if (!written) return Failure::Stopped;
                    if ((info.flags & sink::kFlagEndOfStream) != 0) {
                        sawEndOfStream_ = true;
                        done = true;
                    }
                    break;
                }
                case sink::Out::None:
                    if (untilEndOfStream) host_.idleWait();
                    done = !untilEndOfStream;
                    ++idlePolls;
                    break;
                case sink::Out::Error:
                    return Failure::EncoderFailed;
            }
        }
        return Failure::None;
    }

    // Every audio sample stamped before upToUs (and before the audio's end), in order. False when one could not
    // be read or written.
    bool feedAudio(int64_t upToUs) {
        if (!muxing_) return true;
        const std::vector<AudioSample>& samples = request_.audio;
        while (next_ < samples.size()) {
            const AudioSample& sample = samples[next_];
            if (sample.ptsUs >= upToUs) return true;
            if (sample.ptsUs >= limitUs_) {
                next_ = samples.size();
                return true;
            }
            ++next_;
            const size_t size = static_cast<size_t>(std::max(sample.size, 0));
            if (scratch_.size() < std::max<size_t>(size, 1)) scratch_.resize(std::max<size_t>(size, 1));
            const int read = host_.readAudio(sample, scratch_.data());
            if (read < 0) return false;
            const sink::BufferInfo info{read, sample.ptsUs, sample.flags};
            if (!muxer_->writeSample(audioTrack_, scratch_.data(), info)) return false;
            wroteSample_ = true;
        }
        return true;
    }

    bool finish() {
        stopped_ = true;
        return muxer_->stop();
    }

    // The muxer first, then the encoder and with it the input window. A muxer that started but took no sample
    // is not stopped: its stop can only fail.
    void close() {
        if (muxer_ && muxing_ && !stopped_ && wroteSample_) muxer_->stop();
        muxer_.reset();
        if (encoder_) encoder_->stop();
        encoder_.reset();
        audioFormat_.reset();
        codecs_.reset();
    }

private:
    bool startMuxer() {
        const std::unique_ptr<sink::TrackFormat> format = encoder_->outputFormat();
        if (!format) return false;
        videoTrack_ = muxer_->addTrack(*format);
        if (videoTrack_ < 0) return false;
        audioTrack_ = muxer_->addTrack(*audioFormat_);
        if (audioTrack_ < 0 || !muxer_->start()) return false;
        muxing_ = true;
        return true;
    }

    Host& host_;
    const Request& request_;
    const int64_t limitUs_;
    std::unique_ptr<sink::Codecs> codecs_;
    std::unique_ptr<sink::Encoder> encoder_;
    std::unique_ptr<sink::Muxer> muxer_;
    std::unique_ptr<sink::TrackFormat> audioFormat_;
    int videoTrack_ = -1;
    int audioTrack_ = -1;
    bool muxing_ = false;
    bool stopped_ = false;
    bool wroteSample_ = false;
    bool sawEndOfStream_ = false;
    size_t next_ = 0;
    std::vector<uint8_t> scratch_;
};

Job::Job(const Request& request, Host& host)
    : request_(request), host_(host), durationUs_(audioDurationUs(request.audio, request.audioFormat)) {
    progress_.store(kLeadInSpan, std::memory_order_relaxed);
}

Job::End Job::fail(Failure failure, const char* detail) {
    failure_ = failure;
    detail_ = detail ? detail : "";
    return End::Failed;
}

void Job::run() {
    state_.store(State::Running, std::memory_order_release);
    Writer writer(host_, request_, durationUs_);
    End end = End::Failed;
    {
        // The teardown in its one order, however the render ends, a throw included: what holds GL names while
        // the context is current, then the muxer and the encoder with its window, then the surface.
        struct Teardown {
            Host& host;
            Writer& writer;
            ~Teardown() {
                host.closeScene();
                writer.close();
                host.closeSurface();
            }
        };
        const Teardown teardown{host_, writer};
        end = render(writer);
    }
    if (end == End::Done) progress_.store(1.0f, std::memory_order_relaxed);
    const State state = end == End::Done ? State::Rendered : end == End::Cancelled ? State::Cancelled : State::Failed;
    state_.store(state, std::memory_order_release);
}

Job::End Job::render(Writer& writer) {
    const char* why = nullptr;
    if (const Failure failure = writer.openMuxer(&why); failure != Failure::None) return fail(failure, why);

    // The audio says how long the file runs, and so how far the bit rate must come down for a long render to
    // stay under the muxer's limit.
    const int capped = encode::cappedVideoBps(request_.bitRate, durationUs_, request_.audioBytes);
    EncoderAttempt attempts[kAttemptMaxCount];
    const int codecs = std::clamp(request_.codecCount, 0, kAttemptMaxCodecs);
    const int count = encoderAttempts(encode::clampFps(request_.fps), capped, request_.topFps, codecs, attempts);
    bool opened = false;
    for (int i = 0; i < count && !opened; ++i) {
        const std::string& mime = request_.mimes[attempts[i].codec];
        opened = writer.openEncoder(videoFormat(mime, request_.width, request_.height, attempts[i].rung));
        if (opened) {
            fps_ = attempts[i].rung.fps;
            bitRate_ = attempts[i].rung.bitRate;
        }
    }
    if (!opened) return fail(Failure::EncoderRejected);
    if (!writer.start()) return fail(Failure::EncoderFailed, "the video encoder would not start");
    if ((why = host_.openSurface(writer.window()))) return fail(Failure::Stopped, why);

    const FrameClock clock(fps_, 0);
    frames_ = clock.framesFor(durationUs_);
    const int64_t durationMs = durationUs_ / 1000;
    if ((why = host_.prepareScene(Scene{fps_, durationMs}))) return fail(Failure::Stopped, why);
    if ((why = host_.prepareMark(durationMs))) return fail(Failure::Stopped, why);

    for (int64_t frame = 0; frame < frames_; ++frame) {
        if (cancelled_.load(std::memory_order_relaxed)) break;
        if (!host_.renderFrame(frame)) return fail(Failure::Stopped, "the track's audio could not be decoded");
        const int64_t timeMs = clock.frameTimeMs(frame);
        if ((why = host_.present(clock.ptsNs(frame), timeMs))) return fail(Failure::Stopped, why);
        if (const Failure failure = writer.drain(false); failure != Failure::None) {
            return fail(failure, drainDetail(failure));
        }
        if (!writer.feedAudio(timeMs * 1000)) return fail(Failure::Stopped, "the audio track could not be written");
        progress_.store(frameProgress(frame, frames_), std::memory_order_relaxed);
    }
    if (!writer.endInput()) return fail(Failure::EncoderFailed, "the video encoder would not end its input");
    // A cancelled file is discarded: its tail is not worth the drain.
    if (cancelled_.load(std::memory_order_relaxed)) return End::Cancelled;
    if (const Failure failure = writer.drain(true); failure != Failure::None) {
        return fail(failure, drainDetail(failure));
    }
    if (!writer.muxing()) return fail(Failure::NoOutput);
    if (!writer.sawEndOfStream()) return fail(Failure::Stalled);
    if (!writer.feedAudio(std::numeric_limits<int64_t>::max())) {
        return fail(Failure::Stopped, "the audio track could not be written");
    }
    if (!writer.finish()) return fail(Failure::Stopped, "the file could not be finished");
    return End::Done;
}

}  // namespace geode::output::videoexport
