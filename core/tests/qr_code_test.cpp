// Host-side checks for core/util/QrCode: the QR code "Show on a computer" shows for its link.
// Built and run by CI ("Native host tests" in .github/workflows/native-host-tests.yml); no GL.
//
// What is pinned: the standard's worked vectors (thonky.com's QR tutorial, after ISO/IEC 18004): the
// Reed-Solomon codewords of "HELLO WORLD" 1-M, the fifteen format bits of level M for every mask, the version
// 7 bits; the byte-mode capacities of level M; and, for links of every length up to version 10, a reader
// written here from the standard's tables (alignment centres, block split) rather than the encoder's code:
// the finders, timing and dark module are where they belong, both format copies name the mask, and the data
// unmasked, de-interleaved, checked against its EC words and parsed gives back the bytes encoded.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "util/QrCode.hpp"

namespace qr = geode::util::qr;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void testVectors() {
    const uint8_t data[] = {32, 91, 11, 120, 209, 114, 220, 77, 67, 64, 236, 17, 236, 17, 236, 17};
    const uint8_t ec[] = {196, 35, 39, 119, 235, 215, 231, 226, 93, 23};
    const std::vector<uint8_t> got = qr::reedSolomon(data, sizeof data, 10);
    check(got.size() == 10 && std::memcmp(got.data(), ec, 10) == 0, "RS: HELLO WORLD 1-M EC words");

    const char* format[8] = {"101010000010010", "101000100100101", "101111001111100", "101101101001011",
                             "100010111111001", "100000011001110", "100111110010111", "100101010100000"};
    for (int mask = 0; mask < 8; ++mask) {
        int bits = 0;
        for (const char* c = format[mask]; *c; ++c) bits = (bits << 1) | (*c - '0');
        check(qr::formatBits(mask) == bits, "format bits of level M");
    }
    check(qr::versionBits(7) == 0x07C94, "version 7 bits 000111110010010100");
    check(qr::versionBits(10) == 0x0A4D3, "version 10 bits 001010010011010011");

    // Byte capacities of level M (ISO/IEC 18004 table 7).
    const int capacity[11] = {0, 14, 26, 42, 62, 84, 106, 122, 152, 180, 213};
    for (int v = 1; v <= 10; ++v) {
        check(qr::versionFor(static_cast<size_t>(capacity[v])) == v, "the capacity fits its version");
        check(qr::versionFor(static_cast<size_t>(capacity[v] + 1)) == (v == 10 ? 0 : v + 1), "one more byte takes the next");
    }
}

// ---- An independent reader, from the standard's tables. ----

const std::vector<int> kAlign[11] = {{}, {}, {6, 18}, {6, 22}, {6, 26}, {6, 30}, {6, 34}, {6, 22, 38}, {6, 24, 42},
                                     {6, 26, 46}, {6, 28, 50}};
// Level M blocks: {count, data words} groups (table 9).
const std::vector<std::pair<int, int>> kBlocks[11] = {
    {}, {{1, 16}}, {{1, 28}}, {{1, 44}}, {{2, 32}}, {{2, 43}}, {{4, 27}}, {{4, 31}}, {{2, 38}, {2, 39}},
    {{3, 36}, {2, 37}}, {{4, 43}, {1, 44}}};
const int kEc[11] = {0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26};

bool reserved(int version, int n, int x, int y) {
    if ((x < 9 && y < 9) || (x >= n - 8 && y < 9) || (x < 9 && y >= n - 8)) return true;   // finders, format
    if (x == 6 || y == 6) return true;
    const std::vector<int>& a = kAlign[version];
    for (size_t i = 0; i < a.size(); ++i)
        for (size_t j = 0; j < a.size(); ++j) {
            const bool corner = (i == 0 && j == 0) || (i == 0 && j + 1 == a.size()) || (i + 1 == a.size() && j == 0);
            if (!corner && std::abs(x - a[i]) <= 2 && std::abs(y - a[j]) <= 2) return true;
        }
    if (version >= 7 && ((x >= n - 11 && y < 6) || (y >= n - 11 && x < 6))) return true;
    return false;
}

