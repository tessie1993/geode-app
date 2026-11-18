#include "viz/MilkTextures.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>
#include <utility>

#include "viz/Sha256.hpp"

namespace geode::viz::milk {

namespace {

namespace fs = std::filesystem;

// --- UTF-8 ---------------------------------------------------------------------------------------------------

struct CodePoint {
    uint32_t value;
    size_t length;
    bool valid;
};

// A malformed byte is one U+FFFD, as java.lang.String decodes it.
CodePoint decode(const std::string& s, size_t i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    if (b0 < 0x80) return {b0, 1, true};
    size_t n = 0;
    uint32_t v = 0;
    if ((b0 & 0xE0) == 0xC0) {
        n = 2;
        v = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
        n = 3;
        v = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
        n = 4;
        v = b0 & 0x07;
    } else {
        return {0xFFFD, 1, false};
    }
    if (i + n > s.size()) return {0xFFFD, 1, false};
    for (size_t k = 1; k < n; ++k) {
        const auto c = static_cast<unsigned char>(s[i + k]);
        if ((c & 0xC0) != 0x80) return {0xFFFD, 1, false};
        v = (v << 6) | (c & 0x3F);
    }
    const bool overlong = (n == 2 && v < 0x80) || (n == 3 && v < 0x800) || (n == 4 && v < 0x10000);
    if (overlong || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return {0xFFFD, 1, false};
    return {v, n, true};
}

std::vector<uint32_t> codePoints(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) {
        const CodePoint cp = decode(s, i);
        out.push_back(cp.value);
        i += cp.length;
    }
    return out;
}

void append(std::string& out, uint32_t cp) {
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

size_t utf16Units(uint32_t cp) { return cp >= 0x10000 ? 2 : 1; }

size_t utf16Length(const std::string& s) {
    size_t n = 0;
    for (const uint32_t cp : codePoints(s)) n += utf16Units(cp);
    return n;
}

// java.lang.String order: UTF-16 code units, so a supplementary character sorts below U+E000..U+FFFF.
bool utf16Less(const std::string& a, const std::string& b) {
    const auto unitsOf = [](const std::string& s) {
        std::vector<uint32_t> units;
        for (const uint32_t cp : codePoints(s)) {
            if (cp >= 0x10000) {
                units.push_back(0xD800 + ((cp - 0x10000) >> 10));
                units.push_back(0xDC00 + ((cp - 0x10000) & 0x3FF));
            } else {
                units.push_back(cp);
            }
        }
        return units;
    };
    const std::vector<uint32_t> x = unitsOf(a);
    const std::vector<uint32_t> y = unitsOf(b);
    return std::lexicographical_compare(x.begin(), x.end(), y.begin(), y.end());
}

// --- case ----------------------------------------------------------------------------------------------------

// Character.toLowerCase for ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic, and the two signs that
// lower into other blocks; anything else is its own lowercase.
uint32_t lowerOf(uint32_t c) {
    if (c < 0x80) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c == 0x130) return 'i';
    if (c >= 0x100 && c <= 0x137 && c != 0x131) return (c % 2 == 0) ? c + 1 : c;
    if (c >= 0x139 && c <= 0x148) return (c % 2 == 1) ? c + 1 : c;
    if (c >= 0x14A && c <= 0x177) return (c % 2 == 0) ? c + 1 : c;
    if (c == 0x178) return 0xFF;
    if (c >= 0x179 && c <= 0x17E) return (c % 2 == 1) ? c + 1 : c;
    if (c == 0x386) return 0x3AC;
    if (c >= 0x388 && c <= 0x38A) return c + 37;
    if (c == 0x38C) return 0x3CC;
    if (c == 0x38E || c == 0x38F) return c + 63;
    if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c == 0x212A) return 'k';
    if (c == 0x212B) return 0xE5;
    return c;
}

// equals(ignoreCase): two characters match when their uppercase does, or that uppercase's lowercase does.
uint32_t foldOf(uint32_t c) {
    switch (c) {
        case 0x131: return 'i';    // dotless i uppercases to I
        case 0x17F: return 's';    // long s uppercases to S
        case 0xB5: return 0x3BC;   // micro sign uppercases to Greek capital mu
        case 0x3C2: return 0x3C3;  // final sigma uppercases to capital sigma
        default: return lowerOf(c);
    }
}

// String.lowercase(): lowerOf, except that the capital I with a dot keeps its dot as a combining mark.
std::string lowered(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const CodePoint cp = decode(s, i);
        if (!cp.valid) {
            out += s[i];
        } else if (cp.value == 0x130) {
            out += "i\xCC\x87";
        } else {
            append(out, lowerOf(cp.value));
        }
        i += cp.length;
    }
    return out;
}

