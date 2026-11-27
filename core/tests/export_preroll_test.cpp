// An export's start (#999 F1, F2, F4): the pre-roll (core/output/PreRoll) before frame 0; and a silent
// stretch mid-song, which holds the export's frames like the live visual's (F3).
//
// Each case runs the export's start the way the shell and the renderer run it, with no GL: the pre-roll's
// TrackFeed (ExportFrame's pre-roll decode) into the renderer's own ring and inline analysis, made at the first
// write (Renderer::hearOwn, an export's analysis counting as the player), output::endPreRoll (Renderer::endPreRoll),
// then the range's own TrackFeed frame by frame, each frame hearing the audio up to its end, pumping the analysis
// 1/fps and reading at the ring's head (RendererFrame's beginFrame and readSound). The picture's clock is
// sound::motionRate of the frame.
//
// Before the pre-roll the engine heard its first sample on frame 0, so presence rose from 0 and the clock
// crawled for the first half second of every export (F2) and of every loop's head, which the seam dissolves
// the full-speed tail into (F4). A track that gave no audio left no engine, so the render exported a frozen
// picture (F1); now the shell fails it. The synthetic track plays 93 BPM, a tempo it measures, nothing assumed.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <numbers>
#include <vector>

#include "audio/ingest/TrackFeed.hpp"
#include "output/FrameClock.hpp"
#include "output/PreRoll.hpp"
#include "audio/ingest/PcmRing.hpp"
#include "sound/DriveGain.hpp"
#include "sound/Emotion.hpp"
#include "sound/hop/AnalysisThread.hpp"
#include "sound/interp/FrameBlend.hpp"
#include "viz/styles/catalogue/StyleSchedule.hpp"

using geode::audio::ingest::PcmRing;
using geode::audio::ingest::TrackFeed;
using geode::output::FrameClock;
using geode::output::PreRoll;
using geode::sound::AnalysisThread;
using geode::sound::Emotion;

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        failures++;
    }
}

constexpr int kRate = 48000;
constexpr double kTau = 2.0 * std::numbers::pi;
constexpr double kBpm = 93.0;

// A track: stereo float at 48 kHz, kick on every beat, an off-beat hat and a held minor chord, `seconds` long;
// `silent` keeps it digital silence (a track that decodes to nothing audible).
std::vector<float> track(double seconds, bool silent = false) {
    const int frames = static_cast<int>(seconds * kRate);
    std::vector<float> pcm(static_cast<size_t>(frames) * 2, 0.0f);
    if (silent) return pcm;
    const double beat = 60.0 / kBpm;
    uint32_t noise = 0x1234567u;
    for (int i = 0; i < frames; i++) {
        const double t = static_cast<double>(i) / kRate;
        const double inBeat = std::fmod(t, beat);
        const double offBeat = std::fmod(t + beat / 2.0, beat);
        double s = 0.2 * (std::sin(kTau * 220.0 * t) + std::sin(kTau * 261.63 * t) + std::sin(kTau * 329.63 * t)) / 3.0;
        s += 0.8 * std::exp(-inBeat * 18.0) * std::sin(kTau * (55.0 + 90.0 * std::exp(-inBeat * 30.0)) * inBeat);
        noise = noise * 1664525u + 1013904223u;
        s += 0.15 * std::exp(-offBeat * 60.0) * (static_cast<double>(noise >> 8) / 8388608.0 - 1.0);
        pcm[static_cast<size_t>(i) * 2] = static_cast<float>(s);
        pcm[static_cast<size_t>(i) * 2 + 1] = static_cast<float>(s);
    }
    return pcm;
}

// The renderer's own ring and inline analysis, made at its first write (Renderer::hearOwn in an export), and its read.
struct Renderer {
    static constexpr int kRingFrames = 1 << 16;
    static constexpr int kRunway = 1 << 14;
    std::unique_ptr<PcmRing> ring;
    std::unique_ptr<AnalysisThread> sound;
    geode::sound::interp::FrameReader reader;
    geode::sound::interp::Hits hits;
    Emotion::State feeling{};
    int64_t unpumped = 0;
    bool sectionChangeOwed = false;

