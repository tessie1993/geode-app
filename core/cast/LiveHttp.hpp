#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cast/LiveSegments.hpp"
#include "viz/frame/LinkShare.hpp"

namespace geode::cast {

// What the cast's HTTP server does with one connection, with no socket in it: the request is read off a
// Connection (LiveServer's is a socket, the host test's a script), routed by viz/frame/LinkShare and answered
// from LiveSegments. Everything a TV or a browser gets is decided here and in LinkShare; LiveServer only
// accepts, admits by address and moves bytes. Tested by core/tests/live_http_test.cpp, hostile input included.
//
// This is a network-facing parser. Its bounds: a request line of at most kMaxLineBytes, header lines of at
// most kMaxHeaderBytes together, a body only where a resource takes one (rtc, pair) and only of the length the
// headers announce, at most GEODE_WEBRTC_OFFER_MAX for an offer and share::pair::kBodyMax for a typed code;
// every read under the connection's deadline. Anything over is answered with its status and the connection
// closed, never buffered.

// One accepted connection. Deadlines are on the clock LiveHttp was given, in ms.
class Connection {
public:
    virtual ~Connection() = default;
    // Up to cap bytes into out: > 0 how many, 0 when the peer closed, < 0 when nothing came by deadlineMs,
    // the connection failed or the server is stopping.
    virtual int read(uint8_t* out, size_t cap, int64_t deadlineMs) = 0;
    // All size bytes, by deadlineMs; false when they could not all be sent.
    virtual bool write(const uint8_t* data, size_t size, int64_t deadlineMs) = 0;
    // Send small writes at once (the feed: a fragment must not wait for the next).
    virtual void noDelay() = 0;
    // No more to send: the peer reads end of stream.
    virtual void shutdownWrite() = 0;
    // Waits ms; false when the server is stopping.
    virtual bool pause(int64_t ms) = 0;
};

struct Request {
    std::string line;                   // the request line, without its line break
    std::vector<std::string> headers;   // each header line, without its line break
};

enum class Head {
    Line,              // a request line and its headers, up to the blank line
    TooLong,           // the request line passed kMaxLineBytes: 414
    HeadersTooLarge,   // the header lines passed kMaxHeaderBytes: 431
    Dropped,           // the client closed, went quiet past the deadline or failed: no answer
};

// Reads a request's head, then its body where one is due. Lines end at LF; any CRs before it are dropped.
// What it reads ahead of the blank line stays for the body.
class RequestReader {
public:
    static constexpr size_t kMaxLineBytes = 8 * 1024;
    static constexpr size_t kMaxHeaderBytes = 16 * 1024;
    static constexpr size_t kMaxBodyBytes = 64 * 1024;   // no caller asks for more; refused past it

    explicit RequestReader(Connection& connection) : connection_(connection) {}

    Head readHead(int64_t deadlineMs, Request& out);
    // Exactly length body bytes into out; false when the client closes or goes quiet first.
    bool readBody(int64_t deadlineMs, size_t length, std::string& out);

private:
    Head readLine(size_t maxBytes, int64_t deadlineMs, std::string& out);
    int next(int64_t deadlineMs);   // the next byte, or -1

    Connection& connection_;
    uint8_t buffer_[1024];
    size_t at_ = 0;
    size_t end_ = 0;
};

// Caps the connections one remote address may hold open, so a single device on the Wi-Fi can't take every
// worker and starve the TV. tryAcquire before serving, release after. Addresses are IPv4 in host order.
class PerAddressLimit {
public:
    explicit PerAddressLimit(int max) : max_(max) {}
    bool tryAcquire(uint32_t address);
    void release(uint32_t address);

private:
    const int max_;
    std::mutex lock_;
    std::unordered_map<uint32_t, int> open_;
};

class LiveHttp {
public:
    // How often an idle feed looks for the next fragment, and how long one write to a browser may take: a
    // browser that stops reading can't hold a worker.
    static constexpr int64_t kFeedPollMs = 10;
    static constexpr int64_t kFeedWriteMs = 5'000;
    static constexpr int64_t kDrainMs = 1'000;

    // The answer to a browser's WebRTC offer into out: >= 0, or a negative GEODE_WEBRTC_REFUSED_*.
    using Answer = std::function<int(std::string_view offer, std::string& out)>;

    // One try of the code a computer typed on the short link's page, at nowMs: a share::pair::Result.
    using Pair = std::function<int(std::string_view typed, int64_t nowMs)>;

    // token: the stream's path token (share::kTokenChars lowercase hex). nowMs: a monotonic clock in ms, the
    // one the connections' deadlines are on. pair: the typed path's pairing; with none, every try finds it closed.
    LiveHttp(const LiveSegments& stream, std::string token, Answer answer, std::function<int64_t()> nowMs,
             Pair pair = nullptr);

    // Reads one request off the connection and answers it, all by deadlineMs (a browser's feed alone outlives
    // it; each of its writes has its own deadline), then ends the response and reads what the client still
    // sends until it closes, briefly, so the close is clean. True when a file was served (200): the caller
    // counts a TV's fetch by it. Never throws.
    bool serve(Connection& connection, int64_t deadlineMs);

    // Ends every feed at its next look.
    void stop() { stopped_.store(true); }

    const std::string& token() const { return token_; }
    int64_t notFound() const { return notFound_.load(); }

private:
    bool answer(Connection& connection, int64_t deadlineMs);
    bool feed(Connection& connection, bool sendBody, int64_t deadlineMs);
    bool offer(Connection& connection, RequestReader& reader, const Request& request, int64_t deadlineMs);
    bool pair(Connection& connection, RequestReader& reader, const Request& request, int64_t deadlineMs);
    // A POST's body, of the length its headers announce and at most max, into body. False when there is none
    // to hand on: no length (answered 411 on resource), a length refused (413), or a client that stopped.
    bool postBody(Connection& connection, RequestReader& reader, const Request& request,
                  viz::frame::link::Resource resource, int64_t max, int64_t deadlineMs, std::string& body);
    // The head for status and resource, then body unless the request was a HEAD; true for a 200 that went out.
    bool respond(Connection& connection, int status, viz::frame::link::Resource resource, const uint8_t* body,
                 size_t size, bool sendBody, int64_t deadlineMs);
    void drain(Connection& connection, int64_t deadlineMs);

    const LiveSegments& stream_;
    const std::string token_;
    const Answer answer_;
    const std::function<int64_t()> nowMs_;
    const Pair pair_;
    std::atomic<int> feeds_{0};  // browsers on the live feed now
    std::atomic<bool> stopped_{false};
    std::atomic<int64_t> notFound_{0};
};

}  // namespace geode::cast