bool equalsIgnoreCase(const std::string& a, const std::string& b) {
    const std::vector<uint32_t> x = codePoints(a);
    const std::vector<uint32_t> y = codePoints(b);
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); ++i) {
        if (x[i] != y[i] && foldOf(x[i]) != foldOf(y[i])) return false;
    }
    return true;
}

// --- character classes ---------------------------------------------------------------------------------------

bool asciiAlnum(uint32_t c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }

bool in(uint32_t c, uint32_t lo, uint32_t hi) { return c >= lo && c <= hi; }

// Character.isLetterOrDigit beyond ASCII: false for the punctuation, symbol, mark, number-form, space, private
// use and emoji blocks, true for the rest (the scripts' letters and digits).
bool letterOrDigitBeyondAscii(uint32_t c) {
    if (in(c, 0x80, 0xBF)) return c == 0xAA || c == 0xB5 || c == 0xBA;
    if (c == 0xD7 || c == 0xF7) return false;
    if (in(c, 0x2C2, 0x2C5) || in(c, 0x2D2, 0x2DF) || in(c, 0x2E5, 0x2EB) || c == 0x2ED || in(c, 0x2EF, 0x36F)) return false;
    if (c == 0x375 || c == 0x37E || c == 0x384 || c == 0x385 || c == 0x387 || in(c, 0x483, 0x489)) return false;
    if (in(c, 0x2000, 0x206F)) return false;
    if (in(c, 0x2070, 0x209F)) return c == 0x2071 || c == 0x207F || in(c, 0x2090, 0x209C);
    if (in(c, 0x20A0, 0x20FF)) return false;
    if (in(c, 0x2100, 0x214F)) {
        constexpr std::pair<uint32_t, uint32_t> kSymbols[] = {
            {0x2100, 0x2101}, {0x2103, 0x2106}, {0x2108, 0x2109}, {0x2114, 0x2114}, {0x2116, 0x2118},
            {0x211E, 0x2123}, {0x2125, 0x2125}, {0x2127, 0x2127}, {0x2129, 0x2129}, {0x212E, 0x212E},
            {0x213A, 0x213B}, {0x2140, 0x2144}, {0x214A, 0x214D}, {0x214F, 0x214F}};
        for (const auto& [lo, hi] : kSymbols) {
            if (in(c, lo, hi)) return false;
        }
        return true;
    }
    if (in(c, 0x2150, 0x218F)) return c == 0x2183 || c == 0x2184;
    if (in(c, 0x2190, 0x2BFF) || in(c, 0x2E00, 0x2E7F)) return false;
    if (in(c, 0x3000, 0x3004) || in(c, 0x3007, 0x3030) || in(c, 0x3036, 0x3037) || in(c, 0x303D, 0x303F)) return false;
    if (c == 0x309B || c == 0x309C || c == 0x30A0 || c == 0x30FB || in(c, 0x3099, 0x309A)) return false;
    if (in(c, 0xD800, 0xF8FF) || in(c, 0xFD3E, 0xFD3F) || in(c, 0xFE00, 0xFE6F) || c == 0xFEFF) return false;
    if (in(c, 0xFF01, 0xFF0F) || in(c, 0xFF1A, 0xFF20) || in(c, 0xFF3B, 0xFF40) || in(c, 0xFF5B, 0xFF65)) return false;
    if (in(c, 0xFFE0, 0xFFFF)) return false;
    if (in(c, 0x1F000, 0x1FBFF) || c >= 0xE0000) return false;
    return true;
}

bool letterOrDigit(uint32_t c) { return c < 0x80 ? asciiAlnum(c) : letterOrDigitBeyondAscii(c); }