bool masked(int mask, int x, int y) {
    const int i = y, j = x;   // the standard's row i, column j
    switch (mask) {
        case 0: return (i + j) % 2 == 0;
        case 1: return i % 2 == 0;
        case 2: return j % 3 == 0;
        case 3: return (i + j) % 3 == 0;
        case 4: return (i / 2 + j / 3) % 2 == 0;
        case 5: return (i * j) % 2 + (i * j) % 3 == 0;
        case 6: return ((i * j) % 2 + (i * j) % 3) % 2 == 0;
        default: return ((i * j) % 3 + (i + j) % 2) % 2 == 0;
    }
}

bool finderAt(const qr::Symbol& s, int x0, int y0) {
    for (int y = 0; y < 7; ++y)
        for (int x = 0; x < 7; ++x) {
            const int d = std::max(std::abs(x - 3), std::abs(y - 3));
            if (s.dark(x0 + x, y0 + y) != (d != 2)) return false;
        }
    return true;
}

std::string read(const qr::Symbol& s, const char* label) {
    const int n = s.side;
    const int version = (n - 17) / 4;
    check(n == 4 * version + 17 && version >= 1 && version <= 10, label);
    check(finderAt(s, 0, 0) && finderAt(s, n - 7, 0) && finderAt(s, 0, n - 7), "three finders");
    for (int i = 8; i < n - 8; ++i) check(s.dark(i, 6) == (i % 2 == 0) && s.dark(6, i) == (i % 2 == 0), "timing");
    check(s.dark(8, n - 8), "the dark module");

    // Format: the copy around the top-left finder, then the split one.
    int a = 0, b = 0;
    const int ax[15] = {8, 8, 8, 8, 8, 8, 8, 8, 7, 5, 4, 3, 2, 1, 0};
    const int ay[15] = {0, 1, 2, 3, 4, 5, 7, 8, 8, 8, 8, 8, 8, 8, 8};
    for (int i = 14; i >= 0; --i) a = (a << 1) | (s.dark(ax[i], ay[i]) ? 1 : 0);
    for (int i = 14; i >= 0; --i) b = (b << 1) | (i < 8 ? s.dark(n - 1 - i, 8) : s.dark(8, n - 15 + i)) ;
    check(a == b, "both format copies agree");
    int mask = -1;
    for (int m = 0; m < 8; ++m)
        if (qr::formatBits(m) == a) mask = m;
    check(mask >= 0 && mask == s.mask, "the format names level M and the mask used");
    if (mask < 0) return {};

    std::vector<uint8_t> words;
    int bit = 0, acc = 0;
    for (int right = n - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        const bool up = ((n - 1 - right) / 2) % 2 == 0;
        for (int k = 0; k < n; ++k) {
            const int y = up ? n - 1 - k : k;
            for (int x = right; x >= right - 1; --x) {
                if (reserved(version, n, x, y)) continue;
                acc = (acc << 1) | ((s.dark(x, y) != masked(mask, x, y)) ? 1 : 0);
                if (++bit == 8) {
                    words.push_back(static_cast<uint8_t>(acc));
                    bit = acc = 0;
                }
            }
        }
    }
    // De-interleave: data words column by column over the blocks, then EC words likewise.
    std::vector<std::vector<uint8_t>> data, ec;
    for (auto [count, len] : kBlocks[version])
        for (int i = 0; i < count; ++i) data.emplace_back(static_cast<size_t>(len));
    size_t at = 0, longest = 0;
    for (auto& d : data) longest = std::max(longest, d.size());
    for (size_t i = 0; i < longest; ++i)
        for (auto& d : data)
            if (i < d.size()) d[i] = at < words.size() ? words[at++] : 0;
    ec.assign(data.size(), std::vector<uint8_t>(static_cast<size_t>(kEc[version])));
    for (size_t i = 0; i < ec[0].size(); ++i)
        for (auto& e : ec) e[i] = at < words.size() ? words[at++] : 0;
    for (size_t i = 0; i < data.size(); ++i) check(qr::reedSolomon(data[i].data(), data[i].size(), kEc[version]) == ec[i], "EC words");

    std::vector<int> bits;
    for (auto& d : data)
        for (uint8_t w : d)
            for (int i = 7; i >= 0; --i) bits.push_back((w >> i) & 1);
    size_t p = 0;
    const auto take = [&](int count) {
        int v = 0;
        for (int i = 0; i < count; ++i) v = (v << 1) | (p < bits.size() ? bits[p++] : 0);
        return v;
    };
    check(take(4) == 4, "byte mode");
    const int length = take(version <= 9 ? 8 : 16);
    std::string out;
    for (int i = 0; i < length; ++i) out += static_cast<char>(take(8));
    return out;
}

