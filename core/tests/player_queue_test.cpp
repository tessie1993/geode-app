// Host test for core/player: the queue half of the native player (order, shuffle, repeat, next and
// previous, edits, the end of the queue). Pure logic: commands in, a PlayerState and events out; no
// clock, no thread, no file.
//
// What is pinned is what the Kotlin queue and ExoPlayer did before the queue moved to C++: a manual next
// and previous wrap in every repeat mode; a track that ends on its own follows the repeat mode; the
// shuffle order is a permutation that keeps the current track; what the user sees is what plays.
//
// Build and run (one command):
//   g++ -std=c++20 -O1 -Wall -Wextra -Werror -Icore core/tests/player_queue_test.cpp core/player/*.cpp
//   -o player_queue_test && ./player_queue_test
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <set>
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

std::vector<Track> tracks(int n) {
    std::vector<Track> out;
    for (int i = 0; i < n; ++i) out.push_back(Track{static_cast<TrackId>(i + 1), kLength});
    return out;
}

Player started(int n, int startIndex = 0) {
    Player p;
    p.apply(SetQueue{tracks(n), startIndex});
    p.takeEvents();
    return p;
}

TrackId current(const Player& p) { return p.state().current.value_or(-1); }

Event::Failure lastFailure(const std::vector<Event>& events) {
    Event::Failure out = Event::Failure::Skipped;
    for (const Event& e : events) {
        if (e.kind == Event::Kind::PlaybackFailed) out = e.failure;
    }
    return out;
}

int count(const std::vector<Event>& events, Event::Kind kind) {
    return static_cast<int>(std::count_if(events.begin(), events.end(), [&](const Event& e) { return e.kind == kind; }));
}

void aQueueStartsAtItsIndex() {
    Player p;
    p.apply(SetQueue{tracks(5), 2});
    const PlayerState s = p.state();
    check(s.size == 5 && s.position == 2 && current(p) == 3, "the queue starts at the index it was given");
    check(s.playing && !s.ended && s.positionMs == 0, "and plays from the top");
    const std::vector<Event> ev = p.takeEvents();
    check(count(ev, Event::Kind::TrackChanged) == 1 && count(ev, Event::Kind::QueueChanged) == 1, "one track change, one queue change");
    check(p.takeEvents().empty(), "events are handed over once");

    Player q;
    q.apply(SetQueue{tracks(3), 99, -5});
    check(q.state().position == 2 && q.state().positionMs == 0, "a saved index and position are held inside the queue");
    q.apply(SetQueue{tracks(3), -4, 999999});
    check(q.state().position == 0 && q.state().positionMs == kLength, "an index below 0 is 0, a position past the end is the end");

    SetQueue resume{tracks(3), 1, 5000};
    resume.play = false;
    q.apply(resume);
    check(!q.state().playing && q.state().positionMs == 5000, "a resumed queue waits for play");

    q.apply(SetQueue{});
    check(q.state().size == 0 && !q.state().current && !q.state().playing, "nothing to resume leaves the player empty");
}

void manualSkipsWrapInEveryRepeatMode() {
    for (Repeat r : {Repeat::Off, Repeat::One, Repeat::All}) {
        Player p = started(3, 2);
        p.apply(SetRepeat{r});
        p.apply(Next{});
        check(current(p) == 1 && p.state().position == 0, "next from the last track goes to the first, in every repeat mode");
        p.apply(Previous{});
        check(current(p) == 3, "previous from the first goes to the last, in every repeat mode");
        p.apply(Previous{});
        check(current(p) == 2, "previous steps back");
        p.apply(Next{});
        check(current(p) == 3, "next steps on, even in repeat one");
    }
}

void aTrackThatEndsFollowsTheRepeatMode() {
    Player off = started(2, 0);
    off.apply(TrackEnded{});
    check(current(off) == 2 && off.state().positionMs == 0, "repeat off: the next track follows");
    off.takeEvents();
    off.apply(TrackEnded{});
    check(off.state().ended && !off.state().playing && current(off) == 2, "repeat off: the last track ends the queue");
    check(count(off.takeEvents(), Event::Kind::QueueEnded) == 1, "and says so once");

    Player one = started(2, 0);
    one.apply(SetRepeat{Repeat::One});
    one.apply(TrackEnded{});
    check(current(one) == 1 && !one.state().ended && one.state().playing, "repeat one: the same track again");
    const std::vector<Event> ev = one.takeEvents();
    check(count(ev, Event::Kind::TrackChanged) == 1 && ev[0].reason == Event::Reason::Repeat, "and it is a track change by repeat");

    Player all = started(2, 1);
    all.apply(SetRepeat{Repeat::All});
    all.apply(TrackEnded{});
    check(current(all) == 1 && all.state().playing, "repeat all: the queue starts over");

    Player single = started(1, 0);
    single.apply(SetRepeat{Repeat::All});
    single.apply(TrackEnded{});
    check(current(single) == 1 && single.state().playing && !single.state().ended, "repeat all on one track plays it again");
}