// \w as Android's regex reads it: letters, digits, marks, connector punctuation and the joiners.
bool wordChar(uint32_t c) {
    if (c < 0x80) return asciiAlnum(c) || c == '_';
    if (letterOrDigitBeyondAscii(c)) return true;
    return in(c, 0x300, 0x36F) || in(c, 0x483, 0x489) || in(c, 0x20D0, 0x20FF) || in(c, 0xFE20, 0xFE2F) ||
           in(c, 0x302A, 0x302F) || in(c, 0x3099, 0x309A) || c == 0x200C || c == 0x200D || c == 0x203F ||
           c == 0x2040 || c == 0x2054 || c == 0xFE33 || c == 0xFE34 || in(c, 0xFE4D, 0xFE4F) || c == 0xFF3F;
}

// Whether the code point that ends just before byte i is a word character.
bool wordBefore(const std::string& s, size_t i) {
    if (i == 0) return false;
    size_t start = i - 1;
    while (start > 0 && i - start < 4 && (static_cast<unsigned char>(s[start]) & 0xC0) == 0x80) --start;
    const CodePoint cp = decode(s, start);
    return start + cp.length == i && cp.valid && wordChar(cp.value);
}

// The end of the run of word characters from byte i.
size_t wordRunEnd(const std::string& s, size_t i) {
    while (i < s.size()) {
        const CodePoint cp = decode(s, i);
        if (!cp.valid || !wordChar(cp.value)) break;
        i += cp.length;
    }
    return i;
}

bool isBlank(const std::string& s) {
    for (const uint32_t c : codePoints(s)) {
        const bool space = c == ' ' || in(c, 0x09, 0x0D) || in(c, 0x1C, 0x1F) || c == 0xA0 || c == 0x1680 ||
                           in(c, 0x2000, 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F ||
                           c == 0x3000;
        if (!space) return false;
    }
    return true;
}

// --- names ---------------------------------------------------------------------------------------------------

std::string afterLastDot(const std::string& name) {
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? std::string() : name.substr(dot + 1);
}

bool startsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

bool isBuiltinSampler(const std::string& lower) {
    static const std::set<std::string> kBuiltins = {"main",     "blur1",    "blur2",         "blur3",       "noise_lq",
                                                    "noise_lq_lite", "noise_mq", "noise_hq", "noisevol_lq", "noisevol_hq"};
    return kBuiltins.count(lower) > 0;
}

// Every `sampler(_fw|_fc|_pw|_pc)?_<name>` in text, in order, before any filtering.
std::vector<std::string> samplerMatches(const std::string& text) {
    static constexpr char kSampler[] = "sampler";
    static constexpr size_t kSamplerLength = sizeof(kSampler) - 1;
    std::vector<std::string> out;
    size_t i = 0;
    while ((i = text.find(kSampler, i)) != std::string::npos) {
        if (wordBefore(text, i)) {
            ++i;
            continue;
        }
        const size_t at = i + kSamplerLength;
        size_t nameAt = std::string::npos;
        if (at < text.size() && text[at] == '_') {
            const std::string mode = text.substr(at + 1, 2);
            const bool filtered = mode == "fw" || mode == "fc" || mode == "pw" || mode == "pc";
            if (filtered && at + 3 < text.size() && text[at + 3] == '_' && wordRunEnd(text, at + 4) > at + 4) {
                nameAt = at + 4;
            } else if (wordRunEnd(text, at + 1) > at + 1) {
                nameAt = at + 1;
            }
        }
        if (nameAt == std::string::npos) {
            ++i;
            continue;
        }
        const size_t end = wordRunEnd(text, nameAt);
        out.push_back(text.substr(nameAt, end - nameAt));
        i = end;
    }
    return out;
}

size_t commonPrefix(const std::string& a, const std::string& b) {
    const std::vector<uint32_t> x = codePoints(a);
    const std::vector<uint32_t> y = codePoints(b);
    size_t units = 0;
    for (size_t i = 0; i < x.size() && i < y.size() && x[i] == y[i]; ++i) units += utf16Units(x[i]);
    return units;
}

std::vector<std::string> linkNamesFor(const std::string& expected, const std::string& texture) {
    const std::string ext = afterLastDot(texture);
    std::vector<std::string> names{expected + "." + ext};
    const std::string lower = lowered(expected) + "." + lowered(ext);
    if (lower != names.front()) names.push_back(lower);
    return names;
}

}  // namespace

std::string extensionOf(const std::string& name) { return lowered(afterLastDot(name)); }