void testRoundTrip() {
    std::string base = "http://192.168.100.200:65535/0123456789abcdef0123456789abcdef/";
    while (base.size() < qr::kMaxBytes) base += "x";
    for (size_t len : {size_t{0}, size_t{1}, size_t{14}, size_t{15}, size_t{48}, size_t{62}, size_t{63}, size_t{84},
                       size_t{106}, size_t{122}, size_t{152}, size_t{180}, size_t{213}}) {
        const std::string text = base.substr(0, len);
        const qr::Symbol best = qr::encode(reinterpret_cast<const uint8_t*>(text.data()), text.size());
        check(read(best, "a version 1..10 side") == text, "reads back what was encoded");
        for (int mask = 0; mask < 8; ++mask) {
            const qr::Symbol forced = qr::encode(reinterpret_cast<const uint8_t*>(text.data()), text.size(), mask);
            check(read(forced, "forced mask side") == text, "every mask reads back");
            check(qr::penalty(best) <= qr::penalty(forced), "the chosen mask has the lowest penalty");
        }
    }
    const std::string link = "http://192.168.1.23:41237/9f86d081884c7d659a2feaa0c55ad015/";
    const qr::Symbol s = qr::encode(reinterpret_cast<const uint8_t*>(link.data()), link.size());
    check(s.version == 4 && s.side == 33, "a typical link is version 4 (33 modules)");
    check(qr::encode(reinterpret_cast<const uint8_t*>(base.data()), qr::kMaxBytes + 1).side == 0, "too long: no symbol");
}

void testAbi() {
    const std::string link = "http://10.0.0.2:8080/00000000000000000000000000000000/";
    std::vector<uint8_t> out(GEODE_QR_MAX_MODULES);
    const int side = geode_qr_encode(reinterpret_cast<const uint8_t*>(link.data()), static_cast<int>(link.size()), out.data(),
                                     static_cast<int>(out.size()));
    const qr::Symbol s = qr::encode(reinterpret_cast<const uint8_t*>(link.data()), link.size());
    check(side == s.side && std::memcmp(out.data(), s.modules.data(), s.modules.size()) == 0, "abi: the same modules");
    check(geode_qr_encode(reinterpret_cast<const uint8_t*>(link.data()), static_cast<int>(link.size()), out.data(), 10) == 0,
          "abi: a short buffer is refused");
    check(geode_qr_encode(nullptr, 5, out.data(), static_cast<int>(out.size())) == 0, "abi: null data is refused");
    check(geode_qr_encode(out.data(), GEODE_QR_MAX_BYTES + 1, out.data(), static_cast<int>(out.size())) == 0, "abi: too long");
}

}  // namespace

int main() {
    testVectors();
    testRoundTrip();
    testAbi();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("qr_code_test: all passed\n");
    return 0;
}
