// Host-side checks for Record's clock (core/output/Recorder) and its C ABI: which stage frames go into the
// file and their PTS at 30, 60 and 120 fps stages, where each heard sample sits beside the picture, a pause,
// a seek, a rate change mid-recording, a slow frame, clocks that drift, and every way a recording ends.
// A stage is simulated on a 1 ms clock: a source that writes chunks in real time, a display loop that hands
// the recorder what arrived and asks for a frame, and an encoder thread that takes the audio every 5 ms.
// Built and run by CI (.github/workflows/native-host-tests.yml, "Native recorder tests"); no device.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "api/geode_api.h"
#include "output/Recorder.hpp"

using namespace geode::output;
using record::Recorder;
using record::Stop;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int64_t kMs = 1'000'000;
constexpr int64_t kSec = 1'000'000'000;
constexpr int kRate = Recorder::kAudioRateHz;
constexpr double kScale = 29.788;   // 32767 / 1100
constexpr int64_t kT0 = 5 * kSec;   // the clock when the stage starts: never 0 on a phone

// Source sample i as a stereo pair that names it and is never digital silence.
void encode(int64_t i, float* out) {
    out[0] = (static_cast<float>(i % 2000 + 1) - 1000.5f) / 1100.0f;
    out[1] = (static_cast<float>(i / 2000 % 2000 + 1) - 1000.5f) / 1100.0f;
}

// The source index a stream pair names, or -1 for silence.
int64_t decode(const int16_t* pair) {
    if (pair[0] == 0 && pair[1] == 0) return -1;
    const int64_t low = std::llround(pair[0] / kScale + 1000.5) - 1;
    const int64_t high = std::llround(pair[1] / kScale + 1000.5) - 1;
    return low + high * 2000;
}

struct Heard {
    int64_t lastIndex;
    int64_t atNs;
};

// One simulated stage.
struct Stage {
    record::Config config{1080, 2400, 30, 0, 0};
    int displayFps = 60;
    int sourceRateHz = kRate;
    int chunkFrames = 4096;        // a decoder buffer: the source writes this much at a time
    double sourceSpeed = 1.0;      // the sample clock against the wall clock
    int64_t jitterNs = 0;          // each display frame is this early or late, alternating
    bool encoderRuns = true;

    Recorder* recorder = nullptr;
    int64_t nowNs = kT0;
    int64_t startNs = -1;
    int64_t displayFrame = 0;
    int64_t written = 0;           // source frames written to the ring
    int64_t delivered = 0;         // source frames the renderer has taken
    double sourceClock = 0.0;      // source frames due by now
    bool playing = true;
    bool drawing = true;
    std::vector<int64_t> pts;
    std::vector<Heard> heard;
    std::vector<int16_t> stream;
    std::vector<float> block;
    int16_t out[4096 * 2];

    void begin() { recorder = new Recorder(config); }
    ~Stage() { delete recorder; }

    void deliver() {
        const int64_t frames = written - delivered;
        if (frames <= 0) return;
        block.resize(static_cast<size_t>(frames) * 2);
        for (int64_t i = 0; i < frames; ++i) encode(delivered + i, &block[static_cast<size_t>(i) * 2]);
        recorder->hear(block.data(), static_cast<int>(frames), 2, sourceRateHz, nowNs);
        delivered = written;
        if (recorder->started()) heard.push_back({delivered - 1, nowNs});
    }

    void encoderTick() {
        if (encoderRuns) {
            while (encoded < recorder->framesTaken()) {
                recorder->videoEncoded();
                ++encoded;
            }
        }
        int64_t ptsUs = 0;
        for (;;) {
            const int n = recorder->takeAudio(nowNs, out, 4096, &ptsUs);
            if (n <= 0) break;
            check(ptsUs == static_cast<int64_t>(stream.size() / 2) * 1'000'000 / kRate, "an audio block is stamped where it starts");
            stream.insert(stream.end(), out, out + n * 2);
        }
    }
    int64_t encoded = 0;

