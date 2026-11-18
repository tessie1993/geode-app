#include "shell/OpenedAudio.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

#include "shell/Text.hpp"

namespace geode::shell {

namespace {

struct Range {
    char32_t lo;
    char32_t hi;
};

// Java's Character.getType in CONTROL, FORMAT, PRIVATE_USE, SURROGATE or UNASSIGNED, sorted.
constexpr Range kHidden[] = {
#include "shell/UnicodeHidden.inc"
};

bool isHidden(char32_t cp) {
    size_t lo = 0;
    size_t hi = sizeof kHidden / sizeof kHidden[0];
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (cp < kHidden[mid].lo)
            hi = mid;
        else if (cp > kHidden[mid].hi)
            lo = mid + 1;
        else
            return true;
    }
    return false;
}

// Kotlin's String.trim(): whitespace (Char.isWhitespace) off both ends, as code points.
std::vector<char32_t> trimmed(std::string_view utf8) {
    std::vector<char32_t> cps = text::decode(utf8);
    size_t a = 0;
    size_t b = cps.size();
    while (a < b && text::isWhitespace(cps[a])) ++a;
    while (b > a && text::isWhitespace(cps[b - 1])) --b;
    return std::vector<char32_t>(cps.begin() + static_cast<std::ptrdiff_t>(a), cps.begin() + static_cast<std::ptrdiff_t>(b));
}

// Kotlin's Char.equals(other, ignoreCase = true) against an ASCII letter: equal after uppercase, or after
// lowercase of that. The few non-ASCII chars that fold to these four letters are the ones the JVM folds.
bool foldsTo(char32_t c, char letter) {
    if (c == static_cast<char32_t>(letter) || c == static_cast<char32_t>(letter - 'a' + 'A')) return true;
    switch (letter) {
        case 'i': return c == 0x0131 || c == 0x0130;  // dotless i (upper-cases to I), dotted I (lower-cases to i)
        case 'k': return c == 0x212A;                 // Kelvin sign (lower-cases to k)
        default: return false;
    }
}

bool isMilkName(const std::optional<std::string>& segment) {
    if (!segment) return false;
    const std::vector<char32_t> t = trimmed(*segment);
    if (t.size() < 5) return false;
    const size_t n = t.size();
    return t[n - 5] == U'.' && foldsTo(t[n - 4], 'm') && foldsTo(t[n - 3], 'i') && foldsTo(t[n - 2], 'l') && foldsTo(t[n - 1], 'k');
}

bool isAudioType(const std::string& type) {
    const size_t semi = type.find(';');
    // Kotlin's trim then lowercase(); the only strings that can match are ASCII, and no non-ASCII char folds
    // to one of their letters, so folding ASCII is the whole of it.
    const std::vector<char32_t> t = trimmed(std::string_view(type).substr(0, semi));
    std::string base;
    for (char32_t c : t) {
        if (c >= U'A' && c <= U'Z') c = c - U'A' + U'a';
        text::append(base, c);
    }
    if (base.size() > 6 && base.compare(0, 6, "audio/") == 0) return true;
    return base == "application/ogg" || base == "application/x-flac";
}

std::string hex(std::uint8_t b) {
    static const char* const digits = "0123456789abcdef";
    return std::string{digits[b >> 4], digits[b & 15]};
}

}  // namespace

OpenedRoute openedAudioRoute(const std::optional<std::string>& action, const std::optional<std::string>& scheme,
                             const std::optional<std::string>& type, const std::optional<std::string>& lastSegment,
                             bool fromHistory) {
    if (action != std::optional<std::string>("android.intent.action.VIEW") || scheme != std::optional<std::string>("content"))
        return OpenedRoute::NotOurs;
    const bool audio = type ? isAudioType(*type) : !isMilkName(lastSegment);
    if (!audio) return OpenedRoute::NotOurs;
    return fromHistory ? OpenedRoute::Stale : OpenedRoute::Open;
}

bool openedAudioRefuses(const std::optional<std::string>& scheme, const std::optional<std::string>& authority,
                        const std::string& ownPackage, const std::optional<std::string>& providerPackage, bool readGranted) {
    if (scheme != std::optional<std::string>("content") || !authority || text::isBlank(*authority)) return true;
    const std::string& a = *authority;
    if (a == ownPackage || (a.size() > ownPackage.size() && a.compare(0, ownPackage.size(), ownPackage) == 0 && a[ownPackage.size()] == '.'))
        return true;
    if (!providerPackage || *providerPackage == ownPackage) return true;
    return !readGranted;
}

std::optional<std::string> openedAudioDisplayName(const std::optional<std::string>& raw) {
    if (!raw || raw->empty()) return std::nullopt;
    std::vector<char32_t> clean;
    int count = 0;
    bool spacePending = false;
    for (const char32_t cp : text::decode(*raw)) {
        if (count >= kOpenedNameMaxCodePoints) continue;
        if (text::isWhitespace(cp)) {
            spacePending = !clean.empty();
        } else if (isHidden(cp)) {
            continue;
        } else {
            if (spacePending) {
                clean.push_back(U' ');
                ++count;
                spacePending = false;
            }
            if (count < kOpenedNameMaxCodePoints) {
                clean.push_back(cp);
                ++count;
            }
        }
    }
    // name.substringAfterLast('.', "") is a file extension when it is one to five ASCII letters or digits.
    size_t stemEnd = clean.size();
    for (size_t i = clean.size(); i-- > 0;) {
        if (clean[i] != U'.') continue;
        const size_t len = clean.size() - i - 1;
        bool ext = len >= 1 && len <= 5;
        for (size_t k = i + 1; ext && k < clean.size(); ++k) {
            const char32_t c = clean[k];
            ext = (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= U'0' && c <= U'9');
        }
        if (ext) {
            stemEnd = i;
            while (stemEnd > 0 && clean[stemEnd - 1] == U' ') --stemEnd;  // trimEnd: only spaces can be left
        }
        break;
    }
    const size_t end = stemEnd == 0 ? clean.size() : stemEnd;  // stem.ifEmpty { name }
    if (end == 0) return std::nullopt;
    std::string out;
    for (size_t i = 0; i < end; ++i) text::append(out, clean[i]);
    return out;
}

std::string openedMediaId(const std::array<std::uint8_t, 16>& random) {
    std::array<std::uint8_t, 16> b = random;
    b[6] = static_cast<std::uint8_t>((b[6] & 0x0F) | 0x40);
    b[8] = static_cast<std::uint8_t>((b[8] & 0x3F) | 0x80);
    std::string id = kOpenedMediaIdPrefix;
    for (size_t i = 0; i < b.size(); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) id += '-';
        id += hex(b[i]);
    }
    return id;
}

bool openedAudioIsOpened(const std::optional<std::string>& mediaId) {
    const std::string prefix = kOpenedMediaIdPrefix;
    return mediaId && mediaId->compare(0, prefix.size(), prefix) == 0 && mediaId->size() >= prefix.size();
}

}  // namespace geode::shell