std::string stemOf(const std::string& name) {
    const size_t dot = name.rfind('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

bool isStoreTexture(const std::string& name) {
    static const std::set<std::string> kExtensions = {"png", "jpg", "jpeg", "bmp", "tga", "dds", "dib"};
    return kExtensions.count(extensionOf(name)) > 0;
}

bool admitsTexture(const std::string& name, int64_t declaredBytes) {
    return isStoreTexture(name) && declaredBytes <= kTextureMaxBytes;
}

bool fitsTexture(int64_t width, int64_t height) {
    return width >= 1 && width <= kTextureMaxSide && height >= 1 && height <= kTextureMaxSide;
}

bool isTextureImage(const std::string& name, const uint8_t* bytes, size_t length, int width, int height) {
    const auto le16 = [bytes](size_t at) { return static_cast<int64_t>(bytes[at] | (bytes[at + 1] << 8)); };
    const auto le32 = [bytes](size_t at) {
        return static_cast<int64_t>(static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8) |
                                    (static_cast<uint32_t>(bytes[at + 2]) << 16) |
                                    (static_cast<uint32_t>(bytes[at + 3]) << 24));
    };
    const std::string ext = extensionOf(name);
    if (ext == "dds") {
        // The magic, then a 124-byte header whose height and width follow its size and flags.
        constexpr size_t kHeader = 128;
        if (!bytes || length < kHeader) return false;
        if (bytes[0] != 'D' || bytes[1] != 'D' || bytes[2] != 'S' || bytes[3] != ' ') return false;
        // A DDS height past 2^31 was negative to the Kotlin reader; it fails the side check either way.
        return fitsTexture(le32(16), le32(12));
    }
    if (ext == "tga") {
        // Width and height close the 18-byte header's image specification.
        constexpr size_t kHeader = 18;
        if (!bytes || length < kHeader) return false;
        const int colorMapType = bytes[1];
        const int imageType = bytes[2];
        const int pixelDepth = bytes[16];
        const bool knownType = imageType == 1 || imageType == 2 || imageType == 3 || imageType == 9 ||
                               imageType == 10 || imageType == 11;
        const bool knownDepth =
            pixelDepth == 8 || pixelDepth == 15 || pixelDepth == 16 || pixelDepth == 24 || pixelDepth == 32;
        return colorMapType <= 1 && knownType && knownDepth && fitsTexture(le16(12), le16(14));
    }
    return fitsTexture(width, height);
}

std::string storedTextureName(const std::string& name) {
    const std::string rawBase = stemOf(name);
    const std::string ext = extensionOf(name);
    std::string base;
    for (size_t i = 0; i < rawBase.size();) {
        const CodePoint cp = decode(rawBase, i);
        base += asciiAlnum(cp.value) ? static_cast<char>(cp.value) : '_';
        i += cp.length;
    }
    std::string safeBase = base;
    if (!base.empty() && std::isdigit(static_cast<unsigned char>(base.front()))) {
        safeBase = "t" + base;
    } else if (base.empty()) {
        safeBase = "tex";
    }
    if (safeBase == rawBase) return safeBase + "." + ext;
    Sha256 sha;
    sha.update(rawBase.data(), rawBase.size());
    return safeBase + "_" + Sha256::hex(sha.digest(), 4) + "." + ext;
}

std::string acceptedTextureName(const std::string& name, const uint8_t* bytes, size_t length, int width, int height) {
    if (!isStoreTexture(name) || !bytes || length == 0) return {};
    if (!isTextureImage(name, bytes, length, width, height)) return {};
    return storedTextureName(name);
}

std::string showPresetName(const std::string& texture) {
    std::string flat = texture;
    std::replace(flat.begin(), flat.end(), '.', '_');
    return "show_" + flat + ".milk";
}

std::string showPresetText(const std::string& texture) {
    const std::string base = stemOf(texture);
    return "MILKDROP_PRESET_VERSION=201\n"
           "PSVERSION=2\n"
           "PSVERSION_WARP=2\n"
           "PSVERSION_COMP=2\n"
           "[preset00]\n"
           "fRating=3.0\n"
           "fGammaAdj=1.6\n"
           "fDecay=0.97\n"
           "fVideoEchoZoom=1.0\n"
           "fVideoEchoAlpha=0.0\n"
           "nWaveMode=7\n"
           "bAdditiveWaves=0\n"
           "fWaveAlpha=0.001\n"
           "zoom=1.0\n"
           "rot=0.0\n"
           "cx=0.5\n"
           "cy=0.5\n"
           "warp=0.0\n"
           "sx=1.0\n"
           "sy=1.0\n"
           "ob_size=0.0\n"
           "ib_size=0.0\n"
           "per_frame_1=wave_a = 0;\n"
           "comp_1=`sampler sampler_" + base + ";\n"
           "comp_2=`shader_body\n"
           "comp_3=`{\n"
           "comp_4=`   float2 uv2 = (uv - 0.5) / (1.0 + 0.10 * bass) + 0.5;\n"
           "comp_5=`   float3 img = tex2D(sampler_" + base + ", uv2).xyz;\n"
           "comp_6=`   float pulse = 0.75 + 0.45 * treb + 0.30 * bass;\n"
           "comp_7=`   ret = img * pulse;\n"
           "comp_8=`}";
}

std::vector<std::string> removedPresetNames(const std::string& texture, const std::vector<std::string>& remaining) {
    const std::string generated = showPresetName(texture);
    const std::string stem = stemOf(texture);
    const std::string legacy = "show_" + stem + ".milk";
    std::vector<std::string> out{generated};
    const bool shared = std::any_of(remaining.begin(), remaining.end(), [&stem](const std::string& r) { return stemOf(r) == stem; });
    if (legacy != generated && !shared) out.push_back(legacy);
    return out;
}

std::vector<std::string> samplerReferences(const std::string& presetText) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const std::string& name : samplerMatches(presetText)) {
        const std::string lower = lowered(name);
        if (!seen.insert(lower).second) continue;
        if (isBuiltinSampler(lower) || startsWith(lower, "rand")) continue;
        out.push_back(name);
    }
    return out;
}

