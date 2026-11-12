// Host-side checks for core/stage/CommandQueue: how other threads hand commands to a stage thread or the vsync
// thread (docs/agents/native-stage.md).
//
// What is pinned: first in, first out across the ring's wrap; last-wins commands merging per kind and target and
// never across an ordered one; a full queue refusing a new command and keeping what it holds, while a merge still
// lands; the wake called once per empty-to-non-empty; a partial drain; and four producers against one consumer
// losing nothing and keeping each producer's order.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "stage/CommandQueue.hpp"

using namespace geode::stage;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

Command cmd(uint16_t kind, int32_t a, void* target = nullptr) {
    Command c;
    c.kind = kind;
    c.a = a;
    c.target = target;
    return c;
}

int wakes = 0;
void countWake(void* context) { ++*static_cast<int*>(context); }

void fifoAcrossTheWrap() {
    CommandQueue<4> q;
    Command out[4];
    int expected = 0;
    bool inOrder = true;
    for (int round = 0; round < 10; ++round) {
        for (int i = 0; i < 3; ++i) q.push(cmd(1, round * 3 + i), Merge::Ordered);
        const size_t n = q.drain(out, 4);
        if (n != 3) inOrder = false;
        for (size_t i = 0; i < n; ++i) inOrder = inOrder && out[i].a == expected++;
    }
    check(inOrder, "commands come out in the order they went in, across the ring's wrap");
    check(q.size() == 0, "and the queue is empty after each drain");
}

void lastWins() {
    CommandQueue<8> q;
    int target = 0;
    check(q.push(cmd(3, 100), Merge::LastWins) == Pushed::Queued, "a first size is queued");
    check(q.push(cmd(3, 200), Merge::LastWins) == Pushed::Merged, "a second size replaces it");
    check(q.push(cmd(3, 300, &target), Merge::LastWins) == Pushed::Queued, "the same kind for another target keeps its own slot");
    check(q.push(cmd(4, 1), Merge::LastWins) == Pushed::Queued, "another kind keeps its own slot");
    check(q.push(cmd(3, 400), Merge::LastWins) == Pushed::Merged, "a later size still merges past other last-wins kinds");
    Command out[8];
    const size_t n = q.drain(out, 8);
    check(n == 3, "three slots");
    check(n == 3 && out[0].kind == 3 && out[0].a == 400 && out[0].target == nullptr, "the merged size, with the last value, in the first one's place");
    check(n == 3 && out[1].a == 300 && out[1].target == &target, "the other target's size");
    check(n == 3 && out[2].kind == 4, "then the other kind");
}

void neverAcrossAnOrderedCommand() {
    CommandQueue<8> q;
    q.push(cmd(3, 1), Merge::LastWins);          // size
    q.push(cmd(1, 7), Merge::Ordered);           // a window hand-over
    q.push(cmd(3, 2), Merge::LastWins);          // a size for the new window
    q.push(cmd(3, 3), Merge::LastWins);          // merges with the one after the hand-over only
    Command out[8];
    const size_t n = q.drain(out, 8);
    check(n == 3, "a size before the hand-over and one after it");
    check(n == 3 && out[0].a == 1 && out[1].kind == 1 && out[2].a == 3, "the old window's size stays before the hand-over");
    // An ordered command never merges, even with its own kind.
    q.push(cmd(1, 1), Merge::Ordered);
    q.push(cmd(1, 2), Merge::Ordered);
    check(q.size() == 2, "two hand-overs are two hand-overs");
}

