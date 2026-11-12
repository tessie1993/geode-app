#include "output/LoopRender.hpp"

#include <algorithm>
#include <utility>

#include "output/EncodePlan.hpp"
#include "viz/frame/LoopSeam.hpp"

namespace geode::output::looprender {

namespace {

namespace loopseam = geode::viz::frame::loopseam;

// A keyframe every second (VideoFormat's own default) keeps a seam preview cheap to scrub and lets the extend
// step start every repeat on an IDR.
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

void driftParams(std::vector<float>& params, int colorShiftSlot, int temperatureSlot, float hueOffset,
                 float warmthOffset) {
    const int count = static_cast<int>(params.size());
    const bool hasShift = colorShiftSlot >= 0 && colorShiftSlot < count;
    const bool hasTemperature = temperatureSlot >= 0 && temperatureSlot < count;
    float shift = 0.0f;
    float temperature = 0.0f;
    applyDrift(hasShift ? params[static_cast<size_t>(colorShiftSlot)] : 0.0f,
               hasTemperature ? params[static_cast<size_t>(temperatureSlot)] : 0.0f, hueOffset, warmthOffset, &shift,
               &temperature);
    if (hasShift) params[static_cast<size_t>(colorShiftSlot)] = shift;
    if (hasTemperature) params[static_cast<size_t>(temperatureSlot)] = temperature;
}

// One stop's video-only file: a surface-fed encoder drained into a muxer with one track. The muxer starts
// when the encoder announces its format; codec config buffers never reach it (the format carries them).
class Job::Writer {
public:
    Failure open(std::unique_ptr<sink::Codecs> codecs, const sink::VideoFormat& format) {
        codecs_ = std::move(codecs);
        if (!codecs_) return Failure::EncoderFailed;
        encoder_ = codecs_->createEncoder(format.mime);
        if (!encoder_ || !encoder_->configure(format)) return Failure::EncoderFailed;
        muxer_ = codecs_->createMuxer();
        if (!muxer_) return Failure::CacheWrite;
        if (!encoder_->useSurfaceInput() || !encoder_->start()) return Failure::EncoderFailed;
        return Failure::None;
    }

    void* window() const { return encoder_ ? encoder_->inputWindow() : nullptr; }

    // Moves what the encoder has ready into the file. Between frames it stops at the first empty poll; at the
    // end it keeps polling, waiting kIdleWaitUs on each empty one, until the end of stream or kMaxIdlePolls.
    Failure drain(bool untilEndOfStream, Host& host) {
        int idlePolls = 0;
        bool done = false;
        while (!done && idlePolls < kMaxIdlePolls) {
            int index = -1;
            sink::BufferInfo info{};
            switch (encoder_->dequeueOutput(&index, &info)) {
                case sink::Out::FormatChanged: {
                    const std::unique_ptr<sink::TrackFormat> format = encoder_->outputFormat();
                    if (!format) return Failure::Stopped;
                    track_ = muxer_->addTrack(*format);
                    if (track_ < 0 || !muxer_->start()) return Failure::Stopped;
                    muxing_ = true;
                    break;
                }
                case sink::Out::Sample: {
                    bool written = true;
                    if (muxing_ && track_ >= 0 && info.size > 0 && (info.flags & sink::kFlagCodecConfig) == 0) {
                        const uint8_t* data = encoder_->outputBuffer(index);
                        written = data != nullptr && muxer_->writeSample(track_, data, info);
                        if (written) sampleWritten_ = true;
                    }
                    encoder_->releaseOutput(index);
                    if (!written) return Failure::Stopped;
                    if ((info.flags & sink::kFlagEndOfStream) != 0) {
                        sawEndOfStream_ = true;
                        done = true;
                    }
                    break;
                }
                case sink::Out::None:
                    if (untilEndOfStream) host.idleWait();
                    done = !untilEndOfStream;
                    ++idlePolls;
                    break;
                case sink::Out::Error:
                    return Failure::EncoderFailed;
            }
        }
        return Failure::None;
    }

    Failure finish(Host& host) {
        if (!encoder_->endSurfaceInput()) return Failure::EncoderFailed;
        if (const Failure failure = drain(true, host); failure != Failure::None) return failure;
        if (!muxing_) return Failure::NoOutput;
        if (!sawEndOfStream_) return Failure::Stalled;
        muxed_ = true;
        return muxer_->stop() ? Failure::None : Failure::Stopped;
    }

