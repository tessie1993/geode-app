#pragma once

// The pure maths of the export post-passes (ExportPasses.hpp): no GL, so core/tests/export_passes_test.cpp
// pins it on the host. Each function is a line-for-line port of the Kotlin it replaced
// (export/WatermarkOverlay.kt, LoopRender's seam), float for float, because
// the acceptance for #442 is the same frames before and after: a rounding that moved one tile edge or one
// dissolve weight would show up in a pixel diff of a fixed export. The watermark's placement (corner, end
// card) came later, with Settings › Preferences; with the tiled default every value is still the old one.

#include <algorithm>
#include <cmath>
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

// Where the mark goes (Settings › Preferences › Watermark), the GEODE_WATERMARK_* codes of geode_api.h. Tiled is
// the free tier's original diagonal pattern and every unknown code; Corner is one small mark; End card a large
// centred mark over a dimmed picture for the video's last seconds.
constexpr int kNone = 0;
constexpr int kTiled = 1;
constexpr int kCorner = 2;
constexpr int kEndCard = 3;
constexpr int kTopLeft = 0;
constexpr int kTopRight = 1;
constexpr int kBottomLeft = 2;
constexpr int kBottomRight = 3;
// What the frames are: a whole video whose end is known, a stretch of one (a Studio scene segment, layered
// later), a loop (it has no end), or live (Record: the end is when the user stops).
constexpr int kWhole = 0;
constexpr int kSegment = 1;
constexpr int kLoop = 2;
constexpr int kLive = 3;

// Whether frames are marked and where: the entitlement's `required`, or Pro's own `proAdds`. An end card needs
// the video's end, so anything but a whole video gets the corner instead.
inline int placement(bool required, bool proAdds, int requested, int use) {
    if (!required && !proAdds) return kNone;
    const int p = requested >= kTiled && requested <= kEndCard ? requested : kTiled;
    return p == kEndCard && use != kWhole ? kCorner : p;
}

// The end card's wordmark is twice the tiled one; the corner's is the tiled size.
constexpr float kEndCardTextFraction = 0.09f;
inline float textPx(int shortSide, int placement) {
    return placement == kEndCard ? std::max(static_cast<float>(shortSide) * kEndCardTextFraction, kMinTextPx * 2.0f)
                                 : textPx(shortSide);
}

// The tile's rotation in degrees: only the tiled pattern is diagonal.
inline float angleDegrees(int placement) { return placement == kCorner || placement == kEndCard ? 0.0f : -24.0f; }

// The text's alpha in the raster (0..255): the tiled pattern stays faint, being everywhere; a single mark reads
// more solidly.
inline int textAlpha(int placement) {
    if (placement == kCorner) return 150;
    if (placement == kEndCard) return 235;
    return 92;
}

// A single mark's tile hugs the text (plus room for its shadow) instead of the pattern's power-of-two repeat.
constexpr float kSinglePad = 0.4f;
constexpr float kSingleHeight = 1.8f;
inline int tileWidth(float textWidth, float textPx, int placement) {
    if (placement != kCorner && placement != kEndCard) return tileWidth(textWidth);
    return std::clamp(truncate(std::ceil(textWidth + 2.0f * textPx * kSinglePad)), 1, kMaxTilePx);
}

inline int tileHeight(int tileWidth, float textPx, int placement) {
    if (placement != kCorner && placement != kEndCard) return tileHeight(tileWidth);
    return std::clamp(truncate(std::ceil(textPx * kSingleHeight)), 1, kMaxTilePx);
}

// Where the tile lands: texture coordinates are (frame fraction, top down - origin) × tiles, so the tiled
// pattern is origin 0 and a single mark is its top-left corner in the frame with the tile drawn 1:1 and
// nothing outside it (clip).
struct Layout {
    float originU = 0.0f;
    float originV = 0.0f;
    float tilesU = 0.0f;
    float tilesV = 0.0f;
    bool clip = false;
};

// A corner mark sits this share of the short side in from both edges.
constexpr float kCornerMargin = 0.035f;

inline Layout layout(int placement, int corner, int frameWidth, int frameHeight, int tileWidth, int tileHeight) {
    const Tiles t = tiles(frameWidth, frameHeight, tileWidth, tileHeight);
    if (placement != kCorner && placement != kEndCard) return {0.0f, 0.0f, t.u, t.v, false};
    if (frameWidth <= 0 || frameHeight <= 0 || tileWidth <= 0 || tileHeight <= 0) return {};
    const float fw = static_cast<float>(frameWidth);
    const float fh = static_cast<float>(frameHeight);
    const float spareX = fw - static_cast<float>(tileWidth);
    const float spareY = fh - static_cast<float>(tileHeight);
    float x = spareX * 0.5f;
    float y = spareY * 0.5f;
    if (placement == kCorner) {
        const int c = corner >= kTopLeft && corner <= kBottomRight ? corner : kBottomRight;
        const float margin = static_cast<float>(std::min(frameWidth, frameHeight)) * kCornerMargin;
        x = c == kTopRight || c == kBottomRight ? spareX - margin : margin;
        y = c == kBottomLeft || c == kBottomRight ? spareY - margin : margin;
    }
    return {x / fw, y / fh, t.u, t.v, true};
}

// The end card: the last kEndCardMs of the video (half of a shorter one), fading in over kEndCardFadeMs, over
// the picture dimmed by kEndCardDim. An unknown length, or a video under a second, shows it throughout, so a
// mark is never lost.
constexpr int64_t kEndCardMs = 3000;
constexpr int64_t kEndCardFadeMs = 500;
constexpr float kEndCardDim = 0.55f;

inline float endCardAlpha(int64_t timeMs, int64_t lengthMs) {
    if (lengthMs < 2 * kEndCardFadeMs) return 1.0f;
    const int64_t start = lengthMs - std::min(kEndCardMs, lengthMs / 2);
    return std::clamp(static_cast<float>(timeMs - start) / static_cast<float>(kEndCardFadeMs), 0.0f, 1.0f);
}

// The whole mark's opacity at a frame, and how much it darkens the picture under it (before that opacity).
inline float opacity(int placement, int64_t timeMs, int64_t lengthMs) {
    return placement == kEndCard ? endCardAlpha(timeMs, lengthMs) : 1.0f;
}

inline float dim(int placement) { return placement == kEndCard ? kEndCardDim : 0.0f; }

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

}  // namespace geode::viz::frame::exportmath
