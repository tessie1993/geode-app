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

// The bound outside the cube that both readers share: the sample less the way out, never under it.
float bounded(float sample, float out) {
    const float d = kShapeLo + (kShapeHi - kShapeLo) * sample;
    return out > 0.0f ? std::max(out, d - out) : d;
}

float atlasTexel(const std::vector<uint8_t>& a, int x, int y, int z) {
    const std::size_t i = (static_cast<std::size_t>(z) * kAtlasHeight + static_cast<std::size_t>(y)) * kAtlasWidth +
                          static_cast<std::size_t>(x);
    return static_cast<float>(a[i]) / 255.0f;
}

}  // namespace

TileOrigin tileOrigin(int index) {
    const int i = std::clamp(index, 0, kShapeCount - 1);
    return {(i % kAtlasCols) * kShapeRes, (i / kAtlasCols) * kShapeRes};
}

std::vector<uint8_t> packAtlas(const std::vector<uint8_t>& slabs) {
    if (slabs.size() != kShapeBytes) return {};
    std::vector<uint8_t> atlas(kAtlasBytes, 0);
    const std::size_t row = kShapeRes;
    for (int s = 0; s < kShapeCount; ++s) {
        const TileOrigin o = tileOrigin(s);
        for (int z = 0; z < kShapeRes; ++z) {
            for (int y = 0; y < kShapeRes; ++y) {
                const std::size_t from = ((static_cast<std::size_t>(s) * kShapeRes + static_cast<std::size_t>(z)) * kShapeRes +
                                          static_cast<std::size_t>(y)) * row;
                const std::size_t to = (static_cast<std::size_t>(z) * kAtlasHeight + static_cast<std::size_t>(o.y + y)) * kAtlasWidth +
                                       static_cast<std::size_t>(o.x);
                std::copy_n(slabs.begin() + static_cast<std::ptrdiff_t>(from), row, atlas.begin() + static_cast<std::ptrdiff_t>(to));
            }
        }
    }
    return atlas;
}

float atlasDistance(const std::vector<uint8_t>& atlas, int index, float x, float y, float z) {
    if (atlas.size() != kAtlasBytes) return std::sqrt(x * x + y * y + z * z) - 0.6f;
    const float cx = std::clamp(x, -1.0f, 1.0f);
    const float cy = std::clamp(y, -1.0f, 1.0f);
    const float cz = std::clamp(z, -1.0f, 1.0f);
    const float out = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy) + (z - cz) * (z - cz));
    const TileOrigin o = tileOrigin(index);
    // lib_world: the normalized coordinate is (origin + clamp(uvw * res, 0.5, res - 0.5)) / size;
    // GL_LINEAR then reads texel space at that coordinate * size - 0.5, which stays inside the tile.
    const float res = static_cast<float>(kShapeRes);
    const auto norm = [res](float c, int origin, int size) {
        return (static_cast<float>(origin) + std::clamp((c * 0.5f + 0.5f) * res, 0.5f, res - 0.5f)) / static_cast<float>(size);
    };
    const float fx = norm(cx, o.x, kAtlasWidth) * static_cast<float>(kAtlasWidth) - 0.5f;
    const float fy = norm(cy, o.y, kAtlasHeight) * static_cast<float>(kAtlasHeight) - 0.5f;
    const float fz = norm(cz, 0, kAtlasDepth) * static_cast<float>(kAtlasDepth) - 0.5f;
    // The upper neighbour of the tile's last texel centre has weight zero: keep both reads in the tile.
    const int x0 = std::clamp(static_cast<int>(std::floor(fx)), o.x, o.x + kShapeRes - 2);
    const int y0 = std::clamp(static_cast<int>(std::floor(fy)), o.y, o.y + kShapeRes - 2);
    const int z0 = std::clamp(static_cast<int>(std::floor(fz)), 0, kShapeRes - 2);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const float tz = fz - static_cast<float>(z0);
    float c[2][2];
    for (int dz = 0; dz < 2; ++dz) {
        for (int dy = 0; dy < 2; ++dy) {
            const float a = atlasTexel(atlas, x0, y0 + dy, z0 + dz);
            const float b = atlasTexel(atlas, x0 + 1, y0 + dy, z0 + dz);
            c[dz][dy] = a + (b - a) * tx;
        }
    }
    const float near = c[0][0] + (c[0][1] - c[0][0]) * ty;
    const float far = c[1][0] + (c[1][1] - c[1][0]) * ty;
    return bounded(near + (far - near) * tz, out);
}

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
    return bounded(near + (far - near) * tz, out);
}

}  // namespace geode::viz::world
