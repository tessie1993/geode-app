// Host-side checks for core/viz/CacheSentinels (#381): the program-binary cache's crash sentinel,
// shared by every Renderer in the process. Built and run by CI ("Native host tests" in
// .github/workflows/android.yml) with the system compiler; no GL, no files.
//
// What is pinned: only the first open of a namespace in a process runs the crash check (so a second
// renderer never reads the first one's live sentinel as a crash), and arming is counted (the file
// exists from the first holder's start to the last holder's end).
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "viz/CacheSentinels.hpp"

using geode::viz::CacheSentinels;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

void firstOpenOnlyChecks() {
    CacheSentinels s;
    int checks = 0;
    check(s.open("ns-a", [&] { checks++; }), "first open of a namespace is first");
    check(!s.open("ns-a", [&] { checks++; }), "second open of the same namespace is not first");
    check(checks == 1, "the crash check runs once per namespace per process");
    check(s.open("ns-b", [&] { checks++; }), "another namespace (another driver) gets its own check");
    check(checks == 2, "and its check runs");
    s.resetForTest();
    check(s.open("ns-a", [] {}), "a new process checks again");
}

// The bug: renderer A arms the sentinel; renderer B opens meanwhile. B must not run the crash check.
void secondRendererDuringLoad() {
    CacheSentinels s;
    bool fileExists = false;
    bool crashSeen = false;
    s.open("ns", [&] { crashSeen = crashSeen || fileExists; });
    s.arm("ns/sentinel", [&] { fileExists = true; });
    s.open("ns", [&] { crashSeen = crashSeen || fileExists; });
    s.disarm("ns/sentinel", [&] { fileExists = false; });
    check(!crashSeen, "a live load's sentinel is never read as a crash by a second renderer");
    check(!fileExists, "the sentinel is gone once the load ends");
}

void countedArming() {
    CacheSentinels s;
    int creates = 0;
    int removes = 0;
    bool file = false;
    auto create = [&] { creates++; file = true; };
    auto remove = [&] { removes++; file = false; };
    check(s.arm("x", create), "first holder creates the file");
    check(!s.arm("x", create), "second holder shares it");
    check(!s.disarm("x", remove), "first to finish leaves the file for the other");
    check(file, "the file still guards the other load");
    check(s.disarm("x", remove), "last to finish removes it");
    check(!file && creates == 1 && removes == 1, "one create and one remove");
    check(!s.disarm("x", remove), "an unmatched disarm does nothing");
    check(removes == 1, "and removes nothing");
}

void concurrentArming() {
    CacheSentinels s;
    int live = 0;
    bool bad = false;
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; t++) {
        threads.emplace_back([&] {
            for (int i = 0; i < 2000; i++) {
                s.arm("y", [&] { if (live != 0) bad = true; live = 1; });
                s.disarm("y", [&] { live = 0; });
            }
        });
    }
    for (auto& t : threads) t.join();
    check(!bad, "the file is never created while it already exists");
    check(live == 0, "no sentinel is left after every load ends");
}

}  // namespace

int main() {
    firstOpenOnlyChecks();
    secondRendererDuringLoad();
    countedArming();
    concurrentArming();
    if (failures == 0) std::printf("cache_sentinels_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
