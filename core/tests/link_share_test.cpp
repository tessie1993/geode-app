// Host-side checks for core/viz/frame/LinkShare and LinkPage: "Show on a computer", the Cast stream served to a
// browser through a link. Built and run by CI ("Native host tests" in .github/workflows/native-host-tests.yml).
//
// What is pinned: the routes (every name under the token, the bare token's redirect, OPTIONS, other methods,
// queries, traversal, a wrong, short, long or upper-case token, anything off the token: 404); the token check's
// constant-time loop; every head's security headers (no-store, nosniff, no-referrer, the page's CSP with no
// 'unsafe-inline' and no other origin, CORS only on the stream's files); the codecs read from an init segment
// built like the encoder's (avcC profile, flags, level; esds object type and AAC audio object type) and the
// master playlist around them; the page's files (no inline script, style or handler, nothing off the phone);
// the share's state machine; who may connect by address (a TV cast: the Cast device alone; a live link: the
// bound subnet's site-local peers); whether the TV plays after the LOAD; and the Private Network Access answer.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/frame/LinkPage.hpp"
#include "viz/frame/LinkShare.hpp"

namespace share = geode::viz::frame::link;
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

share::Route get(const std::string& target, const char* method = "GET") { return share::route(method, target, kToken); }

void testRoutes() {
    const std::string t = "/" + kToken;
    check(get(t + "/").status == 200 && get(t + "/").resource == Resource::Page, "the page at /<token>/");
    check(get(t + "/index.html").resource == Resource::Page, "and at index.html");
    check(get(t + "/player.js").resource == Resource::Script, "the script");
    check(get(t + "/player.css").resource == Resource::Style, "the style");
    check(get(t + "/index.m3u8").resource == Resource::Master, "the master playlist");
    check(get(t + "/live.m3u8").resource == Resource::Playlist, "the live playlist");
    check(get(t + "/init.mp4").resource == Resource::Init, "the init segment");
    const share::Route seg = get(t + "/seg42.m4s");
    check(seg.status == 200 && seg.resource == Resource::Segment && seg.sequence == 42, "a segment and its sequence");
    check(get(t + "/seg0.m4s").sequence == 0, "segment 0");
    check(get(t + "/live.m3u8?_=123").resource == Resource::Playlist, "a query is ignored on a known name");
    check(get(t + "/player.js#x").resource == Resource::Script, "a fragment too");
    const share::Route bare = get(t);
    check(bare.status == 308 && bare.resource == Resource::Redirect, "the bare token redirects to its slash");
    check(get(t, "OPTIONS").status == 405, "no preflight on the bare token");
    check(get(t + "/live.m3u8", "HEAD").status == 200, "HEAD reads");
    check(get(t + "/live.m3u8", "OPTIONS").status == 204, "OPTIONS: the receiver's preflight");
    check(get(t + "/live.m3u8", "POST").status == 405 && get(t + "/", "PUT").status == 405, "other methods: 405");
    check(get(t + "/live.m3u8", "get").status == 405, "methods are case-sensitive");
    const share::Route feed = get(t + "/stream.mp4");
    check(feed.status == 200 && feed.resource == Resource::Stream && feed.sequence == -1, "the browser's feed");
    check(get(t + "/stream.mp4", "HEAD").status == 200, "the feed answers HEAD");
    check(get(t + "/stream.mp4", "OPTIONS").status == 405, "no preflight on the feed: only the page reads it");
    check(get(t + "/stream.mp4?_=1").resource == Resource::Stream, "a query on the feed is ignored");
    check(get(t + "/stream.mp4/").status == 404 && get(t + "/STREAM.MP4").status == 404 && get("/stream.mp4").status == 404,
          "the feed only by its exact name under the token");

    const char* missing[] = {"/", "/index.html", "/live.m3u8", "/favicon.ico", "", "live.m3u8"};
    for (const char* m : missing) check(get(m).status == 404 && get(m).resource == Resource::None, "off the token: 404");
    const std::string notFound[] = {
        t + "/seg.m4s", t + "/seg-1.m4s", t + "/segx.m4s", t + "/seg1.mp4", t + "/seg1234567890123456789.m4s",
        t + "/../live.m3u8", t + "/a/live.m3u8", t + "//", t + "/live.m3u8/", t + "/LIVE.M3U8", t + "/%6cive.m3u8",
        "/" + kToken.substr(0, 31) + "/live.m3u8", "/" + kToken + "0/live.m3u8", "/0123456789ABCDEF0123456789ABCDEF/",
        "/" + kToken.substr(0, 31) + "e/", "http://10.0.0.2:80/" + kToken + "/", "/?" + kToken + "/", "/x/" + kToken + "/"};
    for (const std::string& target : notFound) check(get(target).status == 404, "a wrong name or token: 404");
    for (const std::string& target : notFound) check(get(target, "POST").status == 404, "and 404 whatever the method");
    check(share::route("GET", "/" + kToken + "/", "").status == 404, "no token: nothing is served");
    check(share::route("GET", "//", "").status == 404, "an empty token never matches an empty path");
    check(share::route("GET", "/ABC/", "ABC").status == 404, "a malformed token serves nothing");
}

