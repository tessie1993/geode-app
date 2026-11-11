#include "util/QrCode.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace geode::util::qr {

namespace {

// Level M, per version 1..10 (ISO/IEC 18004 table 9).
constexpr std::array<int, kMaxVersion + 1> kEcPerBlock = {0, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26};
constexpr std::array<int, kMaxVersion + 1> kBlocks = {0, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5};

int side(int version) { return 4 * version + 17; }

// Modules left for data and EC once every function pattern is drawn, in bits.
int rawDataModules(int version) {
    int n = (16 * version + 128) * version + 64;
    if (version >= 2) {
        const int align = version / 7 + 2;
        n -= (25 * align - 10) * align - 55;
        if (version >= 7) n -= 36;
    }
    return n;
}

int totalCodewords(int version) { return rawDataModules(version) / 8; }
int dataCodewords(int version) { return totalCodewords(version) - kEcPerBlock[version] * kBlocks[version]; }
int countBits(int version) { return version <= 9 ? 8 : 16; }

uint8_t gfMultiply(uint8_t x, uint8_t y) {
    int z = 0;
    for (int i = 7; i >= 0; --i) {
        z = (z << 1) ^ ((z >> 7) * 0x11D);
        z ^= ((y >> i) & 1) * x;
    }
    return static_cast<uint8_t>(z);
}

std::vector<uint8_t> rsDivisor(int degree) {
    std::vector<uint8_t> result(static_cast<size_t>(degree), 0);
    result.back() = 1;
    uint8_t root = 1;
    for (int i = 0; i < degree; ++i) {
        for (size_t j = 0; j < result.size(); ++j) {
            result[j] = gfMultiply(result[j], root);
            if (j + 1 < result.size()) result[j] ^= result[j + 1];
        }
        root = gfMultiply(root, 0x02);
    }
    return result;
}

std::vector<int> alignmentPositions(int version) {
    if (version == 1) return {};
    const int count = version / 7 + 2;
    const int step = (version * 4 + count * 2 + 1) / (count * 2 - 2) * 2;
    std::vector<int> out(static_cast<size_t>(count));
    out[0] = 6;
    for (int i = count - 1, pos = side(version) - 7; i >= 1; --i, pos -= step) out[static_cast<size_t>(i)] = pos;
    return out;
}

class Grid {
public:
    explicit Grid(int version) : n_(side(version)), dark_(static_cast<size_t>(n_ * n_), 0), fn_(dark_.size(), 0) {}

    int n() const { return n_; }
    bool dark(int x, int y) const { return dark_[idx(x, y)] != 0; }
    bool function(int x, int y) const { return fn_[idx(x, y)] != 0; }
    void set(int x, int y, bool d) { dark_[idx(x, y)] = d ? 1 : 0; }
    void setFunction(int x, int y, bool d) {
        set(x, y, d);
        fn_[idx(x, y)] = 1;
    }
    std::vector<uint8_t>& modules() { return dark_; }

private:
    size_t idx(int x, int y) const { return static_cast<size_t>(y) * n_ + x; }
    int n_;
    std::vector<uint8_t> dark_;
    std::vector<uint8_t> fn_;
};

void drawFinder(Grid& g, int cx, int cy) {
    for (int dy = -4; dy <= 4; ++dy) {
        for (int dx = -4; dx <= 4; ++dx) {
            const int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= g.n() || y >= g.n()) continue;
            const int dist = std::max(std::abs(dx), std::abs(dy));
            g.setFunction(x, y, dist != 2 && dist != 4);
        }
    }
}

void drawAlignment(Grid& g, int cx, int cy) {
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) g.setFunction(cx + dx, cy + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
}

void drawFormat(Grid& g, int mask) {
    const int bits = formatBits(mask);
    const auto bit = [bits](int i) { return ((bits >> i) & 1) != 0; };
    const int n = g.n();
    for (int i = 0; i <= 5; ++i) g.setFunction(8, i, bit(i));
    g.setFunction(8, 7, bit(6));
    g.setFunction(8, 8, bit(7));
    g.setFunction(7, 8, bit(8));
    for (int i = 9; i < 15; ++i) g.setFunction(14 - i, 8, bit(i));
    for (int i = 0; i < 8; ++i) g.setFunction(n - 1 - i, 8, bit(i));
    for (int i = 8; i < 15; ++i) g.setFunction(8, n - 15 + i, bit(i));
    g.setFunction(8, n - 8, true);   // the dark module
}

void drawVersion(Grid& g, int version) {
    if (version < 7) return;
    const int bits = versionBits(version);
    const int n = g.n();
    for (int i = 0; i < 18; ++i) {
        const bool d = ((bits >> i) & 1) != 0;
        const int a = n - 11 + i % 3, b = i / 3;
        g.setFunction(a, b, d);
        g.setFunction(b, a, d);
    }
}

