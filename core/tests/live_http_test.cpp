// Host test for cast/LiveHttp: what the cast's HTTP server does with one connection, with no socket. A
// scripted Connection plays the client. Pinned: the request head read within its bounds (a line of 8 KiB, 16 KiB
// of headers, the body only to the length announced), a slow, silent or closed client dropped, the cap on one
// address's connections, every answer byte for byte (the head LinkShare gives, then the playlist, the init
// segment, a segment, the page's files), HEAD, the browser's feed (its order, its viewer cap, its end when the
// server stops or the browser goes), the WebRTC offer's path, and hostile input: oversize lines and headers,
// lines with no spaces, NULs and high bytes, wrong tokens, bad, doubled or chunked lengths, a truncated body.
//
// Build & run:
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore -Icore/tests/stub core/tests/live_http_test.cpp \
//       core/cast/LiveHttp.cpp core/cast/LiveSegments.cpp core/viz/frame/LinkShare.cpp -o live_http_test && ./live_http_test
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "api/geode_api.h"
#include "cast/LiveHttp.hpp"
#include "cast/LiveSegments.hpp"
#include "viz/frame/LinkPage.hpp"
#include "viz/frame/LinkShare.hpp"

namespace share = geode::viz::frame::link;
using geode::cast::Connection;
using geode::cast::Head;
using geode::cast::LiveHttp;
using geode::cast::LiveSegments;
using geode::cast::PerAddressLimit;
using geode::cast::Request;
using geode::cast::RequestReader;
using share::Resource;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

const std::string kToken = "0123456789abcdef0123456789abcdef";
constexpr int64_t kDeadline = 10'000;

int64_t zero() { return 0; }

// The client's side of one connection: what it sends, in pieces of at most `piece` bytes, and then either
// closes or goes quiet; what it was sent.
class Script : public Connection {
public:
    explicit Script(std::string sends, size_t piece = SIZE_MAX, bool closes = true)
        : in(std::move(sends)), piece_(piece), closes_(closes) {}

    int read(uint8_t* out, size_t cap, int64_t) override {
        ++reads;
        if (at_ == in.size()) return closes_ ? 0 : -1;
        const size_t n = std::min({cap, piece_, in.size() - at_});
        std::memcpy(out, in.data() + at_, n);
        at_ += n;
        return static_cast<int>(n);
    }
    bool write(const uint8_t* data, size_t size, int64_t deadlineMs) override {
        lastWriteDeadline = deadlineMs;
        if (out.size() + size > writeLimit) return false;
        out.append(reinterpret_cast<const char*>(data), size);
        return true;
    }
    void noDelay() override { noDelayed = true; }
    void shutdownWrite() override { shut = true; }
    bool pause(int64_t ms) override {
        pausedMs = ms;
        ++pauses;
        if (onPause) onPause(pauses);
        return !stopping;
    }

    size_t unread() const { return in.size() - at_; }

    std::string in;
    std::string out;
    size_t writeLimit = SIZE_MAX;   // a write that would pass this fails: the client stopped reading
    std::function<void(int)> onPause;
    bool stopping = false;
    bool noDelayed = false;
    bool shut = false;
    int reads = 0;
    int pauses = 0;
    int64_t pausedMs = 0;
    int64_t lastWriteDeadline = 0;

private:
    size_t at_ = 0;
    const size_t piece_;
    const bool closes_;
};

// ---- The request reader ----

Head readHead(Script& script, Request& request) {
    RequestReader reader(script);
    return reader.readHead(kDeadline, request);
}