void testToken() {
    check(share::validToken(kToken), "32 lowercase hex");
    check(!share::validToken(kToken.substr(1)) && !share::validToken(kToken + "0") && !share::validToken(""), "length");
    check(!share::validToken("0123456789abcdef0123456789abcdeG") && !share::validToken("0123456789ABCDEF0123456789abcdef"),
          "lowercase hex only");
    check(share::tokenMatches(kToken, kToken), "equal");
    check(!share::tokenMatches(kToken.substr(0, 31) + "0", kToken), "the last character differs");
    check(!share::tokenMatches("1" + kToken.substr(1), kToken), "the first differs");
    check(!share::tokenMatches(kToken.substr(0, 16), kToken) && !share::tokenMatches(kToken + kToken, kToken), "a prefix or a repeat");
    check(!share::tokenMatches("", ""), "empty never matches");
}

void testHeads() {
    const std::string page = share::responseHead(200, Resource::Page, 1234, kToken);
    check(page.rfind("HTTP/1.1 200 OK\r\n", 0) == 0, "status line");
    check(has(page, "Content-Type: text/html; charset=utf-8\r\n") && has(page, "Content-Length: 1234\r\n"), "page type and length");
    check(has(page, "Content-Security-Policy: default-src 'none'; script-src 'self'; style-src 'self'; media-src 'self' blob:;"),
          "the page's CSP");
    check(!has(page, "unsafe") && !has(page, "http:") && !has(page, "https:") && !has(page, " *"), "no inline, eval or other origin");
    check(has(page, "frame-ancestors 'none'") && has(page, "X-Frame-Options: DENY\r\n"), "not framed");
    check(!has(page, "Access-Control"), "no CORS on the page");
    for (Resource r : {Resource::Page, Resource::Script, Resource::Style, Resource::Master, Resource::Playlist, Resource::Init,
                       Resource::Segment, Resource::Stream}) {
        const std::string h = share::responseHead(200, r, 10, kToken);
        check(has(h, "Cache-Control: no-store\r\n") && has(h, "X-Content-Type-Options: nosniff\r\n") &&
                  has(h, "Referrer-Policy: no-referrer\r\n") && has(h, "Connection: close\r\n"),
              "every response: no-store, nosniff, no-referrer, close");
        check(h.size() >= 4 && h.compare(h.size() - 4, 4, "\r\n\r\n") == 0, "the head ends with a blank line");
    }
    const std::string seg = share::responseHead(200, Resource::Segment, 99, kToken);
    check(has(seg, "Content-Type: video/mp4\r\n") && has(seg, "Access-Control-Allow-Origin: *\r\n") && has(seg, "Accept-Ranges: none"),
          "a segment: mp4 with CORS for the TV");
    check(!has(seg, "Content-Security-Policy"), "the CSP is the page's");
    check(has(share::responseHead(200, Resource::Playlist, 1, kToken), "application/vnd.apple.mpegurl"), "playlist type");
    check(has(share::responseHead(200, Resource::Script, 1, kToken), "Content-Type: text/javascript; charset=utf-8"), "script type");
    check(has(share::responseHead(200, Resource::Style, 1, kToken), "Content-Type: text/css; charset=utf-8"), "style type");
    const std::string pre = share::responseHead(204, Resource::Playlist, 0, kToken);
    check(pre.rfind("HTTP/1.1 204 No Content\r\n", 0) == 0 && has(pre, "Access-Control-Allow-Methods") && !has(pre, "\r\nContent-Length:"),
          "a preflight");
    const std::string missing = share::responseHead(404, Resource::None, 500, kToken);
    check(missing.rfind("HTTP/1.1 404 Not Found\r\n", 0) == 0 && has(missing, "Content-Length: 0\r\n"), "404 with no body");
    check(!has(missing, "\r\nContent-Type:") && !has(missing, "Access-Control") && !has(missing, kToken.c_str()), "and nothing else");
    const std::string moved = share::responseHead(308, Resource::Redirect, 0, kToken);
    check(has(moved, ("Location: /" + kToken + "/\r\n").c_str()), "the redirect names /<token>/");
    check(has(share::responseHead(405, Resource::Page, 0, kToken), "Allow: GET, HEAD, OPTIONS\r\n"), "405 says what is allowed");
    check(share::responseHead(414, Resource::None, 0, kToken).rfind("HTTP/1.1 414 URI Too Long", 0) == 0, "414");
    check(share::responseHead(431, Resource::None, 0, kToken).rfind("HTTP/1.1 431 Request Header Fields Too Large", 0) == 0, "431");

    // The browser's feed: mp4 with no length (it ends when the connection closes), no CORS, no CSP.
    const std::string feed = share::responseHead(200, Resource::Stream, 12345, kToken);
    check(has(feed, "Content-Type: video/mp4\r\n") && !has(feed, "Content-Length") && has(feed, "Connection: close\r\n"),
          "the feed: mp4, no length, closed to end");
    check(!has(feed, "Access-Control") && !has(feed, "Content-Security-Policy"), "the feed has no CORS: the page is same-origin");
    check(has(share::responseHead(405, Resource::Stream, 0, kToken), "Allow: GET, HEAD\r\n"), "405 on the feed: no OPTIONS");
    const std::string busy = share::responseHead(503, Resource::Stream, 0, kToken);
    check(busy.rfind("HTTP/1.1 503 Service Unavailable\r\n", 0) == 0 && has(busy, "Retry-After: 2\r\n") &&
              has(busy, "Content-Length: 0\r\n") && !has(busy, "\r\nContent-Type:"),
          "503 when the feed's viewers are full: retry, no body");
    check(share::kMaxStreams == GEODE_LINK_MAX_STREAMS && share::kMaxStreams >= 1, "abi: the feed's viewer cap");
}

