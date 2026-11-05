#include "viz/UnderlayPrep.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace geode::viz::underlay {

namespace {

// One source index and its weight, for one output index along one axis.
struct Tap {
    int index;
    float weight;
};

// The taps of every output index along one axis: output i reads taps[offsets[i] .. offsets[i + 1]).
struct Axis {
    std::vector<Tap> taps;
    std::vector<int> offsets;
};

// Shrinking: each output pixel is the area average of the source span it covers (a true box filter, so
// a 32x blur shrink averages its whole neighbourhood instead of aliasing on four samples). Growing:
// bilinear between the two nearest source centres, clamped at the edges.
Axis buildAxis(int srcN, int dstN) {
    Axis axis;
    axis.offsets.reserve(static_cast<size_t>(dstN) + 1);
    axis.offsets.push_back(0);
    const double scale = static_cast<double>(srcN) / static_cast<double>(dstN);
    for (int i = 0; i < dstN; ++i) {
        if (srcN > dstN) {
            const double lo = i * scale;
            const double hi = (i + 1) * scale;
            const int first = std::max(0, static_cast<int>(std::floor(lo)));
            const int last = std::min(srcN - 1, static_cast<int>(std::ceil(hi)) - 1);
            for (int j = first; j <= last; ++j) {
                const double overlap = std::min(hi, j + 1.0) - std::max(lo, static_cast<double>(j));
                if (overlap > 0.0) axis.taps.push_back({j, static_cast<float>(overlap / scale)});
            }
        } else if (srcN == dstN) {
            axis.taps.push_back({i, 1.0f});
        } else {
            const double centre = std::clamp((i + 0.5) * scale - 0.5, 0.0, static_cast<double>(srcN - 1));
            const int j0 = static_cast<int>(std::floor(centre));
            const int j1 = std::min(j0 + 1, srcN - 1);
            const auto f = static_cast<float>(centre - j0);
            axis.taps.push_back({j0, 1.0f - f});
            if (j1 != j0 && f > 0.0f) axis.taps.push_back({j1, f});
        }
        axis.offsets.push_back(static_cast<int>(axis.taps.size()));
    }
    return axis;
}

uint32_t channel(float v) {
    return static_cast<uint32_t>(std::clamp(std::lround(v), 0L, 255L));
}

}  // namespace

int sampleSize(int srcW, int srcH, int targetW, int targetH) {
    if (srcW <= 0 || srcH <= 0 || targetW <= 0 || targetH <= 0) return 1;
    int64_t sample = 1;
    while (srcW / (sample * 2) >= targetW && srcH / (sample * 2) >= targetH) sample *= 2;
    return static_cast<int>(sample);
}

Rect centreCrop(int w, int h, int targetW, int targetH) {
    if (w <= 0 || h <= 0 || targetW <= 0 || targetH <= 0) return {0, 0, std::max(w, 0), std::max(h, 0)};
    // Float maths on purpose: the same ratios, truncation and rounding the Kotlin crop used, so a
    // background lands on exactly the pixels it did before the port.
    const float targetAspect = static_cast<float>(targetW) / static_cast<float>(targetH);
    const float srcAspect = static_cast<float>(w) / static_cast<float>(h);
    if (srcAspect > targetAspect) {
        const int cropW = std::clamp(static_cast<int>(static_cast<float>(h) * targetAspect), 1, w);
        return {(w - cropW) / 2, 0, cropW, h};
    }
    if (srcAspect < targetAspect) {
        const int cropH = std::clamp(static_cast<int>(static_cast<float>(w) / targetAspect), 1, h);
        return {0, (h - cropH) / 2, w, cropH};
    }
    return {0, 0, w, h};
}

int blurDivisor(int blurRadius) {
    if (blurRadius <= 0) return 1;
    return std::min(blurRadius, kMaxBlurDivisor - 1) + 1;
}

