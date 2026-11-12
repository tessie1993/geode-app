// Host-side checks for core/viz/frame/LinkPair and the typed path of LinkShare and LinkPage: a computer with no
// scanner opens the short link and types the code the phone shows. Built and run by CI ("Native host tests" in
// .github/workflows/native-host-tests.yml).
//
// What is pinned: the code (Crockford base32 from the OS's random bytes, typed case-free with separators, the
// compare's full loop); one code per session, spent by a right try; its lifetime; the tries a code takes, the gap
// after a wrong one and the lock-out; a closed pairing; the routes off the token (the page, its script, the style,
// POST /pair; nothing of the stream, the token's routes unchanged); their heads; the page's files; the short link
// and the token's path; and the C ABI on the OS's random.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <string_view>

#include "api/geode_api.h"
#include "viz/frame/LinkPage.hpp"
#include "viz/frame/LinkPair.hpp"
#include "viz/frame/LinkShare.hpp"

namespace share = geode::viz::frame::link;
namespace pairing = geode::viz::frame::link::pair;
using share::Resource;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

const std::string kToken = "0123456789abcdef0123456789abcdef";

// A stand-in for the OS's random: a counter, so every code is known in advance and differs from the last.
struct Counter {
    uint8_t next = 1;
    bool fail = false;
    bool operator()(uint8_t* out, size_t n) {
        if (fail) return false;
        for (size_t i = 0; i < n; ++i) out[i] = next++;
        return true;
    }
};

void testCode() {
    const uint8_t zeros[5] = {0, 0, 0, 0, 0};
    const uint8_t ones[5] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint8_t mixed[5] = {0x08, 0x86, 0x42, 0x98, 0xE8};
    check(pairing::codeFrom(zeros) == "00000000", "all-zero bytes");
    check(pairing::codeFrom(ones) == "ZZZZZZZZ", "all-one bytes");
    check(pairing::codeFrom(mixed) == "12345678", "5 bits a symbol, high bits first");
    check(pairing::display("ABCD2345") == "ABCD-2345", "shown as two groups of four");
    for (int b = 0; b < 256; ++b) {
        const uint8_t bytes[5] = {static_cast<uint8_t>(b), static_cast<uint8_t>(b * 7), static_cast<uint8_t>(b * 13),
                                  static_cast<uint8_t>(b * 29), static_cast<uint8_t>(b * 31)};
        const std::string c = pairing::codeFrom(bytes);
        bool clean = c.size() == static_cast<size_t>(pairing::kCodeChars);
        for (char ch : c) clean = clean && ch != 'I' && ch != 'L' && ch != 'O' && ch != 'U';
        check(clean, "eight symbols, never I, L, O or U");
        check(pairing::codeMatches(c, c) && pairing::codeMatches(pairing::display(c), c), "a code matches itself, dash or not");
    }

    char out[pairing::kCodeChars];
    check(pairing::normalize("abcd-2345", out) && std::memcmp(out, "ABCD2345", 8) == 0, "typed lowercase");
    check(pairing::normalize(" A B C D 2 3 4 5 ", out), "spaces skipped");
    check(pairing::normalize("oOiIlL00", out) && std::memcmp(out, "00111100", 8) == 0, "O read as 0, I and L as 1");
    check(!pairing::normalize("ABCD234", out) && !pairing::normalize("ABCD23456", out) && !pairing::normalize("", out),
          "exactly eight symbols");
    check(!pairing::normalize("ABCD-234U", out) && !pairing::normalize("ABCD_2345", out) && !pairing::normalize("ABCD\n2345", out),
          "anything else refuses the whole");
    check(!pairing::normalize("ABCD2345\xC3\xA9", out), "non-ASCII refuses it");

    check(!pairing::codeMatches("ABCD2346", "ABCD2345") && !pairing::codeMatches("BBCD2345", "ABCD2345"), "first or last differs");
    check(!pairing::codeMatches("", "ABCD2345") && !pairing::codeMatches("ABCD", "ABCD2345"), "short never matches");
    check(!pairing::codeMatches("", "") && !pairing::codeMatches("ABCD2345", ""), "no code never matches");
    check(!pairing::codeMatches("ABCD2345ABCD2345", "ABCD2345"), "a repeat never matches");
}