void testReader() {
    Request r;
    {
        Script s("GET /abc/live.m3u8 HTTP/1.1\r\nHost: x\r\n\r\n");
        check(readHead(s, r) == Head::Line && r.line == "GET /abc/live.m3u8 HTTP/1.1" &&
                  r.headers == std::vector<std::string>{"Host: x"},
              "a request line and its header");
    }
    for (size_t piece : {size_t{1}, size_t{3}, SIZE_MAX}) {
        Script s("GET /a/seg3.m4s HTTP/1.1\r\nHost: x\r\nOrigin: https://r\r\n\r\n", piece);
        check(readHead(s, r) == Head::Line && r.line == "GET /a/seg3.m4s HTTP/1.1" &&
                  r.headers == std::vector<std::string>{"Host: x", "Origin: https://r"},
              "headers are read to the blank line, however the bytes arrive");
    }
    {
        Script s("GET / HTTP/1.1\nHost: x\r\r\n\n");
        check(readHead(s, r) == Head::Line && r.line == "GET / HTTP/1.1" && r.headers == std::vector<std::string>{"Host: x"},
              "a bare LF ends a line; CRs before it are dropped");
    }
    for (size_t piece : {size_t{1}, size_t{7}, SIZE_MAX}) {
        Script s("POST /a/rtc HTTP/1.1\r\nContent-Length: 5\r\n\r\nv=0\r\nNEXT", piece);
        RequestReader reader(s);
        std::string body;
        check(reader.readHead(kDeadline, r) == Head::Line && r.line == "POST /a/rtc HTTP/1.1" &&
                  r.headers == std::vector<std::string>{"Content-Length: 5"},
              "the head of a request with a body");
        check(reader.readBody(kDeadline, 5, body) && body == "v=0\r\n", "the body is read to its length, no further");
    }
    {
        Script quiet("ab", SIZE_MAX, false);
        RequestReader reader(quiet);
        std::string body;
        check(!reader.readBody(kDeadline, 5, body), "a short body from a client gone quiet is dropped");
        Script closed("ab");
        RequestReader other(closed);
        check(!other.readBody(kDeadline, 5, body), "and from one that closed");
        Script none("");
        RequestReader empty(none);
        check(empty.readBody(kDeadline, 0, body) && body.empty() && none.reads == 0, "no body is read without asking the client");
        Script big(std::string(RequestReader::kMaxBodyBytes + 1, 'x'));
        RequestReader greedy(big);
        check(!greedy.readBody(kDeadline, RequestReader::kMaxBodyBytes + 1, body) && big.reads == 0,
              "a body past the reader's own cap is refused unread");
    }
    {
        std::string sends = "GET / HTTP/1.1\r\n";
        for (size_t i = 0; i < RequestReader::kMaxHeaderBytes / 64 + 2; ++i) sends += "X-Pad: " + std::string(56, 'p') + "\r\n";
        Script s(sends + "\r\n");
        check(readHead(s, r) == Head::HeadersTooLarge, "headers over their cap are refused");
    }
    {
        Script fits("GET / HTTP/1.1\n" + std::string(RequestReader::kMaxHeaderBytes, 'h') + "\n\n");
        check(readHead(fits, r) == Head::Line && r.headers.size() == 1, "one header line of exactly the cap is kept");
        Script over("GET / HTTP/1.1\n" + std::string(RequestReader::kMaxHeaderBytes + 1, 'h') + "\n\n");
        check(readHead(over, r) == Head::HeadersTooLarge, "one byte more is not");
        Script after("GET / HTTP/1.1\n" + std::string(RequestReader::kMaxHeaderBytes, 'h') + "\nX: 1\n\n");
        check(readHead(after, r) == Head::HeadersTooLarge, "nor any header after a full budget");
    }
    {
        Script s(std::string(RequestReader::kMaxLineBytes + 10, 'a'));
        check(readHead(s, r) == Head::TooLong, "a line over the cap is refused");
        const std::string line(RequestReader::kMaxLineBytes, 'G');
        Script exact(line + "\n\n");
        check(readHead(exact, r) == Head::Line && r.line == line && r.headers.empty(), "a line exactly at the cap is kept");
        Script endless(std::string(1024 * 1024, 'a'));
        check(readHead(endless, r) == Head::TooLong && endless.unread() > 1024 * 1000, "a megabyte with no line break is not read through");
    }
    {
        Script slow("GET / HT", 1, false);
        check(readHead(slow, r) == Head::Dropped, "a client that stops mid-line is dropped at the deadline");
        Script silent("", SIZE_MAX, false);
        check(readHead(silent, r) == Head::Dropped, "a silent client is dropped");
        Script closed("");
        check(readHead(closed, r) == Head::Dropped, "a closed client is dropped");
        Script noBlank("GET / HTTP/1.1\r\nHost: x\r\n");
        check(readHead(noBlank, r) == Head::Dropped, "headers with no blank line are no request");
    }
}

