#pragma once
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cast/LiveHttp.hpp"
#include "cast/LiveSegments.hpp"

namespace geode::cast {

// The cast's HTTP server: the live stream (LiveSegments) served over the local network to the TV's Cast
// receiver and to a browser through "Show on a computer". With the Cast SDK's own traffic and WebRtcLink, the
// one thing the app's INTERNET permission is for (the owner's rule: Cast is the only internet use).
//
// This file is the sockets and the threads and nothing else: it listens on the phone's own Wi-Fi or Ethernet
// IPv4 address (never the wildcard) on a port the OS picks, lets a connection in or closes it unread by its
// source address (viz/frame/LinkShare: admit), and hands each one to LiveHttp, which reads the request under
// its bounds and decides every byte of the answer. Android build only (plain POSIX sockets, no library);
// tools/network-gate.sh holds socket headers to core/cast/. What is decided is host-tested in
// live_http_test, live_segments_test and link_share_test.
//
// Bounds: kWorkers threads serve; at most kWorkers + kQueue connections are in flight, one past that is closed
// at once (so many addresses can't pile up sockets); one remote address holds at most kMaxPerAddress (a
// browser opens up to six to one host); a connection gets one kConnectionMs deadline from accept however
// slowly it sends or reads. A browser's feed alone outlives it, each of its writes under its own deadline.
//
// Accepted risks, written down: the stream is plain HTTP with the token in the path (no certificate can be
// trusted for a LAN address, and the Default Media Receiver can't do TLS to one), so anyone on the Wi-Fi who
// has the link, or sees the traffic, can watch; the server binds only to the phone's site-local address and
// runs only while the stream does.
//
// Threads: the muxer's drain thread writes segments(); the accept thread and the workers are this class's
// own; the shell polls the rest from any thread. Nothing here calls out of native.
class LiveServer {
public:
    static constexpr int kWorkers = 8;
    // Connections waiting for a worker; a player has one or two in flight, a browser up to six.
    static constexpr int kQueue = 16;
    static constexpr int64_t kConnectionMs = 10'000;
    static constexpr int kMaxPerAddress = 6;
    static constexpr size_t kTokenBytes = 16;

    // boundDotted and bound: the address to listen on, dotted and as IPv4 in host order; prefix its prefix
    // length (the subnet a live link admits). answer: a browser's WebRTC offer, see LiveHttp::Answer.
    LiveServer(std::string boundDotted, uint32_t bound, int prefix, LiveHttp::Answer answer);
    ~LiveServer();
    LiveServer(const LiveServer&) = delete;
    LiveServer& operator=(const LiveServer&) = delete;

    // Binds, listens and starts serving; the port, or 0 when the address could not be bound.
    int start();
    // Stops accepting, ends every connection and joins the threads. Idempotent; the counts stay readable.
    void stop();

    // Who the stream is for (share::kWant* bits) and the Cast device's IPv4 address (0 = none named).
    void admit(uint32_t tv, int wants);

    LiveSegments& segments() { return segments_; }
    const LiveSegments& segments() const { return segments_; }

    int port() const { return port_.load(); }
    // The page at the bound address, dotted ("http://a.b.c.d:port/token/"); "" before start.
    std::string pageUrl() const;
    // The playlist's URL, as the TV's receiver loads it.
    std::string playlistUrl() const;
    // The link a user sees, copies and shares (share::shareUrl); the dotted page when that cannot be made.
    std::string shareUrl() const;
    // A short name for this link ("Blue Comet").
    std::string label() const;
    // When the TV last got a file, on the boot clock in ms (Android's elapsedRealtime); 0 = never.
    int64_t lastReceiverFetchMs() const { return lastReceiverFetch_.load(); }
    // What was refused and not found, for the log: counts and the first refused address, never a path or
    // the token. "" when there is nothing to say.
    std::string refusals() const;

    // The boot clock in ms: the one every deadline here, and lastReceiverFetchMs, is on.
    static int64_t nowMs();

private:
    struct Accepted {
        int fd;
        uint32_t address;
        int peer;
        int64_t deadlineMs;
    };

    void acceptLoop();
    void workLoop();
    void take(int fd, uint32_t address);
    void serve(const Accepted& client);
    void halt();   // stop()'s work, under life_

    const std::string boundDotted_;
    const uint32_t bound_;
    const int prefix_;
    LiveSegments segments_;
    LiveHttp http_;
    PerAddressLimit perAddress_{kMaxPerAddress};

    int listenFd_ = -1;
    int wake_[2] = {-1, -1};   // a pipe: a byte in it, never read, ends every wait
    std::atomic<int> port_{0};
    std::atomic<uint32_t> tv_{0};
    std::atomic<int> wants_{0};
    std::atomic<bool> stopping_{false};
    std::atomic<int64_t> lastReceiverFetch_{0};
    std::atomic<int64_t> refused_{0};
    std::atomic<uint32_t> firstRefused_{0};
    std::atomic<int64_t> overflow_{0};

    std::mutex life_;                  // start() and stop() one at a time: started_, the threads, the descriptors
    std::mutex lock_;                  // queue_, inFlight_
    std::condition_variable ready_;
    std::deque<Accepted> queue_;
    int inFlight_ = 0;                 // queued or being served
    bool started_ = false;
    std::thread acceptThread_;
    std::vector<std::thread> workers_;
};

}  // namespace geode::cast
