// Host-side checks for the export geometry (output/ExportGeometry, #606): the picture size for every quality
// and ratio, odd sizes and the 4096 cap, the fps clamp, and integer-exact frame counts for long tracks. Built
// and run by CI (see .github/workflows/android.yml, "Native export geometry tests") with the system compiler.
// The expected sizes are what Kotlin's ExportAspect.of gave before the port.
#include <climits>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include "api/geode_api.h"
#include "output/ExportGeometry.hpp"

using namespace geode::output;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void checkSize(int shortSide, int w, int h, int wantW, int wantH, const char* what) {
    const ExportSize s = exportSize(shortSide, w, h);
    if (s.width != wantW || s.height != wantH) {
        std::printf("FAIL: %s: %d @ %d:%d gave %dx%d, want %dx%d\n", what, shortSide, w, h, s.width, s.height, wantW,
                    wantH);
        ++failures;
    }
    // The ABI gives the same answer.
    int aw = -1;
    int ah = -1;
    const int ok = geode_export_size(shortSide, w, h, &aw, &ah);
    check(ok == (wantW > 0 ? 1 : 0) && aw == wantW && ah == wantH, what);
}

struct Ratio {
    int w;
    int h;
};
// ExportRatio's entries: 16:9, 9:16, 1:1, 4:5, 4:3, 21:9.
constexpr Ratio kRatios[] = {{16, 9}, {9, 16}, {1, 1}, {4, 5}, {4, 3}, {21, 9}};
// ExportQuality's short sides: 720p, 1080p, 4K.
constexpr int kShortSides[] = {720, 1080, 2160};

void everyAspect() {
    // 720p
    checkSize(720, 16, 9, 1280, 720, "720p 16:9");
    checkSize(720, 9, 16, 720, 1280, "720p 9:16");
    checkSize(720, 1, 1, 720, 720, "720p 1:1");
    checkSize(720, 4, 5, 720, 900, "720p 4:5");
    checkSize(720, 4, 3, 960, 720, "720p 4:3");
    checkSize(720, 21, 9, 1680, 720, "720p 21:9");
    // 1080p
    checkSize(1080, 16, 9, 1920, 1080, "1080p 16:9");
    checkSize(1080, 9, 16, 1080, 1920, "1080p 9:16");
    checkSize(1080, 1, 1, 1080, 1080, "1080p 1:1");
    checkSize(1080, 4, 5, 1080, 1350, "1080p 4:5");
    checkSize(1080, 4, 3, 1440, 1080, "1080p 4:3");
    checkSize(1080, 21, 9, 2520, 1080, "1080p 21:9");
    // 4K: 21:9 is the one aspect past the cap (5040 long), so its short side scales to 1755, rounded up to 1756.
    checkSize(2160, 16, 9, 3840, 2160, "4K 16:9");
    checkSize(2160, 9, 16, 2160, 3840, "4K 9:16");
    checkSize(2160, 1, 1, 2160, 2160, "4K 1:1");
    checkSize(2160, 4, 5, 2160, 2700, "4K 4:5");
    checkSize(2160, 4, 3, 2880, 2160, "4K 4:3");
    checkSize(2160, 21, 9, 4096, 1756, "4K 21:9 (capped)");

    // Whatever the pair, both sides are even, neither passes the cap, and landscape stays landscape.
    for (int s : kShortSides) {
        for (const Ratio& r : kRatios) {
            const ExportSize e = exportSize(s, r.w, r.h);
            check(e.width % 2 == 0 && e.height % 2 == 0, "even sides");
            check(e.width <= kMaxExportSide && e.height <= kMaxExportSide, "within the cap");
            check((r.w >= r.h) == (e.width >= e.height), "orientation kept");
        }
    }
}

void oddSizesAndCap() {
    // Odd results round up, never down: 721 * 16 / 9 = 1281 (truncated) -> 1282, and 721 -> 722.
    checkSize(721, 16, 9, 1282, 722, "odd 721 16:9");
    checkSize(721, 9, 16, 722, 1282, "odd 721 9:16");
    checkSize(1081, 1, 1, 1082, 1082, "odd 1081 1:1");
    checkSize(719, 4, 5, 720, 898, "odd 719 4:5");   // 719 * 5 / 4 = 898 (truncated), already even
    checkSize(1, 1, 1, 2, 2, "odd 1 1:1");
    // The cap in portrait: the long side is the height.
    checkSize(2160, 9, 21, 1756, 4096, "4K 9:21 (capped, portrait)");
    // A short side past the cap on a square: 4097 -> long 4097 -> capped, short 4097 * 4096 / 4097 = 4096.
    checkSize(4096, 1, 1, 4096, 4096, "exactly the cap");
    checkSize(4097, 1, 1, 4096, 4096, "one past the cap");
    // 3000 @ 16:9: long 5333, short 3000 * 4096 / 5333 = 2304 (truncated).
    checkSize(3000, 16, 9, 4096, 2304, "3000 16:9 (capped)");
    // A cap that leaves an odd short side: 4K at 21:9 already covers it (1755 -> 1756).
    // Bad input gives nothing.
    checkSize(0, 16, 9, 0, 0, "zero short side");
    checkSize(-720, 16, 9, 0, 0, "negative short side");
    checkSize(720, 0, 9, 0, 0, "zero ratio term");
    checkSize(720, 16, -9, 0, 0, "negative ratio term");
    check(geode_export_size(720, 16, 9, nullptr, nullptr) == 1, "null outs are allowed");
}