void testPerAddressLimit() {
    PerAddressLimit limit(2);
    const uint32_t phone = 0x0A000005;
    const uint32_t tv = 0x0A000009;
    check(limit.tryAcquire(phone) && limit.tryAcquire(phone), "two from one address");
    check(!limit.tryAcquire(phone), "one address cannot take more than its share");
    check(limit.tryAcquire(tv), "a different device, such as the TV, is still served");
    limit.release(phone);
    check(limit.tryAcquire(phone), "a released place is free again");
    limit.release(0x01020304);
    limit.release(tv);
    limit.release(tv);
    check(limit.tryAcquire(tv) && limit.tryAcquire(tv) && !limit.tryAcquire(tv), "a release too many gives nothing away");
    PerAddressLimit none(0);
    check(!none.tryAcquire(phone), "a limit of 0 admits nobody");
}

// ---- A stream to serve ----

using Buf = std::vector<uint8_t>;

Buf box(const char* type, const Buf& body) {
    Buf b;
    const auto size = static_cast<uint32_t>(8 + body.size());
    for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<uint8_t>(size >> s));
    b.insert(b.end(), type, type + 4);
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

Buf header() {
    Buf b = box("ftyp", Buf{'i', 's', 'o', '6'});
    const Buf moov = box("moov", Buf(24, 0));
    b.insert(b.end(), moov.begin(), moov.end());
    return b;
}

// A fragment whose bytes say which it is. It names no track, so it is kept as written.
Buf fragment(uint8_t n) {
    Buf b = box("moof", Buf{n, n, n});
    const Buf mdat = box("mdat", Buf(50, n));
    b.insert(b.end(), mdat.begin(), mdat.end());
    return b;
}

void add(LiveSegments& stream, const Buf& bytes) { stream.write(bytes.data(), bytes.size()); }

std::string text(const Buf& b) { return std::string(b.begin(), b.end()); }

std::string get(const std::string& name, const char* method = "GET") {
    return std::string(method) + " /" + kToken + "/" + name + " HTTP/1.1\r\nHost: 10.0.0.2:4321\r\n\r\n";
}

std::string head(int status, Resource resource, size_t length = 0) {
    return share::responseHead(status, resource, static_cast<int64_t>(length), kToken);
}

struct Served {
    bool counted;
    std::string out;
    bool shut;
};

Served serve(LiveHttp& http, const std::string& sends, size_t piece = SIZE_MAX) {
    Script s(sends, piece);
    const bool counted = http.serve(s, kDeadline);
    return {counted, s.out, s.shut};
}

// ---- The files ----