std::string normalized(const std::string& name) {
    std::string out;
    const std::string lower = lowered(name);
    for (size_t i = 0; i < lower.size();) {
        const CodePoint cp = decode(lower, i);
        if (cp.valid && letterOrDigit(cp.value)) out.append(lower, i, cp.length);
        i += cp.length;
    }
    return out;
}

std::string withoutRenameSuffix(const std::string& stem) {
    constexpr size_t kSuffix = 9;
    if (stem.size() < kSuffix || stem[stem.size() - kSuffix] != '_') return stem;
    for (size_t i = stem.size() - kSuffix + 1; i < stem.size(); ++i) {
        const char c = stem[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return stem;
    }
    return stem.substr(0, stem.size() - kSuffix);
}

std::vector<std::string> textureOrder(std::vector<std::string> names) {
    std::vector<std::pair<std::string, std::string>> keyed;
    keyed.reserve(names.size());
    for (std::string& n : names) keyed.emplace_back(lowered(n), std::move(n));
    std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return utf16Less(a.first, b.first); });
    std::vector<std::string> out;
    out.reserve(keyed.size());
    for (auto& k : keyed) out.push_back(std::move(k.second));
    return out;
}

Link resolve(const std::string& expected, const std::vector<std::string>& stored, const Overrides& overrides) {
    Link link;
    link.expected = expected;
    const auto chosen = overrides.find(lowered(expected));
    // A pinned texture that has since been deleted falls through to the ladder rather than resolving to a
    // broken link that looks deliberate.
    if (chosen != overrides.end() && !chosen->second.empty() &&
        std::find(stored.begin(), stored.end(), chosen->second) != stored.end()) {
        link.texture = chosen->second;
        link.kind = LinkKind::Overridden;
    } else if (stored.empty()) {
        link.kind = LinkKind::Missing;
        return link;
    } else {
        const auto exact = std::find_if(stored.begin(), stored.end(),
                                        [&expected](const std::string& s) { return equalsIgnoreCase(stemOf(s), expected); });
        const std::string wanted = normalized(expected);
        if (exact != stored.end()) {
            link.texture = *exact;
            link.kind = LinkKind::Matched;
        } else if (const auto renamed = std::find_if(stored.begin(), stored.end(),
                                                     [&wanted](const std::string& s) {
                                                         return normalized(withoutRenameSuffix(stemOf(s))) == wanted;
                                                     });
                   renamed != stored.end()) {
            link.texture = *renamed;
            link.kind = LinkKind::Renamed;
        } else {
            // The closest-named texture; on a tie the shorter name, then the earlier one.
            size_t best = 0;
            size_t bestPrefix = commonPrefix(normalized(stemOf(stored[0])), wanted);
            size_t bestLength = utf16Length(stored[0]);
            for (size_t i = 1; i < stored.size(); ++i) {
                const size_t prefix = commonPrefix(normalized(stemOf(stored[i])), wanted);
                const size_t length = utf16Length(stored[i]);
                if (prefix > bestPrefix || (prefix == bestPrefix && length < bestLength)) {
                    best = i;
                    bestPrefix = prefix;
                    bestLength = length;
                }
            }
            link.texture = stored[best];
            link.kind = LinkKind::Substituted;
        }
    }
    link.linkNames = linkNamesFor(expected, link.texture);
    return link;
}

