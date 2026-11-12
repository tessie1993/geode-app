#include "viz/Underlay.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace geode::viz::underlay {

namespace {

// The old path's cap on the shrink; 1 + kBlurMax stays under it, so it only guards the maths.
constexpr int kMaxDivisor = 32;

float clamp01(float v) {
    if (!(v > 0.0f)) return 0.0f;  // NaN and negatives
    return v < 1.0f ? v : 1.0f;
}

constexpr const char* kBlendNames[kBlendCount] = {"SCREEN", "MULTIPLY", "ADD"};

uint32_t channel(uint32_t p, int shift) { return (p >> shift) & 0xFFu; }

// Cell edge `k` of `cells` cells over `length` pixels: k * length / cells, in 64 bits so it cannot wrap.
int edge(int k, int length, int cells) {
    return static_cast<int>(static_cast<int64_t>(k) * static_cast<int64_t>(length) / static_cast<int64_t>(cells));
}

}  // namespace

float clampAmount(float amount) { return clamp01(amount); }

float clampDim(float dim) { return clamp01(dim); }

int clampBlur(int radius) { return std::clamp(radius, kBlurMin, kBlurMax); }

Rect cropRect(int srcW, int srcH, int targetW, int targetH) {
    const Rect whole{0, 0, srcW, srcH};
    if (srcW <= 0 || srcH <= 0 || targetW <= 0 || targetH <= 0) return whole;
    // Float maths and truncation as the Kotlin did (Int * Float, then toInt()), so the cut lands on the same pixel.
    const float targetAspect = static_cast<float>(targetW) / static_cast<float>(targetH);
    const float srcAspect = static_cast<float>(srcW) / static_cast<float>(srcH);
    if (srcAspect > targetAspect) {
        const int cropW = std::clamp(static_cast<int>(static_cast<float>(srcH) * targetAspect), 1, srcW);
        return Rect{(srcW - cropW) / 2, 0, cropW, srcH};
    }
    if (srcAspect < targetAspect) {
        const int cropH = std::clamp(static_cast<int>(static_cast<float>(srcW) / targetAspect), 1, srcH);
        return Rect{0, (srcH - cropH) / 2, srcW, cropH};
    }
    return whole;
}