    static void sink(void* self, const float* interleaved, int frames, int channels, int rate) {
        auto& r = *static_cast<Renderer*>(self);
        if (!r.sound) {
            r.ring = std::make_unique<PcmRing>(kRingFrames, 2, kRunway);
            r.sound = std::make_unique<AnalysisThread>(*r.ring);
            r.sound->setPlayerSource(true);
            r.reader.restart();
        }
        for (int done = 0; done < frames;) {
            const int n = std::min(frames - done, kRunway);
            if (r.unpumped + n > kRingFrames / 2) {
                r.sound->pump(0.0f);
                r.unpumped = 0;
            }
            r.ring->writeFloat(interleaved + static_cast<size_t>(done) * static_cast<size_t>(channels), n, channels, rate);
            r.unpumped += n;
            done += n;
        }
    }
    // Renderer::endPreRoll.
    bool endPreRoll(int64_t rangeStartMs) {
        if (!sound || ring->headFrames() <= 0) return false;
        sectionChangeOwed = geode::output::endPreRoll(*sound, reader, ring->headFrames(), geode::output::preRollFor(rangeStartMs));
        unpumped = 0;
        return true;
    }
    // RendererFrame: the inline pump, then the read at the head.
    GeodeSoundFrame frame(float dt) {
        GeodeSoundFrame f{};
        if (!sound) return f;
        sound->pump(dt);
        unpumped = 0;
        reader.read(*sound, ring->headFrames(), f, feeling, hits);
        if (sectionChangeOwed) {
            f.sectionChange = 1.0f;
            sectionChangeOwed = false;
        }
        return f;
    }
};

// One track decode (TrackPump) from startMs: the decoder lands on the 1024-frame chunk holding the start (its sync frame) and
// pushes chunk after chunk with their pts, the feed dropping what precedes the start.
class Decode {
public:
    Decode(const std::vector<float>& pcm, int64_t startMs) : pcm_(pcm), feed_(startMs) {
        next_ = static_cast<int64_t>(startMs) * kRate / 1000 / kChunk * kChunk;
    }
    // TrackPump::deliverMs: everything up to endMs from this decode's start into r.
    bool deliverUntilMs(int64_t endMs, Renderer& r) {
        while (!feed_.hasRate() && push()) {
        }
        if (!feed_.hasRate()) return false;
        feed_.setTargetMs(endMs);
        while (!feed_.deliver(&Renderer::sink, &r)) {
            if (!push()) return false;
        }
        return true;
    }

private:
    static constexpr int kChunk = 1024;
    bool push() {
        const int64_t total = static_cast<int64_t>(pcm_.size() / 2);
        if (next_ >= total) return false;
        const int64_t frames = std::min<int64_t>(kChunk, total - next_);
        feed_.pushDecoded(reinterpret_cast<const uint8_t*>(pcm_.data() + next_ * 2), static_cast<size_t>(frames * 2 * 4),
                          true, 2, kRate, next_ * 1000000 / kRate);
        next_ += frames;
        return true;
    }
    const std::vector<float>& pcm_;
    TrackFeed feed_;
    int64_t next_ = 0;
};

struct Start {
    bool heard = false;                // endPreRoll's answer: false fails the render
    std::vector<GeodeSoundFrame> frames;
    std::vector<Emotion::State> feelings;
};

// An export of `frames` frames from rangeStartMs at fps: the pre-roll, then the range.
Start exportStart(const std::vector<float>& pcm, int64_t rangeStartMs, int fps, int frames) {
    Renderer r;
    Start out;
    const PreRoll plan = geode::output::preRollFor(rangeStartMs);
    Decode(pcm, plan.fromMs).deliverUntilMs(plan.heardMs, r);
    out.heard = r.endPreRoll(rangeStartMs);
    if (!out.heard) return out;
    Decode range(pcm, rangeStartMs);
    const FrameClock clock(fps, kRate);
    for (int n = 0; n < frames; n++) {
        range.deliverUntilMs(clock.frameTimeMs(n + 1), r);
        out.frames.push_back(r.frame(static_cast<float>(clock.dtSeconds())));
        out.feelings.push_back(r.feeling);
    }
    return out;
}

// The live engine at the same sample: the whole track from 0 into one engine, read at rangeStart + frames.
GeodeSoundFrame liveAt(const std::vector<float>& pcm, int64_t untilMs) {
    Renderer r;
    Decode(pcm, 0).deliverUntilMs(untilMs, r);
    return r.frame(1.0f / 60.0f);
}

// Live from the track's start, frame by frame: no pre-roll.
Start liveFromStart(const std::vector<float>& pcm, int fps, int frames) {
    Renderer r;
    Start out;
    Decode play(pcm, 0);
    const FrameClock clock(fps, kRate);
    for (int n = 0; n < frames; n++) {
        play.deliverUntilMs(clock.frameTimeMs(n + 1), r);
        out.frames.push_back(r.frame(static_cast<float>(clock.dtSeconds())));
        out.feelings.push_back(r.feeling);
    }
    return out;
}

