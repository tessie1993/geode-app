// Host test for core/util/ThreadNice: a thread has set no nice until it does, the nice is the calling thread's own
// (a spawned thread does not see its spawner's), and the ranks hold: the threads that draw above those that feed
// them, those above a default thread. Off Android setThreadNice only records, so no privilege is needed.
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -pthread -Icore core/tests/thread_nice_test.cpp -o thread_nice_test
#include <cstdio>
#include <thread>

#include "util/ThreadNice.hpp"

namespace util = geode::util;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        failures++;
    }
}
}  // namespace

int main() {
    static_assert(util::kDisplayNice < util::kFeedNice && util::kFeedNice < util::kDefaultNice);
    static_assert(util::kDisplayNice >= -8, "never above THREAD_PRIORITY_URGENT_DISPLAY, the UI thread's and RenderThread's rank");
    static_assert(util::kNiceInherited > 19, "the mark is no nice a thread can have");
    check(util::threadNice() == util::kNiceInherited, "a thread has set no nice until it does");
    check(util::setThreadNice(util::kDisplayNice), "off Android a nice is always taken");
    check(util::threadNice() == util::kDisplayNice, "the nice set is the one read back");
    int spawned = 0;
    std::thread([&] { spawned = util::threadNice(); }).join();
    check(spawned == util::kNiceInherited, "a spawned thread does not see its spawner's nice");
    std::thread([&] {
        util::setThreadNice(util::kFeedNice);
        spawned = util::threadNice();
    }).join();
    check(spawned == util::kFeedNice && util::threadNice() == util::kDisplayNice, "each thread keeps its own");
    if (failures == 0) std::printf("thread_nice_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