void fullQueue() {
    CommandQueue<3> q;
    q.push(cmd(1, 1), Merge::Ordered);
    q.push(cmd(3, 2), Merge::LastWins);
    q.push(cmd(1, 3), Merge::Ordered);
    check(q.push(cmd(1, 4), Merge::Ordered) == Pushed::Full, "a full queue refuses an ordered command");
    check(q.push(cmd(5, 4), Merge::LastWins) == Pushed::Full, "and a last-wins command with nothing to merge into");
    check(q.size() == 3, "keeping what it holds");
    // The size sits before an ordered command, so a new size cannot merge into it either.
    check(q.push(cmd(3, 9), Merge::LastWins) == Pushed::Full, "no merge across the ordered command, even when full");
    Command out[3];
    check(q.drain(out, 3) == 3 && out[0].a == 1 && out[1].a == 2 && out[2].a == 3, "the held commands come out untouched");
    CommandQueue<2> tail;
    tail.push(cmd(1, 1), Merge::Ordered);
    tail.push(cmd(3, 2), Merge::LastWins);
    check(tail.push(cmd(3, 5), Merge::LastWins) == Pushed::Merged, "a full queue still takes a merge into its newest slot");
}

void wakeOnFirst() {
    wakes = 0;
    CommandQueue<8> q(&countWake, &wakes);
    q.push(cmd(1, 1), Merge::Ordered);
    q.push(cmd(1, 2), Merge::Ordered);
    q.push(cmd(3, 3), Merge::LastWins);
    q.push(cmd(3, 4), Merge::LastWins);
    check(wakes == 1, "one wake while the queue stays non-empty");
    Command out[8];
    q.drain(out, 8);
    q.push(cmd(1, 5), Merge::Ordered);
    check(wakes == 2, "a wake again once it was emptied");
    q.drain(out, 8);
    int other = 0;
    q.setWake(&countWake, &other);
    q.push(cmd(1, 6), Merge::Ordered);
    check(other == 1 && wakes == 2, "the wake set last is the one called");
}

void partialDrain() {
    CommandQueue<8> q;
    for (int i = 0; i < 5; ++i) q.push(cmd(1, i), Merge::Ordered);
    Command out[2];
    check(q.drain(out, 2) == 2 && out[0].a == 0 && out[1].a == 1, "a drain takes at most what it is given room for");
    check(q.size() == 3, "leaving the rest");
    check(q.drain(out, 2) == 2 && out[0].a == 2, "in order");
    check(q.drain(out, 2) == 1 && out[0].a == 4, "to the last");
    check(q.drain(out, 2) == 0, "then none");
}

void producers() {
    constexpr int kProducers = 4;
    constexpr int kEach = 20000;
    CommandQueue<16> q;
    std::atomic<int> started{0};
    std::vector<std::thread> threads;
    for (int p = 0; p < kProducers; ++p) {
        threads.emplace_back([&q, &started, p] {
            started.fetch_add(1);
            for (int i = 0; i < kEach;) {
                Command c = cmd(static_cast<uint16_t>(10 + p), i);
                if (q.push(c, Merge::Ordered) != Pushed::Full) {
                    ++i;
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    int next[kProducers] = {};
    bool ordered = true;
    int received = 0;
    Command out[16];
    while (received < kProducers * kEach) {
        const size_t n = q.drain(out, 16);
        if (n == 0) std::this_thread::yield();
        for (size_t i = 0; i < n; ++i) {
            const int p = out[i].kind - 10;
            if (p < 0 || p >= kProducers || out[i].a != next[p]) ordered = false;
            if (p >= 0 && p < kProducers) next[p] = out[i].a + 1;
            ++received;
        }
    }
    for (auto& t : threads) t.join();
    check(started.load() == kProducers, "every producer ran");
    check(ordered, "each producer's commands arrive in its own order");
    check(received == kProducers * kEach && q.size() == 0, "and none is lost");
}

}  // namespace

int main() {
    fifoAcrossTheWrap();
    lastWins();
    neverAcrossAnOrderedCommand();
    fullQueue();
    wakeOnFirst();
    partialDrain();
    producers();
    if (failures) {
        std::fprintf(stderr, "%d command queue check(s) failed\n", failures);
        return 1;
    }
    std::printf("command queue: all checks passed\n");
    return 0;
}
