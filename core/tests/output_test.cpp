// Host-side checks for the offline render's planning (core/output: FrameClock, LoopPlan, EncodePlan,
// Chapters, ExportRange, RenderEta, RenderPlan) and its C ABI. Built and run by CI (.github/workflows/android.yml,
// "Native export planning tests") with the system compiler; no device. Where a value pins the Kotlin it
// replaces (VideoExporter, ProjectRenderPlan, LoopSpec, LoopExtendPlan, MuxerBudget, ExportAspect, fallbackExportFps,
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
#include "output/RenderPlan.hpp"
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

// Pins the Kotlin ProjectRenderPlan.plan (#636) it replaces, rule by rule. Live = an enabled clip of the wanted
// kind on a live lane; MIN_CLIP_DURATION_MS is 33.
constexpr int kLive = PlanClip::kEnabled | PlanClip::kWanted | PlanClip::kLaneLive;
constexpr int64_t kMin = 33;

PlanClip scene(int64_t start, int64_t end, int lane = 0, int flags = kLive) { return {start, end, 0, 0, lane, flags}; }

PlanClip song(int64_t start, int64_t end, int64_t in = 0, int64_t source = 0, int lane = 1, int flags = kLive) {
    return {start, end, in, source, lane, flags};
}

bool plan(const std::vector<PlanClip>& v, const std::vector<PlanClip>& a, RenderPlan* out) {
    return planRender(v.data(), static_cast<int>(v.size()), a.data(), static_cast<int>(a.size()), kMin, out);
}

