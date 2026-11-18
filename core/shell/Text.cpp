#include "shell/Text.hpp"

namespace geode::shell::text {

namespace {

bool isCont(unsigned char b) { return (b & 0xC0) == 0x80; }

bool isHigh(char32_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool isLow(char32_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

}  // namespace

std::vector<char32_t> decode(std::string_view s) {
    std::vector<char32_t> out;
    size_t i = 0;
    const size_t n = s.size();
    auto byte = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
    while (i < n) {
        const unsigned char b = byte(i);
        char32_t cp = 0xFFFD;
        size_t len = 1;
        if (b < 0x80) {
            cp = b;
        } else if (b == 0xC0 && i + 1 < n && byte(i + 1) == 0x80) {
            cp = 0;  // modified UTF-8's NUL
            len = 2;
        } else if (b >= 0xC2 && b <= 0xDF && i + 1 < n && isCont(byte(i + 1))) {
            cp = ((b & 0x1Fu) << 6) | (byte(i + 1) & 0x3Fu);
            len = 2;
        } else if (b >= 0xE0 && b <= 0xEF && i + 2 < n && isCont(byte(i + 1)) && isCont(byte(i + 2))) {
            const char32_t v = ((b & 0x0Fu) << 12) | ((byte(i + 1) & 0x3Fu) << 6) | (byte(i + 2) & 0x3Fu);
            // Overlong three-byte forms are not valid; surrogates are kept (modified UTF-8 writes them).
            if (v >= 0x800) {
                cp = v;
                len = 3;
            }
        } else if (b >= 0xF0 && b <= 0xF4 && i + 3 < n && isCont(byte(i + 1)) && isCont(byte(i + 2)) && isCont(byte(i + 3))) {
            const char32_t v = ((b & 0x07u) << 18) | ((byte(i + 1) & 0x3Fu) << 12) | ((byte(i + 2) & 0x3Fu) << 6) | (byte(i + 3) & 0x3Fu);
            if (v >= 0x10000 && v <= 0x10FFFF) {
                cp = v;
                len = 4;
            }
        }
        out.push_back(cp);
        i += len;
    }
    // Modified UTF-8 writes a supplementary character as two surrogates: join a high one and a low one next to it.
    std::vector<char32_t> joined;
    for (size_t k = 0; k < out.size(); ++k) {
        if (isHigh(out[k]) && k + 1 < out.size() && isLow(out[k + 1])) {
            joined.push_back(0x10000 + ((out[k] - 0xD800) << 10) + (out[k + 1] - 0xDC00));
            ++k;
        } else {
            joined.push_back(out[k]);
        }
    }
    return joined;
}

void append(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

bool isWhitespace(char32_t c) {
    if ((c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x1F)) return true;
    switch (c) {
        case 0x0020:
        case 0x00A0:
        case 0x1680:
        case 0x2028:
        case 0x2029:
        case 0x202F:
        case 0x205F:
        case 0x3000: return true;
        default: return c >= 0x2000 && c <= 0x200A;
    }
}

bool isBlank(std::string_view utf8) {
    for (const char32_t cp : decode(utf8))
        if (!isWhitespace(cp)) return false;
    return true;
}

}  // namespace geode::shell::text