void whatFollowsIsKnownForTheGaplessHandOver() {
    Player p = started(3, 1);
    check(p.state().hasNext && p.state().upNext == 3, "the track after the current one is up next");
    p.apply(Next{});
    check(!p.state().hasNext && !p.state().upNext, "repeat off: nothing follows the last track");
    p.apply(SetRepeat{Repeat::All});
    check(p.state().hasNext && p.state().upNext == 1, "repeat all: the first follows the last");
    p.apply(SetRepeat{Repeat::One});
    check(!p.state().hasNext, "repeat one: a skip has nothing after the last track");
    check(p.state().upNext == 3, "but the track that follows on its own is itself");
}

std::vector<TrackId> order(const Player& p) { return p.state().order; }

void shuffleIsAPermutationThatKeepsTheCurrentTrack() {
    Player p = started(30, 11);
    p.apply(SetShuffle{true, 7});
    const std::vector<TrackId> o = order(p);
    std::vector<TrackId> sorted = o;
    std::sort(sorted.begin(), sorted.end());
    std::vector<TrackId> all;
    for (const Track& t : tracks(30)) all.push_back(t.id);
    check(sorted == all, "the shuffled order holds every track once");
    check(current(p) == 12 && p.state().position == 0 && o[0] == 12, "the current track stays current, first in the order");
    check(p.state().shuffle, "the state says so");
    check(count(p.takeEvents(), Event::Kind::QueueChanged) == 1, "a new order is one queue change");

    Player same = started(30, 11);
    same.apply(SetShuffle{true, 7});
    check(order(same) == o, "the same seed gives the same order");

    bool differs = false;
    for (std::uint64_t seed = 8; seed < 12; ++seed) {
        Player other = started(30, 11);
        other.apply(SetShuffle{true, seed});
        differs = differs || order(other) != o;
    }
    check(differs, "another seed gives another order");

    // Walking next from the start of the order visits every track once, then wraps.
    std::set<TrackId> seen{current(p)};
    for (int i = 1; i < 30; ++i) {
        p.apply(Next{});
        seen.insert(current(p));
    }
    check(seen.size() == 30, "next walks the whole shuffled order");
    p.apply(Next{});
    check(current(p) == 12, "and wraps to its first");

    p.apply(SetShuffle{false, 0});
    check(current(p) == 12 && p.state().position == 11, "shuffle off: the same track, at its place in the list");
    check(order(p) == all, "and the list order is back");
}

void shuffleOfOneOrNoneIsHarmless() {
    Player p;
    p.apply(SetShuffle{true, 3});
    check(p.state().size == 0 && p.state().shuffle, "shuffle on an empty queue is only a mode");
    p.apply(SetQueue{tracks(1), 0});
    check(order(p) == std::vector<TrackId>{1}, "a one-track queue shuffles to itself");
    p.apply(SetQueue{tracks(6), 4});
    check(current(p) == 5 && order(p)[0] == 5, "a queue set while shuffled keeps its start track first");
}

void theQueueTheUserSeesIsTheOrderItPlays() {
    Player p = started(6, 0);
    p.apply(SetShuffle{true, 5});
    const std::vector<TrackId> seen = order(p);
    p.apply(PlayPosition{3});
    check(current(p) == seen[3] && p.state().position == 3 && p.state().positionMs == 0 && p.state().playing, "a pick plays that place of the order");
    p.apply(Next{});
    check(current(p) == seen[4], "next follows the order the user sees");
}

