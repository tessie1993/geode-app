#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace geode::util::qr {

// A QR code encoder (ISO/IEC 18004), enough for "Show on a computer": byte mode, error correction
// level M (15 %), versions 1 to kMaxVersion, the mask with the lowest penalty. The link it carries is
// about 60 bytes (version 4 or 5); version 10 holds 213. Drawn by the shell from the module grid, so the
// app needs no QR library. Layout after Nayuki's reference encoder (MIT); tested in
// core/tests/qr_code_test.cpp against the standard's worked vectors and by reading the symbol back.

constexpr int kMaxVersion = 10;
constexpr int kMaxSide = 4 * kMaxVersion + 17;   // 57 modules
constexpr int kMaxBytes = 213;                    // byte-mode capacity of version 10-M

// The smallest version whose level-M byte capacity holds n bytes; 0 when none up to kMaxVersion does.
int versionFor(size_t n);

// Data and EC codewords of a version at level M, in transmission order (interleaved), for data bytes.
std::vector<uint8_t> codewords(const uint8_t* data, size_t n, int version);

// Reed-Solomon over GF(2^8) mod 0x11D: the ecLen EC codewords of data.
std::vector<uint8_t> reedSolomon(const uint8_t* data, size_t n, int ecLen);

// The 15 format bits for level M and mask (0..7), BCH-coded and XORed with 0x5412.
int formatBits(int mask);

// The 18 version bits (versions 7 and up).
int versionBits(int version);

struct Symbol {
    int version = 0;
    int mask = -1;
    int side = 0;
    std::vector<uint8_t> modules;   // side * side, row-major, 1 = dark

    bool dark(int x, int y) const { return modules[static_cast<size_t>(y) * side + x] != 0; }
};

// Encodes data; an empty Symbol (side 0) when it does not fit. forceMask in 0..7 skips the penalty choice.
Symbol encode(const uint8_t* data, size_t n, int forceMask = -1);

// The standard's penalty score (N1..N4) of a finished symbol: lower reads better.
int penalty(const Symbol& s);

}  // namespace geode::util::qr
