#include "viz/frame/LinkPair.hpp"

namespace geode::viz::frame::link::pair {

namespace {

constexpr char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

// A typed character as a code symbol, 0 for a separator to skip, -1 for anything else.
int symbol(char c) {
    if (c == '-' || c == ' ') return 0;
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    if (c == 'O') c = '0';
    if (c == 'I' || c == 'L') c = '1';
    for (const char* a = kAlphabet; *a; ++a)
        if (*a == c) return c;
    return -1;
}

}  // namespace

std::string codeFrom(const uint8_t* bytes) {
    uint64_t bits = 0;
    for (int i = 0; i < kCodeBytes; ++i) bits = (bits << 8) | bytes[i];
    std::string code(kCodeChars, '0');
    for (int i = 0; i < kCodeChars; ++i) code[i] = kAlphabet[(bits >> (5 * (kCodeChars - 1 - i))) & 31u];
    return code;
}

std::string display(std::string_view code) {
    if (code.size() != static_cast<size_t>(kCodeChars)) return std::string(code);
    std::string out(code.substr(0, kCodeChars / 2));
    out += '-';
    out += code.substr(kCodeChars / 2);
    return out;
}

bool normalize(std::string_view typed, char* out) {
    int n = 0;
    for (char c : typed) {
        const int s = symbol(c);
        if (s < 0) return false;
        if (s == 0) continue;
        if (n == kCodeChars) return false;
        out[n++] = static_cast<char>(s);
    }
    return n == kCodeChars;
}

bool codeMatches(std::string_view typed, std::string_view code) {
    char got[kCodeChars] = {};
    const bool read = normalize(typed, got);
    unsigned diff = code.size() == static_cast<size_t>(kCodeChars) ? 0u : 1u;
    for (int i = 0; i < kCodeChars; ++i) {
        const unsigned want = i < static_cast<int>(code.size()) ? static_cast<unsigned char>(code[i]) : 0u;
        diff |= static_cast<unsigned char>(got[i]) ^ want;
    }
    return read && diff == 0;
}

void Pairing::issue(int64_t nowMs) {
    uint8_t bytes[kCodeBytes];
    if (!random_ || !random_(bytes, sizeof bytes)) {
        close();
        return;
    }
    code_ = codeFrom(bytes);
    issuedMs_ = nowMs;
    wrong_ = 0;
}

void Pairing::open(int64_t nowMs) {
    if (open_) return;
    open_ = true;
    issue(nowMs);
}

void Pairing::close() {
    open_ = false;
    code_.assign(code_.size(), '\0');
    code_.clear();
    wrong_ = 0;
    spent_ = 0;
    failed_ = false;
    lastWrongMs_ = 0;
    lockedUntilMs_ = 0;
}

std::string Pairing::code(int64_t nowMs) {
    if (!open_) return {};
    if (nowMs - issuedMs_ >= kCodeLifeMs) issue(nowMs);
    return open_ ? code_ : std::string();
}

int Pairing::attempt(std::string_view typed, int64_t nowMs) {
    if (!open_) return kClosed;
    if (nowMs < lockedUntilMs_) return kThrottled;
    if (failed_ && nowMs - lastWrongMs_ < kGapMs) return kThrottled;
    failed_ = true;
    lastWrongMs_ = nowMs;
    if (nowMs - issuedMs_ >= kCodeLifeMs) {
        // The code on the sheet has already moved on: a new one, and this try is not counted against it.
        issue(nowMs);
        return open_ ? kWrong : kClosed;
    }
    if (codeMatches(typed, code_)) {
        failed_ = false;
        spent_ = 0;
        issue(nowMs);
        return kPaired;
    }
    if (++wrong_ >= kTriesPerCode) {
        issue(nowMs);
        if (++spent_ >= kSpentBeforeLock) {
            spent_ = 0;
            lockedUntilMs_ = nowMs + kLockMs;
        }
    }
    return open_ ? kWrong : kClosed;
}

}  // namespace geode::viz::frame::link::pair
