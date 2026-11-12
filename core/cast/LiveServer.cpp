#include "cast/LiveServer.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <utility>

#include "viz/frame/LinkShare.hpp"

namespace geode::cast {

namespace share = viz::frame::link;

namespace {

constexpr int kBacklog = 50;
// No wait is longer than this before it looks at the stop flag again, should the wake pipe ever fail.
constexpr int64_t kMaxWaitMs = 250;
constexpr int kAcceptWaitMs = 1'000;
constexpr int kNoDescriptorWaitMs = 100;
constexpr size_t kMaxRead = 64 * 1024;

void closeFd(int& fd) {
    if (fd >= 0) ::close(fd);
    fd = -1;
}

// Non-blocking and not inherited by a process the app starts; false when the descriptor refuses.
bool prepare(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return false;
    const int fdFlags = ::fcntl(fd, F_GETFD, 0);
    return fdFlags >= 0 && ::fcntl(fd, F_SETFD, fdFlags | FD_CLOEXEC) >= 0;
}

// The stream's path token: kTokenBytes from the kernel's generator as lowercase hex; "" when it gives none
// (the server then never starts: a guessable token is worse than no stream).
std::string randomToken() {
    uint8_t bytes[LiveServer::kTokenBytes];
    size_t got = 0;
    while (got < sizeof bytes) {
        const ssize_t n = ::getrandom(bytes + got, sizeof bytes - got, 0);
        if (n > 0) {
            got += static_cast<size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return {};
        }
    }
    static constexpr char kHex[] = "0123456789abcdef";
    std::string token;
    token.reserve(2 * sizeof bytes);
    for (const uint8_t b : bytes) {
        token.push_back(kHex[b >> 4]);
        token.push_back(kHex[b & 0x0F]);
    }
    return token;
}

// One accepted socket, non-blocking: every read and write waits in poll() for the socket or the server's
// wake pipe, never past its deadline, so no client can hold a worker and stop() ends every wait at once.
class SocketConnection final : public Connection {
public:
    SocketConnection(int fd, int wake, const std::atomic<bool>& stopping) : fd_(fd), wake_(wake), stopping_(stopping) {}

    int read(uint8_t* out, size_t cap, int64_t deadlineMs) override {
        for (;;) {
            if (!waitFor(POLLIN, deadlineMs)) return -1;
            const ssize_t n = ::recv(fd_, out, std::min(cap, kMaxRead), 0);
            if (n >= 0) return static_cast<int>(n);
            if (errno != EAGAIN && errno != EINTR) return -1;
        }
    }

    bool write(const uint8_t* data, size_t size, int64_t deadlineMs) override {
        size_t sent = 0;
        while (sent < size) {
            // MSG_NOSIGNAL: a client that left is an error here, not a SIGPIPE for the whole app.
            const ssize_t n = ::send(fd_, data + sent, size - sent, MSG_NOSIGNAL);
            if (n > 0) {
                sent += static_cast<size_t>(n);
            } else if (n < 0 && errno == EAGAIN) {
                if (!waitFor(POLLOUT, deadlineMs)) return false;
            } else if (n >= 0 || errno != EINTR) {
                return false;
            }
        }
        return true;
    }

    void noDelay() override {
        const int on = 1;
        ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
    }

    void shutdownWrite() override { ::shutdown(fd_, SHUT_WR); }

    bool pause(int64_t ms) override {
        if (stopping_.load()) return false;
        pollfd wake = {wake_, POLLIN, 0};
        const int n = ::poll(&wake, 1, static_cast<int>(std::clamp<int64_t>(ms, 0, kMaxWaitMs)));
        return n <= 0 && !stopping_.load();
    }

private:
    // True when the socket is ready for events (or failed: the call after reports it); false at the
    // deadline or when the server is stopping.
    bool waitFor(short events, int64_t deadlineMs) {
        for (;;) {
            if (stopping_.load()) return false;
            const int64_t left = deadlineMs - LiveServer::nowMs();
            if (left <= 0) return false;
            pollfd fds[2] = {{fd_, events, 0}, {wake_, POLLIN, 0}};
            const int n = ::poll(fds, 2, static_cast<int>(std::min(left, kMaxWaitMs)));
            if (n < 0 && errno != EINTR) return false;
            if (n <= 0) continue;
            if (fds[1].revents != 0) return false;
            if (fds[0].revents != 0) return true;
        }
    }