void testFiles() {
    LiveSegments stream(zero);
    LiveHttp http(stream, kToken, nullptr, zero);
    check(http.token() == kToken, "the token it was given");

    Served r = serve(http, get(""));
    check(r.counted && r.out == head(200, Resource::Page, share::kPageHtml.size()) + std::string(share::kPageHtml) && r.shut,
          "the page, before the stream has anything");
    r = serve(http, get("player.js"));
    check(r.counted && r.out == head(200, Resource::Script, share::kPageJs.size()) + std::string(share::kPageJs), "its script");
    r = serve(http, get("player.css"));
    check(r.counted && r.out == head(200, Resource::Style, share::kPageCss.size()) + std::string(share::kPageCss), "its style");
    for (const char* name : {"live.m3u8", "index.m3u8", "init.mp4", "seg0.m4s", "stream.mp4"}) {
        r = serve(http, get(name));
        check(!r.counted && r.out == head(404, Resource::None) && r.shut, "a known name with nothing behind it yet: a bare 404");
    }
    check(http.notFound() == 0, "which is not a stranger's 404");

    add(stream, header());
    r = serve(http, get("init.mp4"));
    check(r.counted && r.out == head(200, Resource::Init, header().size()) + text(header()), "the init segment");
    r = serve(http, get("live.m3u8"));
    check(!r.counted && r.out == head(404, Resource::None), "no playlist before a receiver could start on it");
    for (uint8_t n = 0; n < 8; ++n) add(stream, fragment(n));

    const std::string playlist =
        "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:2\n"
        "#EXT-X-MAP:URI=\"init.mp4\"\n"
        "#EXTINF:1.000,\nseg2.m4s\n#EXTINF:1.000,\nseg3.m4s\n#EXTINF:1.000,\nseg4.m4s\n"
        "#EXTINF:1.000,\nseg5.m4s\n#EXTINF:1.000,\nseg6.m4s\n#EXTINF:1.000,\nseg7.m4s\n";
    for (size_t piece : {size_t{1}, SIZE_MAX}) {
        r = serve(http, get("live.m3u8"), piece);
        check(r.counted && r.out == head(200, Resource::Playlist, playlist.size()) + playlist, "the playlist a TV polls");
    }
    r = serve(http, get("live.m3u8?_=17"));
    check(r.counted && r.out == head(200, Resource::Playlist, playlist.size()) + playlist, "with a query too");
    r = serve(http, get("live.m3u8", "HEAD"));
    check(r.counted && r.out == head(200, Resource::Playlist, playlist.size()), "HEAD: the same head and no body");
    r = serve(http, get("live.m3u8", "OPTIONS"));
    check(!r.counted && r.out == head(204, Resource::Playlist), "the receiver's preflight is no fetch");
    r = serve(http, get("live.m3u8", "DELETE"));
    check(!r.counted && r.out == head(405, Resource::Playlist), "another method: 405");
    r = serve(http, get("seg5.m4s"));
    check(r.counted && r.out == head(200, Resource::Segment, fragment(5).size()) + text(fragment(5)), "a segment in the window");
    for (const char* name : {"seg1.m4s", "seg8.m4s", "seg99999999999.m4s"}) {
        r = serve(http, get(name));
        check(!r.counted && r.out == head(404, Resource::None), "a segment out of the window: 404");
    }
    r = serve(http, "GET /" + kToken + " HTTP/1.1\r\n\r\n");
    check(!r.counted && r.out == head(308, Resource::Redirect), "the bare token redirects");
    check(http.notFound() == 0, "none of these was a stranger");

    // What the client still sends after its request is read off, so the close is clean.
    Script chatty(get("init.mp4") + std::string(5000, 'z'), 100);
    check(http.serve(chatty, kDeadline) && chatty.unread() == 0 && chatty.shut, "what the client still sends is drained");

    // A client that stops reading: the answer does not count as served.
    Script deaf(get("seg5.m4s"));
    deaf.writeLimit = 40;
    check(!http.serve(deaf, kDeadline) && deaf.shut, "a response that could not be sent is no fetch");
    Script halfDeaf(get("seg5.m4s"));
    halfDeaf.writeLimit = head(200, Resource::Segment, fragment(5).size()).size() + 10;
    check(!http.serve(halfDeaf, kDeadline), "nor one cut off in its body");
}

// ---- Hostile input ----