void fpsClamp() {
    const int cases[][2] = {{-5, 24}, {0, 24}, {23, 24}, {24, 24}, {25, 25}, {30, 30},
                            {50, 50}, {60, 60}, {61, 60}, {120, 60}, {INT_MAX, 60}};
    for (const auto& c : cases) {
        check(exportFps(c[0]) == c[1], "fps clamp");
        check(geode_export_fps(c[0]) == c[1], "fps clamp (ABI)");
    }
}

void frameDurations() {
    check(exportFrameDurationNs(60) == 16'666'666, "60 fps frame");
    check(exportFrameDurationNs(30) == 33'333'333, "30 fps frame");
    check(exportFrameDurationNs(25) == 40'000'000, "25 fps frame");
    check(exportFrameDurationNs(24) == 41'666'666, "24 fps frame");
    check(exportFrameDurationNs(50) == 20'000'000, "50 fps frame");
    check(exportFrameDurationNs(0) == 0 && exportFrameDurationNs(-1) == 0, "no frame at fps <= 0");
    check(geode_export_frame_duration_ns(60) == 16'666'666, "60 fps frame (ABI)");
}

void frameCounts() {
    constexpr int64_t kSecond = 1'000'000;
    constexpr int64_t kHour = 3600 * kSecond;
    // Short and empty.
    check(exportTotalFrames(0, 60) == 1, "empty track renders one frame");
    check(exportTotalFrames(-kSecond, 60) == 1, "negative duration renders one frame");
    check(exportTotalFrames(1, 60) == 1, "1 us renders one frame");
    check(exportTotalFrames(kSecond, 0) == 1, "fps 0 renders one frame");
    check(exportTotalFrames(kSecond, 60) == 60, "one second at 60");
    check(exportTotalFrames(16'666, 60) == 1 && exportTotalFrames(33'333, 60) == 1 && exportTotalFrames(33'334, 60) == 2,
          "frame boundaries at 60");
    // A pop song's odd length at 24: 213.333333 s * 24 = 5119.99999 -> 5119 (floor, never rounded up).
    check(exportTotalFrames(213'333'333, 24) == 5119, "3:33.333333 at 24");
    // Long tracks, exact to the frame.
    check(exportTotalFrames(kHour, 60) == 216'000, "one hour at 60");
    check(exportTotalFrames(10 * kHour, 60) == 2'160'000, "ten hours at 60");
    check(exportTotalFrames(10 * kHour + 999'999, 60) == 2'160'059, "ten hours and 0.999999 s at 60");
    check(exportTotalFrames(24 * kHour - 1, 30) == 2'591'999, "a day less 1 us at 30");
    check(exportTotalFrames(100 * kHour, 25) == 9'000'000, "a hundred hours at 25");
    // Against the plain formula over a sweep of long durations at every rate the clamp allows.
    for (int fps = kMinExportFps; fps <= kMaxExportFps; ++fps) {
        for (int64_t d = 0; d <= 48 * kHour; d += 7'919'993) {   // prime step: lands off every boundary
            const int64_t want = d * fps / kSecond;
            if (exportTotalFrames(d, fps) != (want < 1 ? 1 : static_cast<int>(want))) {
                std::printf("FAIL: frames at %lld us %d fps\n", static_cast<long long>(d), fps);
                ++failures;
            }
        }
    }
    // The ceiling: no wrap past INT_MAX.
    check(exportTotalFrames(INT64_MAX, 60) == INT_MAX, "INT64_MAX duration saturates");
    check(exportTotalFrames(static_cast<int64_t>(INT_MAX) * kSecond / 60 + 2 * kSecond, 60) == INT_MAX,
          "just past INT_MAX frames saturates");
    check(exportTotalFrames(static_cast<int64_t>(INT_MAX / 60) * kSecond, 60) == (INT_MAX / 60) * 60,
          "just under INT_MAX frames is exact");
    check(geode_export_total_frames(kHour, 60) == 216'000, "one hour at 60 (ABI)");
    // The frames, laid end to end at the truncated frame duration, never run past the track.
    for (int fps : {24, 25, 30, 50, 60}) {
        const int frames = exportTotalFrames(10 * kHour, fps);
        check(static_cast<int64_t>(frames) * exportFrameDurationNs(fps) <= 10 * kHour * 1000, "frames fit the track");
    }
}

}  // namespace

int main() {
    everyAspect();
    oddSizesAndCap();
    fpsClamp();
    frameDurations();
    frameCounts();
    if (failures) {
        std::printf("%d export geometry check(s) failed\n", failures);
        return 1;
    }
    std::printf("export geometry tests passed\n");
    return 0;
}
