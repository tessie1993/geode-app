// Host test for core/player: the transport half of the native player (play and pause, seek, the
// previous-restarts rule, the A-B loop, progress from the decode thread). Pure logic: no clock, no thread.
//
// What is pinned is what QueueController.kt, PlaybackQueue.PREV_RESTART_MS and the native A-B rules
// (core/audio/dsp/PlaybackControls) did before: previous restarts the track when it is more than the
// restart time in; a seek is held to the track, and ignored while the length is unknown; A marks, B marks
// more than the minimum loop length on, a third press clears; reaching B goes back to A.
//
// Build and run (one command):
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/player_transport_test.cpp core/player/*.cpp
//   -o player_transport_test && ./player_transport_test
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "player/Player.hpp"

using namespace geode::player;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

constexpr std::int64_t kLength = 200000;  // every test track lasts this long, in milliseconds
constexpr std::int64_t kRestartMs = static_cast<std::int64_t>(kPrevRestartSeconds * 1000.0);

Player started(int n, std::int64_t durationMs = kLength) {
    std::vector<Track> list;
    for (int i = 0; i < n; ++i) list.push_back(Track{static_cast<TrackId>(i + 1), durationMs});
    Player p;
    p.apply(SetQueue{list, 0});
    p.takeEvents();
    return p;
}

TrackId current(const Player& p) { return p.state().current.value_or(-1); }

int count(const std::vector<Event>& events, Event::Kind kind) {
    return static_cast<int>(std::count_if(events.begin(), events.end(), [&](const Event& e) { return e.kind == kind; }));
}

void thePreviousRuleHasAThreshold() {
    check(kPrevRestartSeconds == 3.0, "previous restarts a track once it is more than three seconds in");

    Player p = started(3);
    p.apply(Next{});
    p.apply(Tick{kRestartMs + 1});
    p.takeEvents();
    p.apply(Previous{});
    check(current(p) == 2 && p.state().positionMs == 0, "past the threshold: previous restarts this track");
    const std::vector<Event> ev = p.takeEvents();
    check(count(ev, Event::Kind::Seeked) == 1 && count(ev, Event::Kind::TrackChanged) == 0, "as a seek, not a track change");

    p.apply(Tick{kRestartMs});
    p.apply(Previous{});
    check(current(p) == 1, "at the threshold exactly: previous goes to the track before");

    p.apply(Tick{1});
    p.apply(Previous{});
    check(current(p) == 3, "near the start of the first track: previous wraps to the last");

    Player e = started(2);
    e.apply(TrackEnded{});
    e.apply(TrackEnded{});
    e.apply(Previous{});
    check(current(e) == 2 && e.state().positionMs == 0 && !e.state().ended && e.state().playing, "after the end, previous replays the last track");
}

void seekIsHeldToTheTrack() {
    Player p = started(2);
    p.apply(Seek{50000});
    check(p.state().positionMs == 50000, "a seek lands where asked");
    p.apply(Seek{-300});
    check(p.state().positionMs == 0, "below the start it lands on the start");
    p.apply(Seek{kLength * 3});
    check(p.state().positionMs == kLength, "past the end it lands on the end");
    check(p.state().playing, "a seek does not pause");
    const std::vector<Event> ev = p.takeEvents();
    check(count(ev, Event::Kind::Seeked) == 3 && ev.back().positionMs == kLength, "each seek is told once, as it landed");

    Player unknown = started(2, 0);
    unknown.apply(Seek{5000});
    check(unknown.state().positionMs == 0 && unknown.takeEvents().empty(), "with the length unknown a seek is ignored");
    unknown.apply(SetDuration{1, kLength});
    unknown.apply(Seek{5000});
    check(unknown.state().positionMs == 5000 && unknown.state().durationMs == kLength, "until the decoder reports it");

    Player other = started(2);
    other.apply(SetDuration{2, 4000});
    check(other.state().durationMs == kLength, "the length of a track that is not playing changes nothing yet");
    other.apply(Next{});
    check(other.state().durationMs == 4000, "and is the length when it plays");
}

void seekingAnEndedQueuePlaysAgain() {
    Player p = started(1);
    p.apply(TrackEnded{});
    check(p.state().ended && !p.state().playing, "the queue has ended");
    p.apply(Seek{1000});
    check(!p.state().ended && p.state().playing && p.state().positionMs == 1000, "a seek brings it back to life");

    p.apply(TrackEnded{});
    p.takeEvents();
    p.apply(Play{});
    check(!p.state().ended && p.state().playing && p.state().positionMs == 0, "play after the end replays the track");
    check(count(p.takeEvents(), Event::Kind::PlayingChanged) == 1, "and the change is told once");
}

void playAndPauseAreToldOnceEach() {
    Player p = started(2);
    p.apply(Pause{});
    check(!p.state().playing, "pause pauses");
    check(count(p.takeEvents(), Event::Kind::PlayingChanged) == 1, "one event");
    p.apply(Pause{});
    check(p.takeEvents().empty(), "pausing a paused player is not news");
    p.apply(Play{});
    const std::vector<Event> ev = p.takeEvents();
    check(p.state().playing && count(ev, Event::Kind::PlayingChanged) == 1 && ev[0].playing, "play plays, and says so");
    p.apply(Play{});
    check(p.takeEvents().empty(), "playing a playing player is not news");
    p.apply(Pause{});
    p.takeEvents();
    p.apply(Next{});
    check(!p.state().playing && current(p) == 2, "a skip keeps a pause");
}

void progressComesFromTheDecodeThread() {
    Player p = started(2);
    p.apply(Tick{4000});
    check(p.state().positionMs == 4000, "a tick is the position");
    p.apply(Tick{-5});
    check(p.state().positionMs == 0, "never below the start");
    check(p.takeEvents().empty(), "progress is state, not an event");
    p.apply(TrackEnded{});
    check(current(p) == 2 && p.state().positionMs == 0, "the end of a track moves on");
    p.apply(Tick{3000});
    p.apply(TrackEnded{});
    p.apply(Tick{9000});
    check(p.state().ended && p.state().positionMs == kLength, "a tick after the queue ended changes nothing");
}

void theAbLoopMarksWrapsAndClears() {
    Player p = started(2);
    p.apply(Tick{10000});
    p.apply(CycleAbLoop{});
    check(p.state().abLoop.startMs == 10000 && p.state().abLoop.endMs < 0, "the first press marks A");
    p.apply(Tick{10000 + kAbLoopMinMs});
    p.apply(CycleAbLoop{});
    check(p.state().abLoop.startMs == 10000 + kAbLoopMinMs && p.state().abLoop.endMs < 0, "a point not past the minimum length moves A there");
    p.apply(Tick{30000});
    p.apply(CycleAbLoop{});
    check(p.state().abLoop.startMs == 10000 + kAbLoopMinMs && p.state().abLoop.endMs == 30000, "a point far enough past A marks B");

    p.takeEvents();
    p.apply(Tick{29999});
    check(p.state().positionMs == 29999 && p.takeEvents().empty(), "before B the track plays on");
    p.apply(Tick{30000});
    check(p.state().positionMs == 10000 + kAbLoopMinMs, "at B it goes back to A");
    const std::vector<Event> ev = p.takeEvents();
    check(count(ev, Event::Kind::Seeked) == 1 && ev[0].loop && ev[0].positionMs == 10000 + kAbLoopMinMs, "told once, as a loop");

    p.apply(CycleAbLoop{});
    check(p.state().abLoop.startMs < 0 && p.state().abLoop.endMs < 0, "with both set, a press clears");
    p.apply(Tick{40000});
    check(p.state().positionMs == 40000, "and the track plays on past B");
}

void anAbLoopWithOnlyAIsNotALoop() {
    Player p = started(2);
    p.apply(Tick{5000});
    p.apply(CycleAbLoop{});
    p.apply(Tick{150000});
    check(p.state().positionMs == 150000 && p.takeEvents().empty(), "A alone never seeks");
}

void theAbLoopEndsWithTheTrack() {
    Player p = started(3);
    p.apply(Tick{1000});
    p.apply(CycleAbLoop{});
    p.apply(Tick{9000});
    p.apply(CycleAbLoop{});
    p.apply(Next{});
    check(p.state().abLoop.startMs < 0 && p.state().abLoop.endMs < 0, "next clears the loop");

    p.apply(Tick{1000});
    p.apply(CycleAbLoop{});
    p.apply(Previous{});
    check(p.state().abLoop.startMs < 0, "previous clears it");

    p.apply(Tick{1000});
    p.apply(CycleAbLoop{});
    p.apply(Tick{9000});
    p.apply(CycleAbLoop{});
    p.apply(ClearAbLoop{});
    check(p.state().abLoop.startMs < 0 && p.state().abLoop.endMs < 0, "clear clears it");

    p.apply(Tick{1000});
    p.apply(CycleAbLoop{});
    p.apply(PlayPosition{2});
    check(p.state().abLoop.startMs < 0, "a track picked from the queue does not keep another track's loop");

    p.apply(Tick{4000});
    p.apply(CycleAbLoop{});
    p.apply(TrackEnded{});
    check(p.state().abLoop.startMs < 0, "a track that ends on its own does not carry the loop on");
}

void whenTheLoopRestartsTheTrackIsNotNew() {
    Player p = started(2);
    p.apply(Tick{1000});
    p.apply(CycleAbLoop{});
    p.apply(Tick{5000});
    p.apply(CycleAbLoop{});
    p.takeEvents();
    for (int i = 0; i < 3; ++i) {
        p.apply(Tick{5000});
        p.apply(Tick{1500});
    }
    const std::vector<Event> ev = p.takeEvents();
    check(count(ev, Event::Kind::Seeked) == 3 && count(ev, Event::Kind::TrackChanged) == 0, "each wrap is one seek and no track change");
}

}  // namespace

int main() {
    thePreviousRuleHasAThreshold();
    seekIsHeldToTheTrack();
    seekingAnEndedQueuePlaysAgain();
    playAndPauseAreToldOnceEach();
    progressComesFromTheDecodeThread();
    theAbLoopMarksWrapsAndClears();
    anAbLoopWithOnlyAIsNotALoop();
    theAbLoopEndsWithTheTrack();
    whenTheLoopRestartsTheTrackIsNotNew();
    if (failures == 0) std::printf("player_transport_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
