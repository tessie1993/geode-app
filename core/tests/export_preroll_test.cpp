// An export's start (#999 F1, F2, F4): the pre-roll (core/output/PreRoll) before frame 0; and a silent
// stretch mid-song, which holds the export's frames like the live visual's (F3).
// Built with the runner's own compiler in CI:
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/export_preroll_test.cpp core/sound/*.cpp
//     core/output/FrameClock.cpp core/audio/ingest/TrackFeed.cpp core/audio/ingest/AiffReader.cpp
//     core/viz/StyleSchedule.cpp -o export_preroll_test
//
// Each case runs the export's start the way the shell and the renderer run it, with no GL: the pre-roll's
// TrackFeed (the shell's first TrackAudio) into a sound engine made at the first write (Renderer::writeAudio,
// an export's engine counting as the player), output::endPreRoll (Renderer::endPreRoll), then the range's
// own TrackFeed frame by frame, each frame hearing the audio up to its end and stepping the engine 1/fps
// (OffscreenSceneRenderer.renderFrame). The picture's clock is sound::motionRate of the frame (RendererFrame).
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
#include "sound/DriveGain.hpp"
#include "sound/SoundEngine.hpp"
#include "viz/StyleSchedule.hpp"

using geode::audio::ingest::TrackFeed;
using geode::output::FrameClock;
using geode::output::PreRoll;
using geode::sound::SoundEngine;

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

// The renderer's engine, made at its first write like Renderer::writeAudio in an export.
struct Renderer {
    std::unique_ptr<SoundEngine> sound;
    static void sink(void* self, const float* interleaved, int frames, int channels, int rate) {
        auto& r = *static_cast<Renderer*>(self);
        if (!r.sound) {
            r.sound = std::make_unique<SoundEngine>(rate);
            r.sound->setPlayerSource(true);
        }
        r.sound->write(interleaved, frames, channels);
    }
};

// One TrackAudio from startMs: the decoder lands on the 1024-frame chunk holding the start (its sync frame) and
// pushes chunk after chunk with their pts, the feed dropping what precedes the start.
class Decode {
public:
    Decode(const std::vector<float>& pcm, int64_t startMs) : pcm_(pcm), feed_(startMs) {
        next_ = static_cast<int64_t>(startMs) * kRate / 1000 / kChunk * kChunk;
    }
    // TrackAudio.deliverUntilMs: everything up to endMs from this decode's start into r.
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
};

// An export of `frames` frames from rangeStartMs at fps: the pre-roll, then the range.
Start exportStart(const std::vector<float>& pcm, int64_t rangeStartMs, int fps, int frames) {
    Renderer r;
    Start out;
    const PreRoll plan = geode::output::preRollFor(rangeStartMs);
    Decode(pcm, plan.fromMs).deliverUntilMs(plan.heardMs, r);
    out.heard = geode::output::endPreRoll(r.sound.get(), plan);
    if (!out.heard) return out;
    Decode range(pcm, rangeStartMs);
    const FrameClock clock(fps, kRate);
    for (int n = 0; n < frames; n++) {
        range.deliverUntilMs(clock.frameTimeMs(n + 1), r);
        GeodeSoundFrame f{};
        if (r.sound) r.sound->step(static_cast<float>(clock.dtSeconds()), f);
        out.frames.push_back(f);
    }
    return out;
}

// The live engine at the same sample: the whole track from 0 into one engine, read at rangeStart + frames.
GeodeSoundFrame liveAt(const std::vector<float>& pcm, int64_t untilMs) {
    Renderer r;
    Decode(pcm, 0).deliverUntilMs(untilMs, r);
    GeodeSoundFrame f{};
    r.sound->step(1.0f / 60.0f, f);
    return f;
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
    check(!geode::output::endPreRoll(nullptr, geode::output::preRollFor(0)), "no engine: the render fails (F1)");
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
    loopSeam();
    unheard();
    silentStretchHolds();
    if (failures == 0) std::printf("export_preroll_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
