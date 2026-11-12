#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace geode::viz::frame::link::pair {

// Show on a computer's typed path (owner: "Qr with link as pc does not have scanner"). The QR holds the whole
// link, token and all, for a device that scans. A computer instead types a short link with no token in it
// (LinkShare's pairUrl, the page at "/") and then the code the phone's sheet shows; only a right code is answered
// with the token's path, so no stream byte reaches a browser that has not shown the code.
//
// The code: kCodeChars symbols of Crockford's base32 (no I, L, O or U; typed case-free, O read as 0, I and L
// as 1, dashes and spaces ignored), kCodeBytes random bytes from the OS, shown as two groups of four. It is
// single-session: a right code is spent at once and a new one shown. A code lives kCodeLifeMs, then a new one
// replaces it. Guessing is held back three ways: kTriesPerCode wrong tries spend a code; a try within kGapMs
// of the last wrong one is not compared at all; kSpentBeforeLock codes spent by wrong tries lock every try out
// for kLockMs. The compare takes the same time whatever was typed. Pure; core/tests/link_pair_test.cpp.
//
// What it does not do: the typed code, like the rest of the page, crosses plain http (LinkShare's note), so it
// keeps out whoever has only the short link, not someone reading the Wi-Fi's traffic.

inline constexpr int kCodeChars = 8;
inline constexpr int kCodeBytes = 5;   // 40 bits, 5 per symbol
inline constexpr int64_t kCodeLifeMs = 5 * 60'000;
inline constexpr int kTriesPerCode = 5;
inline constexpr int64_t kGapMs = 1'000;
inline constexpr int kSpentBeforeLock = 3;
inline constexpr int64_t kLockMs = 60'000;
// The longest body a POST on "/pair" may carry: the code typed with room for spaces and dashes.
inline constexpr int64_t kBodyMax = 64;

// HTTP statuses of a try: the token's path follows a 200.
enum Result : int { kPaired = 200, kWrong = 403, kClosed = 404, kThrottled = 429 };

// kCodeChars symbols from kCodeBytes bytes (5 bits each, high bits first).
std::string codeFrom(const uint8_t* bytes);

// The code as the sheet shows it: "ABCD-EFGH".
std::string display(std::string_view code);

// What was typed, read into kCodeChars symbols; false when it is not exactly kCodeChars of them.
bool normalize(std::string_view typed, char* out);

// typed matches code, in the same time whatever typed holds (its length and where it differs included).
bool codeMatches(std::string_view typed, std::string_view code);

// Fills n bytes from a secure source; false when it cannot.
using Random = std::function<bool(uint8_t*, size_t)>;

class Pairing {
   public:
    explicit Pairing(Random random) : random_(std::move(random)) {}

    // Opens with a new code (a no-op while open); closes and forgets the code.
    void open(int64_t nowMs);
    void close();
    bool isOpen() const { return open_; }

    // The code to show now ("" while closed), a new one once the last has lived kCodeLifeMs.
    std::string code(int64_t nowMs);

    // One typed try: a Result.
    int attempt(std::string_view typed, int64_t nowMs);

   private:
    void issue(int64_t nowMs);

    Random random_;
    bool open_ = false;
    std::string code_;
    int64_t issuedMs_ = 0;
    int wrong_ = 0;
    int spent_ = 0;
    bool failed_ = false;
    int64_t lastWrongMs_ = 0;
    int64_t lockedUntilMs_ = 0;
};

}  // namespace geode::viz::frame::link::pair