    // Runs the stage for ms of wall time.
    void run(int64_t ms) {
        for (int64_t tick = 0; tick < ms; ++tick) {
            if (playing) {
                sourceClock += sourceSpeed * sourceRateHz / 1000.0;
                while (sourceClock - static_cast<double>(written) >= chunkFrames) written += chunkFrames;
            }
            const int64_t due = kT0 + displayFrame * kSec / displayFps;
            if (nowNs >= due) {
                const int64_t at = nowNs + (displayFrame % 2 == 0 ? jitterNs : -jitterNs);
                const int64_t keep = nowNs;
                nowNs = at;
                if (drawing) {
                    deliver();
                    const int64_t p = recorder->frame(nowNs, config.stageWidth, config.stageHeight);
                    if (p >= 0) {
                        if (startNs < 0) startNs = nowNs;
                        pts.push_back(p);
                    }
                }
                nowNs = keep;
                ++displayFrame;
            }
            if (tick % 5 == 0) encoderTick();
            nowNs += kMs;
        }
    }

    void finish(Stop why = Stop::User) {
        recorder->stop(why, nowNs);
        encoderTick();
    }

    // Where source sample `index` sits in the stream, or -1.
    int64_t find(int64_t index) const {
        for (size_t p = 0; p < stream.size() / 2; ++p) {
            if (decode(&stream[p * 2]) == index) return static_cast<int64_t>(p);
        }
        return -1;
    }

    // The furthest a block's last sample sits from the moment the visual heard it, over blocks heard in
    // [fromNs, toNs) after the start; blocks that were cut (a burst) are not in the stream and are skipped.
    int64_t worstSyncNs(int64_t fromNs = 0, int64_t toNs = INT64_MAX) const {
        std::vector<int64_t> where(static_cast<size_t>(written) + 1, -1);
        for (size_t p = 0; p < stream.size() / 2; ++p) {
            const int64_t i = decode(&stream[p * 2]);
            if (i >= 0 && i <= written) where[static_cast<size_t>(i)] = static_cast<int64_t>(p);
        }
        int64_t worst = 0;
        for (const Heard& h : heard) {
            const int64_t elapsed = h.atNs - startNs;
            if (elapsed < fromNs || elapsed >= toNs) continue;
            const int64_t p = where[static_cast<size_t>(h.lastIndex)];
            if (p < 0) continue;
            worst = std::max<int64_t>(worst, std::llabs(p * kSec / kRate - elapsed));
        }
        return worst;
    }

    // The stream without its last 200 ms: the stop cuts the newest chunk short, so the tail may be silent.
    int64_t body() const { return static_cast<int64_t>(stream.size() / 2) - kRate / 5; }

    // Counts what the stream holds between two stream positions: silent frames and breaks in the source
    // order (a sample that does not follow the one before it).
    void scan(int64_t from, int64_t to, int64_t* silent, int64_t* breaks) const {
        *silent = 0;
        *breaks = 0;
        int64_t last = -2;
        for (int64_t p = from; p < to && p < static_cast<int64_t>(stream.size() / 2); ++p) {
            const int64_t i = decode(&stream[static_cast<size_t>(p) * 2]);
            if (i < 0) {
                ++*silent;
            } else {
                if (last >= 0 && i != last + 1) ++*breaks;
                last = i;
            }
        }
    }
};

constexpr int64_t kToleranceNs = Recorder::kSyncToleranceMs * kMs;

