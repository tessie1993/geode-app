#include "util/CubeLut.hpp"

#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>

namespace geode::util {

namespace {

bool isSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

std::string_view trim(std::string_view s) {
    size_t a = 0;
    size_t b = s.size();
    while (a < b && isSpace(s[a])) ++a;
    while (b > a && isSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> split(std::string_view line) {
    std::vector<std::string> parts;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && isSpace(line[i])) ++i;
        const size_t start = i;
        while (i < line.size() && !isSpace(line[i])) ++i;
        if (i > start) parts.emplace_back(line.substr(start, i - start));
    }
    return parts;
}

std::optional<float> toFloat(const std::string& s) {
    if (s.empty()) return std::nullopt;
    char* end = nullptr;
    errno = 0;
    const float v = std::strtof(s.c_str(), &end);
    if (end != s.c_str() + s.size()) return std::nullopt;
    return v;
}

std::optional<int> toInt(const std::string& s) {
    if (s.empty()) return std::nullopt;
    char* end = nullptr;
    errno = 0;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (end != s.c_str() + s.size() || errno == ERANGE || v < -2147483647L - 1 || v > 2147483647L) return std::nullopt;
    return static_cast<int>(v);
}

using Rgb = std::array<float, 3>;

std::optional<Rgb> triple(const std::vector<std::string>& parts, size_t from) {
    if (parts.size() < from + 3) return std::nullopt;
    const auto r = toFloat(parts[from]);
    const auto g = toFloat(parts[from + 1]);
    const auto b = toFloat(parts[from + 2]);
    if (!r || !g || !b) return std::nullopt;
    return Rgb{*r, *g, *b};
}

std::string upper(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// Kotlin's (v * 255).roundToInt().coerceIn(0, 255), with NaN as 0 as roundToInt gives.
uint32_t channel(float v) {
    if (std::isnan(v)) return 0;
    const float scaled = std::floor(v * 255.0f + 0.5f);
    return static_cast<uint32_t>(scaled < 0.0f ? 0.0f : (scaled > 255.0f ? 255.0f : scaled));
}

uint32_t argb(float r, float g, float b) { return 0xFF000000u | (channel(r) << 16) | (channel(g) << 8) | channel(b); }

}  // namespace

std::optional<CubeLut> CubeLut::parse(std::string_view text) {
    int size3d = 0;
    int size1d = 0;
    Rgb domainMin{0.0f, 0.0f, 0.0f};
    Rgb domainMax{1.0f, 1.0f, 1.0f};
    std::vector<Rgb> rows;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        std::string_view raw = text.substr(pos, nl - pos);
        pos = nl + 1;
        const size_t hash = raw.find('#');
        const std::string_view line = trim(hash == std::string_view::npos ? raw : raw.substr(0, hash));
        if (line.empty()) continue;
        const auto parts = split(line);
        const std::string head = upper(parts[0]);
        if (head == "TITLE") continue;
        if (head == "LUT_3D_SIZE") {
            size3d = parts.size() > 1 ? toInt(parts[1]).value_or(0) : 0;
        } else if (head == "LUT_1D_SIZE") {
            size1d = parts.size() > 1 ? toInt(parts[1]).value_or(0) : 0;
        } else if (head == "DOMAIN_MIN") {
            domainMin = triple(parts, 1).value_or(domainMin);
        } else if (head == "DOMAIN_MAX") {
            domainMax = triple(parts, 1).value_or(domainMax);
        } else if (const auto row = triple(parts, 0)) {
            rows.push_back(*row);
        }
    }
    Rgb span{};
    for (int c = 0; c < 3; ++c) {
        const float d = domainMax[static_cast<size_t>(c)] - domainMin[static_cast<size_t>(c)];
        span[static_cast<size_t>(c)] = d > 0.0f ? d : 1.0f;
    }
    auto norm = [&](float v, int c) {
        const float t = (v - domainMin[static_cast<size_t>(c)]) / span[static_cast<size_t>(c)];
        return std::isnan(t) ? t : (t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t));
    };

    if (size3d >= 2 && size3d <= kMaxSize &&
        rows.size() >= static_cast<size_t>(size3d) * static_cast<size_t>(size3d) * static_cast<size_t>(size3d)) {
        const int n = size3d;
        std::vector<uint32_t> table(static_cast<size_t>(n) * n * n);
        for (int r = 0; r < n; ++r) {
            for (int g = 0; g < n; ++g) {
                for (int b = 0; b < n; ++b) {
                    const Rgb& row = rows[static_cast<size_t>(r + g * n + b * n * n)];
                    table[static_cast<size_t>((r * n + g) * n + b)] = argb(norm(row[0], 0), norm(row[1], 1), norm(row[2], 2));
                }
            }
        }
        return CubeLut(n, std::move(table));
    }
    if (size1d >= 2 && size1d <= kMaxSize && rows.size() >= static_cast<size_t>(size1d)) {
        const int n = size1d;
        std::vector<uint32_t> table(static_cast<size_t>(n) * n * n);
        for (int r = 0; r < n; ++r) {
            for (int g = 0; g < n; ++g) {
                for (int b = 0; b < n; ++b) {
                    table[static_cast<size_t>((r * n + g) * n + b)] =
                        argb(norm(rows[static_cast<size_t>(r)][0], 0), norm(rows[static_cast<size_t>(g)][1], 1),
                             norm(rows[static_cast<size_t>(b)][2], 2));
                }
            }
        }
        return CubeLut(n, std::move(table));
    }
    return std::nullopt;
}

CubeLut CubeLut::gamma(float red, float green, float blue, int size) {
    const int n = size < 2 ? 2 : (size > kMaxSize ? kMaxSize : size);
    const float last = static_cast<float>(n - 1);
    std::vector<float> r(static_cast<size_t>(n));
    std::vector<float> g(static_cast<size_t>(n));
    std::vector<float> b(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float x = static_cast<float>(i) / last;
        r[static_cast<size_t>(i)] = std::pow(x, 1.0f / std::fmax(red, 0.01f));
        g[static_cast<size_t>(i)] = std::pow(x, 1.0f / std::fmax(green, 0.01f));
        b[static_cast<size_t>(i)] = std::pow(x, 1.0f / std::fmax(blue, 0.01f));
    }
    std::vector<uint32_t> table(static_cast<size_t>(n) * n * n);
    for (int ri = 0; ri < n; ++ri) {
        for (int gi = 0; gi < n; ++gi) {
            for (int bi = 0; bi < n; ++bi) {
                table[static_cast<size_t>((ri * n + gi) * n + bi)] =
                    argb(r[static_cast<size_t>(ri)], g[static_cast<size_t>(gi)], b[static_cast<size_t>(bi)]);
            }
        }
    }
    return CubeLut(n, std::move(table));
}

}  // namespace geode::util