void renderPlan() {
    RenderPlan p;

    // A take: one scene over the whole of a song the project knows the length of.
    check(plan({scene(0, 180'000)}, {song(0, 180'000, 0, 180'000)}, &p) && p.visual == 0 && p.song == 0 &&
              p.programmeStartMs == 0 && p.durationMs == 180'000 && p.songStartMs == 0 && p.wholeSong,
          "plan: the whole song");
    check(plan({scene(0, 180'000)}, {song(0, 180'000)}, &p) && !p.wholeSong && p.durationMs == 180'000,
          "plan: an unbounded song is a range");
    check(plan({scene(0, 180'000)}, {song(0, 120'000, 0, 180'000)}, &p) && !p.wholeSong && p.durationMs == 120'000,
          "plan: a song clip shorter than its source is a range");
    check(plan({scene(0, 200'000)}, {song(0, 200'000, 0, 180'000)}, &p) && p.wholeSong,
          "plan: a song clip at least as long as its source is the whole song");

    // Cut against each other, programme time mapped to song time.
    check(plan({scene(10'000, 50'000)}, {song(0, 180'000, 0, 180'000)}, &p) && p.programmeStartMs == 10'000 &&
              p.durationMs == 40'000 && p.songStartMs == 10'000 && !p.wholeSong,
          "plan: a scene inside the song");
    check(plan({scene(0, 100'000)}, {song(20'000, 80'000, 5'000, 180'000)}, &p) && p.programmeStartMs == 20'000 &&
              p.durationMs == 60'000 && p.songStartMs == 5'000 && !p.wholeSong,
          "plan: a song with sourceInMs > 0 starts there and is a range");
    check(plan({scene(30'000, 100'000)}, {song(20'000, 80'000, 5'000, 180'000)}, &p) && p.songStartMs == 15'000 &&
              p.durationMs == 50'000,
          "plan: source in plus the cut into the song");
    check(plan({scene(0, 10'000)}, {song(0, 10'000, -4'000, 0)}, &p) && p.songStartMs == 0,
          "plan: song start floors at 0");

    // Nothing to render.
    check(!plan({}, {song(0, 1'000)}, &p), "plan: no visual");
    check(!plan({scene(0, 1'000)}, {}, &p), "plan: no song");
    check(!plan({scene(0, 1'000)}, {song(1'000, 2'000)}, &p), "plan: touching is no overlap");
    p = RenderPlan{};
    p.visual = 7;
    check(!plan({scene(0, 1'032)}, {song(1'000, 2'000)}, &p) && p.visual == 7, "plan: 32 ms is under the floor");
    check(plan({scene(0, 1'033)}, {song(1'000, 2'000)}, &p) && p.durationMs == 33, "plan: exactly the floor");

    // The earliest clip wins, on each side, whatever order the clips arrive in.
    check(plan({scene(50'000, 60'000), scene(10'000, 20'000)}, {song(0, 100'000)}, &p) && p.visual == 1 &&
              p.programmeStartMs == 10'000,
          "plan: the earliest scene");
    check(plan({scene(0, 100'000)}, {song(60'000, 90'000), song(30'000, 50'000)}, &p) && p.song == 1 &&
              p.programmeStartMs == 30'000,
          "plan: the earliest overlapping song");
    check(plan({scene(10'000, 20'000), scene(10'000, 30'000)}, {song(0, 100'000)}, &p) && p.visual == 0,
          "plan: a tie keeps the first");
    check(plan({scene(40'000, 60'000)}, {song(0, 30'000), song(50'000, 90'000)}, &p) && p.song == 1,
          "plan: an earlier song that misses the scene is passed over");

    // Lanes: the first live lane holding what it needs, by lane index; muted and hidden lanes skipped.
    check(plan({scene(10'000, 20'000, 2), scene(50'000, 60'000, 0)}, {song(0, 100'000)}, &p) && p.visual == 1,
          "plan: the first visual lane, not the earliest clip");
    check(plan({scene(10'000, 20'000, 0, PlanClip::kEnabled | PlanClip::kWanted), scene(50'000, 60'000, 2)},
               {song(0, 100'000)}, &p) &&
              p.visual == 1,
          "plan: a hidden visual lane is skipped");
    check(plan({scene(0, 100'000)},
               {song(0, 100'000, 0, 0, 1, PlanClip::kEnabled | PlanClip::kWanted), song(40'000, 90'000, 0, 0, 3)}, &p) &&
              p.song == 1,
          "plan: a muted audio lane is skipped");
    check(plan({scene(0, 1'000, 0, PlanClip::kLaneLive | PlanClip::kWanted), scene(50'000, 60'000, 1)},
               {song(0, 100'000)}, &p) &&
              p.visual == 1,
          "plan: a visual lane with only disabled scenes is skipped");
    check(plan({scene(0, 1'000, 0, PlanClip::kLaneLive | PlanClip::kEnabled), scene(50'000, 60'000, 1)},
               {song(0, 100'000)}, &p) &&
              p.visual == 1,
          "plan: a visual lane with no scene is skipped");
    check(!plan({scene(0, 100'000)},
                {song(0, 100'000, 0, 0, 1, PlanClip::kLaneLive | PlanClip::kEnabled), song(0, 100'000, 0, 0, 3)}, &p),
          "plan: the first live audio lane holding any enabled clip is the only one tried");
    check(plan({scene(0, 100'000)},
               {song(0, 100'000, 0, 0, 1, PlanClip::kLaneLive | PlanClip::kWanted), song(40'000, 90'000, 0, 0, 3)}, &p) &&
              p.song == 1,
          "plan: an audio lane with only disabled clips is skipped");
    check(plan({scene(0, 100'000)},
               {song(0, 100'000, 0, 0, 1, PlanClip::kLaneLive | PlanClip::kWanted), song(40'000, 90'000)}, &p) &&
              p.song == 1,
          "plan: a disabled song is skipped on the same lane");
    check(plan({scene(0, 1'000, 0, PlanClip::kLaneLive | PlanClip::kWanted), scene(50'000, 60'000)}, {song(0, 100'000)},
               &p) &&
              p.visual == 1,
          "plan: a disabled scene is skipped on the same lane");

    // The ABI: flat spans and lanes, the same answer.
    const int64_t vSpans[] = {50'000, 60'000, 0, 0, 10'000, 30'000, 0, 0};
    const int32_t vLanes[] = {0, GEODE_PLAN_ENABLED | GEODE_PLAN_WANTED | GEODE_PLAN_LANE_LIVE, 0,
                              GEODE_PLAN_ENABLED | GEODE_PLAN_WANTED | GEODE_PLAN_LANE_LIVE};
    const int64_t aSpans[] = {5'000, 100'000, 2'000, 180'000};
    const int32_t aLanes[] = {1, GEODE_PLAN_ENABLED | GEODE_PLAN_WANTED | GEODE_PLAN_LANE_LIVE};
    geode_render_plan abi{};
    check(geode_render_plan_of(vSpans, vLanes, 2, aSpans, aLanes, 1, kMin, &abi) == 1 && abi.visual == 1 &&
              abi.song == 0 && abi.programme_start_ms == 10'000 && abi.duration_ms == 20'000 &&
              abi.song_start_ms == 7'000 && abi.whole_song == 0,
          "plan: ABI");
    check(geode_render_plan_of(vSpans, vLanes, 2, aSpans, aLanes, 1, 20'001, &abi) == 0 && abi.visual == 1,
          "plan: ABI floor is the caller's, out untouched");
    check(geode_render_plan_of(nullptr, nullptr, 0, aSpans, aLanes, 1, kMin, &abi) == 0, "plan: ABI no visuals");
    const int64_t wSpans[] = {0, 180'000, 0, 180'000};
    check(geode_render_plan_of(wSpans, vLanes, 1, wSpans, aLanes, 1, kMin, &abi) == 1 && abi.whole_song == 1,
          "plan: ABI whole song");
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

}  // namespace

int main() {
    frameClock();
    loopSpec();
    loopExtend();
    encodePlan();
    chapters();
    exportRange();
    renderEta();
    renderPlan();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("output_test: all passed\n");
    return 0;
}