void testSession() {
    Counter random;
    pairing::Pairing p([&random](uint8_t* out, size_t n) { return random(out, n); });
    check(!p.isOpen() && p.code(0).empty(), "closed: no code");
    check(p.attempt("00000000", 0) == pairing::kClosed, "closed: 404");
    p.open(1'000);
    const std::string first = p.code(1'000);
    check(p.isOpen() && first.size() == static_cast<size_t>(pairing::kCodeChars), "open: a code");
    p.open(2'000);
    check(p.code(2'000) == first, "opening again keeps the code");

    check(p.attempt(pairing::display(first), 3'000) == pairing::kPaired, "the right code pairs");
    const std::string second = p.code(3'000);
    check(!second.empty() && second != first, "a right code is spent at once: a new one shows");
    check(p.attempt(first, 5'000) == pairing::kWrong, "the spent code no longer pairs");

    // Lifetime: the code moves on, and the sheet's new one pairs.
    pairing::Pairing q([&random](uint8_t* out, size_t n) { return random(out, n); });
    q.open(0);
    const std::string early = q.code(0);
    check(q.code(pairing::kCodeLifeMs - 1) == early, "a code lives its lifetime");
    const std::string later = q.code(pairing::kCodeLifeMs);
    check(later != early, "then a new one replaces it");
    check(q.attempt(early, pairing::kCodeLifeMs + 10) == pairing::kWrong, "the old one is wrong");
    check(q.attempt(later, pairing::kCodeLifeMs + 10 + pairing::kGapMs) == pairing::kPaired, "the new one pairs");

    // A try at an expired code, with no read of the code between: a new one, the try not counted.
    pairing::Pairing r([&random](uint8_t* out, size_t n) { return random(out, n); });
    r.open(0);
    const std::string stale = r.code(0);
    check(r.attempt(stale, pairing::kCodeLifeMs) == pairing::kWrong, "an expired code is wrong");
    check(r.code(pairing::kCodeLifeMs) != stale, "and replaced");

    p.close();
    check(!p.isOpen() && p.code(10'000).empty() && p.attempt(second, 10'000) == pairing::kClosed, "closed again: nothing pairs");
}

void testLimits() {
    Counter random;
    pairing::Pairing p([&random](uint8_t* out, size_t n) { return random(out, n); });
    p.open(0);
    const std::string code = p.code(0);
    int64_t t = 10'000;
    check(p.attempt("ZZZZZZZZ", t) == pairing::kWrong, "a wrong try: 403");
    check(p.attempt(code, t + pairing::kGapMs - 1) == pairing::kThrottled, "even the right code waits out the gap");
    check(p.code(t) == code, "a throttled try is not compared or counted");
    t += pairing::kGapMs;
    for (int i = 1; i < pairing::kTriesPerCode - 1; ++i, t += pairing::kGapMs) check(p.attempt("ZZZZZZZZ", t) == pairing::kWrong, "wrong");
    check(p.code(t) == code, "the code holds until its last try");
    check(p.attempt("ZZZZZZZZ", t) == pairing::kWrong, "the last wrong try");
    check(p.code(t) != code, "spends the code: a new one shows");
    t += pairing::kGapMs;
    check(p.attempt(code, t) == pairing::kWrong, "the spent code is wrong");

    // Codes spent by wrong tries lock every try out.
    pairing::Pairing q([&random](uint8_t* out, size_t n) { return random(out, n); });
    q.open(0);
    t = 1'000;
    for (int spent = 0; spent < pairing::kSpentBeforeLock; ++spent)
        for (int i = 0; i < pairing::kTriesPerCode; ++i, t += pairing::kGapMs) q.attempt("ZZZZZZZZ", t);
    const int64_t locked = t - pairing::kGapMs;
    const std::string now = q.code(t);
    check(q.attempt(now, t) == pairing::kThrottled, "locked: even the right code waits");
    check(q.attempt(now, locked + pairing::kLockMs - 1) == pairing::kThrottled, "for the whole lock");
    check(q.attempt(now, locked + pairing::kLockMs) == pairing::kPaired, "then the shown code pairs");

    // A right try resets the count of spent codes.
    pairing::Pairing r([&random](uint8_t* out, size_t n) { return random(out, n); });
    r.open(0);
    t = 1'000;
    for (int i = 0; i < pairing::kTriesPerCode; ++i, t += pairing::kGapMs) r.attempt("ZZZZZZZZ", t);
    check(r.attempt(r.code(t), t) == pairing::kPaired, "pairs after one spent code");
    t += pairing::kGapMs;
    for (int spent = 0; spent < pairing::kSpentBeforeLock - 1; ++spent)
        for (int i = 0; i < pairing::kTriesPerCode; ++i, t += pairing::kGapMs) r.attempt("ZZZZZZZZ", t);
    check(r.attempt(r.code(t), t) == pairing::kPaired, "not locked: the count started over");

    // Guessing at the fastest pace allowed: a bounded number of compares a minute.
    pairing::Pairing g([&random](uint8_t* out, size_t n) { return random(out, n); });
    g.open(0);
    int compared = 0;
    for (int64_t ms = 0; ms < 60'000; ms += 10)
        if (g.attempt("ZZZZZZZZ", ms) == pairing::kWrong) ++compared;
    check(compared <= pairing::kTriesPerCode * pairing::kSpentBeforeLock, "at most the tries before a lock in a minute");

    // No secure random: closed, never a guessable code.
    Counter broken;
    broken.fail = true;
    pairing::Pairing b([&broken](uint8_t* out, size_t n) { return broken(out, n); });
    b.open(0);
    check(!b.isOpen() && b.code(0).empty() && b.attempt("00000000", 0) == pairing::kClosed, "no random: closed");
    pairing::Pairing none(nullptr);
    none.open(0);
    check(!none.isOpen(), "no source at all: closed");
}

share::Route get(const std::string& target, const char* method = "GET") { return share::route(method, target, kToken); }

void testRoutes() {
    check(get("/").status == 200 && get("/").resource == Resource::PairPage, "the typed page at /");
    check(get("/?x=1").resource == Resource::PairPage && get("/#k").resource == Resource::PairPage, "a query or fragment ignored");
    check(get("/?" + kToken + "/").resource == Resource::PairPage, "a token in a query is not a path");
    check(get("/pair.js").status == 200 && get("/pair.js").resource == Resource::PairScript, "its script");
    check(get("/player.css").status == 200 && get("/player.css").resource == Resource::Style, "the shared style");
    check(get("/", "HEAD").status == 200 && get("/pair.js", "HEAD").status == 200, "HEAD reads");
    check(get("/pair", "POST").status == 200 && get("/pair", "POST").resource == Resource::Pair, "POST the code");
    check(get("/pair").status == 405 && get("/pair", "OPTIONS").status == 405 && get("/pair", "post").status == 405,
          "the code by POST alone");
    check(get("/", "POST").status == 405 && get("/pair.js", "OPTIONS").status == 405, "the page takes no POST or preflight");
    const char* offToken[] = {"/index.html", "/player.js", "/index.m3u8", "/live.m3u8", "/init.mp4", "/seg1.m4s",
                              "/stream.mp4", "/rtc", "/pair/", "/pair.js/", "/PAIR", "/pair.html", "/pairs", "//"};
    for (const char* t : offToken) check(get(t).status == 404 && get(t, "POST").status == 404, "nothing of the stream off the token");
    check(share::route("GET", "/", "").status == 404 && share::route("POST", "/pair", "ABC").status == 404,
          "no valid token: nothing at all");

    const std::string t = "/" + kToken;
    check(get(t + "/").resource == Resource::Page && get(t + "/stream.mp4").resource == Resource::Stream &&
              get(t + "/rtc", "POST").status == 200,
          "the token's routes are as before");
    check(get(t + "/pair", "POST").status == 404 && get(t + "/pair.js").status == 404, "the typed path is not under the token");
}

void testHeads() {
    const std::string page = share::responseHead(200, Resource::PairPage, 100, kToken);
    check(has(page, "Content-Type: text/html; charset=utf-8\r\n") && has(page, "Content-Security-Policy: default-src 'none';") &&
              has(page, "X-Frame-Options: DENY\r\n"),
          "the typed page: html under the page's CSP, not framed");
    check(!has(page, "Access-Control") && !has(page, kToken.c_str()), "no CORS, no token");
    check(has(share::responseHead(200, Resource::PairScript, 1, kToken), "Content-Type: text/javascript; charset=utf-8\r\n"),
          "script type");
    const std::string paired = share::responseHead(200, Resource::Pair, 34, kToken);
    check(has(paired, "Content-Type: text/plain; charset=utf-8\r\n") && has(paired, "Content-Length: 34\r\n") &&
              has(paired, "Cache-Control: no-store\r\n") && has(paired, "Referrer-Policy: no-referrer\r\n") &&
              !has(paired, "Access-Control"),
          "a right code: plain text, never cached, no CORS");
    const std::string wrong = share::responseHead(403, Resource::Pair, 0, kToken);
    check(wrong.rfind("HTTP/1.1 403 Forbidden\r\n", 0) == 0 && has(wrong, "Content-Length: 0\r\n") && !has(wrong, "\r\nContent-Type:"),
          "a wrong code: 403, no body");
    const std::string slow = share::responseHead(429, Resource::Pair, 0, kToken);
    check(slow.rfind("HTTP/1.1 429 Too Many Requests\r\n", 0) == 0 && has(slow, "Retry-After: 2\r\n"), "too soon: 429");
    check(has(share::responseHead(405, Resource::Pair, 0, kToken), "Allow: POST\r\n"), "405 on /pair: POST");
    check(has(share::responseHead(405, Resource::PairPage, 0, kToken), "Allow: GET, HEAD\r\n"), "405 on the page: GET, HEAD");

    char head[1024];
    check(geode_link_response_head(200, GEODE_LINK_PAIR, 3, kToken.c_str(), head, sizeof head) > 0 &&
              std::string(head) == share::responseHead(200, Resource::Pair, 3, kToken),
          "abi: the head");
    int resource = -1;
    int64_t sequence = 0;
    check(geode_link_route("POST", "/pair", kToken.c_str(), &resource, &sequence) == 200 && resource == GEODE_LINK_PAIR,
          "abi: the route");
}

void testPage() {
    const std::string html(share::kPairHtml), js(share::kPairJs);
    check(has(html, "<script src=\"pair.js\" defer></script>") && has(html, "<link rel=\"stylesheet\" href=\"player.css\">"),
          "the page loads its own two files");
    check(html.find("<script>") == std::string::npos && !has(html, "<style") && !has(html, "style=") && !has(html, " on"),
          "no inline script, style or handler");
    for (const std::string* s : {&html, &js}) {
        check(!has(*s, "http://") && !has(*s, "https://") && !has(*s, "//cdn") && !has(*s, "url("), "nothing off the phone");
        check(!has(*s, "eval(") && !has(*s, "innerHTML") && !has(*s, "document.write") && !has(*s, "new Function"),
              "no eval or HTML injection");
        check(!has(*s, "m3u8") && !has(*s, "stream.mp4") && !has(*s, "rtc"), "nothing of the stream");
    }
    check(has(js, "fetch('pair'") && has(js, "method: 'POST'") && has(js, "credentials: 'omit'") && has(js, "cache: 'no-store'"),
          "the code is POSTed, nothing cached or sent besides");
    check(has(js, "/^\\/[0-9a-f]{32}\\/$/") && has(js, "location.replace(path)"), "only a token's path is opened");
    check(has(std::string(share::kPageCss), "#code") && has(std::string(share::kPageCss), "#go"), "the style has the form");

    size_t size = 0;
    check(geode_link_asset(GEODE_LINK_PAIR_PAGE, &size) && size == html.size(), "abi: the page");
    check(geode_link_asset(GEODE_LINK_PAIR_SCRIPT, &size) && size == js.size(), "abi: the script");
    check(geode_link_asset(GEODE_LINK_PAIR, &size) == nullptr, "abi: the POST has no asset");
}

void testUrls() {
    const uint32_t addr = 192u << 24 | 168u << 16 | 1u << 8 | 100u;
    check(share::pairUrl(addr, 41234) == "http://3232235876:41234/", "the short link: the host as one number, no token");
    check(share::pairUrl(0, 41234).empty() && share::pairUrl(addr, 0).empty() && share::pairUrl(addr, 65536).empty(),
          "nothing for no address or a bad port");
    check(share::pairUrl(addr, 41234).size() < share::shareUrl(addr, 41234, kToken).size(), "shorter than the QR's link");
    check(share::pairedPath(kToken) == "/" + kToken + "/", "a right code's answer: the token's path");
    check(share::pairedPath("ABC").empty() && share::pairedPath("").empty(), "never for a malformed token");
    char out[64];
    check(geode_link_pair_url(addr, 41234, out, sizeof out) == 24 && std::string(out) == "http://3232235876:41234/", "abi: the link");
}

void testAbi() {
    char code[32];
    char path[64];
    geode_link_pair_open(0, 0);
    check(geode_link_pair_code(0, code, sizeof code) == 0 && code[0] == '\0', "abi: closed, no code");
    check(geode_link_pair_try("00000000", 0, kToken.c_str(), path, sizeof path) == 404, "abi: closed, 404");
    geode_link_pair_open(1, 1'000);
    check(geode_link_pair_code(1'000, code, sizeof code) == 9 && code[4] == '-', "abi: a code from the OS's random, shown ABCD-EFGH");
    const std::string first = code;
    check(geode_link_pair_code(1'000, nullptr, 0) == 9, "abi: size query");
    check(geode_link_pair_try(first.c_str(), 2'000, kToken.c_str(), path, 10) == 404, "abi: no room for the path, no try");
    check(geode_link_pair_try(first.c_str(), 2'000, "ABC", path, sizeof path) == 404, "abi: a malformed token, no try");
    check(geode_link_pair_try(first.c_str(), 2'000, kToken.c_str(), path, sizeof path) == 200 && std::string(path) == "/" + kToken + "/",
          "abi: the shown code pairs and gets the token's path");
    geode_link_pair_code(2'000, code, sizeof code);
    check(first != code, "abi: then a new code");
    check(geode_link_pair_try(first.c_str(), 3'000, kToken.c_str(), path, sizeof path) == 403 && path[0] == '\0',
          "abi: the spent code is wrong, no path");
    check(geode_link_pair_try(nullptr, 3'500, kToken.c_str(), path, sizeof path) == 429, "abi: too soon after a wrong try");
    std::set<std::string> seen;
    for (int i = 0; i < 64; ++i) {
        geode_link_pair_open(0, 0);
        geode_link_pair_open(1, 0);
        geode_link_pair_code(0, code, sizeof code);
        seen.insert(code);
    }
    check(seen.size() >= 60, "abi: each session a fresh code");
    geode_link_pair_open(0, 0);
    check(geode_link_pair_code(0, code, sizeof code) == 0, "abi: closed again");
}

}  // namespace

int main() {
    testCode();
    testSession();
    testLimits();
    testRoutes();
    testHeads();
    testPage();
    testUrls();
    testAbi();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("link_pair_test: all passed\n");
    return 0;
}
