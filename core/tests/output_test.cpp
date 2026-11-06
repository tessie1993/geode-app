// Host-side checks for the offline render's planning (core/output: FrameClock, LoopPlan, EncodePlan,
// Chapters, ExportRange, RenderEta) and its C ABI. Built and run by CI (.github/workflows/android.yml,
// "Native export planning tests") with the system compiler; no device. Where a value pins the Kotlin it
// replaces (VideoExporter, LoopSpec, LoopExtendPlan, MuxerBudget, ExportAspect, fallbackExportFps,
// ChapterMarkers, ExportRange.of, RenderEta and its JVM RenderEtaTest), the comment says so; the deliberate
// changes (exact pts, the seam budget's lost frame, the 0:00 lead-in) say why.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <random>
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

// ── #415 follow-up: boundaries, long renders and parity with the deleted Kotlin tests ──────────────────
// Each block names the mutants of the PR's code it kills. Expected values are hand-worked literals, the
// deleted JVM tests' literals (MuxerBudgetTest, ExportFpsTest), or an independent oracle (std::llround
// in double; the Kotlin test's own bytes = bps * µs / 8e6).

// Kills: framesFor rounding up or to nearest instead of floor; nonNegative() dropped from pts, start or ms;
// frameTimeMs / frameStartUs / ptsNs using the wrong per-second unit; a 32-bit product over ten hours.
void frameClockBoundaries() {
    const FrameClock c60(60, 48000);
    check(c60.framesFor(999'999) == 59, "a render just short of a second ends on its last whole frame (59)");
    check(c60.framesFor(1'016'666) == 60, "1.0167 s at 60 fps draws 60 frames, not 61");
    check(c60.framesFor(1) == 1, "a 1 µs render still draws one frame");
    const int64_t tenHoursUs = 36'000'000'000LL;
    check(c60.framesFor(tenHoursUs) == 2'160'000, "ten hours at 60 fps is 2 160 000 frames");
    check(FrameClock(24, 48000).framesFor(tenHoursUs) == 864'000, "ten hours at 24 fps is 864 000 frames");
    check(c60.ptsNs(2'160'000) == 36'000'000'000'000LL, "the frame after ten hours lands exactly on 10 h in ns");
    check(FrameClock(30, 48000).ptsNs(30 * 3600 * 4) == 14'400'000'000'000LL, "four hours at 30 fps does not drift");
    const FrameClock c24(24, 44100);
    check(c24.ptsNs(1) == 41'666'666 && c24.frameStartUs(1) == 41'666 && c24.frameTimeMs(1) == 41,
          "frame 1 at 24 fps in ns, µs and ms, each floored");
    check(FrameClock(25, 48000).ptsNs(1) == 40'000'000, "frame 1 at 25 fps is exactly 40 ms");
    check(c60.ptsNs(-1) == 0 && c60.frameStartUs(-1) == 0 && c60.frameTimeMs(-1) == 0, "a negative frame is time 0");
    check(FrameClock(-30, 48000).fps() == 1, "a negative rate is taken as 1 fps");

    // Units agree: the ns stamp, the µs start and the ms keyframe time are one instant at three scales.
    std::mt19937 rng(415);
    const int fpsList[] = {24, 25, 30, 50, 60};
    bool unitsAgree = true;
    for (int i = 0; i < 4000; ++i) {
        const FrameClock c(fpsList[rng() % 5u], 48000);
        const int64_t n = static_cast<int64_t>(rng() % 10'000'000u);
        if (c.ptsNs(n) / 1000 != c.frameStartUs(n) || c.frameStartUs(n) / 1000 != c.frameTimeMs(n)) unitsAgree = false;
    }
    check(unitsAgree, "pts / 1000 is the frame start in µs, and that / 1000 is its time in ms");

    check(geode_export_total_frames(1'000'000, 0) == 1 && geode_export_total_frames(2'500'000, 24) == 60,
          "ABI total frames (0 fps taken as 1)");
    check(geode_export_frame_pts_ns(3, 60) == 50'000'000, "ABI pts");
    check(geode_export_frame_time_ms(1, 24) == 41, "ABI frame time in ms");
}

// Kills: samplesThrough's (2x + fps) / 2fps rounding mutated (- fps, no + fps, / fps), the rate guard
// dropped, and any accumulation that would drift over an hour. Oracle: std::llround in double, which
// rounds halves away from zero (cppreference), exact here since (n + 1) * rate stays far under 2^53.
void soundEveryFrameHears() {
    const FrameClock c24(24, 44100);
    check(c24.samplesThrough(1) == 3675 && c24.samplesThrough(2) == 5513, "1837.5 a frame: 3675, then 5512.5 up");
    check(FrameClock(60, 48000).samplesThrough(60 * 3600 - 1) == 172'800'000, "an hour at 48 kHz, to the sample");
    check(c24.samplesThrough(24 * 3600 - 1) == 158'760'000, "an hour at 44.1 kHz / 24 fps, to the sample");
    bool shares = true;
    for (int64_t n = 1; n < 24 * 600; ++n) {
        const int64_t got = c24.samplesThrough(n) - c24.samplesThrough(n - 1);
        if (got != 1837 && got != 1838) shares = false;
    }
    check(shares, "each frame hears 1837 or 1838 new samples at 44.1 kHz / 24 fps, so each sample once");
    check(FrameClock(60, 0).samplesThrough(100) == 0 && FrameClock(60, -48000).samplesThrough(100) == 0,
          "no sample rate hears nothing, never a negative position");

    std::mt19937 rng(4151);
    const int rates[] = {22050, 32000, 44100, 48000, 88200, 96000};
    const int fpsList[] = {24, 25, 30, 50, 60};
    bool rounds = true;
    for (int i = 0; i < 20000; ++i) {
        const int rate = rates[rng() % 6u];
        const int fps = fpsList[rng() % 5u];
        const int64_t n = static_cast<int64_t>(rng() % 1'000'000u);
        const long long want = std::llround(static_cast<double>(n + 1) * rate / fps);
        if (FrameClock(fps, rate).samplesThrough(n) != want) rounds = false;
    }
    check(rounds, "samplesThrough is llround((n + 1) * rate / fps) for every rate pair");
    check(geode_export_samples_through(0, 24, 44100) == 1838, "ABI samples through");
}

// Kills: LoopSpec::of's clamps off by one or dropped, valid()'s `<=` half-loop bound as `<`, its fps
// bounds as `<` / `>`, loopFrames' floor of 2 and crossfadeFrames' 1 .. loop/2 clamp dropped, frame counts
// rounding up, and a loop duration taken from loopMs rather than from the whole frames drawn.
void loopSpecBoundaries() {
    check(LoopSpec::of(29'999).loopMs == 30'000 && LoopSpec::of(30'000).loopMs == 30'000, "30 s is the shortest loop");
    check(LoopSpec::of(300'001).loopMs == 300'000 && LoopSpec::of(300'000).loopMs == 300'000, "5 min is the longest");
    check(LoopSpec::of(60'000, 199).crossfadeMs == 200 && LoopSpec::of(60'000, 200).crossfadeMs == 200, "200 ms dissolve");
    check(LoopSpec::of(60'000, 5'001).crossfadeMs == 5'000 && LoopSpec::of(60'000, 5'000).crossfadeMs == 5'000, "5 s dissolve");
    check(LoopSpec::of(60'000, 1'000, 23).fps == 24 && LoopSpec::of(60'000, 1'000, 24).fps == 24, "24 fps floor");
    check(LoopSpec::of(60'000, 1'000, 61).fps == 60 && LoopSpec::of(60'000, 1'000, 60).fps == 60, "60 fps ceiling");

    LoopSpec s;
    s.loopMs = 1000;
    s.crossfadeMs = 500;
    s.fps = 60;
    check(s.valid(), "a dissolve of exactly half the loop is valid");
    s.fps = 24;
    check(s.valid(), "24 fps is valid");
    s.fps = 23;
    check(!s.valid(), "23 fps is not");
    s.fps = 61;
    check(!s.valid(), "61 fps is not");
    s.fps = 60;
    s.crossfadeMs = 0;
    check(!s.valid(), "a spec without a dissolve is not valid");

    geode_loop_spec tiny{};
    tiny.loop_ms = 10;
    tiny.crossfade_ms = 5;
    tiny.fps = 60;
    geode_loop_spec_fill(&tiny);
    check(tiny.loop_frames == 2 && tiny.crossfade_frames == 1 && tiny.source_frames == 3 && tiny.loop_duration_us == 33'333,
          "a loop under a frame still draws 2, with a 1-frame dissolve");
    geode_loop_spec wide{};
    wide.loop_ms = 1000;
    wide.crossfade_ms = 900;
    wide.fps = 30;
    geode_loop_spec_fill(&wide);
    check(wide.loop_frames == 30 && wide.crossfade_frames == 15, "a dissolve never spans more than half the loop");

    const LoopSpec ragged = LoopSpec::of(30'010, 1'000, 60);
    check(ragged.loopFrames() == 1800 && ragged.loopDurationUs() == 30'000'000, "the loop lasts its whole frames");
    check(LoopSpec::of(30'000, 200, 24).crossfadeFrames() == 4, "200 ms at 24 fps is 4 whole frames (4.8)");
    check(LoopSpec::of(30'000, 200).seamPreviewStartMs(40'000) == 0, "a preview lead past the start begins at 0");
    check(seamWeight(1, 3) == 0.5f, "the seam's middle frame is half stash");
}

// Kills: budgeted() truncating instead of ceiling (the Kotlin lost frame), the affordable >= 1 floor, the
// pixel product in 32 bits, and a shortened dissolve the stash still cannot hold. The budget is the deleted
// LoopRender.SEAM_STASH_BUDGET_BYTES, 192 MiB of RGBA frames.
void seamStashBudget() {
    const LoopSpec qhd = LoopSpec::of(60'000, 1'000, 60).budgeted(2560, 1440);
    check(qhd.crossfadeFrames() == 13 && qhd.crossfadeMs == 217, "1440p holds 13 frames; Kotlin's 216 ms held 12");
    const LoopSpec uhd30 = LoopSpec::of(60'000, 1'000, 30).budgeted(3840, 2160);
    check(uhd30.crossfadeFrames() == 6 && uhd30.crossfadeMs == 200, "4K at 30 fps: 6 frames, 200 ms");
    const LoopSpec uhd24 = LoopSpec::of(60'000, 1'000, 24).budgeted(3840, 2160);
    check(uhd24.crossfadeFrames() == 6 && uhd24.crossfadeMs == 250, "4K at 24 fps: 6 frames, 250 ms");
    const LoopSpec huge = LoopSpec::of(60'000, 1'000, 60).budgeted(20'000, 20'000);
    check(huge.crossfadeFrames() == 1 && huge.crossfadeMs == 17, "a frame bigger than the stash still keeps one");
    const LoopSpec none = LoopSpec::of(60'000, 1'000, 60).budgeted(0, 0);
    check(none.crossfadeMs == 1'000, "a zero size is taken as 1x1, which the stash always holds");

    const int64_t budget = 192LL * 1024 * 1024;
    std::mt19937 rng(41515);
    bool holds = true;
    bool maximal = true;
    bool shortest = true;
    for (int i = 0; i < 3000; ++i) {
        const int w = 16 + static_cast<int>(rng() % 7665u);
        const int h = 16 + static_cast<int>(rng() % 7665u);
        const int64_t fadeMs = 200 + static_cast<int64_t>(rng() % 4801u);
        const int fps = 24 + static_cast<int>(rng() % 37u);
        const LoopSpec spec = LoopSpec::of(120'000, fadeMs, fps);
        const LoopSpec b = spec.budgeted(w, h);
        const int64_t frameBytes = static_cast<int64_t>(w) * h * 4;
        const int frames = b.crossfadeFrames();
        if (frames > spec.crossfadeFrames() || (frames > 1 && frames * frameBytes > budget)) holds = false;
        if (frames != spec.crossfadeFrames() && (frames + 1) * frameBytes <= budget) maximal = false;
        if (frames != spec.crossfadeFrames() && frames > 1) {
            LoopSpec shorter = b;
            shorter.crossfadeMs -= 1;
            if (shorter.crossfadeFrames() >= frames) shortest = false;
        }
    }
    check(holds, "a budgeted dissolve fits the 192 MiB stash and is never longer than asked");
    check(maximal, "a budgeted dissolve keeps every frame the stash affords");
    check(shortest, "a budgeted dissolve is the shortest whole-ms length with those frames");
}

// Kills: the ceil off by one (+loop instead of +loop - 1), the loops >= 1 and total >= 1 guards, the
// seam preview's upper clamp, fittingBitRate's 1 s floor, INT_MAX cap and room >= 0, a 32-bit offset
// over a long render, uneven or unordered drift stops, and ABI copies past the caller's capacity.
void loopExtendLong() {
    const int64_t loopUs = 30'000'000;
    check(LoopExtendPlan::of(loopUs, 90'000'000, 1).repeats() == 3, "90 s of 30 s loops is 3 repeats, not 4");
    check(LoopExtendPlan::of(loopUs, 90'000'001, 1).repeats() == 4, "one µs more needs a fourth");
    check(LoopExtendPlan::of(loopUs, 100'000'000, 0).segments().size() == 1, "no stops is taken as one");
    check(LoopExtendPlan::of(loopUs, 100'000'000, -3).segments().size() == 1, "negative stops are taken as one");
    const LoopExtendPlan empty = LoopExtendPlan::of(loopUs, 0, 2);
    check(empty.repeats() == 1 && empty.totalDurationUs() == 1, "an empty running time still copies the loop once");

    const LoopExtendPlan tenH = LoopExtendPlan::of(loopUs, 36'000'000'000LL, 3);
    check(tenH.repeats() == 1200 && tenH.segments().size() == 3 && tenH.segments()[2].repeats == 400,
          "ten hours of 30 s loops: 1200 repeats, 400 per stop");
    check(tenH.segments()[2].startUs == 24'000'000'000LL, "the third stop starts at 6 h 40 min");
    check(tenH.writes().back().offsetUs == 35'970'000'000LL && tenH.writes().back().loopIndex == 2,
          "the last copy lands 35 970 s in, past what 32 bits of µs hold");
    check(tenH.estimatedBytes(100'000'000, 864'000'000) == 123'281'280'000LL, "ten hours weigh 123 GB with tables");
    check(tenH.seamPreviewStartMs(35'970'000) == 35'968'000, "a seam preview at the last wrap of a long file");

    const LoopExtendPlan p = LoopExtendPlan::of(loopUs, 100'000'000, 3);
    check(p.seamPreviewStartMs(200'000) == 100'000, "a seam preview never starts past the file's end");
    check(LoopExtendPlan::of(500'000, 500'000, 1).fittingBitRate(1'000'000, 0) == 8'000'000, "under a second is 1 s");
    check(LoopExtendPlan::of(loopUs, 100'900'000, 3).fittingBitRate(4'000'000, 1'000'000) == 240'000,
          "whole seconds, as Kotlin's Long division gave");
    check(LoopExtendPlan::of(1'000'000, 1'000'000, 1).fittingBitRate(1'000'000'000'000LL, 0) == 2'147'483'647,
          "a bit rate too big for an int is capped");
    check(LoopExtendPlan::of(1'000'000, 1'000'000, 1).fittingBitRate(1'000, 5'000) == 0, "no room is 0 bit/s");

    std::mt19937 rng(41516);
    bool tiles = true;
    bool even = true;
    for (int i = 0; i < 2000; ++i) {
        const int64_t loop = 30'000'000 + static_cast<int64_t>(rng() % 270'000'001u);
        const int64_t total = 1'000'000 + static_cast<int64_t>(rng()) * 20;  // 1 s .. ~24 h
        const int loops = 1 + static_cast<int>(rng() % 8u);
        const LoopExtendPlan plan = LoopExtendPlan::of(loop, total, loops);
        const auto& w = plan.writes();
        if (w.empty() || w.front().offsetUs != 0) tiles = false;
        for (size_t k = 1; k < w.size(); ++k) {
            if (w[k].offsetUs - w[k - 1].offsetUs != loop || w[k].loopIndex < w[k - 1].loopIndex) tiles = false;
        }
        if (!w.empty() && (w.back().offsetUs >= total || w.back().offsetUs + loop < total)) tiles = false;
        const auto& segs = plan.segments();
        if (static_cast<int>(segs.size()) != std::min(loops, plan.repeats())) even = false;
        int sum = 0;
        for (size_t k = 0; k < segs.size(); ++k) {
            sum += segs[k].repeats;
            if (segs[k].loopIndex != static_cast<int>(k) || segs[k].repeats < 1) even = false;
            if (segs[k].repeats != segs[0].repeats && segs[k].repeats != segs[0].repeats - 1) even = false;
            if (k > 0 && segs[k].repeats > segs[k - 1].repeats) even = false;
            if (k > 0 && segs[k].startUs != segs[k - 1].startUs + segs[k - 1].repeats * loop) even = false;
        }
        if (sum != plan.repeats()) even = false;
    }
    check(tiles, "copies run back to back from 0, one loop apart, ending on the first that reaches the total");
    check(even, "drift stops share the repeats within one, earlier stops first, none empty");

    int64_t off[4] = {-7, -7, -7, -7};
    int32_t idx[4] = {-7, -7, -7, -7};
    check(geode_loop_extend_plan(loopUs, 100'000'000, 3, off, idx, 2) == 4 && off[1] == 30'000'000 && off[2] == -7 &&
              idx[2] == -7,
          "ABI plan writes only the capacity given and still counts every repeat");
    int64_t off2[2] = {-7, -7};
    int32_t idx2[2] = {-7, -7};
    check(geode_loop_extend_plan(loopUs, 100'000'000, 3, off2, idx2, -1) == 4 && off2[0] == -7 && idx2[0] == -7,
          "ABI plan with a negative capacity writes nothing");
}

// The deleted Kotlin MuxerBudgetTest, ported with its literals, plus the bounds it did not reach.
// Kills: the cap ignoring the explicit limit or the ABI's <= 0 default, the audio >= 0 guard, the
// fitting/request comparison, audioBytes' negative guard, the floor and the min(floor, request) rule.
void muxerBudgetParity() {
    using namespace encode;
    const int fourK = 40'000'000;   // ExportQuality.UHD4K.bitRate
    const int fullHd = 12'000'000;  // ExportQuality.FHD1080.bitRate
    const int64_t minuteUs = 60'000'000LL;
    const int64_t hour = 60 * minuteUs;
    const int64_t safe = 3'800'000'000LL;  // MuxerBudget.SAFE_BYTES
    auto bytes = [](int64_t bps, int64_t us) { return bps * us / 8'000'000LL; };

    check(cappedVideoBps(fourK, 5 * minuteUs, audioBytes(5 * minuteUs)) == fourK, "a short render keeps the preset's rate");
    const int64_t audio = audioBytes(hour);
    const int capped = cappedVideoBps(fourK, hour, audio);
    check(capped < fourK && bytes(capped, hour) + audio <= safe, "an hour of 4K fits the safe size");
    check(bytes(capped, hour) + audio < 4LL * 1024 * 1024 * 1024, "and stays well under 4 GiB");
    check(bytes(capped + 1'000, hour) + audio > safe, "within a kilobit of the most that fits");
    const int cappedHd = cappedVideoBps(fullHd, hour, audio);
    check(cappedHd < fullHd && bytes(cappedHd, hour) + audio <= safe && cappedHd == 8'252'444, "an hour of 1080p too");
    check(cappedVideoBps(fourK, 600 * minuteUs, audioBytes(600 * minuteUs)) == 2'000'000, "never below the floor");
    check(cappedVideoBps(1'000'000, 600 * minuteUs, audioBytes(600 * minuteUs)) == 1'000'000, "a low request is kept");
    check(cappedVideoBps(fourK, 0, 0) == fourK && cappedVideoBps(fourK, -1, 0) == fourK, "unknown duration");
    check(audioBytes(1'000'000) == 24'000, "192 kbit/s for one second is 24 000 bytes");

    check(audioBytes(-1'000'000) == 0 && audioBytes(600 * minuteUs) == 864'000'000, "audio bytes: none, ten hours");
    check(geode_export_audio_bytes(1'000'000, 128'000) == 16'000, "ABI audio bytes at 128 kbit/s");
    check(cappedVideoBps(fourK, hour, -1'000'000'000'000LL) == 8'444'444, "negative audio is no audio");
    check(cappedVideoBps(8'444'444, hour, 0) == 8'444'444 && cappedVideoBps(8'444'445, hour, 0) == 8'444'444,
          "a request that just fits is kept, one bit more is capped");
    check(cappedVideoBps(0, hour, 0) == 0 && cappedVideoBps(-5, hour, 0) == -5, "a non-positive request is left alone");
    check(geode_export_capped_video_bps(fourK, hour, 0, 1'800'000'000LL) == 4'000'000, "ABI honours an explicit limit");
    check(geode_export_capped_video_bps(fourK, hour, 0, -5) == 8'444'444, "ABI takes a negative limit as the target");

    std::mt19937 rng(41517);
    bool bounded = true;
    bool fits = true;
    for (int i = 0; i < 5000; ++i) {
        const int64_t dur = 1'000'000 + static_cast<int64_t>(rng()) * 16;  // 1 s .. ~19 h
        const int req = 1'000'000 + static_cast<int>(rng() % 79'000'001u);
        const int64_t a = audioBytes(dur);
        const int got = cappedVideoBps(req, dur, a);
        const int64_t roomBits = (safe - a) * 8'000'000LL;
        if (got > req || got < std::min(2'000'000, req)) bounded = false;
        if (got > std::min(2'000'000, req)) {
            if (static_cast<int64_t>(got) * dur > roomBits) fits = false;
            if (got != req && (static_cast<int64_t>(got) + 1) * dur <= roomBits) fits = false;
        }
    }
    check(bounded, "the cap stays between min(floor, request) and the request");
    check(fits, "above the floor the capped rate fits the target, and one bit/s more would not");
}

// Kills: frameSize's landscape test flipped, the 4096 cap dropped or applied to one orientation only,
// even() rounding down, the long side rounded instead of truncated. Hand-worked for every quality x ratio.
void frameSizes() {
    using namespace encode;
    struct Row {
        int shortSide, wr, hr, w, h;
    };
    const Row rows[] = {
        {720, 16, 9, 1280, 720},   {720, 9, 16, 720, 1280},   {720, 1, 1, 720, 720},     {720, 4, 5, 720, 900},
        {720, 4, 3, 960, 720},     {720, 21, 9, 1680, 720},   {1080, 4, 3, 1440, 1080},  {1080, 21, 9, 2520, 1080},
        {2160, 16, 9, 3840, 2160}, {2160, 9, 16, 2160, 3840}, {2160, 1, 1, 2160, 2160},  {2160, 4, 5, 2160, 2700},
        {2160, 4, 3, 2880, 2160},  {2160, 9, 21, 1756, 4096}, {1081, 1, 1, 1082, 1082},  {721, 4, 3, 962, 722},
    };
    bool all = true;
    for (const Row& r : rows) {
        const FrameSize s = frameSize(r.shortSide, r.wr, r.hr);
        if (s.width != r.w || s.height != r.h) {
            std::printf("  frameSize(%d, %d:%d) = %dx%d, want %dx%d\n", r.shortSide, r.wr, r.hr, s.width, s.height, r.w, r.h);
            all = false;
        }
    }
    check(all, "every export quality and ratio, a portrait 4K cap and odd sides rounded up to even");
    const FrameSize zero = frameSize(0, 16, 9);
    const FrameSize noRatio = frameSize(1080, 0, 9);
    check(zero.width == 0 && zero.height == 0 && noRatio.width == 0 && noRatio.height == 0, "no size, no frame");
    geode_export_frame_size(2160, 21, 9, nullptr, nullptr);
    int w = 0;
    geode_export_frame_size(2160, 9, 21, &w, nullptr);
    check(w == 1756, "ABI frame size tolerates a missing out pointer");

    // ExportFpsTest.encoderFallbackStaysInTheRequestsCadenceFamily, ported.
    check(fallbackFps(25) == 25 && geode_export_fallback_fps(60) == 30 && geode_export_fallback_fps(50) == 25,
          "25 is retried as 25; ABI fallback");
    const int offered[] = {24, 25, 30, 50, 60};  // EXPORT_FPS_OPTIONS
    bool family = true;
    for (int fps : offered) {
        if (fallbackFps(fps) > fps || fallbackFps(fps) < 24) family = false;
    }
    check(family, "the retry of every offered rate is no faster than asked and never under 24");
    check(clampFps(23) == 24 && clampFps(24) == 24 && clampFps(60) == 60 && clampFps(61) == 60, "fps clamp bounds");
}

// Kills: the clamps to 0 in both chapter builders dropped, the lead-in test as >= 0, the sort dropped,
// the last chapter not running to the total, and ABI copies past the capacity. The tiling property is
// what a player needs: one chapter list from 0:00, gap-free, every marker once.
void chapterBoundaries() {
    const int64_t starts[] = {-5, 1'000};
    const int64_t durs[] = {1'000, -1};
    const auto c = chaptersFromClips(starts, durs, 2);
    check(c.size() == 2 && c[0].startMs == 0 && c[1].durationMs == 0, "a clip never starts before 0:00 or runs backwards");
    check(chaptersFromClips(nullptr, durs, 2).empty() && chaptersFromClips(starts, durs, 0).empty(), "no clips");
    const int64_t early[] = {-5, 10'000};
    const auto e = chaptersFromMarkers(early, 2, 30'000);
    check(e.size() == 2 && e[0].startMs == 0 && e[0].durationMs == 10'000 && e[0].source == 0,
          "a marker before 0:00 opens at 0:00, with no lead-in");
    const int64_t ties[] = {5'000, 5'000, 0};
    const auto t = chaptersFromMarkers(ties, 3, 9'000);
    check(t.size() == 3 && t[0].source == 2 && t[1].source == 0 && t[2].source == 1 && t[1].durationMs == 0 &&
              t[2].durationMs == 4'000,
          "markers at one time keep their order, the first of them an empty chapter");
    check(chaptersFromMarkers(nullptr, 3, 9'000).empty(), "no markers");

    std::mt19937 rng(41518);
    bool tiled = true;
    for (int i = 0; i < 3000; ++i) {
        const int count = 2 + static_cast<int>(rng() % 11u);
        std::vector<int64_t> marks(static_cast<size_t>(count));
        int64_t lowest = INT64_MAX;
        int64_t highest = 0;
        for (auto& m : marks) {
            m = static_cast<int64_t>(rng() % 600'001u);
            if (rng() % 4u == 0) m = 0;
            lowest = std::min(lowest, m);
            highest = std::max(highest, m);
        }
        const int64_t total = static_cast<int64_t>(rng() % 700'001u);
        const auto ch = chaptersFromMarkers(marks.data(), count, total);
        const bool lead = lowest > 0;
        if (ch.size() != marks.size() + (lead ? 1u : 0u) || ch.front().startMs != 0) {
            tiled = false;
            continue;
        }
        if (lead && ch.front().source != ChapterSpan::kLeadIn) tiled = false;
        std::vector<int> seen(static_cast<size_t>(count), 0);
        for (size_t k = 0; k < ch.size(); ++k) {
            if (ch[k].durationMs < 0) tiled = false;
            if (k > 0 && ch[k].startMs != ch[k - 1].endMs()) tiled = false;
            if (ch[k].source >= 0 && ch[k].source < count) {
                ++seen[static_cast<size_t>(ch[k].source)];
            } else if (!(k == 0 && lead)) {
                tiled = false;
            }
        }
        for (int n : seen) {
            if (n != 1) tiled = false;
        }
        if (ch.back().endMs() != std::max(total, highest)) tiled = false;
    }
    check(tiled, "marker chapters tile 0:00 to the end with no gap, every marker once, a lead-in only when needed");

    int64_t s[2] = {-7, -7};
    int64_t d[2] = {-7, -7};
    int32_t src[2] = {-7, -7};
    const int64_t clipStarts[] = {0, 60'000};
    const int64_t clipDurs[] = {60'000, 30'000};
    check(geode_chapters_from_clips(clipStarts, clipDurs, 2, s, d, src, 1) == 2 && d[0] == 60'000 && s[1] == -7 &&
              src[1] == -7,
          "ABI clip chapters write only the capacity given");
    const int64_t late[] = {30'000, 90'000};
    check(geode_chapters_from_markers(late, 2, 120'000, nullptr, nullptr, nullptr, 0) == 3, "ABI chapter count query");
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
    frameClockBoundaries();
    soundEveryFrameHears();
    loopSpecBoundaries();
    seamStashBudget();
    loopExtendLong();
    muxerBudgetParity();
    frameSizes();
    chapterBoundaries();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("output_test: all passed\n");
    return 0;
}
