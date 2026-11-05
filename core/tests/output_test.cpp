// Host-side checks for the offline render's planning (core/output: FrameClock, LoopPlan, EncodePlan,
// Chapters, ExportRange, RenderEta) and its C ABI. Built and run by CI (.github/workflows/android.yml,
// "Native export planning tests") with the system compiler; no device. Where a value pins the Kotlin it
// replaces (VideoExporter, LoopSpec, LoopExtendPlan, MuxerBudget, ExportAspect, fallbackExportFps,
// ChapterMarkers, ExportRange.of, RenderEta and its JVM RenderEtaTest), the comment says so; the deliberate
// changes (exact pts, the seam budget's lost frame, the 0:00 lead-in) say why.
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <vector>

#include "api/geode_api.h"
#include "output/Chapters.hpp"
#include "output/EncodePlan.hpp"
#include "output/ExportRange.hpp"
#include "output/FrameClock.hpp"
#include "output/LoopPlan.hpp"
#include "output/RenderEta.hpp"
#include "viz/FrameStep.hpp"

using namespace geode::output;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void frameClock() {
    const FrameClock c60(60, 48000);
    check(c60.framesFor(1'000'000) == 60, "one second at 60 fps is 60 frames");
    check(c60.framesFor(0) == 1, "an empty render still draws one frame");
    check(c60.framesFor(-5) == 1, "a negative duration draws one frame");
    check(c60.ptsNs(1) == 16'666'666, "frame 1 at 60 fps");
    // The Kotlin exporter stamped n * 16 666 666, i.e. 49 999 998 here; one division keeps it exact.
    check(c60.ptsNs(3) == 50'000'000, "frame 3 at 60 fps is exactly 50 ms");
    check(FrameClock(24, 48000).ptsNs(24) == 1'000'000'000, "24 frames at 24 fps is exactly one second");
    check(FrameClock(24, 48000).ptsNs(24 * 3600) == 3600LL * 1'000'000'000, "an hour at 24 fps does not drift");
    check(FrameClock(30, 48000).frameTimeMs(1) == 33, "frame 1 at 30 fps reads 33 ms, as the offscreen renderer does");
    check(c60.frameStartUs(1) == 16'666, "frame start in µs");
    check(c60.samplesThrough(0) == 800, "48 kHz / 60 fps hears 800 samples by the end of frame 0");
    check(FrameClock(60, 44100).samplesThrough(0) == 735, "44.1 kHz / 60 fps");
    check(FrameClock(24, 44100).samplesThrough(0) == 1838, "1837.5 rounds half up, as llround does");

    const int rates[] = {44100, 48000, 96000};
    const int fpsList[] = {24, 25, 30, 50, 60};
    for (int rate : rates) {
        for (int fps : fpsList) {
            const FrameClock c(fps, rate);
            check(c.samplesThrough(fps - 1) == rate, "one second of frames hears exactly one second of samples");
            bool monotonic = true;
            for (int64_t n = 1; n < 2000; ++n) {
                if (c.ptsNs(n) <= c.ptsNs(n - 1) || c.samplesThrough(n) < c.samplesThrough(n - 1)) monotonic = false;
            }
            check(monotonic, "pts and sample positions never go backwards");
        }
    }
    check(FrameClock(0, 48000).fps() == 1, "a zero rate is taken as 1 fps, not a division by zero");

    // The renderer's step (#377): an export steps exactly 1/fps on every frame, the first included.
    using geode::viz::frameStepSeconds;
    check(frameStepSeconds(60, -1.0, 0.0) == static_cast<float>(1.0 / 60), "export frame 0 steps 1/fps");
    check(frameStepSeconds(24, -1.0, 0.0) == static_cast<float>(1.0 / 24), "a 24 fps export's first frame is 1/24 s");
    check(frameStepSeconds(60, 0.016, 0.033) == static_cast<float>(1.0 / 60), "ms-truncated times no longer leak in");
    check(frameStepSeconds(0, -1.0, 5.0) == static_cast<float>(1.0 / 60), "live's first frame is 1/60 s");
    check(frameStepSeconds(0, 1.0, 1.02) > 0.0199f && frameStepSeconds(0, 1.0, 1.02) < 0.0201f, "live measures");
    check(frameStepSeconds(0, 1.0, 3.0) == 0.1f && frameStepSeconds(0, 1.0, 1.0) == 0.001f, "live is clamped");
}

void loopSpec() {
    const LoopSpec low = LoopSpec::of(10'000, 50, 100);
    check(low.loopMs == 30'000 && low.crossfadeMs == 200 && low.fps == 60, "of() clamps up to the minimums");
    check(low.valid(), "a clamped spec is valid");
    check(low.loopFrames() == 1800 && low.crossfadeFrames() == 12 && low.sourceFrames() == 1812, "frame counts");
    check(low.loopDurationUs() == 30'000'000, "loop duration");
    const LoopSpec high = LoopSpec::of(1'000'000, 100'000, 10);
    check(high.loopMs == 300'000 && high.crossfadeMs == 5'000 && high.fps == 24, "of() clamps down to the maximums");
    check(LoopSpec::of(30'000, 20'000).crossfadeMs == 5'000, "the dissolve is capped at 5 s");
    check(low.seamPreviewStartMs() == 30'000 - 200 - 2'000, "seam preview lead");
    LoopSpec bad;
    bad.loopMs = 1000;
    bad.crossfadeMs = 600;
    bad.fps = 60;
    check(!bad.valid(), "a dissolve over half the loop is invalid");

    // 4K: 33 177 600 bytes a frame, the 192 MiB stash holds 6.
    const LoopSpec uhd = LoopSpec::of(60'000, 1'000, 60).budgeted(3840, 2160);
    check(uhd.crossfadeFrames() == 6 && uhd.crossfadeMs == 100, "4K seam budget");
    // 3200x2000 affords 7. Kotlin gave 7000/60 = 116 ms, which is 6 frames; the budget now keeps all 7.
    const LoopSpec seven = LoopSpec::of(60'000, 1'000, 60).budgeted(3200, 2000);
    check(seven.crossfadeFrames() == 7 && seven.crossfadeMs == 117, "the seam budget keeps every affordable frame");
    const LoopSpec hd = LoopSpec::of(60'000, 1'000, 60).budgeted(1920, 1080);
    check(hd.crossfadeFrames() == 24 && hd.crossfadeMs == 400, "1080p seam budget");
    const LoopSpec small = LoopSpec::of(60'000, 300, 30).budgeted(1280, 720);
    check(small.crossfadeMs == 300, "a dissolve the stash can hold is left alone");

    check(seamWeight(0, 10) == 0.0f && seamWeight(9, 10) == 1.0f, "the seam reaches both ends exactly");
    check(seamWeight(3, 1) == 1.0f, "a one-frame seam is all stash");
}

void loopExtend() {
    const LoopExtendPlan p = LoopExtendPlan::of(30'000'000, 100'000'000, 3);
    check(p.repeats() == 4, "ceil(100 / 30) repeats");
    check(p.segments().size() == 3, "three drift stops");
    check(p.segments()[0].repeats == 2 && p.segments()[1].repeats == 1 && p.segments()[2].repeats == 1,
          "the remainder goes to the earlier stops");
    check(p.segments()[1].startUs == 60'000'000 && p.segments()[2].startUs == 90'000'000, "stop starts");
    const int64_t expect[] = {0, 30'000'000, 60'000'000, 90'000'000};
    const int loopOf[] = {0, 0, 1, 2};
    for (int i = 0; i < 4; ++i) {
        check(p.writes()[static_cast<size_t>(i)].offsetUs == expect[i], "write offset");
        check(p.writes()[static_cast<size_t>(i)].loopIndex == loopOf[i], "write loop index");
    }
    check(LoopExtendPlan::of(30'000'000, 30'000'000, 5).segments().size() == 1, "a stop with no repeat is dropped");
    check(p.estimatedBytes(1000, 500) == 4590, "4 repeats + soundtrack + 2 % for the sample tables");
    check(p.fittingBitRate(4'000'000, 1'000'000) == 240'000, "the bit rate that fits 3 MB in 100 s");
    check(p.seamPreviewStartMs(1'000) == 0 && p.seamPreviewStartMs(50'000) == 48'000, "seam preview inside the file");

    std::vector<int64_t> offsets(8);
    std::vector<int32_t> loops(8);
    check(geode_loop_extend_plan(30'000'000, 100'000'000, 3, nullptr, nullptr, 0) == 4, "ABI count query");
    check(geode_loop_extend_plan(30'000'000, 100'000'000, 3, offsets.data(), loops.data(), 8) == 4 &&
              offsets[3] == 90'000'000 && loops[2] == 1,
          "ABI plan");
    check(geode_loop_estimated_bytes(30'000'000, 100'000'000, 1000, 500) == 4590, "ABI estimated bytes");
    check(geode_loop_fitting_bit_rate(100'000'000, 4'000'000, 1'000'000) == 240'000, "ABI fitting bit rate");
    geode_loop_spec s{};
    geode_loop_spec_of(60'000, 1'000, 60, &s);
    geode_loop_spec_budgeted(&s, 3200, 2000, &s);
    check(s.crossfade_frames == 7 && s.loop_frames == 3600 && s.source_frames == 3607, "ABI loop spec, aliased");
    geode_loop_spec raw{};
    raw.loop_ms = 1000;
    raw.crossfade_ms = 100;
    raw.fps = 30;
    geode_loop_spec_fill(&raw);
    check(raw.loop_ms == 1000 && raw.loop_frames == 30 && raw.crossfade_frames == 3 && raw.loop_duration_us == 1'000'000,
          "ABI fill derives without clamping");
}

void encodePlan() {
    using namespace encode;
    const int64_t hourUs = 3'600'000'000LL;
    check(audioBytes(1'000'000) == 24'000, "a second of 192 kbit/s AAC");
    check(cappedVideoBps(40'000'000, hourUs, audioBytes(hourUs)) == 8'252'444, "an hour of 4K is capped to fit");
    check(cappedVideoBps(10'000'000, 60'000'000, 0) == 10'000'000, "a short render keeps its rate");
    check(cappedVideoBps(40'000'000, 100 * hourUs, 0) == kFloorBps, "never below the floor");
    check(cappedVideoBps(1'000'000, 100 * hourUs, 0) == 1'000'000, "a request under the floor is kept");
    check(cappedVideoBps(40'000'000, 0, 0) == 40'000'000, "an unknown duration leaves the request alone");
    check(geode_export_capped_video_bps(40'000'000, hourUs, audioBytes(hourUs), 0) == 8'252'444, "ABI default limit");
    check(geode_export_muxer_hard_limit_bytes() == 4LL * 1024 * 1024 * 1024 - 64LL * 1024 * 1024, "the hard stop");

    const FrameSize hd = frameSize(1080, 16, 9);
    check(hd.width == 1920 && hd.height == 1080, "16:9 1080p");
    const FrameSize tall = frameSize(1080, 9, 16);
    check(tall.width == 1080 && tall.height == 1920, "9:16 1080p");
    const FrameSize wide = frameSize(2160, 21, 9);
    check(wide.width == 4096 && wide.height == 1756, "21:9 4K shrinks to 4096 wide, the short side rounded to even");
    const FrameSize insta = frameSize(1080, 4, 5);
    check(insta.width == 1080 && insta.height == 1350, "4:5");
    const FrameSize square = frameSize(1080, 1, 1);
    check(square.width == 1080 && square.height == 1080, "1:1");
    int w = 0;
    int h = 0;
    geode_export_frame_size(1080, 16, 9, &w, &h);
    check(w == 1920 && h == 1080, "ABI frame size");

    check(fallbackFps(60) == 30 && fallbackFps(50) == 25 && fallbackFps(30) == 30 && fallbackFps(24) == 24,
          "the retry keeps the cadence family");
    check(clampFps(10) == 24 && clampFps(120) == 60 && clampFps(25) == 25, "fps clamp");
}

void chapters() {
    const int64_t one[] = {0};
    const int64_t oneDur[] = {60'000};
    check(chaptersFromClips(one, oneDur, 1).empty(), "one clip is not a mix");
    const int64_t starts[] = {0, 60'000};
    const int64_t durs[] = {60'000, 30'000};
    const auto clips = chaptersFromClips(starts, durs, 2);
    check(clips.size() == 2 && clips[1].startMs == 60'000 && clips[1].durationMs == 30'000 && clips[1].source == 1,
          "a chapter per clip");

    const int64_t late[] = {30'000, 90'000};
    const auto lead = chaptersFromMarkers(late, 2, 120'000);
    check(lead.size() == 3, "a lead-in chapter is added when the first marker is after 0:00");
    check(lead[0].startMs == 0 && lead[0].durationMs == 30'000 && lead[0].source == ChapterSpan::kLeadIn, "the lead-in");
    check(lead[1].startMs == 30'000 && lead[1].durationMs == 60'000 && lead[1].source == 0, "first marker");
    check(lead[2].endMs() == 120'000 && lead[2].source == 1, "the last chapter runs to the end");
    const int64_t atZero[] = {0, 60'000};
    check(chaptersFromMarkers(atZero, 2, 100'000).size() == 2, "no lead-in when a marker sits at 0:00");
    const int64_t unsorted[] = {90'000, 0};
    const auto sorted = chaptersFromMarkers(unsorted, 2, 100'000);
    check(sorted[0].source == 1 && sorted[1].source == 0 && sorted[1].durationMs == 10'000, "markers are sorted");
    const int64_t past[] = {0, 200'000};
    check(chaptersFromMarkers(past, 2, 100'000)[1].durationMs == 0, "a marker past the end is an empty chapter");
    check(chaptersFromMarkers(late, 1, 100'000).empty(), "one marker is no chapters");

    int64_t s[4];
    int64_t d[4];
    int32_t src[4];
    check(geode_chapters_from_markers(late, 2, 120'000, s, d, src, 4) == 3 && src[0] == -1 && s[2] == 90'000,
          "ABI chapters");
}


// The Kotlin ExportRange.of: both ends clamped into the track; a part under a second, or the whole track,
// renders the whole track.
void exportRange() {
    ExportRange r;
    check(clampExportRange(10'000, 40'000, 180'000, &r) && r.startMs == 10'000 && r.durationMs == 30'000, "a part");
    check(clampExportRange(-5'000, 40'000, 180'000, &r) && r.startMs == 0 && r.durationMs == 40'000,
          "a start before 0:00 is clamped");
    check(clampExportRange(170'000, 999'000, 180'000, &r) && r.startMs == 170'000 && r.durationMs == 10'000,
          "an end past the track is clamped");
    r = {7, 7};
    check(!clampExportRange(0, 180'000, 180'000, &r) && r.startMs == 7, "the whole track is no range, out untouched");
    check(!clampExportRange(0, 500'000, 180'000, &r), "past both ends is the whole track");
    check(!clampExportRange(10'000, 10'999, 180'000, &r), "under a second is no range");
    check(clampExportRange(10'000, 11'000, 180'000, &r) && r.durationMs == kMinExportRangeMs, "exactly a second is a range");
    check(!clampExportRange(40'000, 10'000, 180'000, &r), "ends the wrong way round");
    check(!clampExportRange(0, 1'000, 0, &r), "a track with no length");
    check(clampExportRange(0, 1'000, 180'000, nullptr), "out may be null");

    int64_t start = -1;
    int64_t duration = -1;
    check(geode_export_range_clamp(10'000, 40'000, 180'000, &start, &duration) == 1 && start == 10'000 &&
              duration == 30'000,
          "ABI range");
    check(geode_export_range_clamp(0, 180'000, 180'000, &start, &duration) == 0 && start == 10'000, "ABI whole track");
    check(geode_export_range_min_ms() == 1'000, "ABI minimum");
}

// QA #24, moved from the JVM RenderEtaTest: a fast lead-in (analysis, audio transcode) made the old estimate
// start low, climb, then count down. The lead-in is flagged not steady and must not bend the estimate.
void renderEta() {
    RenderEtaState eta;
    int64_t now = 0;
    bool silent = true;
    // Analysis takes 0..0.2 of the bar in 3 s, the AAC transcode 0.2..0.28 in 2 s.
    for (int step = 0; step <= 30; ++step) {
        silent = silent && renderEtaSample(eta, 0.2f * static_cast<float>(step) / 30.0f, now, false) == -1;
        now += 100;
    }
    for (int step = 0; step <= 20; ++step) {
        silent = silent && renderEtaSample(eta, 0.2f + 0.08f * static_cast<float>(step) / 20.0f, now, false) == -1;
        now += 100;
    }
    check(silent, "eta: the lead-in shows nothing");
    // The render takes the rest of the bar at an even pace over four minutes.
    const int64_t renderMs = 240'000;
    const int64_t renderStart = now;
    int shown = 0;
    int64_t previous = -1;
    bool early = true;
    bool close = true;
    bool down = true;
    while (now - renderStart <= renderMs) {
        const float done = static_cast<float>(now - renderStart) / static_cast<float>(renderMs);
        const int64_t estimate = renderEtaSample(eta, 0.28f + 0.72f * done, now, true);
        if (now - renderStart < 10'000 && estimate != -1) early = false;
        if (estimate != -1) {
            const int64_t truth = (renderMs - (now - renderStart)) / 1000;
            if (std::llabs(estimate - truth) > 3) close = false;
            if (previous != -1 && estimate > previous + 1) down = false;
            previous = estimate;
            ++shown;
        }
        now += 100;
    }
    check(early, "eta: nothing for the warm-up and the first span");
    check(close, "eta: within 3 s of the truth");
    check(down, "eta: never climbs");
    check(shown > 2'000, "eta: shown for most of the render");

    RenderEtaState back;
    renderEtaSample(back, 0.5f, 0, true);
    check(renderEtaSample(back, 0.4f, 100, true) == -1 && back.latestProgress == 0.4f && !back.steady,
          "eta: progress moving backwards starts over");
    check(renderEtaSample(back, 1.0f, 200, true) == 0, "eta: done is 0 s left");
    check(renderEtaSample(back, 1.7f, 300, true) == 0 && back.latestProgress == 1.0f, "eta: progress is clamped");

    // The ABI state: zeroed is fresh, and it carries across calls exactly as the C++ one does.
    geode_render_eta abi{};
    RenderEtaState mirror;
    bool same = true;
    for (int64_t t = 0; t <= 60'000; t += 250) {
        const float p = static_cast<float>(t) / 120'000.0f;
        if (geode_render_eta_sample(&abi, p, t, 1) != renderEtaSample(mirror, p, t, true)) same = false;
    }
    check(same, "eta: ABI matches");
    check(geode_render_eta_sample(nullptr, 0.5f, 0, 1) == -1, "eta: ABI null state");

    int64_t n = -1;
    check(renderEtaPhrase(29, &n) == EtaPhrase::AlmostDone && n == 0, "eta words: under 30 s is almost done");
    check(renderEtaPhrase(30, &n) == EtaPhrase::AboutAMinute && renderEtaPhrase(89, &n) == EtaPhrase::AboutAMinute,
          "eta words: about a minute");
    check(renderEtaPhrase(90, &n) == EtaPhrase::Minutes && n == 2, "eta words: 90 s rounds to 2 minutes");
    check(renderEtaPhrase(3'599, &n) == EtaPhrase::Minutes && n == 60, "eta words: just under an hour");
    check(renderEtaPhrase(3'600, &n) == EtaPhrase::Hours && n == 1, "eta words: an hour");
    check(renderEtaPhrase(5'400, &n) == EtaPhrase::Hours && n == 2, "eta words: 1.5 h rounds up");
    check(geode_render_eta_phrase(150, &n) == 2 && n == 3 && geode_render_eta_phrase(10, nullptr) == 0, "eta words: ABI");
}

// #532 tests: render range edges the PR's cases leave open, each named by the mutant it kills.
void exportRangeClampsBeforeItMeasures() {
    ExportRange r{7, 7};
    // Kills measuring the part before clamping it: 170..999 s is 829 s asked, but only 0.5 s of track.
    check(!clampExportRange(179'500, 999'000, 180'000, &r), "range: what is left after the end is clamped is under a second");
    // The same at the start: -5..0.5 s is 5.5 s asked, 0.5 s of track.
    check(!clampExportRange(-5'000, 500, 180'000, &r), "range: what is left after 0:00 is clamped is under a second");
    check(!clampExportRange(200'000, 250'000, 180'000, &r), "range: a part wholly past the end");
    // Kills writing out before the checks: every "whole track" answer leaves out as it was.
    check(r.startMs == 7 && r.durationMs == 7, "range: a short part leaves out untouched");
    check(!clampExportRange(0, 1'000, -5, &r) && r.startMs == 7, "range: a negative track length is no range");
    // Kills `start == 0 && end >= track` -> `start == 0`: a part from 0:00 that stops early is a range.
    check(clampExportRange(0, 4'000, 5'000, &r) && r.startMs == 0 && r.durationMs == 4'000, "range: from 0:00, short of the end");
    // Kills `start == 0 && end >= track` -> `end >= track`: a part that runs to the end from later is a range.
    check(clampExportRange(1'000, 5'000, 5'000, &r) && r.startMs == 1'000 && r.durationMs == 4'000, "range: to the end");
    // A one-second track has no part shorter than itself that is a second long.
    check(!clampExportRange(0, 1'000, 1'000, &r), "range: a one-second track renders whole");
    // ABI: a refusal leaves the caller's outputs alone; null outputs are allowed.
    int64_t start = -1;
    int64_t duration = -1;
    check(geode_export_range_clamp(10'000, 10'500, 180'000, &start, &duration) == 0 && start == -1 && duration == -1,
          "range ABI: under a second leaves outputs untouched");
    check(geode_export_range_clamp(10'000, 40'000, 180'000, nullptr, nullptr) == 1, "range ABI: null outputs");
}

// The ETA's rule as RenderEta.hpp states it, in hand-worked numbers. Samples once a second at 1 % a second (100 s
// render), from t = 0: warm-up to 4 s, then a 6 s span, so the first estimate is at 10 s: 90 % left at 1 %/s = 90 s.
void renderEtaFirstSpeaksAfterWarmupAndSpan() {
    RenderEtaState s;
    bool quiet = true;
    for (int64_t t = 0; t < 10'000; t += 1'000) {
        if (renderEtaSample(s, static_cast<float>(t) / 100'000.0f, t, true) != -1) quiet = false;
    }
    // Kills warm-up `<` -> `<=` and span `<` -> `<=` (each moves the first estimate to 11 s).
    check(quiet, "eta: nothing before warm-up plus span");
    check(renderEtaSample(s, 0.10f, 10'000, true) == 90, "eta: at 10 s, 90 % left at 1 %/s is 90 s");
    // Kills a countdown that does not count (or counts the wrong way): one second later, one second less.
    check(renderEtaSample(s, 0.11f, 11'000, true) == 89, "eta: a second later at the same pace, 89 s");
}

// Kills dropping the smoothing, the wall-time countdown, the exp sign, or ms read as s (each moves the answer
// far from 23). Base at 4 s with 0 % done; at 10 s 10 % done: 54 s shown. Then the render races: at 18 s 60 % done,
// a fresh figure of 9.3 s. The shown value counts 8 s down to 46 and eases 1 - e^-1 (63 %) of the way to 9.3:
// 46 - 0.632 * 36.67 = 22.8, so 23 s, never a jump straight to 9.
void renderEtaEasesTowardANewPace() {
    RenderEtaState s;
    renderEtaSample(s, 0.0f, 0, true);
    renderEtaSample(s, 0.0f, 4'000, true);
    check(renderEtaSample(s, 0.1f, 10'000, true) == 54, "eta: 10 % in 6 s leaves 54 s");
    check(renderEtaSample(s, 0.6f, 18'000, true) == 23, "eta: a sudden race is eased into, 23 s not 9");
}

// Kills losing the lower clamp: progress read as -1 for the baseline would make 10 % look like 110 % gained.
void renderEtaClampsNegativeProgress() {
    RenderEtaState s;
    renderEtaSample(s, -1.0f, 0, true);
    renderEtaSample(s, -1.0f, 4'000, true);
    check(renderEtaSample(s, 0.1f, 10'000, true) == 54, "eta: progress under 0 counts as 0");
}

// A render stuck at 0 % never divides by its zero progress, and 100 % is 0 s only while steady.
void renderEtaAtTheEnds() {
    RenderEtaState s;
    bool quiet = true;
    // Kills dropping the `gained < minProgress` gate (a 0 / 0 rate).
    for (int64_t t = 0; t <= 60'000; t += 500) {
        if (renderEtaSample(s, 0.0f, t, true) != -1) quiet = false;
    }
    check(quiet, "eta: a minute stuck at 0 % says nothing");
    RenderEtaState fresh;
    check(renderEtaSample(fresh, 1.0f, 0, true) == 0, "eta: done on the first sample is 0 s");
    // Kills testing `>= 1` before `steady`: a finished lead-in is not the render finishing.
    RenderEtaState leadIn;
    check(renderEtaSample(leadIn, 1.0f, 0, false) == -1, "eta: a lead-in at 100 % says nothing");
}

// Kills the gained gate removed or loosened: at 0.1 %/s the span is long enough at 10 s, but 1 % is not gained
// until 14 s after the 4 s baseline. At 15 s: 98.5 % left, 1.1 % in 11 s, so 985 s.
void renderEtaWaitsForOnePercent() {
    RenderEtaState s;
    bool quiet = true;
    for (int64_t t = 0; t <= 13'000; t += 1'000) {
        if (renderEtaSample(s, static_cast<float>(t) / 1'000'000.0f, t, true) != -1) quiet = false;
    }
    check(quiet, "eta: under 1 % gained says nothing");
    renderEtaSample(s, 0.014f, 14'000, true);
    const int64_t e = renderEtaSample(s, 0.015f, 15'000, true);
    check(e >= 984 && e <= 986, "eta: 1.1 % in 11 s with 98.5 % left is 985 s");
}

// Kills `clamped < latest` -> `<=`: a bar that moves every 500 ms, sampled every 100 ms, repeats itself; a repeat
// is not progress going backwards. Even pace, 120 s render.
void renderEtaRepeatedProgressIsNotARestart() {
    RenderEtaState s;
    int shown = 0;
    bool close = true;
    for (int64_t t = 0; t <= 110'000; t += 100) {
        const float p = static_cast<float>((t / 500) * 500) / 120'000.0f;
        const int64_t e = renderEtaSample(s, p, t, true);
        if (e == -1) continue;
        ++shown;
        if (std::llabs(e - (120'000 - t) / 1'000) > 3) close = false;
    }
    check(shown > 900, "eta: a bar that repeats itself still gets an estimate");
    check(close, "eta: within 3 s of the truth with a stepping bar");
}

// Kills a partial restart: after a retry sends the bar back, the new run waits out warm-up and span again,
// whatever the old run had shown.
void renderEtaRetryStartsOver() {
    RenderEtaState s;
    int64_t t = 0;
    int64_t last = -1;
    for (; t <= 30'000; t += 100) last = renderEtaSample(s, static_cast<float>(t) / 100'000.0f, t, true);
    check(last != -1, "eta: the first run was showing");
    const int64_t retry = t;
    bool quiet = true;
    int64_t after = -1;
    for (; t <= retry + 12'000; t += 100) {
        after = renderEtaSample(s, static_cast<float>(t - retry) / 100'000.0f, t, true);
        if (t - retry < 10'000 && after != -1) quiet = false;
    }
    check(quiet, "eta: a retry shows nothing for warm-up plus span");
    check(after >= 86 && after <= 90, "eta: then about 88 s of the new 100 s run");
}

// A tiny seeded generator (xorshift32), so the sequence is the same on every standard library.
uint32_t nextRandom(uint32_t& x) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

// Kills a field dropped from either copy in geode_render_eta_sample: an uneven render (bursts, stalls, retries,
// lead-in samples) through the ABI and through RenderEtaState give the same answers. The PR's parity loop is
// an even pace, where the smoothing state hardly matters.
void renderEtaAbiMatchesOnAnUnevenRender() {
    geode_render_eta abi{};
    RenderEtaState mirror;
    uint32_t seed = 0x5EED532u;
    float p = 0.0f;
    int64_t t = 0;
    bool same = true;
    int shown = 0;
    for (int i = 0; i < 5'000; ++i) {
        const uint32_t r = nextRandom(seed);
        t += 50 + static_cast<int64_t>(r % 400);
        const uint32_t kind = (r >> 9) % 1'000;
        if (kind < 2) {
            p = 0.0f;  // a retry
        } else {
            p += static_cast<float>((r >> 16) % 300) / 100'000.0f;
            if (p > 1.0f) p = 0.0f;
        }
        const bool steady = kind >= 5;
        const int64_t a = geode_render_eta_sample(&abi, p, t, steady ? 1 : 0);
        const int64_t b = renderEtaSample(mirror, p, t, steady);
        if (a != b) same = false;
        if (b > 0) ++shown;
    }
    check(shown > 100, "eta ABI: the uneven render showed estimates");
    check(same, "eta ABI: matches the state struct on an uneven render");
}

// The Kotlin RenderEta.describe buckets: minutes rounded to the nearest, hours too.
void renderEtaPhraseRoundsToTheNearest() {
    int64_t n = -1;
    // Kills `+ 30` -> `+ 31`: 2 min 29 s reads 2 minutes.
    check(renderEtaPhrase(149, &n) == EtaPhrase::Minutes && n == 2, "eta words: 149 s is 2 minutes");
    // Kills `+ 1800` -> `+ 1801`: 1 h 29 min 59 s reads 1 hour.
    check(renderEtaPhrase(5'399, &n) == EtaPhrase::Hours && n == 1, "eta words: 5399 s is 1 hour");
    // Kills a count left over from an earlier bucket: about a minute carries none.
    n = -1;
    check(renderEtaPhrase(60, &n) == EtaPhrase::AboutAMinute && n == 0, "eta words: about a minute carries no count");
    n = -1;
    check(renderEtaPhrase(-5, &n) == EtaPhrase::AlmostDone && n == 0, "eta words: under nothing is almost done");
    check(renderEtaPhrase(86'400, &n) == EtaPhrase::Hours && n == 24, "eta words: a day is 24 hours");
}

}  // namespace

int main() {
    frameClock();
    loopSpec();
    loopExtend();
    encodePlan();
    chapters();
    exportRange();
    renderEta();
    exportRangeClampsBeforeItMeasures();
    renderEtaFirstSpeaksAfterWarmupAndSpan();
    renderEtaEasesTowardANewPace();
    renderEtaClampsNegativeProgress();
    renderEtaAtTheEnds();
    renderEtaWaitsForOnePercent();
    renderEtaRepeatedProgressIsNotARestart();
    renderEtaRetryStartsOver();
    renderEtaAbiMatchesOnAnUnevenRender();
    renderEtaPhraseRoundsToTheNearest();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("output_test: all passed\n");
    return 0;
}