void blur(uint32_t* pixels, int width, int height, int radius) {
    if (!pixels || width <= 0 || height <= 0) return;
    const int divisor = std::clamp(1 + clampBlur(radius), 1, kMaxDivisor);
    if (divisor <= 1) return;
    const int smallW = std::max(1, width / divisor);
    const int smallH = std::max(1, height / divisor);
    const auto stride = static_cast<size_t>(width);
    const auto smallStride = static_cast<size_t>(smallW);

    // Shrink: the cells tile the image (edges k * size / cells), so no pixel is left out or counted twice.
    std::vector<uint32_t> small(smallStride * static_cast<size_t>(smallH));
    for (int j = 0; j < smallH; ++j) {
        const int y0 = edge(j, height, smallH);
        const int y1 = edge(j + 1, height, smallH);
        for (int i = 0; i < smallW; ++i) {
            const int x0 = edge(i, width, smallW);
            const int x1 = edge(i + 1, width, smallW);
            uint64_t sum[4] = {0, 0, 0, 0};
            for (int y = y0; y < y1; ++y) {
                const uint32_t* row = pixels + static_cast<size_t>(y) * stride;
                for (int x = x0; x < x1; ++x) {
                    const uint32_t p = row[x];
                    for (int c = 0; c < 4; ++c) sum[c] += channel(p, c * 8);
                }
            }
            const auto n = static_cast<uint64_t>(x1 - x0) * static_cast<uint64_t>(y1 - y0);
            uint32_t mean = 0;
            // Rounded to nearest, so a flat cell keeps its exact value.
            for (int c = 0; c < 4; ++c) mean |= static_cast<uint32_t>((sum[c] + n / 2) / n) << (c * 8);
            small[static_cast<size_t>(j) * smallStride + static_cast<size_t>(i)] = mean;
        }
    }

    // Back up: bilinear at pixel centres, edges clamped, as the platform's filtered scale samples.
    const float sx = static_cast<float>(smallW) / static_cast<float>(width);
    const float sy = static_cast<float>(smallH) / static_cast<float>(height);
    const auto maxX = static_cast<float>(smallW - 1);
    const auto maxY = static_cast<float>(smallH - 1);
    for (int y = 0; y < height; ++y) {
        const float fy = std::clamp((static_cast<float>(y) + 0.5f) * sy - 0.5f, 0.0f, maxY);
        const int y0 = static_cast<int>(fy);
        const int y1 = std::min(y0 + 1, smallH - 1);
        const float ty = fy - static_cast<float>(y0);
        const uint32_t* top = small.data() + static_cast<size_t>(y0) * smallStride;
        const uint32_t* bottom = small.data() + static_cast<size_t>(y1) * smallStride;
        uint32_t* out = pixels + static_cast<size_t>(y) * stride;
        for (int x = 0; x < width; ++x) {
            const float fx = std::clamp((static_cast<float>(x) + 0.5f) * sx - 0.5f, 0.0f, maxX);
            const int x0 = static_cast<int>(fx);
            const int x1 = std::min(x0 + 1, smallW - 1);
            const float tx = fx - static_cast<float>(x0);
            uint32_t result = 0;
            for (int c = 0; c < 4; ++c) {
                const int shift = c * 8;
                const float upper = static_cast<float>(channel(top[x0], shift)) * (1.0f - tx) +
                                    static_cast<float>(channel(top[x1], shift)) * tx;
                const float lower = static_cast<float>(channel(bottom[x0], shift)) * (1.0f - tx) +
                                    static_cast<float>(channel(bottom[x1], shift)) * tx;
                const long v = std::lround(upper * (1.0f - ty) + lower * ty);
                result |= static_cast<uint32_t>(std::clamp(v, 0L, 255L)) << shift;
            }
            out[x] = result;
        }
    }
}

void dim(uint32_t* pixels, int count, float dimAmount) {
    if (!pixels || count <= 0) return;
    const float keep = 1.0f - clampDim(dimAmount);
    const auto scaled = [keep](uint32_t c) {
        return static_cast<uint32_t>(static_cast<int>(static_cast<float>(c) * keep));
    };
    for (int i = 0; i < count; ++i) {
        const uint32_t p = pixels[i];
        pixels[i] = (p & 0xFF000000u) | (scaled(channel(p, 16)) << 16) | (scaled(channel(p, 8)) << 8) |
                    scaled(channel(p, 0));
    }
}

void finish(uint32_t* pixels, int width, int height, int blurRadius, float dimAmount) {
    if (!pixels || width <= 0 || height <= 0) return;
    if (clampBlur(blurRadius) > 0) blur(pixels, width, height, blurRadius);
    if (clampDim(dimAmount) > 0.0f) dim(pixels, width * height, dimAmount);
}

void toRgbaBytes(const uint32_t* argb, size_t count, uint8_t* out) {
    if (!argb || !out) return;
    for (size_t i = 0; i < count; i++) {
        const uint32_t px = argb[i];
        uint8_t* dst = out + i * 4;
        dst[0] = static_cast<uint8_t>((px >> 16) & 0xFFu);  // R
        dst[1] = static_cast<uint8_t>((px >> 8) & 0xFFu);   // G
        dst[2] = static_cast<uint8_t>(px & 0xFFu);          // B
        dst[3] = static_cast<uint8_t>((px >> 24) & 0xFFu);  // A
    }
}

const char* blendName(int blend) { return kBlendNames[blend >= 0 && blend < kBlendCount ? blend : kBlendScreen]; }

int blendOf(const char* name) {
    if (!name) return kBlendScreen;
    for (int i = 0; i < kBlendCount; ++i) {
        if (std::strcmp(name, kBlendNames[i]) == 0) return i;
    }
    return kBlendScreen;
}

}  // namespace geode::viz::underlay