void resize(const uint32_t* src, int srcStride, int srcW, int srcH, uint32_t* dst, int dstW, int dstH) {
    if (!src || !dst || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;
    if (srcW == dstW && srcH == dstH) {
        for (int y = 0; y < dstH; ++y) {
            std::memcpy(dst + static_cast<size_t>(y) * dstW, src + static_cast<size_t>(y) * srcStride,
                        static_cast<size_t>(dstW) * sizeof(uint32_t));
        }
        return;
    }
    const Axis xs = buildAxis(srcW, dstW);
    const Axis ys = buildAxis(srcH, dstH);
    // One output row at a time: the vertical taps sum source rows (premultiplied) into one row of
    // floats, then the horizontal taps read it. Memory is one source row, not a whole intermediate
    // image, which matters at 4K export sizes.
    std::vector<float> acc(static_cast<size_t>(srcW) * 4);
    for (int oy = 0; oy < dstH; ++oy) {
        std::fill(acc.begin(), acc.end(), 0.0f);
        for (int t = ys.offsets[oy]; t < ys.offsets[oy + 1]; ++t) {
            const Tap tap = ys.taps[static_cast<size_t>(t)];
            const uint32_t* row = src + static_cast<size_t>(tap.index) * srcStride;
            float* a = acc.data();
            for (int x = 0; x < srcW; ++x, a += 4) {
                const uint32_t p = row[x];
                const auto alpha = static_cast<float>(p >> 24);
                const float k = tap.weight * alpha / 255.0f;
                a[0] += k * static_cast<float>((p >> 16) & 0xFFu);
                a[1] += k * static_cast<float>((p >> 8) & 0xFFu);
                a[2] += k * static_cast<float>(p & 0xFFu);
                a[3] += tap.weight * alpha;
            }
        }
        uint32_t* out = dst + static_cast<size_t>(oy) * dstW;
        for (int ox = 0; ox < dstW; ++ox) {
            float r = 0.0f, g = 0.0f, b = 0.0f, alpha = 0.0f;
            for (int t = xs.offsets[ox]; t < xs.offsets[ox + 1]; ++t) {
                const Tap tap = xs.taps[static_cast<size_t>(t)];
                const float* a = acc.data() + static_cast<size_t>(tap.index) * 4;
                r += tap.weight * a[0];
                g += tap.weight * a[1];
                b += tap.weight * a[2];
                alpha += tap.weight * a[3];
            }
            // Back to non-premultiplied, as getPixels hands it and the underlay upload expects.
            const float un = alpha > 0.0f ? 255.0f / alpha : 0.0f;
            out[ox] = (channel(alpha) << 24) | (channel(r * un) << 16) | (channel(g * un) << 8) | channel(b * un);
        }
    }
}

void applyDim(uint32_t* pixels, int64_t count, float dim) {
    if (!pixels || count <= 0 || !(dim > 0.0f)) return;
    const float keep = 1.0f - std::min(dim, 1.0f);
    for (int64_t i = 0; i < count; ++i) {
        const uint32_t p = pixels[i];
        const auto r = static_cast<uint32_t>(static_cast<float>((p >> 16) & 0xFFu) * keep);
        const auto g = static_cast<uint32_t>(static_cast<float>((p >> 8) & 0xFFu) * keep);
        const auto b = static_cast<uint32_t>(static_cast<float>(p & 0xFFu) * keep);
        pixels[i] = (p & 0xFF000000u) | (r << 16) | (g << 8) | b;
    }
}

int prepare(const uint32_t* src, int w, int h, int targetW, int targetH, int blurRadius, float dim, uint32_t* out,
            int64_t outCapacity) {
    if (!src || !out || w <= 0 || h <= 0 || targetW <= 0 || targetH <= 0) return 0;
    const int64_t count = static_cast<int64_t>(targetW) * targetH;
    if (count > outCapacity || count > INT32_MAX) return 0;
    const Rect crop = centreCrop(w, h, targetW, targetH);
    resize(src + static_cast<size_t>(crop.y) * w + crop.x, w, crop.w, crop.h, out, targetW, targetH);
    // Cheap blur: shrink by the divisor (an area average, so every small pixel is its neighbourhood's
    // mean) and scale back up bilinearly. Not a Gaussian, but cheap enough to redo on every slider drag,
    // which matters more for a background layer than the exact kernel shape.
    const int divisor = blurDivisor(blurRadius);
    if (divisor > 1) {
        const int smallW = std::max(1, targetW / divisor);
        const int smallH = std::max(1, targetH / divisor);
        std::vector<uint32_t> small(static_cast<size_t>(smallW) * smallH);
        resize(out, targetW, targetW, targetH, small.data(), smallW, smallH);
        resize(small.data(), smallW, smallW, smallH, out, targetW, targetH);
    }
    applyDim(out, count, dim);
    return static_cast<int>(count);
}

}  // namespace geode::viz::underlay