// ---- An init segment built like FragmentedMp4Muxer's: ftyp, moov with an avc1 and an mp4a trak. ----

using Bytes = std::vector<uint8_t>;

void u8(Bytes& b, uint8_t v) { b.push_back(v); }
void u16(Bytes& b, uint16_t v) {
    u8(b, static_cast<uint8_t>(v >> 8));
    u8(b, static_cast<uint8_t>(v));
}
void u32(Bytes& b, uint32_t v) {
    u16(b, static_cast<uint16_t>(v >> 16));
    u16(b, static_cast<uint16_t>(v));
}
void zeros(Bytes& b, size_t n) { b.insert(b.end(), n, 0); }
void add(Bytes& b, const Bytes& more) { b.insert(b.end(), more.begin(), more.end()); }

Bytes box(const char* type, const Bytes& body) {
    Bytes b;
    u32(b, static_cast<uint32_t>(8 + body.size()));
    b.insert(b.end(), type, type + 4);
    add(b, body);
    return b;
}

Bytes hdlr(const char* kind) {
    Bytes b;
    zeros(b, 8);   // version, flags, pre_defined
    b.insert(b.end(), kind, kind + 4);
    zeros(b, 13);
    return box("hdlr", b);
}

Bytes stbl(const Bytes& entry) {
    Bytes stsd;
    zeros(stsd, 4);
    u32(stsd, 1);
    add(stsd, entry);
    return box("stbl", box("stsd", stsd));
}