void drawFunctionPatterns(Grid& g, int version) {
    const int n = g.n();
    for (int i = 0; i < n; ++i) {
        g.setFunction(6, i, i % 2 == 0);
        g.setFunction(i, 6, i % 2 == 0);
    }
    drawFinder(g, 3, 3);
    drawFinder(g, n - 4, 3);
    drawFinder(g, 3, n - 4);
    const std::vector<int> align = alignmentPositions(version);
    const size_t count = align.size();
    for (size_t i = 0; i < count; ++i) {
        for (size_t j = 0; j < count; ++j) {
            const bool corner = (i == 0 && j == 0) || (i == 0 && j == count - 1) || (i == count - 1 && j == 0);
            if (!corner) drawAlignment(g, align[i], align[j]);
        }
    }
    drawFormat(g, 0);   // reserves the format area; redrawn with the chosen mask
    drawVersion(g, version);
}

void placeCodewords(Grid& g, const std::vector<uint8_t>& data) {
    const int n = g.n();
    size_t i = 0;
    const size_t bits = data.size() * 8;
    for (int right = n - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;
        for (int vert = 0; vert < n; ++vert) {
            for (int j = 0; j < 2; ++j) {
                const int x = right - j;
                const bool upward = ((right + 1) & 2) == 0;
                const int y = upward ? n - 1 - vert : vert;
                if (g.function(x, y) || i >= bits) continue;
                g.set(x, y, ((data[i >> 3] >> (7 - (i & 7))) & 1) != 0);
                ++i;
            }
        }
    }
}

bool maskBit(int mask, int x, int y) {
    switch (mask) {
        case 0: return (x + y) % 2 == 0;
        case 1: return y % 2 == 0;
        case 2: return x % 3 == 0;
        case 3: return (x + y) % 3 == 0;
        case 4: return (x / 3 + y / 2) % 2 == 0;
        case 5: return x * y % 2 + x * y % 3 == 0;
        case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
        default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}

void applyMask(Grid& g, int mask) {
    for (int y = 0; y < g.n(); ++y)
        for (int x = 0; x < g.n(); ++x)
            if (!g.function(x, y) && maskBit(mask, x, y)) g.set(x, y, !g.dark(x, y));
}

// N1 and N3 along one line of modules.
int linePenalty(const std::vector<uint8_t>& line) {
    int score = 0;
    const size_t n = line.size();
    size_t run = 1;
    for (size_t i = 1; i <= n; ++i) {
        if (i < n && line[i] == line[i - 1]) {
            ++run;
            continue;
        }
        if (run >= 5) score += 3 + static_cast<int>(run - 5);
        run = 1;
    }
    // 1:1:3:1:1 finder-like runs with four light modules on one side; past the edge counts as light.
    static constexpr uint8_t kA[11] = {0, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1};
    static constexpr uint8_t kB[11] = {1, 0, 1, 1, 1, 0, 1, 0, 0, 0, 0};
    const auto at = [&](long i) -> uint8_t { return i < 0 || i >= static_cast<long>(n) ? 0 : line[static_cast<size_t>(i)]; };
    for (long start = -4; start + 11 <= static_cast<long>(n) + 4; ++start) {
        bool a = true, b = true;
        for (long k = 0; k < 11; ++k) {
            a = a && at(start + k) == kA[k];
            b = b && at(start + k) == kB[k];
        }
        if (a) score += 40;
        if (b) score += 40;
    }
    return score;
}

}  // namespace

int versionFor(size_t n) {
    for (int v = 1; v <= kMaxVersion; ++v) {
        const size_t capacity = static_cast<size_t>((dataCodewords(v) * 8 - 4 - countBits(v)) / 8);
        if (n <= capacity) return v;
    }
    return 0;
}

std::vector<uint8_t> reedSolomon(const uint8_t* data, size_t n, int ecLen) {
    const std::vector<uint8_t> divisor = rsDivisor(ecLen);
    std::vector<uint8_t> result(static_cast<size_t>(ecLen), 0);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t factor = data[i] ^ result[0];
        result.erase(result.begin());
        result.push_back(0);
        for (size_t j = 0; j < result.size(); ++j) result[j] ^= gfMultiply(divisor[j], factor);
    }
    return result;
}

