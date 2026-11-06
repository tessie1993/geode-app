#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace geode::util {

// A colour lookup cube for Studio's clip LUTs: size^3 opaque ARGB entries, indexed [r][g][b] as
// (r * size + g) * size + b. Ported from the Kotlin export/CubeLut; reading the file through the content
// resolver and handing the cube to Media3's SingleColorLut stay Kotlin.
class CubeLut {
public:
    static constexpr int kMaxSize = 65;
    static constexpr int kGammaSize = 33;

    // An Adobe/Resolve .cube text: LUT_3D_SIZE (red fastest in the rows) or LUT_1D_SIZE (expanded to a cube),
    // DOMAIN_MIN / DOMAIN_MAX normalised to 0..1, '#' comments and TITLE ignored. Nothing when the size is
    // outside 2..65 or there are too few rows.
    static std::optional<CubeLut> parse(std::string_view text);

    // Per-channel gamma as a cube: output = input ^ (1 / gamma), each gamma at least 0.01.
    static CubeLut gamma(float red, float green, float blue, int size = kGammaSize);

    int size() const { return size_; }
    const std::vector<uint32_t>& argb() const { return argb_; }
    uint32_t at(int r, int g, int b) const { return argb_[static_cast<size_t>((r * size_ + g) * size_ + b)]; }

private:
    CubeLut(int size, std::vector<uint32_t> argb) : size_(size), argb_(std::move(argb)) {}

    int size_;
    std::vector<uint32_t> argb_;
};

}  // namespace geode::util