Bytes trak(const char* kind, const Bytes& entry) {
    Bytes mdia = box("mdhd", Bytes(24, 0));
    add(mdia, hdlr(kind));
    add(mdia, box("minf", stbl(entry)));
    Bytes t = box("tkhd", Bytes(84, 0));
    add(t, box("mdia", mdia));
    return box("trak", t);
}

Bytes avc1(uint8_t profile, uint8_t flags, uint8_t level) {
    Bytes e;
    zeros(e, 78);
    Bytes avcc = {1, profile, flags, level, 0xFF, 0xE1};
    add(e, box("avcC", avcc));
    add(e, box("pasp", Bytes(8, 0)));
    return box("avc1", e);
}

Bytes mp4a(uint8_t objectType, uint8_t audioObjectType, bool longSizes) {
    Bytes dsi = {static_cast<uint8_t>(audioObjectType << 3 | 1), 0x90};
    Bytes dc = {objectType, 0x15};
    zeros(dc, 11);
    const auto descriptor = [longSizes](uint8_t tag, const Bytes& body) {
        Bytes d = {tag};
        if (longSizes) d.insert(d.end(), {0x80, 0x80, 0x80});
        d.push_back(static_cast<uint8_t>(body.size()));
        d.insert(d.end(), body.begin(), body.end());
        return d;
    };
    add(dc, descriptor(5, dsi));
    Bytes es = {0, 1, 0};
    add(es, descriptor(4, dc));
    add(es, descriptor(6, Bytes{2}));
    Bytes esds;
    zeros(esds, 4);
    add(esds, descriptor(3, es));
    Bytes e;
    zeros(e, 28);
    add(e, box("esds", esds));
    return box("mp4a", e);
}

Bytes init(const Bytes& video, const Bytes& audio) {
    Bytes moov = box("mvhd", Bytes(100, 0));
    if (!video.empty()) add(moov, trak("vide", video));
    if (!audio.empty()) add(moov, trak("soun", audio));
    Bytes out = box("ftyp", Bytes{'i', 's', 'o', '6', 0, 0, 0, 0});
    add(out, box("moov", moov));
    return out;
}

void testCodecs() {
    const Bytes high = init(avc1(0x64, 0x00, 0x1F), mp4a(0x40, 2, false));
    check(share::codecs(high.data(), high.size()) == "avc1.64001f,mp4a.40.2", "High 3.1 and AAC-LC");
    const Bytes base = init(avc1(0x42, 0xC0, 0x1E), mp4a(0x40, 2, true));
    check(share::codecs(base.data(), base.size()) == "avc1.42c01e,mp4a.40.2", "Constrained Baseline 3.0, long descriptor sizes");
    const Bytes he = init(avc1(0x4D, 0x40, 0x28), mp4a(0x40, 5, false));
    check(share::codecs(he.data(), he.size()) == "avc1.4d4028,mp4a.40.5", "Main 4.0 and HE-AAC");
    const Bytes audioFirst = init({}, mp4a(0x40, 2, false));
    check(share::codecs(audioFirst.data(), audioFirst.size()) == "mp4a.40.2", "audio alone");
    const Bytes none = init({}, {});
    check(share::codecs(none.data(), none.size()).empty() && share::masterPlaylist(none.data(), none.size()).empty(),
          "no tracks: no codecs, no master playlist yet");
    check(share::codecs(nullptr, 0).empty(), "null init");
    for (size_t cut = 0; cut < high.size(); cut += 7) share::codecs(high.data(), cut);   // truncated: no read past the end
    Bytes broken = high;
    broken[broken.size() - 20] = 0xFF;
    share::codecs(broken.data(), broken.size());

    const std::string master = share::masterPlaylist(high.data(), high.size());
    check(master.rfind("#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-INDEPENDENT-SEGMENTS\n", 0) == 0, "master header");
    check(has(master, "#EXT-X-STREAM-INF:BANDWIDTH=4576000,CODECS=\"avc1.64001f,mp4a.40.2\",RESOLUTION=1280x720,FRAME-RATE=30.000\n"),
          "one variant: bandwidth, codecs, size, rate");
    check(master.size() > 10 && master.compare(master.size() - 10, 10, "live.m3u8\n") == 0, "it points at live.m3u8");

    char text[512];
    const int len = geode_link_master_playlist(high.data(), high.size(), text, sizeof text);
    check(len == static_cast<int>(master.size()) && master == text, "abi: the master playlist");
    check(geode_link_master_playlist(high.data(), high.size(), nullptr, 0) == len, "abi: the size query");
    char tiny[4] = {'x', 'x', 'x', 'x'};
    geode_link_master_playlist(high.data(), high.size(), tiny, sizeof tiny);
    check(tiny[0] == '\0', "abi: too small writes nothing");
}

