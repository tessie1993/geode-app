#include "viz/world/WorldAssets.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace geode::viz::world {

namespace {

// Set once, never freed: static data for the life of the process, so a reader on any thread can
// hold the pointer without a lock.
std::atomic<const std::vector<uint8_t>*> gShapes{nullptr};

float texel(const std::vector<uint8_t>& v, int slab, int x, int y, int z) {
    const std::size_t i = ((static_cast<std::size_t>(slab) * kShapeRes + static_cast<std::size_t>(z)) * kShapeRes +
                           static_cast<std::size_t>(y)) * kShapeRes + static_cast<std::size_t>(x);
    return static_cast<float>(v[i]) / 255.0f;
}

}  // namespace

bool setShapeVolumes(const std::vector<uint8_t>& bytes) {
    if (bytes.size() != kShapeBytes) return hasShapes();
    const std::vector<uint8_t>* expected = nullptr;
    auto* fresh = new std::vector<uint8_t>(bytes);
    if (!gShapes.compare_exchange_strong(expected, fresh, std::memory_order_acq_rel)) delete fresh;
    return true;
}

bool hasShapes() { return gShapes.load(std::memory_order_acquire) != nullptr; }

float shapeDistance(int index, float x, float y, float z) {
    const float cx = std::clamp(x, -1.0f, 1.0f);
    const float cy = std::clamp(y, -1.0f, 1.0f);
    const float cz = std::clamp(z, -1.0f, 1.0f);
    const float out = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy) + (z - cz) * (z - cz));
    const std::vector<uint8_t>* v = gShapes.load(std::memory_order_acquire);
    if (v == nullptr) return std::sqrt(x * x + y * y + z * z) - 0.6f;
    const int slab = std::clamp(index, 0, kShapeCount - 1);
    // Texel space: centres at (i + 0.5) / res of the unit cube, clamped to the edge texels.
    const auto coord = [](float c) { return std::clamp((c * 0.5f + 0.5f) * kShapeRes - 0.5f, 0.0f, kShapeRes - 1.0f); };
    const float fx = coord(cx);
    const float fy = coord(cy);
    const float fz = coord(cz);
    const int x0 = std::min(static_cast<int>(fx), kShapeRes - 2);
    const int y0 = std::min(static_cast<int>(fy), kShapeRes - 2);
    const int z0 = std::min(static_cast<int>(fz), kShapeRes - 2);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const float tz = fz - static_cast<float>(z0);
    float c[2][2];
    for (int dz = 0; dz < 2; ++dz) {
        for (int dy = 0; dy < 2; ++dy) {
            const float a = texel(*v, slab, x0, y0 + dy, z0 + dz);
            const float b = texel(*v, slab, x0 + 1, y0 + dy, z0 + dz);
            c[dz][dy] = a + (b - a) * tx;
        }
    }
    const float near = c[0][0] + (c[0][1] - c[0][0]) * ty;
    const float far = c[1][0] + (c[1][1] - c[1][0]) * ty;
    const float s = near + (far - near) * tz;
    const float d = kShapeLo + (kShapeHi - kShapeLo) * s;
    return out > 0.0f ? std::max(out, d - out) : d;
}

}  // namespace geode::viz::world