void planCases() {
    using geode::output::preRollFor;
    const PreRoll zero = preRollFor(0);
    check(zero.fromMs == 0 && zero.heardMs == geode::output::kPreRollMs && zero.boundary,
          "a range at the track's start hears the opening, then starts the song over");
    check(preRollFor(-40).boundary && preRollFor(-40).fromMs == 0, "a negative start is the track's start");
    const PreRoll near = preRollFor(1500);
    check(near.fromMs == 0 && near.heardMs == geode::output::kPreRollMs && near.boundary,
          "too little before the range: the opening, then the song over");
    const PreRoll some = preRollFor(geode::output::kPreRollWarmMs);
    check(some.fromMs == 0 && some.heardMs == geode::output::kPreRollWarmMs && !some.boundary,
          "enough before the range: all of it, run on unbroken");
    const PreRoll deep = preRollFor(30000);
    check(deep.fromMs == 30000 - geode::output::kPreRollMs && deep.heardMs == geode::output::kPreRollMs && !deep.boundary,
          "mid-song: the last kPreRollMs before the range, run on unbroken");
}

// F2: every frame of an export's first half second runs the picture's clock whole, at every rate.
void warmStart() {
    const std::vector<float> pcm = track(40.0);
    for (int fps : {24, 30, 60}) {
        for (int64_t startMs : {int64_t{0}, int64_t{1500}, int64_t{30000}}) {
            const Start s = exportStart(pcm, startMs, fps, fps / 2);
            check(s.heard, "the pre-roll heard the track");
            float slowest = 1.0f;
            for (const GeodeSoundFrame& f : s.frames) slowest = std::min(slowest, geode::sound::motionRate(f));
            if (slowest != 1.0f) std::printf("  start %lld ms at %d fps: slowest clock %.3f\n", static_cast<long long>(startMs), fps, slowest);
            check(slowest == 1.0f, "an export's first half second runs the clock whole (F2)");
        }
    }
    // Mid-song the export hears what live hears there: presence and the measured tempo at frame 0.
    const Start mid = exportStart(pcm, 30000, 60, 1);
    const GeodeSoundFrame live = liveAt(pcm, 30000 + 16);
    check(std::fabs(mid.frames[0].presence - live.presence) < 0.01f, "frame 0's presence is live's (F2)");
    check(live.bpm > 0.0f && std::fabs(mid.frames[0].bpm - live.bpm) < 1.0f, "frame 0's tempo is the one live measured there (F2)");
    // At the track's start the song starts over: no tempo it has not played yet, the boundary on frame 0.
    const Start top = exportStart(pcm, 0, 60, 1);
    check(top.frames[0].bpm == 0.0f, "a whole-track export knows no tempo at frame 0");
    check(top.frames[0].sectionChange == 1.0f, "the song's start is a track boundary on frame 0");
}

// A whole-track export feels what live feels from the start: the pre-roll's eight seconds of the song are not felt
// ahead of frame 0, so the song's side of its feeling (how sure, how happy) is live's at each frame, not the song's
// future. The live cues (a loudness rise in the tension) differ by design: the export's levels are warm.
void feelingStartsWithTheRange() {
    const std::vector<float> pcm = track(20.0);
    constexpr int kFps = 30;
    constexpr int kFrames = 8 * kFps;
    const Start ex = exportStart(pcm, 0, kFps, kFrames);
    const Start live = liveFromStart(pcm, kFps, kFrames);
    check(ex.heard && ex.feelings.size() == live.feelings.size(), "both ran");
    if (!ex.heard || ex.feelings.size() != live.feelings.size()) return;
    check(ex.feelings[0].confidence == 0.0f && ex.feelings[0].scale == -1,
          "frame 0 feels nothing of the song yet, as live");
    float worst = 0.0f;
    int at = 0;
    for (int n = 0; n < kFrames; n++) {
        const Emotion::State& a = ex.feelings[static_cast<size_t>(n)];
        const Emotion::State& b = live.feelings[static_cast<size_t>(n)];
        const float d = std::max(std::fabs(a.confidence - b.confidence), std::fabs(a.valence - b.valence));
        if (d > worst) {
            worst = d;
            at = n;
        }
    }
    std::printf("  feeling, export against live from the start: worst %.3f at frame %d\n", worst, at);
    check(worst < 0.02f, "an export's feeling from the start is live's at every frame");
}

// F4: a loop's head runs at the speed of its tail, so the seam dissolves like into like.
void loopSeam() {
    const std::vector<float> pcm = track(14.0);
    constexpr int kFps = 30;
    constexpr int kLoopFrames = 10 * kFps;
    constexpr int kSeamFrames = kFps;  // a 1 s crossfade
    const Start s = exportStart(pcm, 0, kFps, kLoopFrames + kSeamFrames);
    for (int i = 0; i < kSeamFrames; i++) {
        const float head = geode::sound::motionRate(s.frames[static_cast<size_t>(i)]);
        const float tail = geode::sound::motionRate(s.frames[static_cast<size_t>(kLoopFrames + i)]);
        if (head != tail) {
            std::printf("  seam frame %d: head %.3f, tail %.3f\n", i, head, tail);
            check(false, "the loop's head runs at its tail's speed (F4)");
            return;
        }
    }
}

