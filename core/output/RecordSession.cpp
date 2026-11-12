#include "output/RecordSession.hpp"

#include <chrono>
#include <utility>

namespace geode::output::record {

namespace {

constexpr int64_t kNsPerMs = 1'000'000;

}  // namespace

// The sink's questions, answered by the recorder's clock.
class Session::Host final : public sink::Host {
public:
    explicit Host(Session& session) : s_(session) {}

    bool ending() override { return s_.recorder_->stopped() != Stop::None; }
    void inputClosing() override { s_.waitForTee(); }
    int takePcm(int16_t* out, int capacityFrames, int64_t* ptsUs) override {
        return s_.recorder_->takeAudio(s_.now_(), out, capacityFrames, ptsUs);
    }
    void videoEncoded() override { s_.recorder_->videoEncoded(); }
    void wrote(int64_t bytes) override { s_.recorder_->wrote(bytes, s_.now_()); }
    void failed() override { s_.recorder_->stop(Stop::Failed, s_.now_()); }

private:
    Session& s_;
};

Session::Session(std::shared_ptr<Recorder> recorder, std::unique_ptr<sink::Codecs> codecs,
                 std::function<int64_t()> now)
    : recorder_(std::move(recorder)), now_(std::move(now)), codecs_(std::move(codecs)) {}

std::unique_ptr<Session> Session::open(std::shared_ptr<Recorder> recorder, std::unique_ptr<sink::Codecs> codecs,
                                       int stageWidth, int stageHeight, int shortSide, int fps,
                                       std::function<int64_t()> now) {
    if (!recorder || !codecs || !now) return nullptr;
    std::unique_ptr<Session> s(new Session(std::move(recorder), std::move(codecs), std::move(now)));
    s->muxer_ = s->codecs_->createMuxer();
    if (!s->muxer_) return nullptr;
    s->host_ = std::make_unique<Host>(*s);
    s->sink_ = std::make_unique<sink::EncodeSink>(*s->codecs_, *s->muxer_, *s->host_);
    const int64_t limitMs = s->recorder_->limitNs() / kNsPerMs;
    const sink::Rungs rungs = [=](int attempt, sink::VideoFormat* out) {
        EncoderAsk ask;
        if (!encoderAttempt(stageWidth, stageHeight, shortSide, fps, limitMs, attempt, &ask)) return false;
        sink::VideoFormat format;
        format.mime = sink::kMimeAvc;
        format.width = ask.width;
        format.height = ask.height;
        format.colorFormat = sink::kColorFormatSurface;
        format.bitRate = ask.videoBps;
        // The asked rate, unchanged across rungs; the clock caps what it keeps.
        format.frameRate = fps;
        format.iFrameIntervalSec = 1;
        *out = format;
        return true;
    };
    sink::AudioFormat audio;
    audio.mime = sink::kMimeAac;
    audio.sampleRateHz = Recorder::kAudioRateHz;
    audio.channels = Recorder::kChannels;
    audio.aacProfile = sink::kAacProfileLc;
    audio.bitRate = encode::kAacBps;
    if (!s->sink_->open(rungs, audio)) return nullptr;
    s->width_ = s->sink_->video().width;
    s->height_ = s->sink_->video().height;
    try {
        s->thread_ = std::thread(&Session::run, s.get());
    } catch (...) {
        return nullptr;
    }
    return s;
}

Session::~Session() {
    if (!thread_.joinable()) return;
    if (state() != State::Saved && state() != State::Failed) recorder_->stop(Stop::Failed, now_());
    teeClosed();
    thread_.join();
}

void Session::run() {
    bool saved = false;
    try {
        while (!sink_->done()) {
            if (!sink_->step()) std::this_thread::sleep_for(std::chrono::milliseconds(kIdleMs));
        }
        saved = sink_->finish();
    } catch (...) {
        recorder_->stop(Stop::Failed, now_());
        waitForTee();
        saved = false;
    }
    {
        std::lock_guard<std::mutex> lock(windowLock_);
        // The muxer first, then the encoders and their surface.
        muxer_.reset();
        sink_.reset();
    }
    state_.store(static_cast<int>(saved ? State::Saved : State::Failed), std::memory_order_release);
}

void Session::waitForTee() {
    int running = static_cast<int>(State::Running);
    state_.compare_exchange_strong(running, static_cast<int>(State::Closing), std::memory_order_acq_rel);
    std::unique_lock<std::mutex> lock(teeLock_);
    teeChanged_.wait_for(lock, std::chrono::milliseconds(kTeeWaitMs), [this] { return teeClosed_; });
}

void Session::teeClosed() {
    {
        std::lock_guard<std::mutex> lock(teeLock_);
        teeClosed_ = true;
    }
    teeChanged_.notify_all();
}

bool Session::withInputWindow(const std::function<bool(void* window)>& use) {
    std::lock_guard<std::mutex> lock(windowLock_);
    void* window = sink_ ? sink_->inputWindow() : nullptr;
    return window && use && use(window);
}

}  // namespace geode::output::record