    const int fd_;
    const int wake_;
    const std::atomic<bool>& stopping_;
};

}  // namespace

int64_t LiveServer::nowMs() {
    timespec now = {};
    ::clock_gettime(CLOCK_BOOTTIME, &now);
    return static_cast<int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1'000'000;
}

LiveServer::LiveServer(std::string boundDotted, uint32_t bound, int prefix, LiveHttp::Answer answer)
    : boundDotted_(std::move(boundDotted)),
      bound_(bound),
      prefix_(prefix),
      segments_(&LiveServer::nowMs),
      http_(segments_, randomToken(), std::move(answer), &LiveServer::nowMs) {}

LiveServer::~LiveServer() { stop(); }

int LiveServer::start() {
    std::lock_guard<std::mutex> life(life_);
    if (started_) return port_.load();
    if (stopping_.load() || bound_ == 0 || http_.token().empty()) return 0;
    if (::pipe(wake_) != 0) {
        wake_[0] = wake_[1] = -1;
        return 0;
    }
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_port = 0;   // the OS picks
    local.sin_addr.s_addr = htonl(bound_);
    socklen_t size = sizeof local;
    const int on = 1;
    const bool listening =
        listenFd_ >= 0 && prepare(wake_[0]) && prepare(wake_[1]) && prepare(listenFd_) &&
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on) == 0 &&
        ::bind(listenFd_, reinterpret_cast<const sockaddr*>(&local), sizeof local) == 0 &&
        ::listen(listenFd_, kBacklog) == 0 &&
        ::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&local), &size) == 0 && ntohs(local.sin_port) != 0;
    if (!listening) {
        closeFd(listenFd_);
        closeFd(wake_[0]);
        closeFd(wake_[1]);
        return 0;
    }
    port_.store(ntohs(local.sin_port));
    started_ = true;
    try {
        workers_.reserve(kWorkers);
        for (int i = 0; i < kWorkers; ++i) workers_.emplace_back([this] { workLoop(); });
        acceptThread_ = std::thread([this] { acceptLoop(); });
    } catch (...) {
        // No thread to be had: what did start is stopped below, and the stream fails as an unbound one does.
        halt();
        port_.store(0);
        return 0;
    }
    return port_.load();
}

void LiveServer::stop() {
    std::lock_guard<std::mutex> life(life_);
    halt();
}

void LiveServer::halt() {
    {
        std::lock_guard<std::mutex> hold(lock_);
        stopping_.store(true);
    }
    http_.stop();
    if (!started_) return;
    started_ = false;
    const char wake = 1;
    if (::write(wake_[1], &wake, 1) < 0) {
        // Every wait looks at the flag again within kMaxWaitMs, so the stop still lands.
    }
    ready_.notify_all();
    if (acceptThread_.joinable()) acceptThread_.join();
    for (std::thread& worker : workers_)
        if (worker.joinable()) worker.join();
    workers_.clear();
    for (Accepted& waiting : queue_) {
        perAddress_.release(waiting.address);
        closeFd(waiting.fd);
    }
    queue_.clear();
    closeFd(listenFd_);
    closeFd(wake_[0]);
    closeFd(wake_[1]);
}

void LiveServer::admit(uint32_t tv, int wants) {
    tv_.store(tv);
    wants_.store(wants);
}