// F1: a track that gave the engine nothing fails the render; one that decodes to silence is heard and still.
void unheard() {
    check(!Renderer{}.endPreRoll(0), "no engine: the render fails (F1)");
    const std::vector<float> empty;
    check(!exportStart(empty, 0, 30, 3).heard, "a track with no audio fails the render (F1)");
    const Start silence = exportStart(track(12.0, true), 0, 30, 60);
    check(silence.heard, "digital silence is heard");
    bool still = true;
    for (const GeodeSoundFrame& f : silence.frames) still = still && geode::sound::motionRate(f) == 0.0f;
    check(still, "no sound, no movement: a silent track's export stays still");
}

// #999 F3, "freeze, same as live" (owner, 2026-11-12): an export over a silent stretch holds its frames
// like the live visual. Mid-song (warm, the tempo measured), 26 s from 30 s of a track whose 40 to 48 s
// are digital silence, at 30 fps, stepped the way Renderer::beginFrame steps an export: the picture's
// clock is the frame's step times sound::motionRate, and the style schedule (an export's beat cut, here
// every beat) advances on that clock (StyleSchedule::advance). Slider glides and transitions keep render
// time, as live; they are the shell's and the user's, not the music's, and are not modelled here.
void silentStretchHolds() {
    std::vector<float> pcm = track(60.0);
    std::fill(pcm.begin() + 40 * kRate * 2, pcm.begin() + 48 * kRate * 2, 0.0f);
    constexpr int kFps = 30;
    constexpr int64_t kStartMs = 30000;
    const Start s = exportStart(pcm, kStartMs, kFps, 26 * kFps);
    check(s.heard, "the pre-roll heard the track");
    if (!s.heard) return;
    geode::viz::StyleSchedule schedule;
    schedule.set({}, {"plasma", "glow_vortex"}, 1);
    const float step = 1.0f / static_cast<float>(kFps);
    double picture = 0.0;          // the picture's clock (Renderer::timeSeconds_)
    double stillFrom = -1.0;       // the picture's clock where the stretch's still frames start
    bool held = true;
    int cutsBefore = 0;
    int cutsInStretch = 0;
    int cutsAfter = 0;
    int stillFrames = 0;
    float beatsAtStill = -1.0f;
    float beatsAtEnd = -1.0f;
    for (int n = 0; n < static_cast<int>(s.frames.size()); n++) {
        const GeodeSoundFrame& f = s.frames[static_cast<size_t>(n)];
        const double t = static_cast<double>(n) / kFps;  // seconds into the range; the stretch is 10..18
        const float rate = geode::sound::motionRate(f);
        const bool cut = schedule.advance(step, rate, f.beatCount) != nullptr;
        // Three seconds into the silence presence has faded below the still point; to its end, nothing moves.
        if (t >= 13.0 && t < 18.0) {
            if (stillFrom < 0.0) {
                stillFrom = picture;
                beatsAtStill = f.beatCount;
            }
            held = held && rate == 0.0f && picture == stillFrom;
            stillFrames++;
            beatsAtEnd = f.beatCount;
        }
        if (cut) {
            if (t < 10.0) cutsBefore++;
            else if (t >= 13.0 && t < 18.0) cutsInStretch++;
            else if (t >= 18.0) cutsAfter++;
        }
        picture += static_cast<double>(step) * rate;
    }
    check(stillFrames == 5 * kFps, "the stretch's last five seconds were rendered");
    check(held, "no sound, no movement: an export's frames hold through a silent stretch (#999 F3)");
    check(cutsInStretch == 0, "the export's beat cut holds with the picture: no style switch on a frozen frame");
    check(cutsBefore > 0, "the beat cut cuts while the music plays");
    check(cutsAfter > 0, "and again once the music is back");
    const double moving = picture - stillFrom;
    check(moving > 5.0, "the picture's clock runs again after the stretch");
    // Not a check: whether the tracker's flywheel ticked beats through the silence, which the schedule
    // no longer counts (the old render-time step would have cut on them, on frozen frames).
    std::printf("  silent stretch: %d still frames, beat count %.0f -> %.0f\n", stillFrames, beatsAtStill, beatsAtEnd);
}

}  // namespace

int main() {
    planCases();
    warmStart();
    feelingStartsWithTheRange();
    loopSeam();
    unheard();
    silentStretchHolds();
    if (failures == 0) std::printf("export_preroll_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