// The same recording from a 30, a 60 and a 120 fps stage.
void displayRates() {
    std::vector<int64_t> reference;
    int64_t offsets[3] = {0, 0, 0};
    int n = 0;
    for (int fps : {30, 60, 120}) {
        Stage s;
        s.displayFps = fps;
        s.begin();
        s.run(10'000);
        s.finish();
        check(s.pts.size() == 300, "ten seconds at a 30 fps ceiling are 300 frames at any stage rate");
        for (size_t i = 0; i < s.pts.size(); ++i) {
            if (s.pts[i] != static_cast<int64_t>(i) * kSec / 30) {
                check(false, "each frame's PTS is its slot's start");
                break;
            }
        }
        if (reference.empty()) reference = s.pts;
        check(s.pts == reference, "the PTS list is the same at 30, 60 and 120 fps");
        check(s.recorder->framesDropped() == 0, "nothing is dropped while the encoder keeps up");
        int64_t silent = 0;
        int64_t breaks = 0;
        s.scan(kRate / 2, s.body(), &silent, &breaks);
        check(silent == 0 && breaks == 0, "steady sound is in the file whole: no silence, no sample lost or repeated");
        check(s.worstSyncNs() <= kSec / fps + kMs, "steady sound sits within a stage frame of the picture it drove");
        check(static_cast<int64_t>(s.stream.size() / 2) == s.recorder->audioFrames(), "the stream is what was taken");
        check(std::llabs(s.recorder->audioFrames() - s.recorder->recordedMs(s.nowNs) * kRate / 1000) <= kRate / 1000,
              "the sound is as long as the recording");
        const int64_t p = s.find(s.heard[s.heard.size() / 2].lastIndex);
        offsets[n++] = p - (s.heard[s.heard.size() / 2].atNs - s.startNs) * kRate / kSec;
    }
    check(std::llabs(offsets[0] - offsets[1]) <= kRate / 30 && std::llabs(offsets[0] - offsets[2]) <= kRate / 30,
          "the sound sits in the same place, to a frame, whatever the stage rate");
}

void pacing() {
    {   // A 60 fps ceiling on a stage the pacer holds at 45: every frame, at its real time.
        Stage s;
        s.config.fps = 60;
        s.displayFps = 45;
        s.begin();
        s.run(4'000);
        check(s.pts.size() == 180, "a stage slower than the ceiling records every frame it drew");
        bool rising = true;
        bool real = true;
        for (size_t i = 1; i < s.pts.size(); ++i) {
            rising = rising && s.pts[i] > s.pts[i - 1];
            real = real && std::llabs(s.pts[i] - static_cast<int64_t>(i) * kSec / 45) <= 3 * kMs;
        }
        check(rising, "PTS rise strictly");
        check(real, "off the grid a frame carries its real time");
    }
    for (int64_t jitter : {900'000, 2'000'000}) {   // vsync early and late in turn: no slot doubled, none skipped
        Stage s;
        s.config.fps = 60;
        s.displayFps = 60;
        s.jitterNs = jitter;
        s.begin();
        s.run(4'000);
        check(s.pts.size() == 240, "jittered vsync keeps every frame");
        bool grid = true;
        bool near = true;
        for (size_t i = 0; i < s.pts.size(); ++i) {
            const int64_t slot = (s.pts[i] * 60 + kSec / 2) / kSec;
            grid = grid && std::llabs(s.pts[i] - slot * kSec / 60) <= 1;
            near = near && slot == static_cast<int64_t>(i) && (i == 0 || s.pts[i] > s.pts[i - 1]);
        }
        check(near, "each frame on its own slot, PTS rising");
        if (jitter < kMs) check(grid, "and small jitter is stamped on the grid");
    }
    {   // An encoder that puts nothing out: a few frames go in, then the stage is never made to wait.
        Stage s;
        s.encoderRuns = false;
        s.begin();
        s.run(2'000);
        check(s.recorder->framesDropped() > 40, "frames are dropped while the encoder is behind");
        check(s.pts.size() >= static_cast<size_t>(Recorder::kMaxPending) && s.pts.size() <= 16,
              "only the pending few and a probe every quarter second go in");
        s.encoderRuns = true;
        const size_t before = s.pts.size();
        s.run(1'000);
        check(s.pts.size() - before >= 28, "and recording resumes when the encoder catches up");
    }
}

// Paused: the picture keeps rolling over silence, and the sound comes back where the picture is.
void paused() {
    Stage s;
    s.begin();
    s.run(3'000);
    s.playing = false;
    s.run(2'000);
    s.playing = true;
    s.run(3'000);
    s.finish();
    check(s.pts.size() == 240, "a pause records the picture");
    int64_t silent = 0;
    int64_t breaks = 0;
    s.scan(kRate / 2, s.body(), &silent, &breaks);
    check(std::llabs(silent - 2 * kRate) <= kRate / 5, "and two seconds of silence");
    check(breaks == 0, "the sound resumes on the next sample");
    check(s.worstSyncNs(4 * kSec) <= kToleranceNs, "and sits with the picture again");
    check(s.recorder->stopped() == Stop::User, "only the user stopped it");
}

// Playback starting inside a recording: the sink fills its buffer at once, then runs in real time.
void burstAfterSilence() {
    Stage s;
    s.playing = false;
    s.begin();
    s.run(2'000);
    s.written += 3 * 4096;   // 256 ms in one go
    s.sourceClock = static_cast<double>(s.written);
    s.playing = true;
    s.run(3'000);
    s.finish();
    check(s.find(0) >= 0, "the song's first sample is in the file");
    int64_t silent = 0;
    int64_t breaks = 0;
    s.scan(s.find(0), s.body(), &silent, &breaks);
    check(silent == 0 && breaks == 0, "the opening burst is laid back over the silence, whole");
    check(s.worstSyncNs(2 * kSec) <= kToleranceNs, "and ends where the picture heard it");
}

// A seek: the ring starts over and a buffer of the new place arrives at once.
void seek() {
    Stage s;
    s.begin();
    s.run(3'000);
    const int64_t before = s.delivered;
    s.recorder->restarted();
    s.delivered = s.written + 1'000'000;   // somewhere else in the song,
    s.written = s.delivered + 3 * 4096;    // a buffer of it at once
    s.sourceClock = static_cast<double>(s.written);
    s.run(3'000);
    s.finish();
    check(s.find(before - 1) >= 0, "what the visual heard before the seek stays");
    int64_t silent = 0;
    int64_t breaks = 0;
    s.scan(kRate / 2, s.body(), &silent, &breaks);
    check(silent == 0, "a seek leaves no hole");
    check(breaks == 1, "one cut, at the seek");
    check(s.worstSyncNs(3 * kSec + 500 * kMs) <= kToleranceNs, "and the new place sits with the picture");
}

// The source's rate changes mid-recording (the next track is 48 kHz after a 44.1 kHz one).
void rateChange() {
    Stage s;
    s.sourceRateHz = 44'100;
    s.begin();
    s.run(3'000);
    s.sourceRateHz = 48'000;
    s.sourceClock = static_cast<double>(s.written);
    s.run(3'000);
    s.finish();
    check(s.recorder->audioFrames() == 6 * kRate, "six seconds are six seconds of 48 kHz whatever the source rates");
    int64_t silentRun = 0;
    int64_t worstRun = 0;
    for (size_t p = kRate / 2; p < static_cast<size_t>(s.body()); ++p) {
        silentRun = std::abs(s.stream[p * 2]) < 4 && std::abs(s.stream[p * 2 + 1]) < 4 ? silentRun + 1 : 0;
        worstRun = std::max(worstRun, silentRun);
    }
    check(worstRun < kRate / 100, "with no hole at the change");
}

void limits() {
    {   // Settings' maximum length.
        Stage s;
        s.config.maxSec = 15;
        s.displayFps = 60;
        s.begin();
        s.run(17'000);
        check(s.recorder->stopped() == Stop::MaxLength, "the maximum length stops the recording");
        s.encoderTick();
        check(s.pts.size() == 450, "15 s at 30 fps are 450 frames");
        check(s.pts.back() < 15 * kSec, "no frame at or past the limit");
        check(s.recorder->audioFrames() == 15 * kRate, "and exactly 15 s of sound");
        check(s.recorder->recordedMs(s.nowNs) == 15'000, "saved as 15 s");
        check(s.recorder->frame(s.nowNs, 1080, 2400) == -1, "nothing after the end");
    }
    {   // The entitlement's cap is the shorter one.
        Stage s;
        s.config.maxSec = 60;
        s.config.capMs = 5'000;
        s.begin();
        s.run(7'000);
        s.encoderTick();
        check(s.recorder->stopped() == Stop::MaxLength && s.recorder->audioFrames() == 5 * kRate, "a cap under the maximum wins");
        check(s.recorder->limitNs() == 5 * kSec, "the limit is the shorter of the two");
    }
    {   // A stage that stopped drawing still ends on time, from the encoder thread.
        Stage s;
        s.config.maxSec = 15;
        s.begin();
        s.run(10'000);
        s.drawing = false;
        s.run(7'000);
        check(s.recorder->stopped() == Stop::MaxLength && s.recorder->audioFrames() == 15 * kRate,
              "the limit holds when no frame is drawn");
    }
    {   // A rotation.
        Stage s;
        s.begin();
        s.run(1'000);
        check(s.recorder->frame(s.nowNs, 2400, 1080) == -1 && s.recorder->stopped() == Stop::Resized, "a resized stage ends it");
    }
    {   // The file is full.
        Stage s;
        s.begin();
        s.run(1'000);
        s.recorder->wrote(Recorder::kFileLimitBytes - 1, s.nowNs);
        check(s.recorder->stopped() == Stop::None, "under the muxer's target it rolls");
        s.recorder->wrote(1, s.nowNs);
        check(s.recorder->stopped() == Stop::FileFull, "at it, it stops");
        s.recorder->stop(Stop::User, s.nowNs);
        check(s.recorder->stopped() == Stop::FileFull, "the first reason stays");
    }
    {   // The user's stop: the sound runs to the stop, the lookback included.
        Stage s;
        s.begin();
        s.run(2'503);
        const int64_t last = s.delivered - 1;
        s.finish();
        const int64_t length = (s.nowNs - s.startNs) * kRate / kSec;
        check(std::llabs(s.recorder->audioFrames() - length) <= 1, "the sound ends at the stop");
        check(s.find(last) >= 0, "with the last thing the visual heard in it");
        check(s.recorder->takeAudio(s.nowNs + kSec, s.out, 4096, nullptr) == 0, "and nothing after");
    }
    {   // Never started: nothing to save.
        Recorder r(record::Config{1080, 2400, 30, 0, 0});
        float one[2] = {0.5f, 0.5f};
        r.hear(one, 1, 2, kRate, kT0);
        r.stop(Stop::User, kT0);
        int16_t out[2];
        check(r.recordedMs(kT0 + kSec) == 0 && r.takeAudio(kT0 + kSec, out, 1, nullptr) == 0, "no frame, no recording");
    }
}

// A slow frame delivers its sound late: nothing is lost and nothing is padded.
void slowFrames() {
    {
        Stage s;
        s.begin();
        s.run(2'000);
        s.drawing = false;
        s.run(300);
        s.drawing = true;
        s.run(2'000);
        s.finish();
        int64_t silent = 0;
        int64_t breaks = 0;
        s.scan(kRate / 2, s.body(), &silent, &breaks);
        check(silent == 0 && breaks == 0, "a 300 ms stall loses no sound");
        check(s.worstSyncNs() <= kToleranceNs, "and moves none of it");
    }
    {   // Longer than the lookback: the stall is silence in the file, and the sound returns in place.
        Stage s;
        s.begin();
        s.run(2'000);
        s.drawing = false;
        s.run(1'500);
        s.drawing = true;
        s.run(2'000);
        s.finish();
        int64_t silent = 0;
        int64_t breaks = 0;
        s.scan(kRate / 2, s.body(), &silent, &breaks);
        check(silent > 0 && silent < 3 * kRate / 2, "a stall past the lookback is silence, no longer than the stall");
        check(s.worstSyncNs(4 * kSec) <= kToleranceNs, "and the sound is back with the picture");
    }
}

// A sample clock that runs fast or slow against the wall clock, for five minutes.
void drift() {
    for (double speed : {1.0005, 0.9995}) {
        Stage s;
        s.sourceSpeed = speed;
        s.begin();
        s.run(300'000);
        s.finish();
        int64_t worst = 0;
        // Decoding wraps every 4 M samples, so measure against the count instead: the stream position of
        // the newest sample the visual heard, against the time it heard it.
        int64_t silent = 0;
        for (size_t p = kRate / 2; p < s.stream.size() / 2; ++p) silent += decode(&s.stream[p * 2]) < 0 ? 1 : 0;
        const int64_t lost = s.heard.back().lastIndex + 1 - (s.recorder->audioFrames() - silent);
        const int64_t driftFrames = static_cast<int64_t>(std::llround(std::fabs(speed - 1.0) * 300 * kRate));
        worst = std::llabs(std::llabs(lost - silent) - driftFrames);
        check(worst <= 2 * Recorder::kSyncToleranceMs * kRate / 1000 + kRate / 2,
              "drift is taken out in steps: what is cut or padded adds up to the drift");
        check(std::llabs(s.recorder->audioFrames() - 300LL * kRate) <= kRate / 1000, "and the file keeps wall time");
    }
}

void plan() {
    encode::FrameSize a = record::frameSize(1080, 2400, 1080);
    check(a.width == 1080 && a.height == 2400, "a portrait stage records portrait at its own size");
    a = record::frameSize(1080, 2400, 720);
    check(a.width == 720 && a.height == 1600, "a smaller choice scales both sides");
    a = record::frameSize(2400, 1080, 2160);
    check(a.width == 2400 && a.height == 1080, "never sharper than the stage");
    a = record::frameSize(1079, 2399, 1080);
    check(a.width % 2 == 0 && a.height % 2 == 0, "sides are even");
    check(record::frameSize(0, 100, 720).width == 0, "no stage, no frame");
    check(record::videoBps(1920, 1080, 30, 0) == 12'000'000, "1080p30 is 12 Mbit/s");
    check(record::videoBps(1920, 1080, 60, 0) == 18'000'000, "60 fps is half as much again");
    check(record::videoBps(320, 240, 30, 0) == encode::kFloorBps, "never under the floor");
    check(record::videoBps(4096, 2304, 60, 0) == 40'000'000, "never over the ceiling");
    check(record::videoBps(4096, 2304, 60, 1'800'000) < 40'000'000, "a long limit lowers the rate under the muxer's target");
}

void abi() {
    check(geode_recorder_create(0, 0, 30, 0, 0) == nullptr, "no stage, no recorder");
    geode_recorder* r = geode_recorder_create(1080, 2400, 30, 0, 2'000);
    check(r != nullptr, "create");
    check(geode_recorder_frame(r, kT0, 1080, 2400) == 0, "the first frame is time 0");
    check(geode_recorder_frame(r, kT0 + kMs, 1080, 2400) == -1, "the same slot twice is one frame");
    float pcm[960];
    for (float& v : pcm) v = 0.25f;
    geode_recorder_hear(r, pcm, 480, 2, kRate, kT0 + 10 * kMs);
    geode_recorder_video_encoded(r);
    geode_recorder_restarted(r);
    int16_t out[2 * 4800];
    int64_t ptsUs = -1;
    check(geode_recorder_take_audio(r, kT0 + 100 * kMs, out, 4800, &ptsUs) == 0, "nothing is due inside the lookback");
    check(geode_recorder_stopped(r) == 0, "rolling");
    geode_recorder_stop(r, 99, kT0 + 500 * kMs);
    check(geode_recorder_stopped(r) == 0, "an unknown reason stops nothing");
    geode_recorder_stop(r, GEODE_RECORD_STOP_USER, kT0 + 500 * kMs);
    check(geode_recorder_stopped(r) == GEODE_RECORD_STOP_USER, "stopped by the user");
    check(geode_recorder_recorded_ms(r, kT0 + 9 * kSec) == 500, "its length is fixed at the stop");
    int total = 0;
    for (int n; (n = geode_recorder_take_audio(r, kT0 + kSec, out, 4800, &ptsUs)) > 0;) total += n;
    check(total == kRate / 2, "half a second of sound comes out");
    check(geode_recorder_frames_dropped(r) == 0, "nothing dropped");
    geode_recorder_wrote(r, 1, kT0);
    geode_recorder_destroy(r);
    geode_recorder_destroy(nullptr);
    check(geode_recorder_frame(nullptr, 0, 1, 1) == -1 && geode_recorder_take_audio(nullptr, 0, out, 1, nullptr) == 0, "null is safe");
    int w = 0;
    int h = 0;
    geode_record_frame_size(1080, 2400, 720, &w, &h);
    check(w == 720 && h == 1600 && geode_record_video_bps(1920, 1080, 30, 0) == 12'000'000, "the plan through the ABI");
}

}  // namespace

int main() {
    displayRates();
    pacing();
    paused();
    burstAfterSilence();
    seek();
    rateChange();
    limits();
    slowFrames();
    drift();
    plan();
    abi();
    if (failures == 0) std::printf("recorder_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