void testHostile() {
    LiveSegments stream(zero);
    LiveHttp http(stream, kToken, nullptr, zero);
    add(stream, header());
    for (uint8_t n = 0; n < 3; ++n) add(stream, fragment(n));

    Served r = serve(http, std::string(RequestReader::kMaxLineBytes + 1, 'a') + "\r\n\r\n");
    check(!r.counted && r.out == head(414, Resource::None) && r.shut, "an oversize request line: 414");
    r = serve(http, std::string(4 * 1024 * 1024, 'a'));
    check(!r.counted && r.out == head(414, Resource::None), "four megabytes with no line break: 414, not memory");
    std::string padded = "GET /" + kToken + "/live.m3u8 HTTP/1.1\r\n";
    for (int i = 0; i < 400; ++i) padded += "X-Pad: " + std::string(56, 'p') + "\r\n";
    r = serve(http, padded + "\r\n");
    check(!r.counted && r.out == head(431, Resource::None), "oversize headers: 431, even on a good path");
    check(http.notFound() == 0, "neither is counted as a wrong path");

    int64_t strangers = 0;
    const std::string wrongToken = "0123456789abcdef0123456789abcdee";
    const std::string lines[] = {
        "GET / HTTP/1.1",
        "GET /live.m3u8 HTTP/1.1",
        "GET /" + wrongToken + "/live.m3u8 HTTP/1.1",
        "GET /" + kToken.substr(0, 31) + "/live.m3u8 HTTP/1.1",
        "GET /" + kToken + "/../" + kToken + "/live.m3u8 HTTP/1.1",
        "GET /" + kToken + "/%2e%2e/live.m3u8 HTTP/1.1",
        "GET /" + kToken + "/seg-1.m4s HTTP/1.1",
        "GET /" + kToken + "/seg18446744073709551616.m4s HTTP/1.1",
        "GET http://10.0.0.2/" + kToken + "/live.m3u8 HTTP/1.1",
        "GET",
        "GET ",
        "GET  /" + kToken + "/live.m3u8 HTTP/1.1",
        "/" + kToken + "/live.m3u8",
        "",
        " ",
        "   ",
        std::string("GET /") + kToken + std::string("/live.m3u8\0.html HTTP/1.1", 25),
        std::string("\0\0\0\0", 4),
        std::string("GET /\xff\xfe\x80/ HTTP/1.1"),
        "\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03",   // a TLS hello
        "POST /" + wrongToken + "/rtc HTTP/1.1",
        "CONNECT example.invalid:443 HTTP/1.1",
        std::string(RequestReader::kMaxLineBytes - 1, '/'),   // with its CR, exactly the cap
        "GET /" + kToken + "/" + std::string(8000, 'A') + " HTTP/1.1",
    };
    for (const std::string& line : lines) {
        for (size_t piece : {size_t{1}, SIZE_MAX}) {
            r = serve(http, line + "\r\nHost: x\r\n\r\n", piece);
            ++strangers;
            check(!r.counted && r.out == head(404, Resource::None) && r.shut, "anything that is not a known path: a bare 404");
            if (r.out != head(404, Resource::None)) std::printf("  for: %.60s\n", line.c_str());
        }
    }
    check(http.notFound() == strangers, "and each is counted");

    // No request at all: nothing is sent.
    Script closed("");
    check(!http.serve(closed, kDeadline) && closed.out.empty() && closed.shut, "a client that says nothing gets nothing");
    Script half("GET /" + kToken + "/live.m3u8 HTTP/1.1\r\nHost", SIZE_MAX, false);
    check(!http.serve(half, kDeadline) && half.out.empty(), "nor one that never ends its headers");
}

// ---- The browser's feed ----

void testFeed() {
    LiveSegments stream(zero);
    LiveHttp http(stream, kToken, nullptr, zero);
    add(stream, header());
    for (uint8_t n = 0; n < 4; ++n) add(stream, fragment(n));

    Served r = serve(http, get("stream.mp4", "HEAD"));
    check(!r.counted && r.out == head(200, Resource::Stream), "HEAD on the feed: its head alone");

    // A feed starts at the live edge, then gets each fragment as it is cut; it ends when the server stops.
    Script viewer(get("stream.mp4"));
    viewer.onPause = [&](int n) {
        if (n == 1) add(stream, fragment(4));
        if (n == 2) {
            add(stream, fragment(5));
            add(stream, fragment(6));
        }
        if (n == 4) http.stop();
    };
    check(!http.serve(viewer, kDeadline), "a feed is no TV fetch");
    check(viewer.out == head(200, Resource::Stream) + text(header()) + text(fragment(3)) + text(fragment(4)) + text(fragment(5)) +
                            text(fragment(6)),
          "the head, the init segment, the newest fragment, then every one after it in order");
    check(viewer.noDelayed && viewer.shut && viewer.pauses == 4 && viewer.pausedMs == LiveHttp::kFeedPollMs,
          "sent at once, polled every 10 ms, closed at the stop");
    check(viewer.lastWriteDeadline == LiveHttp::kFeedWriteMs, "each write has five seconds, not the request's deadline");

    Script late(get("stream.mp4"));
    check(!http.serve(late, kDeadline) && late.out == head(200, Resource::Stream) + text(header()) && late.pauses == 0,
          "a feed opened on a stopped server ends after its header");
}

