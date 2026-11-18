// Host test for core/audio/ingest/CapturePump: a capture read on a native thread of its own over a fake
// reader. Blocks reach the write as read, begin and end run once each on the pump's thread, a read error or a
// refused begin ends the capture, stop interrupts a waiting read and nothing is written after it returns, a read
// that never lets go is left behind without a later write, a bad start lets go of the reader, quiet follows
// digital zero for 4 s, and once kMaxStuck threads are left behind a new capture is refused until one ends.
//
// Build and run (one command; core/tests/fakegl only for its <android/log.h>, defined below):
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -pthread -Icore -Icore/tests/fakegl core/tests/capture_pump_test.cpp
//   core/audio/ingest/CapturePump.cpp -o capture_pump_test && ./capture_pump_test
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "audio/ingest/CapturePump.hpp"

using geode::audio::ingest::CapturePump;
using geode::audio::ingest::CaptureReader;

// The logcat boundary CapturePump logs through: nothing to see on the host.
extern "C" int __android_log_print(int, const char*, const char*, ...) { return 0; }

static int failures = 0;
#define CHECK(cond, ...)                                             \
    do {                                                             \
        if (!(cond)) {                                               \
            std::fprintf(stderr, "FAIL %s:%d ", __FILE__, __LINE__); \
            std::fprintf(stderr, __VA_ARGS__);                       \
            std::fprintf(stderr, "\n");                              \
            ++failures;                                              \
        }                                                            \
    } while (0)

namespace {

std::atomic<int64_t> clockMs{0};
int64_t fakeNow() { return clockMs.load(); }

// What the reader saw, outliving it (a pump left behind keeps its reader to the end).
struct Seen {
    std::atomic<int> begins{0};
    std::atomic<int> ends{0};
    std::atomic<int> reads{0};
    std::atomic<int> interrupts{0};
    std::atomic<bool> endOnCaller{false};
    std::thread::id caller = std::this_thread::get_id();
};

// Each read hands out `block` bytes of `value` until `limit` reads, then -1 (or waits when `waitAtLimit`).
// `hang` reads wait for interrupt; `stuck` reads ignore it until `release` is set.
struct FakeReader final : CaptureReader {
    explicit FakeReader(std::shared_ptr<Seen> seen) : seen(std::move(seen)) {}

