#include "cast/LiveHttp.hpp"

#include <algorithm>
#include <utility>

#include "api/geode_api.h"
#include "viz/frame/LinkPage.hpp"
#include "viz/frame/LinkPair.hpp"

namespace geode::cast {

namespace share = viz::frame::link;

namespace {

constexpr int kOk = 200;
constexpr int kNotFound = 404;
constexpr int kLengthRequired = 411;
constexpr int kTooLarge = 413;
constexpr int kUriTooLong = 414;
constexpr int kHeadersTooLarge = 431;
constexpr int kBusy = 503;
constexpr size_t kLineBreak = 2;   // what a header line costs beyond its text

const uint8_t* bytesOf(std::string_view text) { return reinterpret_cast<const uint8_t*>(text.data()); }

}  // namespace

// ---- RequestReader ----

int RequestReader::next(int64_t deadlineMs) {
    if (at_ == end_) {
        const int got = connection_.read(buffer_, sizeof buffer_, deadlineMs);
        if (got <= 0) return -1;
        // A connection that says it read more than it was given room for is not believed.
        end_ = std::min(static_cast<size_t>(got), sizeof buffer_);
        at_ = 0;
    }
    return buffer_[at_++];
}

Head RequestReader::readLine(size_t maxBytes, int64_t deadlineMs, std::string& out) {
    out.clear();
    for (;;) {
        const int b = next(deadlineMs);
        if (b < 0) return Head::Dropped;
        if (b == '\n') break;
        // A line of exactly maxBytes and then its LF is kept; one more byte is too long.
        if (out.size() >= maxBytes) return Head::TooLong;
        out.push_back(static_cast<char>(b));
    }
    while (!out.empty() && out.back() == '\r') out.pop_back();
    return Head::Line;
}

Head RequestReader::readHead(int64_t deadlineMs, Request& out) {
    out.headers.clear();
    const Head first = readLine(kMaxLineBytes, deadlineMs, out.line);
    if (first != Head::Line) return first;
    size_t used = 0;
    std::string header;
    for (;;) {
        const size_t left = used < kMaxHeaderBytes ? kMaxHeaderBytes - used : 0;
        const Head got = readLine(left, deadlineMs, header);
        if (got == Head::TooLong) return Head::HeadersTooLarge;
        if (got != Head::Line) return got;
        if (header.empty()) return Head::Line;
        used += header.size() + kLineBreak;
        out.headers.push_back(header);
    }
}

bool RequestReader::readBody(int64_t deadlineMs, size_t length, std::string& out) {
    out.clear();
    if (length > kMaxBodyBytes) return false;
    const size_t ahead = std::min(length, end_ - at_);
    out.append(reinterpret_cast<const char*>(buffer_ + at_), ahead);
    at_ += ahead;
    while (out.size() < length) {
        const size_t want = std::min(length - out.size(), sizeof buffer_);
        const int got = connection_.read(buffer_, want, deadlineMs);
        if (got <= 0) return false;
        out.append(reinterpret_cast<const char*>(buffer_), std::min(static_cast<size_t>(got), want));
    }
    return true;
}

// ---- PerAddressLimit ----

bool PerAddressLimit::tryAcquire(uint32_t address) {
    std::lock_guard<std::mutex> hold(lock_);
    int& open = open_[address];
    if (open >= max_) {
        if (open == 0) open_.erase(address);   // a limit of 0 must not leave an entry per stranger
        return false;
    }
    ++open;
    return true;
}

void PerAddressLimit::release(uint32_t address) {
    std::lock_guard<std::mutex> hold(lock_);
    const auto it = open_.find(address);
    if (it == open_.end()) return;
    if (--it->second <= 0) open_.erase(it);
}

// ---- LiveHttp ----

LiveHttp::LiveHttp(const LiveSegments& stream, std::string token, Answer answer, std::function<int64_t()> nowMs, Pair pair)
    : stream_(stream),
      token_(std::move(token)),
      answer_(std::move(answer)),
      nowMs_(std::move(nowMs)),
      pair_(std::move(pair)) {}

bool LiveHttp::serve(Connection& connection, int64_t deadlineMs) {
    try {
        const bool served = answer(connection, deadlineMs);
        connection.shutdownWrite();
        drain(connection, deadlineMs);
        return served;
    } catch (...) {
        // Out of memory on a request: the connection is dropped, the stream goes on.
        return false;
    }
}

bool LiveHttp::answer(Connection& connection, int64_t deadlineMs) {
    RequestReader reader(connection);
    Request request;
    switch (reader.readHead(deadlineMs, request)) {
        case Head::Line: break;
        case Head::TooLong:
            return respond(connection, kUriTooLong, share::Resource::None, nullptr, 0, true, deadlineMs);
        case Head::HeadersTooLarge:
            return respond(connection, kHeadersTooLarge, share::Resource::None, nullptr, 0, true, deadlineMs);
        case Head::Dropped: return false;
    }
    // "METHOD target version": the method is what stands before the first space, the target what stands
    // between the first and the second. A line with no space has no target and routes to 404.
    const std::string_view line = request.line;
    const size_t firstSpace = line.find(' ');
    const std::string_view method = line.substr(0, firstSpace);
    std::string_view target;
    if (firstSpace != std::string_view::npos) {
        const std::string_view rest = line.substr(firstSpace + 1);
        target = rest.substr(0, rest.find(' '));
    }
    const share::Route route = share::route(method, target, token_);
    if (route.status == kNotFound) notFound_.fetch_add(1);
    if (route.status != kOk) return respond(connection, route.status, route.resource, nullptr, 0, true, deadlineMs);
    const bool sendBody = method != "HEAD";
    if (route.resource == share::Resource::Stream) return feed(connection, sendBody, deadlineMs);
    if (route.resource == share::Resource::Rtc) return offer(connection, reader, request, deadlineMs);
    if (route.resource == share::Resource::Pair) return pair(connection, reader, request, deadlineMs);

    Bytes held;
    std::string text;
    std::string_view body;
    switch (route.resource) {
        case share::Resource::Page: body = share::kPageHtml; break;
        case share::Resource::Script: body = share::kPageJs; break;
        case share::Resource::Style: body = share::kPageCss; break;
        case share::Resource::PairPage: body = share::kPairHtml; break;
        case share::Resource::PairScript: body = share::kPairJs; break;
        case share::Resource::Master:
            text = stream_.masterPlaylist();
            body = text;
            break;
        case share::Resource::Playlist:
            text = stream_.playlist();
            body = text;
            break;
        case share::Resource::Init: held = stream_.init(); break;
        case share::Resource::Segment: held = stream_.segment(route.sequence); break;
        default: break;
    }
    if (held) return respond(connection, kOk, route.resource, held->data(), held->size(), sendBody, deadlineMs);
    // A known name with nothing behind it yet (the stream still starting, a segment out of the window): 404.
    if (body.empty()) return respond(connection, kNotFound, share::Resource::None, nullptr, 0, true, deadlineMs);
    return respond(connection, kOk, route.resource, bytesOf(body), body.size(), sendBody, deadlineMs);
}

// The browser's live feed (stream.mp4): the head (no length: the body ends when the connection does), the
// init segment, then each fragment the moment LiveSegments cuts it, from the newest on, until the stream
// stops or the browser goes. One response instead of a playlist poll and a request per segment, so a
// fragment reaches the browser as soon as it exists and the page can hold half a second. At most
// share::kMaxStreams at once (503 past it; the page retries). Never true: a TV's fetches are its own requests.
bool LiveHttp::feed(Connection& connection, bool sendBody, int64_t deadlineMs) {
    const Bytes init = stream_.init();
    if (!init) {
        respond(connection, kNotFound, share::Resource::None, nullptr, 0, false, deadlineMs);
        return false;
    }
    if (!sendBody) {
        respond(connection, kOk, share::Resource::Stream, nullptr, 0, false, deadlineMs);
        return false;
    }
    struct Seat {
        std::atomic<int>& feeds;
        const bool taken;
        explicit Seat(std::atomic<int>& f) : feeds(f), taken(f.fetch_add(1) + 1 <= share::kMaxStreams) {}
        ~Seat() { feeds.fetch_sub(1); }
    } seat(feeds_);
    if (!seat.taken) {
        respond(connection, kBusy, share::Resource::Stream, nullptr, 0, false, deadlineMs);
        return false;
    }
    connection.noDelay();
    const std::string head = share::responseHead(kOk, share::Resource::Stream, 0, token_);
    const int64_t startBy = nowMs_() + kFeedWriteMs;
    bool open = connection.write(bytesOf(head), head.size(), startBy) && connection.write(init->data(), init->size(), startBy);
    int64_t sent = -1;
    LiveSegments::Next next;
    while (open && !stopped_.load()) {
        if (stream_.nextAfter(sent, next)) {
            sent = next.sequence;
            open = connection.write(next.bytes->data(), next.bytes->size(), nowMs_() + kFeedWriteMs);
        } else {
            open = connection.pause(kFeedPollMs);
        }
    }
    return false;
}

// The browser's WebRTC offer (POST rtc): its body, at most GEODE_WEBRTC_OFFER_MAX bytes by the length its
// headers announce, answered with the phone's answer or the status a refusal maps to. The answer can take up
// to two seconds (the phone gathers its own candidate first). Never true: no TV fetch.
bool LiveHttp::offer(Connection& connection, RequestReader& reader, const Request& request, int64_t deadlineMs) {
    std::string body;
    if (!postBody(connection, reader, request, share::Resource::Rtc, GEODE_WEBRTC_OFFER_MAX, deadlineMs, body)) return false;
    std::string sdp;
    int result = GEODE_WEBRTC_REFUSED_MALFORMED;
    // An SDP is text: an empty one, or one with a NUL in it, is nobody's offer.
    if (!body.empty() && body.find('\0') == std::string::npos) {
        result = answer_ ? answer_(body, sdp) : GEODE_WEBRTC_REFUSED_FAILED;
        if (result >= 0 && sdp.size() + 1 > static_cast<size_t>(GEODE_WEBRTC_ANSWER_CAP)) result = GEODE_WEBRTC_REFUSED_FAILED;
    }
    const int status = share::rtcStatus(result);
    const bool withAnswer = status == kOk && result >= 0;
    respond(connection, status, share::Resource::Rtc, withAnswer ? bytesOf(sdp) : nullptr, withAnswer ? sdp.size() : 0, true,
            deadlineMs);
    return false;
}

// The code a computer typed on the short link's page (POST /pair): its body, at most share::pair::kBodyMax
// bytes, is one try. Only a right code is answered with the token's path, so the page can go to the stream;
// every other status carries nothing. Never true: no TV fetch.
bool LiveHttp::pair(Connection& connection, RequestReader& reader, const Request& request, int64_t deadlineMs) {
    std::string typed;
    if (!postBody(connection, reader, request, share::Resource::Pair, share::pair::kBodyMax, deadlineMs, typed)) return false;
    // A NUL is no code symbol; as '?' it stays a wrong try and the text stays whole wherever it is handed on.
    std::replace(typed.begin(), typed.end(), '\0', '?');
    const std::string path = share::pairedPath(token_);
    const int status = pair_ && !path.empty() ? pair_(typed, nowMs_()) : static_cast<int>(share::pair::kClosed);
    const bool paired = status == kOk;
    respond(connection, status, share::Resource::Pair, paired ? bytesOf(path) : nullptr, paired ? path.size() : 0, true,
            deadlineMs);
    return false;
}

bool LiveHttp::postBody(Connection& connection, RequestReader& reader, const Request& request, share::Resource resource,
                        int64_t max, int64_t deadlineMs, std::string& body) {
    const std::vector<std::string_view> headers(request.headers.begin(), request.headers.end());
    const int64_t length = share::bodyLength(headers.data(), headers.size(), max);
    if (length == share::kNoBody) {
        respond(connection, kLengthRequired, resource, nullptr, 0, true, deadlineMs);
        return false;
    }
    if (length < 0) {
        respond(connection, kTooLarge, resource, nullptr, 0, true, deadlineMs);
        return false;
    }
    return reader.readBody(deadlineMs, static_cast<size_t>(length), body);
}

bool LiveHttp::respond(Connection& connection, int status, share::Resource resource, const uint8_t* body, size_t size,
                       bool sendBody, int64_t deadlineMs) {
    const std::string head = share::responseHead(status, resource, static_cast<int64_t>(body ? size : 0), token_);
    bool sent = connection.write(bytesOf(head), head.size(), deadlineMs);
    if (sent && body && size > 0 && sendBody) sent = connection.write(body, size, deadlineMs);
    return sent && status == kOk;
}

void LiveHttp::drain(Connection& connection, int64_t deadlineMs) {
    uint8_t sink[1024];
    for (;;) {
        const int64_t now = nowMs_();
        if (now >= deadlineMs) return;
        if (connection.read(sink, sizeof sink, std::min(deadlineMs, now + kDrainMs)) <= 0) return;
    }
}

}  // namespace geode::cast