void testFeedEnds() {
    LiveSegments stream(zero);
    LiveHttp http(stream, kToken, nullptr, zero);
    add(stream, header());
    add(stream, fragment(0));

    // The browser goes: the next write fails and the feed ends.
    Script gone(get("stream.mp4"));
    gone.writeLimit = head(200, Resource::Stream).size() + header().size() + fragment(0).size();
    gone.onPause = [&](int) { add(stream, fragment(1)); };
    check(!http.serve(gone, kDeadline) && gone.pauses == 1, "a browser that went ends its feed at the next fragment");

    // The server's own stop, seen in the pause.
    Script stopped(get("stream.mp4"));
    stopped.stopping = true;
    check(!http.serve(stopped, kDeadline) && stopped.pauses == 1, "a pause cut short by the stop ends the feed");

    // A fallen-behind feed jumps to the newest fragment.
    Script behind(get("stream.mp4"));
    behind.onPause = [&](int n) {
        if (n == 1)
            for (uint8_t f = 2; f < 12; ++f) add(stream, fragment(f));
        if (n == 2) behind.stopping = true;
    };
    http.serve(behind, kDeadline);
    check(behind.out == head(200, Resource::Stream) + text(header()) + text(fragment(1)) + text(fragment(11)),
          "a feed that fell out of the window skips to the live edge");
}

void testFeedCap() {
    LiveSegments stream(zero);
    LiveHttp http(stream, kToken, nullptr, zero);
    add(stream, header());
    add(stream, fragment(0));
    check(share::kMaxStreams == 3, "three browsers at once");

    // Three feeds open at once (each opened while the one before it waits), and a fourth asks.
    Script first(get("stream.mp4"));
    Script second(get("stream.mp4"));
    Script third(get("stream.mp4"));
    Script fourth(get("stream.mp4"));
    first.onPause = [&](int n) {
        if (n == 1) http.serve(second, kDeadline);
    };
    second.onPause = [&](int n) {
        if (n == 1) http.serve(third, kDeadline);
    };
    third.onPause = [&](int n) {
        if (n == 1) {
            http.serve(fourth, kDeadline);
            first.stopping = second.stopping = third.stopping = true;
        }
    };
    http.serve(first, kDeadline);
    const std::string playing = head(200, Resource::Stream) + text(header()) + text(fragment(0));
    check(first.out == playing && second.out == playing && third.out == playing, "three are fed");
    check(fourth.out == head(503, Resource::Stream) && fourth.pauses == 0, "the fourth is told to retry");

    Script next(get("stream.mp4"));
    next.stopping = true;
    http.serve(next, kDeadline);
    check(next.out == playing, "once they left there is room again");
}

// ---- The WebRTC offer ----

std::string post(const std::string& headers, const std::string& body) {
    return "POST /" + kToken + "/rtc HTTP/1.1\r\n" + headers + "\r\n" + body;
}

