#include "output/LoopMux.hpp"

#include <algorithm>
#include <limits>
#include <utility>

#include "output/LoopExtend.hpp"
#include "output/LoopPlan.hpp"

namespace geode::output::loopmux {

namespace {

// How many due soundtrack samples one pass takes from audioDue.
constexpr int kDueBatch = 512;

constexpr char kMismatch[] =
    "These loops were not encoded the same way, so they cannot share one video track. "
    "Render the reel again in one pass.";

struct Loop {
    std::unique_ptr<LoopReader> reader;
    std::vector<int64_t> timesUs;
    std::vector<int> flags;
};

// The muxer, stopped on the way out when it started and took a sample but was never finished.
struct Output {
    ~Output() {
        if (muxer && started && !stopped && wrote) muxer->stop();
    }
    std::unique_ptr<sink::Codecs> codecs;
    std::unique_ptr<sink::Muxer> muxer;
    int videoTrack = -1;
    int audioTrack = -1;
    bool started = false;
    bool stopped = false;
    bool wrote = false;
};

// The soundtrack samples due as the video goes in, in order, from where the last call left off.
class AudioFeed {
public:
    AudioFeed(Host& host, const std::vector<Clip>& clips) : host_(host), clips_(clips) {
        std::vector<int64_t> durations;
        for (const Clip& clip : clips_) {
            durations.push_back(clip.durationUs);
            counts_.push_back(static_cast<int>(clip.samples.size()));
            for (const videoexport::AudioSample& sample : clip.samples) ptsUs_.push_back(sample.ptsUs);
        }
        starts_.resize(clips_.size() + 1);
        loopext::clipStarts(durations.data(), static_cast<int>(clips_.size()), starts_.data());
    }

    // The soundtrack's length: the clips back to back.
    int64_t totalUs() const { return starts_.back(); }