void removingTheCurrentTrackPlaysTheNextOne() {
    Player p = started(4, 1);
    p.apply(Seek{9000});
    p.takeEvents();
    p.apply(Remove{1});
    check(p.state().size == 3 && current(p) == 3 && p.state().position == 1, "the track after the removed one takes its place");
    check(p.state().positionMs == 0 && p.state().playing, "from its start, still playing");
    const std::vector<Event> ev = p.takeEvents();
    check(count(ev, Event::Kind::TrackChanged) == 1 && count(ev, Event::Kind::QueueChanged) == 1, "one track change and one queue change");

    p.apply(Remove{0});
    check(current(p) == 3 && p.state().position == 0, "removing another track keeps the current one");
    check(count(p.takeEvents(), Event::Kind::TrackChanged) == 0, "and changes no track");

    Player last = started(3, 2);
    last.apply(Remove{2});
    check(last.state().ended && current(last) == 2 && !last.state().playing, "removing the last track, with none after it, ends the queue");
    check(count(last.takeEvents(), Event::Kind::QueueEnded) == 1, "and says so");

    Player wrap = started(3, 2);
    wrap.apply(SetRepeat{Repeat::All});
    wrap.apply(Remove{2});
    check(current(wrap) == 1 && wrap.state().playing, "repeat all: it wraps to the first");

    Player gone = started(1, 0);
    gone.apply(Remove{0});
    check(gone.state().size == 0 && !gone.state().current && !gone.state().playing, "removing the only track empties the queue");

    Player bad = started(2, 0);
    bad.takeEvents();
    bad.apply(Remove{5});
    bad.apply(Remove{-1});
    check(bad.state().size == 2 && bad.takeEvents().empty(), "a place that is not there is ignored");
}

void removingBeforeTheCurrentTrackKeepsIt() {
    Player p = started(5, 3);
    p.apply(Remove{0});
    check(current(p) == 4 && p.state().position == 2, "the current track keeps playing as the list shifts");
    check(p.state().positionMs == 0 && count(p.takeEvents(), Event::Kind::TrackChanged) == 0, "no track change");
}

void movingTracksFollowsTheCurrentOne() {
    Player p = started(5, 2);
    p.apply(Move{2, 4});
    check(current(p) == 3 && p.state().position == 4, "moving the current track takes it along");
    check((order(p) == std::vector<TrackId>{1, 2, 4, 5, 3}), "the others close the gap");
    p.apply(Move{0, 4});
    check(current(p) == 3 && p.state().position == 3, "moving another track past it shifts the current place");
    p.apply(Move{4, 0});
    check(current(p) == 3 && p.state().position == 4, "and back");
    p.takeEvents();
    p.apply(Move{1, 1});
    p.apply(Move{1, 9});
    check(p.takeEvents().empty(), "a move to the same place or out of range is ignored");

    Player s = started(6, 0);
    s.apply(SetShuffle{true, 2});
    const std::vector<TrackId> before = order(s);
    s.apply(Move{1, 4});
    std::vector<TrackId> expect = before;
    std::rotate(expect.begin() + 1, expect.begin() + 2, expect.begin() + 5);
    check(order(s) == expect, "shuffled: a move changes the order that plays, as shown");
    check(current(s) == before[0], "and keeps the current track");
}

void insertsGoAfterTheCurrentTrackOrAtTheEnd() {
    Player p = started(3, 0);
    p.apply(Insert{Insert::At::AfterCurrent, {Track{40, kLength}}});
    check((order(p) == std::vector<TrackId>{1, 40, 2, 3}), "play next goes right after the current track");
    p.apply(Insert{Insert::At::End, {Track{50, kLength}, Track{60, kLength}}});
    check((order(p) == std::vector<TrackId>{1, 40, 2, 3, 50, 60}), "enqueue goes at the end");
    check(current(p) == 1 && p.state().position == 0, "the current track is not disturbed");
    p.apply(Next{});
    check(current(p) == 40, "next plays what was put next");

    Player s = started(5, 2);
    s.apply(SetShuffle{true, 9});
    s.apply(Insert{Insert::At::AfterCurrent, {Track{40, kLength}}});
    check(order(s)[1] == 40 && order(s)[0] == 3, "shuffled: play next still plays next");
    s.apply(Insert{Insert::At::End, {Track{50, kLength}}});
    check(order(s).back() == 50 && s.state().size == 7, "shuffled: enqueue goes last");

    Player e;
    e.apply(Insert{Insert::At::AfterCurrent, {Track{7, kLength}}});
    check(current(e) == 7 && !e.state().playing && e.state().position == 0, "into an empty queue: the track is current, and waits");
}