void testPage() {
    const std::string html(share::kPageHtml), js(share::kPageJs), css(share::kPageCss);
    check(has(html, "<script src=\"player.js\" defer></script>") && has(html, "<link rel=\"stylesheet\" href=\"player.css\">"),
          "the page loads its own two files");
    // No inline script or style, no handler attribute, nothing from elsewhere.
    check(html.find("<script>") == std::string::npos && !has(html, "<style") && !has(html, "style=") && !has(html, " on"),
          "no inline script, style or handler");
    for (const std::string* s : {&html, &js, &css}) {
        check(!has(*s, "http://") && !has(*s, "https://") && !has(*s, "//cdn") && !has(*s, "@import") && !has(*s, "url("),
              "nothing off the phone");
        check(!has(*s, "eval(") && !has(*s, "innerHTML") && !has(*s, "document.write") && !has(*s, "new Function"),
              "no eval or HTML injection");
    }
    check(has(js, "'index.m3u8'") && has(js, "isTypeSupported") && has(js, "appendBuffer") && has(js, "application/vnd.apple.mpegurl"),
          "MSE first, native HLS after");
    check(has(js, "cache: 'no-store'") && has(js, "credentials: 'omit'"), "fetches neither cache nor send credentials");
    check(has(js, "'stream.mp4'") && has(js, "getReader()") && has(js, "CUSHION_S = 0.5"),
          "MSE reads the one live feed as it arrives, half a second in hand");
    check(!has(js, "live.m3u8"), "the MSE player no longer polls the media playlist");

    size_t size = 0;
    const char* page = geode_link_asset(GEODE_LINK_PAGE, &size);
    check(page && size == html.size() && std::memcmp(page, html.data(), size) == 0, "abi: the page");
    check(geode_link_asset(GEODE_LINK_SCRIPT, &size) && size == js.size(), "abi: the script");
    check(geode_link_asset(GEODE_LINK_STYLE, &size) && size == css.size(), "abi: the style");
    check(geode_link_asset(GEODE_LINK_SEGMENT, &size) == nullptr && size == 0, "abi: no other asset");
}