    bool begin() override {
        ++seen->begins;
        return beginOk;
    }
    void end() override {
        ++seen->ends;
        if (std::this_thread::get_id() == seen->caller) seen->endOnCaller = true;
    }
    int read(void* data, int capacity) override {
        const int n = ++seen->reads;
        if (stuck) {
            while (!release->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            std::memset(data, value, static_cast<size_t>(capacity));
            return capacity;
        }
        if (hang || (limit > 0 && n > limit && waitAtLimit)) {
            std::unique_lock<std::mutex> lock(waitLock);
            wake.wait(lock, [this] { return woken; });
            return 0;
        }
        if (limit > 0 && n > limit) return -1;
        const int bytes = block < capacity ? block : capacity;
        std::memset(data, value, static_cast<size_t>(bytes));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return bytes;
    }
    void interrupt() override {
        ++seen->interrupts;
        {
            std::lock_guard<std::mutex> lock(waitLock);
            woken = true;
        }
        wake.notify_all();
    }

    std::shared_ptr<Seen> seen;
    bool beginOk = true;
    int block = 8;
    int limit = 0;
    bool waitAtLimit = false;
    bool hang = false;
    bool stuck = false;
    std::shared_ptr<std::atomic<bool>> release = std::make_shared<std::atomic<bool>>(false);
    unsigned char value = 0;
    std::mutex waitLock;
    std::condition_variable wake;
    bool woken = false;
};

struct Sink {
    std::mutex lock;
    std::vector<size_t> blocks;
    std::vector<unsigned char> firstBytes;
    std::atomic<int> writes{0};
    float peak = 0.0f;
};

CapturePump::Write writeInto(const std::shared_ptr<Sink>& sink) {
    return [sink](const void* data, size_t bytes) {
        std::lock_guard<std::mutex> lock(sink->lock);
        sink->blocks.push_back(bytes);
        sink->firstBytes.push_back(*static_cast<const unsigned char*>(data));
        ++sink->writes;
        return sink->peak;
    };
}

bool waitFor(const std::function<bool()>& done, int ms = 2000) {
    for (int i = 0; i < ms; ++i) {
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done();
}

void blocksReachTheWriteThenStop() {
    auto seen = std::make_shared<Seen>();
    auto reader = std::make_unique<FakeReader>(seen);
    reader->value = 7;
    reader->block = 6;
    auto sink = std::make_shared<Sink>();
    auto pump = CapturePump::start(std::move(reader), writeInto(sink), 16, fakeNow);
    CHECK(pump != nullptr, "the pump starts");
    if (!pump) return;
    CHECK(waitFor([&] { return sink->writes.load() >= 3; }), "blocks are written");
    CHECK(pump->active(), "running while reading");
    pump->stop();
    const int after = sink->writes.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(sink->writes.load() == after, "no write after stop returns");
    CHECK(!pump->active(), "inactive after stop");
    CHECK(seen->begins == 1 && seen->ends == 1, "begin and end once: %d %d", seen->begins.load(), seen->ends.load());
    CHECK(!seen->endOnCaller, "end runs on the pump's thread");
    std::lock_guard<std::mutex> lock(sink->lock);
    for (size_t i = 0; i < sink->blocks.size(); ++i) {
        CHECK(sink->blocks[i] == 6 && sink->firstBytes[i] == 7, "block %zu as read", i);
    }
    pump->stop();  // a second stop changes nothing
}

void aReadErrorEndsTheCapture() {
    auto seen = std::make_shared<Seen>();
    auto reader = std::make_unique<FakeReader>(seen);
    reader->limit = 2;
    auto sink = std::make_shared<Sink>();
    auto pump = CapturePump::start(std::move(reader), writeInto(sink), 16, fakeNow);
    CHECK(pump != nullptr, "the pump starts");
    if (!pump) return;
    CHECK(waitFor([&] { return !pump->active(); }), "a read error ends it");
    CHECK(waitFor([&] { return seen->ends.load() == 1; }), "end runs after the error");
    CHECK(sink->writes == 2, "the two blocks before the error: %d", sink->writes.load());
    CHECK(!pump->quiet(), "an ended capture is not quiet");
    pump->stop();
    CHECK(seen->ends == 1, "stop does not end it twice");
}

void aRefusedBeginReadsNothing() {
    auto seen = std::make_shared<Seen>();
    auto reader = std::make_unique<FakeReader>(seen);
    reader->beginOk = false;
    auto sink = std::make_shared<Sink>();
    auto pump = CapturePump::start(std::move(reader), writeInto(sink), 16, fakeNow);
    CHECK(pump != nullptr, "the thread starts");
    if (!pump) return;
    CHECK(waitFor([&] { return !pump->active(); }), "a refused begin ends it");
    pump->stop();
    CHECK(seen->reads == 0 && sink->writes == 0, "no read, no write");
    CHECK(seen->ends == 1, "end still runs");
}

void stopInterruptsAWaitingRead() {
    auto seen = std::make_shared<Seen>();
    auto reader = std::make_unique<FakeReader>(seen);
    reader->hang = true;
    auto sink = std::make_shared<Sink>();
    auto pump = CapturePump::start(std::move(reader), writeInto(sink), 16, fakeNow);
    CHECK(pump != nullptr, "the pump starts");
    if (!pump) return;
    CHECK(waitFor([&] { return seen->reads.load() == 1; }), "the read waits");
    const auto t0 = std::chrono::steady_clock::now();
    pump->stop();
    const auto took = std::chrono::steady_clock::now() - t0;
    CHECK(took < std::chrono::milliseconds(CapturePump::kStopWaitMs), "stop returns once the read lets go");
    CHECK(seen->interrupts == 1 && seen->ends == 1, "interrupted and ended");
    CHECK(sink->writes == 0, "nothing written");
}

void aStuckReadIsLeftBehindWithoutAWrite() {
    auto seen = std::make_shared<Seen>();
    auto reader = std::make_unique<FakeReader>(seen);
    reader->stuck = true;
    reader->value = 3;
    auto release = reader->release;
    auto sink = std::make_shared<Sink>();
    auto pump = CapturePump::start(std::move(reader), writeInto(sink), 16, fakeNow);
    CHECK(pump != nullptr, "the pump starts");
    if (!pump) return;
    CHECK(waitFor([&] { return seen->reads.load() == 1; }), "the read is stuck");
    const auto t0 = std::chrono::steady_clock::now();
    pump->stop();
    const auto took = std::chrono::steady_clock::now() - t0;
    CHECK(took >= std::chrono::milliseconds(CapturePump::kStopWaitMs), "stop waits its limit");
    pump.reset();  // the pump is gone; its thread still holds the reader
    release->store(true);
    CHECK(waitFor([&] { return seen->ends.load() == 1; }), "the thread ends alone");
    CHECK(sink->writes == 0, "the late block is never written");
}

void aBadStartLetsGoOfTheReader() {
    auto seen = std::make_shared<Seen>();
    auto sink = std::make_shared<Sink>();
    CHECK(CapturePump::start(std::make_unique<FakeReader>(seen), writeInto(sink), 0, fakeNow) == nullptr,
          "no read size, no pump");
    CHECK(CapturePump::start(std::make_unique<FakeReader>(seen), CapturePump::Write{}, 16, fakeNow) == nullptr,
          "no write, no pump");
    CHECK(CapturePump::start(std::make_unique<FakeReader>(seen), writeInto(sink), 16, nullptr) == nullptr,
          "no clock, no pump");
    CHECK(CapturePump::start(nullptr, writeInto(sink), 16, fakeNow) == nullptr, "no reader, no pump");
    CHECK(seen->ends == 3 && seen->endOnCaller, "each refused reader ended on the caller: %d", seen->ends.load());
    CHECK(seen->begins == 0 && seen->reads == 0, "never read");
}

void quietAfterFourSecondsOfZeros() {
    clockMs = 10000;
    auto seen = std::make_shared<Seen>();
    auto reader = std::make_unique<FakeReader>(seen);
    auto sink = std::make_shared<Sink>();
    auto pump = CapturePump::start(std::move(reader), writeInto(sink), 16, fakeNow);
    CHECK(pump != nullptr, "the pump starts");
    if (!pump) return;
    CHECK(waitFor([&] { return sink->writes.load() >= 1; }), "zeros are written");
    CHECK(!pump->quiet(), "not quiet at the start");
    clockMs = 14001;
    CHECK(pump->quiet(), "quiet after 4 s of digital zero");
    {
        std::lock_guard<std::mutex> lock(sink->lock);
        sink->peak = 0.5f;
    }
    const int seenWrites = sink->writes.load();
    CHECK(waitFor([&] { return sink->writes.load() > seenWrites + 1; }), "sound is written");
    CHECK(!pump->quiet(), "sound ends quiet");
    pump->stop();
    CHECK(!pump->quiet(), "a stopped capture is not quiet");
}

// Each stuck pump is stopped (waiting its limit) and left behind. At kMaxStuck a new capture is refused and its
// reader let go of on the caller; once one stuck read returns and its thread ends, a capture starts again.
void stuckThreadsCapNewCaptures() {
    CHECK(waitFor([] { return CapturePump::stuck() == 0; }), "no thread left behind before: %d", CapturePump::stuck());
    auto sink = std::make_shared<Sink>();
    std::vector<std::shared_ptr<std::atomic<bool>>> releases;
    std::vector<std::shared_ptr<Seen>> seens;
    for (int i = 0; i < CapturePump::kMaxStuck; ++i) {
        auto seen = std::make_shared<Seen>();
        auto reader = std::make_unique<FakeReader>(seen);
        reader->stuck = true;
        releases.push_back(reader->release);
        seens.push_back(seen);
        auto pump = CapturePump::start(std::move(reader), writeInto(sink), 16, fakeNow);
        CHECK(pump != nullptr, "stuck pump %d starts", i);
        if (!pump) return;
        CHECK(waitFor([&] { return seen->reads.load() == 1; }), "pump %d's read is stuck", i);
        pump->stop();
        CHECK(CapturePump::stuck() == i + 1, "left behind: %d", CapturePump::stuck());
    }
    auto refused = std::make_shared<Seen>();
    CHECK(CapturePump::start(std::make_unique<FakeReader>(refused), writeInto(sink), 16, fakeNow) == nullptr,
          "at the limit a new capture is refused");
    CHECK(refused->ends == 1 && refused->endOnCaller && refused->begins == 0,
          "the refused reader is let go of on the caller, never begun");

    releases.front()->store(true);
    CHECK(waitFor([] { return CapturePump::stuck() == CapturePump::kMaxStuck - 1; }), "an ended thread leaves the count");
    auto again = std::make_shared<Seen>();
    auto pump = CapturePump::start(std::make_unique<FakeReader>(again), writeInto(sink), 16, fakeNow);
    CHECK(pump != nullptr, "below the limit a capture starts again");
    if (pump) pump->stop();
    const int written = sink->writes.load();

    for (auto& release : releases) release->store(true);
    CHECK(waitFor([] { return CapturePump::stuck() == 0; }), "every stuck thread ends and leaves the count");
    for (const auto& s : seens) CHECK(s->ends == 1, "each stuck reader ends once");
    CHECK(sink->writes.load() == written, "no stuck read's late block is written");
}

}  // namespace

int main() {
    blocksReachTheWriteThenStop();
    aReadErrorEndsTheCapture();
    aRefusedBeginReadsNothing();
    stopInterruptsAWaitingRead();
    aStuckReadIsLeftBehindWithoutAWrite();
    aBadStartLetsGoOfTheReader();
    quietAfterFourSecondsOfZeros();
    stuckThreadsCapNewCaptures();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("capture_pump_test: ok");
    return 0;
}
