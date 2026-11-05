#pragma once
// Export geometry (#606): the picture size, frame rate and frame clock an export encodes at. Pure integer
// maths, no GL and no Android, so a host test pins it and every render path asks the same rule. Ported
// unchanged from Kotlin's ExportAspect.of and VideoExporter's encoder setup: a render made after the port has
// the same size and frame count as one made before it.
#include <climits>
#include <cstdint>

namespace geode::output {

/** The largest side any export encodes: H.264 level 5.1/5.2 and most hardware encoders stop at 4096. */
inline constexpr int kMaxExportSide = 4096;
/** The frame rates an export may ask the encoder for; anything outside is clamped into this range. */
inline constexpr int kMinExportFps = 24;
inline constexpr int kMaxExportFps = 60;

struct ExportSize {
    int width = 0;
    int height = 0;
};

/** v, or v + 1 when odd: 4:2:0 encoders need even sides. Rounds up, as the Kotlin did. */
constexpr int evenUp(int v) { return (v % 2 == 0) ? v : v + 1; }

/**
 * The export's width and height for a quality's short side and a w:h ratio. The long side is the short side
 * times the ratio (truncated). When it would pass kMaxExportSide it is capped there and the short side is
 * scaled down by the same factor (truncated), so the ratio holds. Each side is then rounded up to even.
 * wRatio >= hRatio is landscape (width is the long side), else portrait.
 * Bad input (a short side or either ratio term <= 0) gives {0, 0}.
 */
constexpr ExportSize exportSize(int shortSide, int wRatio, int hRatio) {
    if (shortSide <= 0 || wRatio <= 0 || hRatio <= 0) return {};
    const bool landscape = wRatio >= hRatio;
    const int64_t big = wRatio > hRatio ? wRatio : hRatio;
    const int64_t small = wRatio < hRatio ? wRatio : hRatio;
    int64_t longSide = static_cast<int64_t>(shortSide) * big / small;
    int64_t shortOut = shortSide;
    if (longSide > kMaxExportSide) {
        shortOut = static_cast<int64_t>(shortSide) * kMaxExportSide / longSide;
        longSide = kMaxExportSide;
    }
    const int w = static_cast<int>(landscape ? longSide : shortOut);
    const int h = static_cast<int>(landscape ? shortOut : longSide);
    return {evenUp(w), evenUp(h)};
}

/** The requested frame rate clamped to kMinExportFps..kMaxExportFps. */
constexpr int exportFps(int requested) {
    return requested < kMinExportFps ? kMinExportFps : (requested > kMaxExportFps ? kMaxExportFps : requested);
}

/**
 * Frames in an export of durationUs at fps: floor(durationUs * fps / 1e6), at least 1 (an empty or broken
 * track still renders one frame) and at most INT_MAX (the old Kotlin toInt() wrapped there instead, which no
 * real track reaches: INT_MAX frames at 60 fps is over 9900 hours). fps <= 0 gives 1.
 * Integer-exact: whole seconds and the remainder are counted apart, so no product can overflow int64 and
 * no floating point decides which frame a ten-hour track ends on.
 */
constexpr int exportTotalFrames(int64_t durationUs, int fps) {
    if (fps <= 0 || durationUs <= 0) return 1;
    const int64_t whole = durationUs / 1'000'000;
    const int64_t rest = durationUs % 1'000'000;
    if (whole >= INT_MAX) return INT_MAX;   // fps >= 1, so the count is past INT_MAX already
    const int64_t frames = whole * fps + rest * fps / 1'000'000;
    if (frames < 1) return 1;
    return frames > INT_MAX ? INT_MAX : static_cast<int>(frames);
}

/** One frame's duration in nanoseconds at fps, truncated (1e9 / fps); 0 for fps <= 0. */
constexpr int64_t exportFrameDurationNs(int fps) { return fps > 0 ? 1'000'000'000LL / fps : 0; }

}  // namespace geode::output
