#pragma once

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "player/Queue.hpp"
#include "player/Transport.hpp"

// The native player's logic: the queue and the transport as one deep module. Commands go in, a PlayerState
// snapshot and the events since the last take come out. Time and randomness are handed in (positions come
// from the decode thread, the shuffle seed from the caller); it reads no clock, opens no file, starts no
// thread. Tracks are opaque ids with a length the caller supplies.
namespace geode::player {

// Replaces the queue and makes `tracks[startIndex]` current, `startPositionMs` in. An empty list empties the
// player (nothing saved to resume). `play` false leaves it waiting for Play, as a resumed session does.
struct SetQueue {
    std::vector<Track> tracks;
    int startIndex = 0;
    std::int64_t startPositionMs = 0;
    bool play = true;
};
struct Play {};
struct Pause {};
// Held to the current track; ignored while its length is unknown.
struct Seek {
    std::int64_t positionMs = 0;
};
struct Next {};      // wraps in every repeat mode
struct Previous {};  // restarts the track once it is kPrevRestartSeconds in, else goes back, wrapping
// Plays the track at a place in the play order from its start (a pick from the queue list).
struct PlayPosition {
    int position = 0;
};
struct SetRepeat {
    Repeat repeat = Repeat::Off;
};
// On: a new order from `seed`, the current track first. Off: the list order.
struct SetShuffle {
    bool on = false;
    std::uint64_t seed = 0;
};
// When the queue ends with repeat off: stop (false) or play it again from its first track (true).
struct SetRestartAtEnd {
    bool on = false;
};
// One press: marks A, then B, then clears (see kAbLoopMinMs).
struct CycleAbLoop {};
struct ClearAbLoop {};
struct Insert {
    enum class At { AfterCurrent, End };
    At at = At::End;
    std::vector<Track> tracks;
};
struct Remove {
    int position = 0;
};
struct Move {
    int from = 0;
    int to = 0;
};
// The decoder learned a track's length.
struct SetDuration {
    TrackId id = 0;
    std::int64_t durationMs = 0;
};
// From the decode thread: the playout position of the current track, and that it ran out.
struct Tick {
    std::int64_t positionMs = 0;
};
struct TrackEnded {};
struct TrackFailed {};

using Command = std::variant<SetQueue, Play, Pause, Seek, Next, Previous, PlayPosition, SetRepeat, SetShuffle,
                             SetRestartAtEnd, CycleAbLoop, ClearAbLoop, Insert, Remove, Move, SetDuration, Tick,
                             TrackEnded, TrackFailed>;

struct PlayerState {
    bool playing = false;  // meant to play and not ended
    bool ended = false;    // the queue ran out on its last track
    int size = 0;
    int position = -1;                  // of the current track, in `order`
    std::optional<TrackId> current;
    std::int64_t positionMs = 0;
    std::int64_t durationMs = 0;        // 0 while unknown
    Repeat repeat = Repeat::Off;
    bool shuffle = false;
    bool restartAtEnd = false;
    AbLoop abLoop;
    bool hasNext = false;               // a skip has somewhere to go that is not wrapping around
    std::optional<TrackId> upNext;      // what plays when the current track ends on its own: open it early
    std::vector<TrackId> order;         // the play order, which is the queue as the user sees it
};

struct Event {
    enum class Kind { TrackChanged, QueueChanged, Seeked, PlayingChanged, QueueEnded, PlaybackFailed };
    enum class Reason { Requested, Skip, Auto, Repeat, Edit, QueueRestart, Error };
    enum class Failure { Skipped, EndOfQueue, SourceUnavailable };

    Kind kind = Kind::QueueChanged;
    Reason reason = Reason::Requested;   // TrackChanged
    Failure failure = Failure::Skipped;  // PlaybackFailed
    std::optional<TrackId> id;           // TrackChanged, QueueEnded (the last track), PlaybackFailed
    int position = -1;                   // TrackChanged: its place in the play order
    std::int64_t positionMs = 0;         // TrackChanged (where it starts), Seeked (where it landed)
    bool loop = false;                   // Seeked: the A-B loop did it
    bool playing = false;                // PlayingChanged
};

class Player {
public:
    void apply(const Command& command);
    PlayerState state() const;
    // What happened since the last call, in order; each thing once.
    std::vector<Event> takeEvents();

private:
    void handle(const SetQueue&);
    void handle(const Play&);
    void handle(const Pause&);
    void handle(const Seek&);
    void handle(const Next&);
    void handle(const Previous&);
    void handle(const PlayPosition&);
    void handle(const SetRepeat&);
    void handle(const SetShuffle&);
    void handle(const SetRestartAtEnd&);
    void handle(const CycleAbLoop&);
    void handle(const ClearAbLoop&);
    void handle(const Insert&);
    void handle(const Remove&);
    void handle(const Move&);
    void handle(const SetDuration&);
    void handle(const Tick&);
    void handle(const TrackEnded&);
    void handle(const TrackFailed&);

    void enterTrack(Event::Reason reason, std::int64_t startMs = 0);
    void emitSeeked(bool loop);
    void emitEnded();

    Queue queue_;
    Transport transport_;
    bool restartAtEnd_ = false;
    int failures_ = 0;  // tracks in a row that would not play
    std::vector<Event> events_;
};

}  // namespace geode::player