std::vector<Link> resolveAll(const std::string& presetText, const std::vector<std::string>& stored, const Overrides& overrides) {
    std::vector<Link> out;
    for (const std::string& expected : samplerReferences(presetText)) out.push_back(resolve(expected, stored, overrides));
    return out;
}

bool needsYou(const std::vector<Link>& links) {
    return std::any_of(links.begin(), links.end(),
                       [](const Link& l) { return l.kind == LinkKind::Substituted || l.kind == LinkKind::Missing; });
}

bool missesTexture(const std::string& presetText, const std::vector<std::string>& entries) {
    std::set<std::string> available;
    for (const std::string& e : entries) available.insert(lowered(stemOf(e)));
    const std::vector<std::string> refs = samplerReferences(presetText);
    return std::any_of(refs.begin(), refs.end(), [&available](const std::string& r) { return available.count(lowered(r)) == 0; });
}

PackEntry packEntryKind(const std::string& name) {
    static const std::set<std::string> kPackTextures = {"png", "jpg", "jpeg", "bmp", "tga", "dds"};
    const std::string ext = extensionOf(name);
    if (ext == "milk") return PackEntry::Preset;
    return kPackTextures.count(ext) > 0 ? PackEntry::Texture : PackEntry::None;
}

std::string packTextureName(const std::string& name) {
    const size_t slash = name.rfind('/');
    return slash == std::string::npos ? name : name.substr(slash + 1);
}

std::string importDisplayName(const std::string& display) { return isBlank(display) ? std::string("preset") : display; }

std::string importFileName(const std::string& milkFileName, bool keepExisting,
                           const std::function<bool(const std::string&)>& taken) {
    static constexpr char kSuffix[] = ".milk";
    static constexpr size_t kSuffixLength = sizeof(kSuffix) - 1;
    std::string stem = milkFileName;
    if (stem.size() >= kSuffixLength && stem.compare(stem.size() - kSuffixLength, kSuffixLength, kSuffix) == 0) {
        stem.resize(stem.size() - kSuffixLength);
    }
    constexpr int kMostTries = 1000000;
    std::string name = stem + kSuffix;
    for (int n = 1; n <= kMostTries; ++n) {
        name = n == 1 ? stem + kSuffix : stem + " " + std::to_string(n) + kSuffix;
        if (!keepExisting || !taken(name)) return name;
    }
    return name;
}

std::vector<std::string> listEntries(const std::string& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        out.push_back(it->path().filename().string());
    }
    return out;
}

std::vector<std::string> listStoreTextures(const std::string& dir) {
    std::vector<std::string> out;
    for (std::string& name : listEntries(dir)) {
        std::error_code ec;
        if (isStoreTexture(name) && fs::is_regular_file(fs::path(dir) / name, ec)) out.push_back(std::move(name));
    }
    return textureOrder(std::move(out));
}

std::string readFile(const std::string& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return {};
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::string> linkedPresets(const std::string& milkDir) {
    std::vector<std::string> out;
    for (std::string& name : listEntries(milkDir)) {
        if (afterLastDot(name) == "milk") out.push_back(std::move(name));
    }
    return out;
}

std::vector<std::string> staleLinkDirs(const std::string& milkDir) {
    std::set<std::string> live;
    for (const std::string& preset : linkedPresets(milkDir)) live.insert(stemOf(preset));
    const fs::path root = fs::path(milkDir) / "textures" / ".links";
    std::vector<std::string> out;
    for (std::string& name : listEntries(root.string())) {
        std::error_code ec;
        if (live.count(name) == 0 && fs::is_directory(root / name, ec)) out.push_back(std::move(name));
    }
    return out;
}

}  // namespace geode::viz::milk
