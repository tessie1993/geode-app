#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// The queue half of the native player: which track is current, the play order (list order, or a shuffle),
// the repeat mode, and what follows. Pure logic: it reads no clock and no global; the shuffle seed is handed
// in. A "position" is a place in the play order, which is what the user sees; an item index is a place in
// `items_`. Not for callers: they use Player.hpp.
namespace geode::player {

// Opaque: the caller's handle for a track (the module never opens a file).
using TrackId = std::int64_t;

struct Track {
    TrackId id = 0;
    std::int64_t durationMs = 0;  // 0 while unknown
};

// The values are Media3's Player.REPEAT_MODE_*, which the saved setting already holds.
enum class Repeat : int { Off = 0, One = 1, All = 2 };

class Queue {
public:
    enum class At { AfterCurrent, End };
    struct Removal {
        bool current = false;  // the removed track was the current one
        bool ended = false;    // ...and nothing follows it: the queue is over
    };
    // What a track that ended on its own led to.
    enum class Auto { Repeated, Advanced, End };

    // Replaces the queue. The current track is `startIndex` of `tracks`, held inside it.
    void set(std::vector<Track> tracks, int startIndex);
    // Shuffle on puts the current track first in a new order drawn from `seed`; off restores the list order.
    void setShuffle(bool on, std::uint64_t seed);
    void setRepeat(Repeat repeat) { repeat_ = repeat; }
    void setDuration(TrackId id, std::int64_t durationMs);

    // True when the empty queue got a current track.
    bool insert(At at, std::vector<Track> tracks);
    Removal remove(int position);
    void move(int from, int to);
    bool select(int position);

    // A skip by the user: wraps in every repeat mode.
    void skipNext();
    void skipPrevious();
    // A track ended on its own: follows the repeat mode.
    Auto advance();

    bool empty() const { return items_.empty(); }
    int size() const { return static_cast<int>(items_.size()); }
    int position() const;  // -1 when empty
    const Track* current() const;
    // Whether a skip has somewhere to go that is not wrapping back (Media3's hasNextMediaItem).
    bool hasNext() const;
    // The track that plays when the current one ends on its own; null when none does.
    const Track* following() const;
    bool shuffled() const { return shuffled_; }
    Repeat repeat() const { return repeat_; }
    std::vector<TrackId> ids() const;  // in play order
    const std::vector<int>& order() const { return order_; }

private:
    void reorder();

    std::vector<Track> items_;
    std::vector<int> order_;  // item indices in play order; the identity while not shuffled
    int cur_ = -1;            // item index
    bool shuffled_ = false;
    std::uint64_t seed_ = 0;
    Repeat repeat_ = Repeat::Off;
};

}  // namespace geode::player