void testState() {
    int s[GEODE_LINK_STATE] = {};
    check(geode_link_step(s, GEODE_LINK_EVENT_START, 0) == GEODE_LINK_DO_PAYWALL && s[0] == GEODE_LINK_PHASE_IDLE,
          "without Pro: the paywall, nothing starts");
    check(geode_link_step(s, GEODE_LINK_EVENT_START, GEODE_LINK_ENTITLED) == 0 && s[0] == GEODE_LINK_PHASE_ENDED &&
              s[1] == GEODE_LINK_REASON_NO_LAN,
          "without Wi-Fi: ended at once, no stream");
    check(geode_link_step(s, GEODE_LINK_EVENT_SHOWN, 0) == 0 && s[0] == GEODE_LINK_PHASE_IDLE && s[1] == 0, "shown: idle");
    const int both = GEODE_LINK_ENTITLED | GEODE_LINK_HAS_LAN;
    check(geode_link_step(s, GEODE_LINK_EVENT_START, both) == GEODE_LINK_DO_START_STREAM && s[0] == GEODE_LINK_PHASE_STARTING,
          "start");
    check(geode_link_step(s, GEODE_LINK_EVENT_START, both) == 0 && s[0] == GEODE_LINK_PHASE_STARTING, "a second start: nothing");
    check(geode_link_step(s, GEODE_LINK_EVENT_READY, 0) == 0 && s[0] == GEODE_LINK_PHASE_LIVE, "ready: live");
    check(geode_link_step(s, GEODE_LINK_EVENT_READY, 0) == 0 && s[0] == GEODE_LINK_PHASE_LIVE, "ready again: still live");
    check(geode_link_step(s, GEODE_LINK_EVENT_FAILED, GEODE_LINK_REASON_NETWORK) == GEODE_LINK_DO_STOP_STREAM &&
              s[0] == GEODE_LINK_PHASE_ENDED && s[1] == GEODE_LINK_REASON_NETWORK,
          "the network changed: stop, say why");
    check(geode_link_step(s, GEODE_LINK_EVENT_FAILED, GEODE_LINK_REASON_STREAM) == 0 && s[1] == GEODE_LINK_REASON_NETWORK,
          "a later failure keeps the first reason");
    check(geode_link_step(s, GEODE_LINK_EVENT_READY, 0) == 0 && s[0] == GEODE_LINK_PHASE_ENDED, "a late ready does not revive it");
    check(geode_link_step(s, GEODE_LINK_EVENT_START, both) == GEODE_LINK_DO_START_STREAM && s[1] == 0, "start again after an end");
    check(geode_link_step(s, GEODE_LINK_EVENT_STOP, 0) == GEODE_LINK_DO_STOP_STREAM && s[0] == GEODE_LINK_PHASE_IDLE,
          "stop while starting");
    check(geode_link_step(s, GEODE_LINK_EVENT_STOP, 0) == 0, "stop when idle: nothing");
    geode_link_step(s, GEODE_LINK_EVENT_START, both);
    check(geode_link_step(s, GEODE_LINK_EVENT_FAILED, 99) == GEODE_LINK_DO_STOP_STREAM && s[1] == GEODE_LINK_REASON_STREAM,
          "an unknown reason reads as the stream's");
    check(geode_link_step(s, GEODE_LINK_EVENT_STOP, 0) == 0 && s[0] == GEODE_LINK_PHASE_IDLE, "stop clears an ended share");
    geode_link_step(s, GEODE_LINK_EVENT_START, both);
    geode_link_step(s, GEODE_LINK_EVENT_READY, 0);
    check(geode_link_step(s, GEODE_LINK_EVENT_FAILED, GEODE_LINK_REASON_TRIAL) == GEODE_LINK_DO_STOP_STREAM &&
              s[1] == GEODE_LINK_REASON_TRIAL,
          "the trial ended");
    check(geode_link_step(nullptr, GEODE_LINK_EVENT_START, both) == 0, "null state");
}

void testAbiRoute() {
    int resource = -1;
    int64_t sequence = 0;
    const std::string target = "/" + kToken + "/seg7.m4s";
    check(geode_link_route("GET", target.c_str(), kToken.c_str(), &resource, &sequence) == 200 && resource == GEODE_LINK_SEGMENT &&
              sequence == 7,
          "abi: route");
    const std::string feedTarget = "/" + kToken + "/stream.mp4";
    check(geode_link_route("GET", feedTarget.c_str(), kToken.c_str(), &resource, &sequence) == 200 &&
              resource == GEODE_LINK_STREAM && sequence == -1,
          "abi: the feed's route");
    check(geode_link_route(nullptr, nullptr, nullptr, &resource, &sequence) == 404 && resource == GEODE_LINK_NONE && sequence == -1,
          "abi: nulls are a 404");
    char head[1024];
    const int len = geode_link_response_head(200, GEODE_LINK_PAGE, 5, kToken.c_str(), head, sizeof head);
    check(len > 0 && std::string(head) == share::responseHead(200, Resource::Page, 5, kToken), "abi: the head");
    check(geode_link_response_head(200, GEODE_LINK_STREAM, -1, kToken.c_str(), head, sizeof head) > 0 &&
              std::string(head) == share::responseHead(200, Resource::Stream, -1, kToken),
          "abi: the feed's head");
    check(geode_link_response_head(200, 77, 5, kToken.c_str(), head, sizeof head) > 0 && !has(head, "\r\nContent-Type:"),
          "abi: an unknown resource has no type");
}