void testOffer() {
    LiveSegments stream(zero);
    std::string heard;
    int calls = 0;
    int result = 0;
    std::string reply = "v=0\r\na=answer\r\n";
    LiveHttp http(stream, kToken,
                  [&](std::string_view offer, std::string& out) {
                      ++calls;
                      heard = std::string(offer);
                      out = reply;
                      return result;
                  },
                  zero);

    const std::string sdp = "v=0\r\no=- 1 1 IN IP4 0.0.0.0\r\n";
    const std::string length = "Content-Length: " + std::to_string(sdp.size()) + "\r\n";
    for (size_t piece : {size_t{1}, size_t{5}, SIZE_MAX}) {
        calls = 0;
        const Served r = serve(http, post("Content-Type: application/sdp\r\n" + length, sdp), piece);
        check(calls == 1 && heard == sdp, "the offer's body, exactly, reaches the answerer");
        check(!r.counted && r.out == head(200, Resource::Rtc, reply.size()) + reply && r.shut, "and its answer goes back");
    }

    calls = 0;
    Served r = serve(http, post("content-length: " + std::to_string(sdp.size()) + "\r\n", sdp + "trailing"));
    check(calls == 1 && heard == sdp, "bytes past the announced length are not the offer");

    calls = 0;
    r = serve(http, post("Host: x\r\n", sdp));
    check(calls == 0 && r.out == head(411, Resource::Rtc), "no length: 411, and the body is not read as one");
    r = serve(http, post(length + length, sdp));
    check(calls == 0 && r.out == head(413, Resource::Rtc), "two lengths: refused");
    r = serve(http, post("Transfer-Encoding: chunked\r\n", "5\r\nv=0\r\n\r\n0\r\n\r\n"));
    check(calls == 0 && r.out == head(413, Resource::Rtc), "a chunked body: refused");
    for (const char* bad : {"-1", "0x10", "1e3", " ", "99999999999999999999999999", "+5", "5 5"}) {
        r = serve(http, post("Content-Length: " + std::string(bad) + "\r\n", sdp));
        check(calls == 0 && r.out == head(413, Resource::Rtc), "a length that is not a plain number: refused");
    }
    r = serve(http, post("Content-Length: " + std::to_string(GEODE_WEBRTC_OFFER_MAX + 1) + "\r\n",
                         std::string(static_cast<size_t>(GEODE_WEBRTC_OFFER_MAX) + 1, 'v')));
    check(calls == 0 && r.out == head(413, Resource::Rtc), "an offer over the cap: refused unread");

    r = serve(http, post("Content-Length: 500\r\n", sdp));
    check(calls == 0 && r.out.empty() && r.shut, "a body shorter than announced: dropped, nothing answered");
    r = serve(http, post("Content-Length: 0\r\n", ""));
    check(calls == 0 && r.out == head(400, Resource::Rtc), "an empty offer: 400 without asking");
    const std::string nul("v=0\r\n\0a=x\r\n", 11);
    r = serve(http, post("Content-Length: 11\r\n", nul));
    check(calls == 0 && r.out == head(400, Resource::Rtc), "an offer with a NUL in it: 400 without asking");

    const std::string full(static_cast<size_t>(GEODE_WEBRTC_OFFER_MAX), 'v');
    r = serve(http, post("Content-Length: " + std::to_string(full.size()) + "\r\n", full), 999);
    check(calls == 1 && heard == full, "an offer of exactly the cap is taken");

    calls = 0;
    result = GEODE_WEBRTC_REFUSED_MALFORMED;
    r = serve(http, post(length, sdp));
    check(calls == 1 && r.out == head(share::rtcStatus(GEODE_WEBRTC_REFUSED_MALFORMED), Resource::Rtc), "a refusal: its status, no body");
    result = -4;
    r = serve(http, post(length, sdp));
    check(r.out == head(503, Resource::Rtc), "no picture yet or no room: 503");
    result = 0;
    reply.assign(static_cast<size_t>(GEODE_WEBRTC_ANSWER_CAP), 'a');
    r = serve(http, post(length, sdp));
    check(r.out == head(share::rtcStatus(GEODE_WEBRTC_REFUSED_FAILED), Resource::Rtc), "an answer over its cap is not sent");
    reply.assign(static_cast<size_t>(GEODE_WEBRTC_ANSWER_CAP) - 1, 'a');
    r = serve(http, post(length, sdp));
    check(r.out == head(200, Resource::Rtc, reply.size()) + reply, "one at the cap is");

    LiveHttp deaf(stream, kToken, nullptr, zero);
    r = serve(deaf, post(length, sdp));
    check(r.out == head(share::rtcStatus(GEODE_WEBRTC_REFUSED_FAILED), Resource::Rtc), "no answerer: the offer fails, the server does not");

    LiveHttp thrower(stream, kToken, [](std::string_view, std::string&) -> int { throw 1; }, zero);
    Script s(post(length, sdp));
    check(!thrower.serve(s, kDeadline), "an answerer that throws drops the connection, no more");

    r = serve(http, "GET /" + kToken + "/rtc HTTP/1.1\r\n\r\n");
    check(r.out == head(405, Resource::Rtc), "rtc takes POST alone");
}

}  // namespace

int main() {
    testReader();
    testPerAddressLimit();
    testFiles();
    testHostile();
    testFeed();
    testFeedEnds();
    testFeedCap();
    testOffer();
    if (failures == 0) std::printf("live_http_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