    // Every sample due before untilUs into the muxer. Read or Write on a failure, else None.
    Failure writeUpTo(Output& out, int64_t untilUs) {
        int due = 0;
        do {
            int clip[kDueBatch];
            int sample[kDueBatch];
            int64_t pts[kDueBatch];
            due = loopext::audioDue(starts_.data(), counts_.data(), static_cast<int>(clips_.size()), ptsUs_.data(),
                                    cursor_, untilUs, clip, sample, pts, kDueBatch);
            for (int i = 0; i < due; ++i) {
                const videoexport::AudioSample& source =
                    clips_[static_cast<size_t>(clip[i])].samples[static_cast<size_t>(sample[i])];
                scratch_.resize(std::max<size_t>(static_cast<size_t>(std::max(source.size, 0)), 1));
                const int read = host_.readAudio(clip[i], source, scratch_.data());
                if (read < 0) return Failure::Read;
                if (!out.muxer->writeSample(out.audioTrack, scratch_.data(), {read, pts[i], source.flags})) {
                    return Failure::Write;
                }
                out.wrote = true;
            }
        } while (due == kDueBatch);
        return Failure::None;
    }

private:
    Host& host_;
    const std::vector<Clip>& clips_;
    std::vector<int64_t> starts_;
    std::vector<int> counts_;
    std::vector<int64_t> ptsUs_;
    loopext::AudioCursor cursor_;
    std::vector<uint8_t> scratch_;
};

const char* audioWords(Failure failure) {
    return failure == Failure::Read ? "a soundtrack sample could not be read" : "the soundtrack could not be written";
}

}  // namespace

Job::Job(const Request& request, Host& host) : request_(request), host_(host) {
    progress_.store(loopext::kAudioProgressSpan, std::memory_order_relaxed);
}

Job::End Job::fail(Failure failure, const char* detail) {
    failure_ = failure;
    detail_ = detail ? detail : "";
    return End::Failed;
}

void Job::run() {
    state_.store(State::Running, std::memory_order_release);
    const End end = copy();
    if (end == End::Done) progress_.store(1.0f, std::memory_order_relaxed);
    state_.store(end == End::Done ? State::Done : end == End::Cancelled ? State::Cancelled : State::Failed,
                 std::memory_order_release);
}

Job::End Job::copy() {
    if (request_.loops <= 0) return fail(Failure::Unusable, "There is no rendered loop to extend.");
    std::vector<Loop> loops(static_cast<size_t>(request_.loops));
    int bufferBytes = 0;
    for (int i = 0; i < request_.loops; ++i) {
        Loop& loop = loops[static_cast<size_t>(i)];
        const char* why = nullptr;
        loop.reader = host_.openLoop(i, &why);
        if (!loop.reader) return fail(Failure::Unusable, why);
        if (!loop.reader->scan(&loop.timesUs, &loop.flags)) {
            return fail(Failure::Read, "the rendered loop could not be read");
        }
        if (!(loop.reader->signature() == loops.front().reader->signature())) return fail(Failure::Unusable, kMismatch);
        const Signature& shape = loop.reader->signature();
        bufferBytes = std::max(bufferBytes, loopext::sampleBufferBytes(loop.reader->declaredBytes(), shape.width, shape.height));
    }

    AudioFeed audio(host_, request_.clips);
    const LoopExtendPlan plan = LoopExtendPlan::of(request_.loopDurationUs, audio.totalUs(), request_.loops);

    Output out;
    out.codecs = host_.codecs();
    out.muxer = out.codecs ? out.codecs->createMuxer() : nullptr;
    if (!out.muxer) return fail(Failure::Write, "the output file could not be opened");
    const std::unique_ptr<sink::TrackFormat> videoFormat = loops.front().reader->trackFormat();
    const std::unique_ptr<sink::TrackFormat> audioFormat = out.codecs->trackFormat(request_.audioFormat);
    if (!videoFormat || !audioFormat) return fail(Failure::Write, "the loop's or the soundtrack's format was refused");
    out.videoTrack = out.muxer->addTrack(*videoFormat);
    out.audioTrack = out.muxer->addTrack(*audioFormat);
    if (out.videoTrack < 0 || out.audioTrack < 0 || !out.muxer->start()) {
        return fail(Failure::Write, "the file's tracks could not be started");
    }
    out.started = true;

    std::vector<uint8_t> buffer(static_cast<size_t>(bufferBytes));
    std::vector<int64_t> pts;
    std::vector<int> flags;
    const std::vector<LoopWrite>& writes = plan.writes();
    for (size_t done = 0; done < writes.size(); ++done) {
        if (cancelled_.load(std::memory_order_relaxed)) return End::Cancelled;
        const LoopWrite& write = writes[done];
        Loop& loop = loops[static_cast<size_t>(write.loopIndex)];
        pts.resize(loop.timesUs.size());
        flags.resize(loop.timesUs.size());
        const int count = loopext::repeatSamples(loop.timesUs.data(), loop.flags.data(),
                                                 static_cast<int>(loop.timesUs.size()), write.offsetUs,
                                                 plan.totalDurationUs(), pts.data(), flags.data());
        if (!loop.reader->rewind()) return fail(Failure::Read, "the rendered loop could not be read");
        for (size_t i = 0; i < static_cast<size_t>(count); ++i) {
            const int size = loop.reader->next(buffer.data(), buffer.size());
            if (size < 0) break;
            if (!out.muxer->writeSample(out.videoTrack, buffer.data(), {size, pts[i], static_cast<uint32_t>(flags[i])})) {
                return fail(Failure::Write, "the rendered loop could not be written");
            }
            out.wrote = true;
        }
        if (const Failure failure = audio.writeUpTo(out, write.offsetUs + plan.loopDurationUs());
            failure != Failure::None) {
            return fail(failure, audioWords(failure));
        }
        progress_.store(loopext::copyProgress(static_cast<int>(done) + 1, plan.repeats()), std::memory_order_relaxed);
    }
    if (const Failure failure = audio.writeUpTo(out, std::numeric_limits<int64_t>::max()); failure != Failure::None) {
        return fail(failure, audioWords(failure));
    }
    out.stopped = true;
    if (!out.muxer->stop()) return fail(Failure::Write, "the file could not be finished");
    return End::Done;
}

}  // namespace geode::output::loopmux