constexpr uint32_t ip(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { return a << 24 | b << 16 | c << 8 | d; }

void testAdmit() {
    using share::admit;
    const uint32_t phone = ip(192, 168, 1, 20), tv = ip(192, 168, 1, 30), laptop = ip(192, 168, 1, 40);
    const int both = share::kWantTv | share::kWantLink;
    // A TV cast: the Cast device alone. The laptop on the same Wi-Fi that read the URL from the receiver is refused.
    check(admit(tv, phone, 24, tv, share::kWantTv) == share::kReceiver, "tv cast: the TV");
    check(admit(laptop, phone, 24, tv, share::kWantTv) == share::kRefused, "tv cast: another device on the Wi-Fi");
    check(admit(phone, phone, 24, tv, share::kWantTv) == share::kRefused, "tv cast: not even the phone itself");
    check(admit(ip(8, 8, 8, 8), phone, 24, tv, share::kWantTv) == share::kRefused, "tv cast: a public address");
    // The TV's address as the SDK names it, wherever it is (a routed network): the TV is let in.
    check(admit(ip(10, 0, 5, 7), phone, 24, ip(10, 0, 5, 7), share::kWantTv) == share::kReceiver, "tv cast: a TV off the subnet");
    // The SDK named no IPv4 address: the bound subnet, or the TV could never fetch.
    check(admit(tv, phone, 24, 0, share::kWantTv) == share::kReceiver, "tv cast, address unknown: a subnet peer");
    check(admit(ip(192, 168, 2, 30), phone, 24, 0, share::kWantTv) == share::kRefused, "tv cast, address unknown: off the subnet");
    // A live link: any site-local peer on the bound subnet, the phone itself included.
    check(admit(laptop, phone, 24, 0, share::kWantLink) == share::kBrowser, "link: a computer on the Wi-Fi");
    check(admit(phone, phone, 24, 0, share::kWantLink) == share::kBrowser, "link: the phone's own browser");
    check(admit(ip(192, 168, 2, 40), phone, 24, 0, share::kWantLink) == share::kRefused, "link: the next subnet");
    check(admit(ip(192, 168, 2, 40), phone, 22, 0, share::kWantLink) == share::kBrowser, "link: a /22 reaches .2");
    check(admit(ip(192, 168, 4, 40), phone, 22, 0, share::kWantLink) == share::kRefused, "link: but not .4");
    check(admit(ip(10, 9, 9, 9), ip(10, 1, 2, 3), 8, 0, share::kWantLink) == share::kBrowser, "link: a /8");
    check(admit(ip(172, 20, 0, 9), ip(172, 20, 0, 1), 16, 0, share::kWantLink) == share::kBrowser, "link: 172.16/12");
    check(admit(ip(172, 32, 0, 9), ip(172, 32, 0, 1), 16, 0, share::kWantLink) == share::kRefused, "link: 172.32 is not private");
    check(admit(ip(100, 64, 0, 9), ip(100, 64, 0, 1), 16, 0, share::kWantLink) == share::kRefused, "link: carrier NAT is not the LAN");
    check(admit(ip(192, 168, 1, 99), phone, 0, 0, share::kWantLink) == share::kBrowser &&
              admit(ip(192, 168, 0, 99), phone, 0, 0, share::kWantLink) == share::kRefused &&
              admit(ip(192, 168, 0, 99), phone, 40, 0, share::kWantLink) == share::kRefused,
          "link: a prefix outside 1..32 reads as 24");
    check(admit(phone, phone, 32, 0, share::kWantLink) == share::kBrowser && admit(laptop, phone, 32, 0, share::kWantLink) == share::kRefused,
          "link: a /32 is the phone alone");
    // Both: the TV counts as the receiver, the laptop as a browser.
    check(admit(tv, phone, 24, tv, both) == share::kReceiver && admit(laptop, phone, 24, tv, both) == share::kBrowser, "both");
    // Nothing wants the stream, or nothing is known.
    check(admit(tv, phone, 24, tv, 0) == share::kRefused && admit(laptop, phone, 24, tv, 0) == share::kRefused, "nobody wants it");
    check(admit(0, phone, 24, tv, both) == share::kRefused, "an unknown peer");
    check(admit(laptop, 0, 24, 0, share::kWantLink) == share::kRefused, "an unknown bound address");
    check(geode_link_admit(tv, phone, 24, tv, GEODE_LINK_WANT_TV) == GEODE_LINK_PEER_RECEIVER &&
              geode_link_admit(laptop, phone, 24, tv, GEODE_LINK_WANT_TV) == GEODE_LINK_PEER_REFUSED,
          "abi: admit");
}

void testReceiverHealth() {
    using share::receiverHealth;
    const int64_t load = 100'000;
    const int playing = share::kPlayerPlaying, idle = share::kPlayerIdle;
    check(receiverHealth(load + 60'000, 0, 0, idle, share::kIdleError) == share::kReceiverOk, "no LOAD sent: nothing to judge");
    // The status may still describe the last media until the receiver takes the LOAD.
    check(receiverHealth(load + 1'000, load, 0, idle, share::kIdleError) == share::kReceiverOk, "an error inside the grace");
    check(receiverHealth(load + 5'000, load, 0, idle, share::kIdleError) == share::kReceiverError, "the receiver could not play it");
    check(receiverHealth(load + 9'000, load, load + 2'000, idle, share::kIdleError) == share::kReceiverError, "an error after fetches");
    for (int why : {share::kIdleFinished, share::kIdleCanceled, share::kIdleInterrupted})
        check(receiverHealth(load + 6'000, load, load + 1'000, idle, why) == share::kReceiverGone, "idle for another reason: gone");
    check(receiverHealth(load + 6'000, load, 0, idle, 0) == share::kReceiverOk, "idle with no reason: still loading");
    // Never fetched.
    check(receiverHealth(load + 20'000, load, 0, share::kPlayerLoading, 0) == share::kReceiverOk, "20 s without a fetch: still fine");
    check(receiverHealth(load + 20'001, load, 0, share::kPlayerLoading, 0) == share::kReceiverUnreachable, "past it: unreachable");
    check(receiverHealth(load + 25'000, load, load - 1, share::kPlayerBuffering, 0) == share::kReceiverUnreachable,
          "a fetch from before the LOAD does not count");
    // Fetching, then quiet.
    check(receiverHealth(load + 50'000, load, load + 20'000, playing, 0) == share::kReceiverOk, "30 s quiet while playing: fine");
    check(receiverHealth(load + 50'001, load, load + 20'000, playing, 0) == share::kReceiverUnreachable, "past it: unreachable");
    check(receiverHealth(load + 50'001, load, load + 20'000, share::kPlayerBuffering, 0) == share::kReceiverUnreachable, "buffering too");
    check(receiverHealth(load + 600'000, load, load + 20'000, share::kPlayerPaused, 0) == share::kReceiverOk, "paused: any quiet is fine");
    check(receiverHealth(load + 600'000, load, load + 20'000, 0, 0) == share::kReceiverOk, "no status: not judged on quiet");
    check(receiverHealth(load - 1, load, 0, playing, 0) == share::kReceiverOk, "a clock before the LOAD");
    check(geode_link_receiver_health(load + 21'000, load, 0, 0, 0) == GEODE_LINK_RECEIVER_UNREACHABLE, "abi: receiver health");
}

void testPrivateNetworkPreflight() {
    const std::string pre = share::responseHead(204, Resource::Segment, 0, kToken);
    check(has(pre, "Access-Control-Allow-Private-Network: true\r\n"), "the TV's preflight is told the private network allows it");
    check(!has(share::responseHead(200, Resource::Segment, 9, kToken), "Private-Network"), "only on the preflight");
    check(!has(share::responseHead(204, Resource::Page, 0, kToken), "Access-Control"), "not on the page's");
}

}  // namespace

int main() {
    testAdmit();
    testReceiverHealth();
    testPrivateNetworkPreflight();
    testRoutes();
    testToken();
    testHeads();
    testCodecs();
    testPage();
    testState();
    testAbiRoute();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("link_share_test: all passed\n");
    return 0;
}
