#pragma once
#include <cstdint>
#include <cstring>

namespace geode::viz::half {

inline float toFloat(uint16_t h) {
    const uint32_t sign = (h & 0x8000u) << 16;
    uint32_t exponent = (h >> 10) & 0x1Fu;
    uint32_t mantissa = h & 0x3FFu;
    uint32_t bits;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else {
            exponent = 127 - 15 + 1;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                exponent--;
            }
            mantissa &= 0x3FFu;
            bits = sign | (exponent << 23) | (mantissa << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7F800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    }
    float out;
    std::memcpy(&out, &bits, sizeof out);
    return out;
}

inline uint16_t fromFloat(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    const uint16_t sign = static_cast<uint16_t>((bits >> 16) & 0x8000u);
    const int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    uint32_t mantissa = bits & 0x7FFFFFu;
    if (((bits >> 23) & 0xFFu) == 0xFFu) return static_cast<uint16_t>(sign | 0x7C00u | (mantissa ? 0x200u : 0u));
    if (exponent >= 31) return static_cast<uint16_t>(sign | 0x7C00u);
    if (exponent <= 0) {
        if (exponent < -10) return sign;
        mantissa |= 0x800000u;
        const uint32_t shift = static_cast<uint32_t>(14 - exponent);
        uint32_t half = mantissa >> shift;
        const uint32_t rem = mantissa & ((1u << shift) - 1u);
        const uint32_t midpoint = 1u << (shift - 1);
        if (rem > midpoint || (rem == midpoint && (half & 1u))) half++;
        return static_cast<uint16_t>(sign | half);
    }
    uint32_t half = static_cast<uint32_t>(exponent << 10) | (mantissa >> 13);
    const uint32_t rem = mantissa & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (half & 1u))) half++;
    return static_cast<uint16_t>(sign | half);
}

}  // namespace geode::viz::half