void LiveServer::acceptLoop() {
    try {
        while (!stopping_.load()) {
            pollfd fds[2] = {{listenFd_, POLLIN, 0}, {wake_[0], POLLIN, 0}};
            const int n = ::poll(fds, 2, kAcceptWaitMs);
            if (n < 0 && errno != EINTR) return;
            if (n <= 0) continue;
            if (fds[1].revents != 0 || (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) return;
            if ((fds[0].revents & POLLIN) == 0) continue;
            sockaddr_in from = {};
            socklen_t size = sizeof from;
            const int fd = ::accept(listenFd_, reinterpret_cast<sockaddr*>(&from), &size);
            if (fd < 0) {
                if (errno == EAGAIN || errno == EINTR || errno == ECONNABORTED) continue;
                // Out of descriptors or memory: wait for a connection to end, don't spin on the one waiting.
                pollfd wake = {wake_[0], POLLIN, 0};
                ::poll(&wake, 1, kNoDescriptorWaitMs);
                continue;
            }
            take(fd, from.sin_family == AF_INET ? ntohl(from.sin_addr.s_addr) : 0);
        }
    } catch (...) {
        // Out of memory: the server stops taking connections and the stream's watchdogs end the cast.
    }
}

// Refuses a stranger, a peer over its share or one past the queue, closing it unread; else queues it.
void LiveServer::take(int fd, uint32_t address) {
    const int peer = share::admit(address, bound_, prefix_, tv_.load(), wants_.load());
    if (peer == share::kRefused) {
        if (refused_.fetch_add(1) == 0) firstRefused_.store(address);
        ::close(fd);
        return;
    }
    if (!perAddress_.tryAcquire(address)) {
        ::close(fd);
        return;
    }
    bool queued = false;
    if (prepare(fd)) {
        try {
            std::lock_guard<std::mutex> hold(lock_);
            if (inFlight_ < kWorkers + kQueue) {
                queue_.push_back({fd, address, peer, nowMs() + kConnectionMs});
                ++inFlight_;
                queued = true;
            } else {
                overflow_.fetch_add(1);
            }
        } catch (...) {
            queued = false;
        }
    }
    if (!queued) {
        perAddress_.release(address);
        ::close(fd);
        return;
    }
    ready_.notify_one();
}

void LiveServer::workLoop() {
    try {
        for (;;) {
            Accepted client = {};
            {
                std::unique_lock<std::mutex> hold(lock_);
                ready_.wait(hold, [this] { return stopping_.load() || !queue_.empty(); });
                if (stopping_.load()) return;   // halt() closes what still waits
                client = queue_.front();
                queue_.pop_front();
            }
            serve(client);
            std::lock_guard<std::mutex> hold(lock_);
            --inFlight_;
        }
    } catch (...) {
        // A lock that fails: this worker ends, the others serve on.
    }
}

void LiveServer::serve(const Accepted& client) {
    SocketConnection connection(client.fd, wake_[0], stopping_);
    // Only a file the TV itself got counts: the shell asks by it whether the TV still plays.
    if (http_.serve(connection, client.deadlineMs) && client.peer == share::kReceiver) lastReceiverFetch_.store(nowMs());
    ::close(client.fd);
    perAddress_.release(client.address);
}

std::string LiveServer::pageUrl() const {
    const int port = port_.load();
    if (port == 0) return {};
    return "http://" + boundDotted_ + ":" + std::to_string(port) + "/" + http_.token() + "/";
}

std::string LiveServer::playlistUrl() const {
    std::string url = pageUrl();
    if (!url.empty()) url.append(share::kPlaylistName);
    return url;
}

std::string LiveServer::shareUrl() const {
    std::string url = share::shareUrl(bound_, port_.load(), http_.token());
    return url.empty() ? pageUrl() : url;
}

std::string LiveServer::label() const { return share::friendlyLabel(http_.token()); }

std::string LiveServer::refusals() const {
    const long long refused = refused_.load();
    const long long missing = http_.notFound();
    const long long dropped = overflow_.load();
    if (refused == 0 && missing == 0 && dropped == 0) return {};
    char first[16] = "-";
    if (refused > 0) {
        const uint32_t a = firstRefused_.load();
        std::snprintf(first, sizeof first, "%u.%u.%u.%u", a >> 24, (a >> 16) & 0xFFu, (a >> 8) & 0xFFu, a & 0xFFu);
    }
    char text[160];
    std::snprintf(text, sizeof text, "refused %lld (first %s), not found %lld, queue full %lld", refused, first, missing, dropped);
    return text;
}

}  // namespace geode::cast