void theEndOfTheQueueStopsOrStartsOver() {
    Player stop = started(2, 1);
    stop.apply(TrackEnded{});
    check(stop.state().ended && !stop.state().playing, "stop: the queue ends where it is");
    check(stop.state().positionMs == kLength, "at the end of its last track");

    Player again = started(3, 2);
    again.apply(SetRestartAtEnd{true});
    again.apply(TrackEnded{});
    check(again.state().position == 0 && current(again) == 1 && again.state().playing, "start again: the first track plays");
    const std::vector<Event> ev = again.takeEvents();
    check(count(ev, Event::Kind::QueueEnded) == 0 && count(ev, Event::Kind::TrackChanged) == 1, "as one track change");

    Player shuffled = started(6, 0);
    shuffled.apply(SetShuffle{true, 4});
    shuffled.apply(SetRestartAtEnd{true});
    const TrackId first = order(shuffled)[0];
    for (int i = 0; i < 5; ++i) shuffled.apply(TrackEnded{});
    shuffled.apply(TrackEnded{});
    check(current(shuffled) == first && shuffled.state().position == 0, "shuffled: starts over from the first of the order");

    Player more = started(2, 1);
    more.apply(TrackEnded{});
    more.apply(Insert{Insert::At::End, {Track{9, kLength}}});
    more.apply(PlayPosition{2});
    check(current(more) == 9 && more.state().playing && !more.state().ended, "more tracks added after the end play on");
}

void aRepeatModeOverridesTheQueueEnd() {
    Player p = started(2, 1);
    p.apply(SetRepeat{Repeat::All});
    p.apply(SetRestartAtEnd{false});
    p.apply(TrackEnded{});
    check(!p.state().ended && current(p) == 1, "with repeat on the queue never ends");
}

void aFailingTrackIsSkippedUntilThreeInARow() {
    Player p = started(6, 0);
    p.apply(TrackFailed{});
    check(current(p) == 2 && p.state().playing, "a track that will not play is skipped");
    const std::vector<Event> ev = p.takeEvents();
    check(count(ev, Event::Kind::PlaybackFailed) == 1 && ev[0].failure == Event::Failure::Skipped, "and the failure is told");
    p.apply(TrackFailed{});
    p.apply(TrackFailed{});
    check(!p.state().playing && current(p) == 3, "the third in a row stops it");
    const std::vector<Event> last = p.takeEvents();
    check(count(last, Event::Kind::PlaybackFailed) == 2 && lastFailure(last) == Event::Failure::SourceUnavailable, "with the source named as gone");

    Player q = started(6, 0);
    q.apply(TrackFailed{});
    q.apply(Tick{500});
    q.apply(TrackFailed{});
    q.apply(TrackFailed{});
    check(q.state().playing, "a track that played in between starts the count again");

    Player end = started(2, 1);
    end.apply(TrackFailed{});
    check(!end.state().playing && current(end) == 2, "nothing after the last track: stop");
    check(lastFailure(end.takeEvents()) == Event::Failure::EndOfQueue, "as the end of the queue");
}

void emptyQueueIgnoresTransportCommands() {
    Player p;
    p.apply(Next{});
    p.apply(Previous{});
    p.apply(Play{});
    p.apply(Seek{4000});
    p.apply(TrackEnded{});
    p.apply(Tick{4000});
    p.apply(PlayPosition{0});
    check(p.takeEvents().empty() && p.state().size == 0 && !p.state().playing, "nothing happens with nothing to play");
}

}  // namespace

int main() {
    aQueueStartsAtItsIndex();
    manualSkipsWrapInEveryRepeatMode();
    aTrackThatEndsFollowsTheRepeatMode();
    whatFollowsIsKnownForTheGaplessHandOver();
    shuffleIsAPermutationThatKeepsTheCurrentTrack();
    shuffleOfOneOrNoneIsHarmless();
    theQueueTheUserSeesIsTheOrderItPlays();
    removingTheCurrentTrackPlaysTheNextOne();
    removingBeforeTheCurrentTrackKeepsIt();
    movingTracksFollowsTheCurrentOne();
    insertsGoAfterTheCurrentTrackOrAtTheEnd();
    theEndOfTheQueueStopsOrStartsOver();
    aRepeatModeOverridesTheQueueEnd();
    aFailingTrackIsSkippedUntilThreeInARow();
    emptyQueueIgnoresTransportCommands();
    if (failures == 0) std::printf("player_queue_test: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
