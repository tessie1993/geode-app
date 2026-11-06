#pragma once

// The pure maths of the export post-passes (ExportPasses.hpp): no GL, so core/tests/export_passes_test.cpp
// pins it on the host. Each function is a line-for-line port of the Kotlin it replaced
// (export/WatermarkOverlay.kt, export/GlTransitionEffect.kt, LoopRender's seam), float for float, because
// the acceptance for #442 is the same frames before and after: a rounding that moved one tile edge or one
// dissolve weight would show up in a pixel diff of a fixed export.

#include <algorithm>
#include <cstdint>

namespace geode::viz::frame::exportmath {

namespace watermark {

// The wordmark's height as a share of the frame's short side, so the pattern has the same density on a
// 9:16 short and a 21:9 master; never under kMinTextPx, where the wordmark would stop reading as text.
constexpr float kTextFraction = 0.045f;
constexpr float kMinTextPx = 14.0f;
// The tile is the rotated text plus this much gap, and never smaller than kMinTilePx on a side.
constexpr float kTileSpacing = 1.35f;
constexpr int kMinTilePx = 64;
// A power of two above this would not fit an int; no frame comes near it.
constexpr int kMaxTilePx = 1 << 14;

inline float textPx(int shortSide) { return std::max(static_cast<float>(shortSide) * kTextFraction, kMinTextPx); }

// Kotlin's `Integer.highestOneBit((value - 1).coerceAtLeast(1)) shl 1`: the smallest power of two at or
// above value, and 2 for anything under 2. Power-of-two tiles make GL_REPEAT legal on any GLES version.
inline int nextPowerOfTwo(int value) {
    int v = std::max(value - 1, 1);
    int high = 1;
    while (v > 1) {
        v >>= 1;
        high <<= 1;
    }
    return std::min(high << 1, kMaxTilePx);
}

// Kotlin's Float.toInt(): truncation toward zero, NaN to 0, saturating at the int range.
inline int truncate(float value) {
    if (!(value == value)) return 0;
    if (value >= 2147483647.0f) return 2147483647;
    if (value <= -2147483648.0f) return -2147483647 - 1;
    return static_cast<int>(value);
}

// The tile's width from the measured width of the text (Paint.measureText at textPx).
// Capped at 2^30 first: past that the next power of two overflows int (an absurd width, but never a
// negative or zero tile).
inline int tileWidth(float textWidth) {
    constexpr int kMaxTilePx = 1 << 30;
    return nextPowerOfTwo(std::clamp(truncate(textWidth * kTileSpacing), kMinTilePx, kMaxTilePx));
}

// Half the width, rounded up to a power of two: the diagonal wordmark needs less height than width.
inline int tileHeight(int tileWidth) { return nextPowerOfTwo(tileWidth / 2); }

// How many tiles span the frame on each axis: the full-screen quad's texture coordinates at its far corner.
struct Tiles {
    float u = 0.0f;
    float v = 0.0f;
};

inline Tiles tiles(int frameWidth, int frameHeight, int tileWidth, int tileHeight) {
    if (tileWidth <= 0 || tileHeight <= 0) return {};
    return {static_cast<float>(frameWidth) / static_cast<float>(tileWidth),
            static_cast<float>(frameHeight) / static_cast<float>(tileHeight)};
}

}  // namespace watermark

namespace seam {

// The dissolve's weight on the stashed opening frame at closing frame `tail` of `seamFrames`. It reaches
// the ends exactly (no weight on the first closing frame, all of it on the last) so the frames either side
// of the wrap are untouched renders rather than mixtures, and the seam has nothing left to give itself away
// with. Clamped to 0..1 as the blend uniform always was.
inline float weight(int tail, int seamFrames) {
    const float w = seamFrames > 1 ? static_cast<float>(tail) / (static_cast<float>(seamFrames) - 1.0f) : 1.0f;
    return std::clamp(w, 0.0f, 1.0f);
}

}  // namespace seam

namespace transition {

// How far into the transition a frame at ptsUs is, the clip's first frame (originUs) being 0. No duration
// means the transition is already over. Long-to-float then float division, as the Kotlin did.
inline float progress(int64_t ptsUs, int64_t originUs, int64_t durationUs) {
    if (durationUs <= 0) return 1.0f;
    const float p = static_cast<float>(ptsUs - originUs) / static_cast<float>(durationUs);
    return std::clamp(p, 0.0f, 1.0f);
}

// GL Transitions' `ratio` uniform: width over height, a zero height read as one pixel.
inline float ratio(int width, int height) { return static_cast<float>(width) / static_cast<float>(std::max(height, 1)); }

// The frame is drawn plain (the copy program) without an outgoing frame, and from the end on.
inline bool passthrough(bool hasFrom, float progress) { return !hasFrom || progress >= 1.0f; }

}  // namespace transition

}  // namespace geode::viz::frame::exportmath