    // The muxer first, then the encoder and with it the input window. A muxer that started but took no sample
    // is not stopped: its stop can only fail.
    void close() {
        if (muxer_ && muxing_ && !muxed_ && sampleWritten_) muxer_->stop();
        muxer_.reset();
        if (encoder_) encoder_->stop();
        encoder_.reset();
    }

private:
    std::unique_ptr<sink::Codecs> codecs_;
    std::unique_ptr<sink::Encoder> encoder_;
    std::unique_ptr<sink::Muxer> muxer_;
    int track_ = -1;
    bool muxing_ = false;
    bool muxed_ = false;
    bool sampleWritten_ = false;
    bool sawEndOfStream_ = false;
};

Job::Job(const Request& request, Host& host) : request_(request), host_(host) {}

Job::End Job::fail(Failure failure, const char* detail) {
    failure_ = failure;
    detail_ = detail ? detail : "";
    return End::Failed;
}

void Job::finish(State state) { state_.store(state, std::memory_order_release); }

// The reel is short, but the extend step copies its samples into the long-form file once per repeat, so that
// file's size is this rate times the long form's length. This is the only point the rate can still change:
// past the muxer's limit the extend would fail after the whole reel had rendered. The first attempt an encoder
// configures with is the one every stop uses.
bool Job::negotiate() {
    const int64_t longFormUs = request_.longFormMs * 1000;
    const int capped = encode::cappedVideoBps(request_.bitRate, longFormUs, encode::audioBytes(longFormUs));
    EncoderAttempt attempts[kAttemptMaxCount];
    const int codecs = std::clamp(request_.codecCount, 0, kAttemptMaxCodecs);
    const int count = encoderAttempts(request_.fps, capped, request_.topFps, codecs, attempts);
    const std::unique_ptr<sink::Codecs> probe = host_.codecs(-1);
    if (!probe) return false;
    for (int i = 0; i < count; ++i) {
        const std::string& mime = request_.mimes[attempts[i].codec];
        const std::unique_ptr<sink::Encoder> encoder = probe->createEncoder(mime.c_str());
        if (!encoder || !encoder->configure(videoFormat(mime, request_.width, request_.height, attempts[i].rung))) {
            continue;
        }
        mime_ = mime;
        fps_ = attempts[i].rung.fps;
        bitRate_ = attempts[i].rung.bitRate;
        // The seam shortened to what the stash's budget holds at this size.
        spec_ = LoopSpec{request_.loopMs, request_.crossfadeMs, fps_}.budgeted(request_.width, request_.height);
        return true;
    }
    return false;
}

void Job::run() {
    state_.store(State::Running, std::memory_order_release);
    if (!negotiate()) {
        fail(Failure::EncoderRejected);
        finish(State::Failed);
        return;
    }
    DriftStop stops[kMaxDriftStops];
    const int count =
        std::min(driftStops(request_.hueTurns, request_.warmth, request_.stops, stops, kMaxDriftStops), kMaxDriftStops);
    int seamFrames = spec_.crossfadeFrames();
    End end = End::Done;
    for (int i = 0; i < count && end == End::Done; ++i) {
        std::string path;
        const int fd = host_.createFile(stops[i].index, &path);
        if (fd < 0) {
            end = fail(Failure::CacheWrite, path.c_str());
            break;
        }
        files_.push_back(path);
        int kept = 0;
        end = renderStop(stops[i], i, count, fd, seamFrames, &kept);
        host_.closeFile(fd);
        // A later stop cannot hold more seam frames than an earlier one did, so the reel reports the shortest
        // seam any of its loops used.
        if (end == End::Done) seamFrames = std::min(seamFrames, kept);
    }
    if (end != End::Done) {
        for (const std::string& file : files_) host_.removeFile(file);
        files_.clear();
        finish(end == End::Cancelled ? State::Cancelled : State::Failed);
        return;
    }
    progress_.store(1.0f, std::memory_order_relaxed);
    crossfadeMs_ = loopseam::reportedCrossfadeMs(seamFrames, fps_);
    finish(State::Rendered);
}

Job::End Job::renderStop(const DriftStop& stop, int index, int count, int fd, int crossfadeFrames, int* seamFrames) {
    Writer writer;
    const sink::VideoFormat format = videoFormat(mime_, request_.width, request_.height, EncoderRung{fps_, bitRate_});
    End end = End::Done;
    if (const Failure failure = writer.open(host_.codecs(fd), format); failure != Failure::None) {
        end = fail(failure);
    } else {
        end = drawStop(writer, stop, index, count, crossfadeFrames, seamFrames);
    }
    host_.closeScene();
    writer.close();
    host_.closeSurface();
    return end;
}

Job::End Job::drawStop(Writer& writer, const DriftStop& stop, int index, int count, int crossfadeFrames,
                       int* seamFrames) {
    if (const char* why = host_.openSurface(writer.window())) return fail(Failure::Stopped, why);
    std::string seamError;
    const int kept = host_.allocateSeam(crossfadeFrames, &seamError);
    if (kept < 0) return fail(Failure::ShaderCompile, seamError.c_str());
    if (kept == 0) return fail(Failure::LowGpuMemory);
    // One loop plus its tail: the tail is the loop's natural continuation, which the opening frames dissolve
    // out of. The engine settings are pinned for exactly that long.
    const int loopFrames = spec_.loopFrames();
    const int sourceFrames = loopFrames + kept;
    const StopScene scene{fps_, static_cast<int64_t>(sourceFrames) * 1000 / fps_, stop.hueOffset, stop.warmthOffset};
    if (const char* why = host_.prepareScene(scene)) return fail(Failure::Stopped, why);
    if (const char* why = host_.prepareMark()) return fail(Failure::Stopped, why);
    for (int frame = 0; frame < sourceFrames; ++frame) {
        if (cancelled_.load(std::memory_order_relaxed)) return End::Cancelled;
        if (!host_.renderFrame(frame)) return fail(Failure::Stopped, "the track's audio could not be decoded");
        const loopseam::Step step = loopseam::step(frame, loopFrames, kept);
        if (step.op == loopseam::Op::Capture) {
            host_.captureSeam(step.index);
        } else {
            if (step.op == loopseam::Op::BlendPresent) host_.blendSeam(step.index, step.tail, kept);
            if (const char* why = host_.present(loopseam::ptsNs(frame, kept, fps_))) return fail(Failure::Stopped, why);
            if (const Failure failure = writer.drain(false, host_); failure != Failure::None) return fail(failure);
        }
        progress_.store(loopseam::progress(index, count, frame, loopFrames, kept), std::memory_order_relaxed);
    }
    if (const Failure failure = writer.finish(host_); failure != Failure::None) return fail(failure);
    *seamFrames = kept;
    return End::Done;
}

}  // namespace geode::output::looprender