std::vector<uint8_t> codewords(const uint8_t* data, size_t n, int version) {
    if (version < 1 || version > kMaxVersion) return {};
    const int capacityBits = dataCodewords(version) * 8;
    std::vector<uint8_t> bits;
    const auto put = [&bits](uint32_t value, int count) {
        for (int i = count - 1; i >= 0; --i) bits.push_back(static_cast<uint8_t>((value >> i) & 1));
    };
    put(0x4, 4);   // byte mode
    put(static_cast<uint32_t>(n), countBits(version));
    for (size_t i = 0; i < n; ++i) put(data[i], 8);
    if (static_cast<int>(bits.size()) > capacityBits) return {};
    put(0, std::min(4, capacityBits - static_cast<int>(bits.size())));   // terminator
    while (bits.size() % 8 != 0) bits.push_back(0);
    for (uint8_t pad = 0xEC; static_cast<int>(bits.size()) < capacityBits; pad ^= 0xEC ^ 0x11) put(pad, 8);
    std::vector<uint8_t> dataWords(bits.size() / 8, 0);
    for (size_t i = 0; i < bits.size(); ++i) dataWords[i >> 3] = static_cast<uint8_t>(dataWords[i >> 3] | (bits[i] << (7 - (i & 7))));

    const int blocks = kBlocks[static_cast<size_t>(version)];
    const int ecLen = kEcPerBlock[static_cast<size_t>(version)];
    const int raw = totalCodewords(version);
    const int shortBlocks = blocks - raw % blocks;
    const int shortLen = raw / blocks;
    std::vector<std::vector<uint8_t>> split;
    size_t at = 0;
    for (int i = 0; i < blocks; ++i) {
        const size_t len = static_cast<size_t>(shortLen - ecLen + (i < shortBlocks ? 0 : 1));
        std::vector<uint8_t> block(dataWords.begin() + static_cast<long>(at), dataWords.begin() + static_cast<long>(at + len));
        at += len;
        const std::vector<uint8_t> ec = reedSolomon(block.data(), block.size(), ecLen);
        if (i < shortBlocks) block.push_back(0);   // a gap, skipped below, so every block is as long
        block.insert(block.end(), ec.begin(), ec.end());
        split.push_back(std::move(block));
    }
    std::vector<uint8_t> out;
    for (size_t i = 0; i < split[0].size(); ++i) {
        for (int j = 0; j < blocks; ++j) {
            if (i == static_cast<size_t>(shortLen - ecLen) && j < shortBlocks) continue;
            out.push_back(split[static_cast<size_t>(j)][i]);
        }
    }
    return out;
}

int formatBits(int mask) {
    const int data = (0 << 3) | (mask & 7);   // level M is 00
    int rem = data;
    for (int i = 0; i < 10; ++i) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
    return ((data << 10) | (rem & 0x3FF)) ^ 0x5412;
}

int versionBits(int version) {
    int rem = version;
    for (int i = 0; i < 12; ++i) rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
    return (version << 12) | (rem & 0xFFF);
}

int penalty(const Symbol& s) {
    int score = 0;
    const int n = s.side;
    std::vector<uint8_t> line(static_cast<size_t>(n));
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) line[static_cast<size_t>(x)] = s.dark(x, y) ? 1 : 0;
        score += linePenalty(line);
    }
    for (int x = 0; x < n; ++x) {
        for (int y = 0; y < n; ++y) line[static_cast<size_t>(y)] = s.dark(x, y) ? 1 : 0;
        score += linePenalty(line);
    }
    for (int y = 0; y + 1 < n; ++y)
        for (int x = 0; x + 1 < n; ++x) {
            const bool d = s.dark(x, y);
            if (d == s.dark(x + 1, y) && d == s.dark(x, y + 1) && d == s.dark(x + 1, y + 1)) score += 3;
        }
    int dark = 0;
    for (uint8_t m : s.modules) dark += m;
    const int total = n * n;
    const int k = (std::abs(dark * 20 - total * 10) + total - 1) / total - 1;
    return score + k * 10;
}

Symbol encode(const uint8_t* data, size_t n, int forceMask) {
    const int version = versionFor(n);
    if (version == 0 || (n > 0 && !data)) return {};
    const std::vector<uint8_t> words = codewords(data, n, version);
    Grid base(version);
    drawFunctionPatterns(base, version);
    placeCodewords(base, words);
    Symbol best;
    int bestScore = 0;
    for (int mask = 0; mask < 8; ++mask) {
        if (forceMask >= 0 && mask != forceMask) continue;
        Grid g = base;
        applyMask(g, mask);
        drawFormat(g, mask);
        Symbol s{version, mask, g.n(), g.modules()};
        if (forceMask >= 0) return s;
        const int score = penalty(s);
        if (best.side == 0 || score < bestScore) {
            best = std::move(s);
            bestScore = score;
        }
    }
    return best;
}

}  // namespace geode::util::qr
