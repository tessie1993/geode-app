// Host-side checks for a track take's choices on song time (core/signal/timeline/TakeTimeline, #457). Built and run
// by CI ("Native host tests" in .github/workflows/android.yml) with the system compiler.
//
// What is pinned: the timeline round-trips through its file exactly (style, preset, every param bit for bit, the
// seed, the clock); recording keeps only changes, thins a drag, punches in on a seek back and stops at its cap;
// replay finds the choice in force at any song position and lists every style switch; a mic or capture take does
// not render again; a damaged file is refused rather than half read; Render again's replay maps a segment's render
// time onto song time, flags only the frames where the choice changed, and keeps its own copy and the take's seed.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "signal/timeline/TakeTimeline.hpp"

using geode::signal::TakeChoice;
using geode::signal::TakeReplay;
using geode::signal::TakeTimeline;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr int kFields = 163;

std::vector<float> params(float seed) {
    std::vector<float> p(kFields);
    for (int i = 0; i < kFields; ++i) p[static_cast<size_t>(i)] = std::sin(seed * 0.37f + static_cast<float>(i)) * 3.0f + 1e-7f * i;
    return p;
}

bool bitsEqual(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

void roundTripsExactly() {
    TakeTimeline t(0xFEEDFACECAFEBEEFull);
    auto p = params(1.0f);
    t.note(0, "neon_chrome_sea", "", p.data(), kFields);
    p[3] = 0.123456789f;
    p[160] = -7.5e-12f;
    t.note(1'000, "neon_chrome_sea", "", p.data(), kFields);
    t.note(2'000, "milkdrop", "/presets/a\ttab\\back\nline.milk", p.data(), kFields);
    p = params(9.0f);
    t.note(5'000, "bulb_01", "", p.data(), kFields);
    const std::string text = t.encode();
    const auto back = TakeTimeline::decode(text);
    check(back.has_value(), "the file reads back");
    if (!back) return;
    check(back->seed() == 0xFEEDFACECAFEBEEFull, "the seed survives");
    check(back->songTime(), "a track take stays on song time");
    check(back->choices().size() == t.choices().size(), "every choice survives");
    bool same = back->choices().size() == t.choices().size();
    for (size_t i = 0; same && i < t.choices().size(); ++i) {
        const TakeChoice& a = t.choices()[i];
        const TakeChoice& b = back->choices()[i];
        same = a.atMs == b.atMs && a.sceneId == b.sceneId && a.milkPath == b.milkPath && bitsEqual(a.params, b.params);
    }
    check(same, "time, style, preset (tabs, newlines and backslashes too) and every param bit for bit");
    check(back->encode() == text, "encoding the read-back gives the same file");
}

void recordingKeepsChangesOnly() {
    TakeTimeline t;
    auto p = params(2.0f);
    t.note(0, "a", "", p.data(), kFields);
    t.note(500, "a", "", p.data(), kFields);
    check(t.choices().size() == 1, "an unchanged look records nothing");
    // A drag: a change every 16 ms becomes one choice per 50 ms.
    for (int i = 1; i <= 60; ++i) {
        p[0] = static_cast<float>(i);
        t.note(1'000 + i * 16, "a", "", p.data(), kFields);
    }
    check(t.choices().size() < 25 && t.choices().size() > 10, "a 1 s drag of 60 steps is thinned to a few choices a second");
    check(t.choices().back().params[0] == 60.0f, "the drag ends on its last value");
    // A style change right after a step is its own choice.
    t.note(t.endMs() + 5, "b", "", p.data(), kFields);
    check(t.choices().back().sceneId == "b", "a style change is never thinned away");
    check(t.styleChanges().size() == 2, "both styles are switches");
}

void aSeekBackPunchesIn() {
    TakeTimeline t;
    auto p = params(3.0f);
    t.note(0, "a", "", p.data(), kFields);
    t.note(10'000, "b", "", p.data(), kFields);
    t.note(20'000, "c", "", p.data(), kFields);
    t.note(15'000, "d", "", p.data(), kFields);
    check(t.choices().size() == 3, "a seek back drops what was recorded from there");
    check(t.choices().back().sceneId == "d" && t.choices().back().atMs == 15'000, "and records on from the new position");
}

void replayFindsTheChoiceInForce() {
    TakeTimeline t;
    check(t.stateAt(0) == nullptr && !t.rendersAgain(), "an empty take replays nothing and does not render again");
    auto p = params(4.0f);
    t.note(1'000, "a", "", p.data(), kFields);
    t.note(3'000, "b", "x.milk", p.data(), kFields);
    check(t.stateAt(0)->sceneId == "a", "before the first choice, the opening look");
    check(t.stateAt(2'999)->sceneId == "a", "just before a switch, the old style");
    check(t.stateAt(3'000)->sceneId == "b" && t.stateAt(3'000)->milkPath == "x.milk", "on the switch, the new one");
    check(t.stateAt(99'000)->sceneId == "b", "after the last choice, the last look");
    const auto switches = t.styleChanges();
    check(switches.size() == 2 && switches[1]->atMs == 3'000, "the switches StyleSchedule plays");
    check(t.rendersAgain(), "a track take with choices renders again");
}

void aWallTimeTakeDoesNotRenderAgain() {
    TakeTimeline t(7, false);
    auto p = params(5.0f);
    t.note(0, "a", "", p.data(), kFields);
    check(!t.rendersAgain(), "a mic or capture take does not render again");
    const auto back = TakeTimeline::decode(t.encode());
    check(back && !back->songTime() && !back->rendersAgain(), "and still does not after a round trip");
}

void aFullTimelineKeepsNoMore() {
    TakeTimeline t;
    std::vector<float> p(4, 0.0f);
    for (size_t i = 0; i < TakeTimeline::kMaxChoices + 50; ++i) {
        p[0] = static_cast<float>(i);
        t.note(static_cast<int64_t>(i) * 100, i % 2 ? "a" : "b", "", p.data(), 4);
    }
    check(t.full() && t.choices().size() == TakeTimeline::kMaxChoices, "a full timeline keeps no more choices");
}

void aDamagedFileIsRefused() {
    check(!TakeTimeline::decode("").has_value(), "an empty file is refused");
    check(!TakeTimeline::decode("geode-take 2\n").has_value(), "a file from a later version is refused");
    check(!TakeTimeline::decode("geode-take 1\nc 0\ta\t\t2\t0:1,9:2\n").has_value(), "a param past the count is refused");
    check(!TakeTimeline::decode("geode-take 1\nc 10\ta\t\t1\t0:1\nc 5\ta\t\t1\t0:2\n").has_value(), "times going back are refused");
    check(!TakeTimeline::decode("geode-take 1\nwhat\n").has_value(), "an unknown line is refused");
    const auto ok = TakeTimeline::decode("geode-take 1\nseed 3\nclock song\nc 0\ta\t\t2\t0:1,1:2\nc 40\ta\t\t2\t1:5\n");
    check(ok && ok->choices().size() == 2 && ok->choices()[1].params[0] == 1.0f && ok->choices()[1].params[1] == 5.0f,
          "a choice carries every param it does not change from the one before");
}

void renderAgainReplaysOnSongTime() {
    TakeTimeline t(42);
    auto a = params(6.0f);
    auto b = params(7.0f);
    t.note(10'000, "a", "", a.data(), kFields);
    t.note(12'000, "a", "", b.data(), kFields);
    t.note(15'000, "b", "y.milk", b.data(), kFields);
    // A segment that starts 11 s into the song: render time 0 is song 11 000.
    TakeReplay replay(t, 11'000);
    t = TakeTimeline();  // the shell may free its handle: the replay holds its own copy
    check(replay.seed() == 42, "the replay keeps the take's seed for the director");
    auto f = replay.at(0);
    check(f.choice && f.changed && bitsEqual(f.choice->params, a), "the first frame applies the choice in force at song 11 s");
    f = replay.at(500);
    check(f.choice && !f.changed, "a frame under the same choice pushes nothing");
    f = replay.at(1'000);
    check(f.choice && f.changed && bitsEqual(f.choice->params, b), "song 12 s: the drag's next choice");
    f = replay.at(3'999);
    check(f.choice && !f.changed && f.choice->sceneId == "a", "just before the switch, still the old style");
    f = replay.at(4'000);
    check(f.choice && f.changed && f.choice->sceneId == "b" && f.choice->milkPath == "y.milk", "song 15 s: the switch");
    f = replay.at(0);
    check(f.choice && f.changed && bitsEqual(f.choice->params, a), "a retried segment starts again from its opening choice");
    TakeReplay copy = replay;
    check(!copy.at(0).changed, "a copy carries on from where it was");

    TakeReplay empty(TakeTimeline(1), 0);
    const auto none = empty.at(0);
    check(none.choice == nullptr && !none.changed, "an empty take replays nothing");
    const auto file = TakeTimeline::decode("geode-take 1\nseed 9\nclock song\nc 5000\ta\t\t2\t0:1,1:2\n");
    check(file.has_value(), "a stored take decodes for a replay");
    if (file) {
        TakeReplay early(*file, 0);
        const auto first = early.at(0);
        check(first.choice && first.choice->atMs == 5000 && early.seed() == 9, "before the first choice, the opening look");
    }
}

}  // namespace

int main() {
    roundTripsExactly();
    recordingKeepsChangesOnly();
    aSeekBackPunchesIn();
    replayFindsTheChoiceInForce();
    aWallTimeTakeDoesNotRenderAgain();
    aFullTimelineKeepsNoMore();
    aDamagedFileIsRefused();
    renderAgainReplaysOnSongTime();
    if (failures == 0) std::printf("take_timeline_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
