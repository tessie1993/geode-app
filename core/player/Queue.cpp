#include "player/Queue.hpp"

#include <algorithm>
#include <numeric>
#include <random>
#include <utility>

namespace geode::player {

void Queue::reorder() {
    const int n = size();
    order_.resize(static_cast<std::size_t>(n));
    std::iota(order_.begin(), order_.end(), 0);
    if (!shuffled_ || n < 2) return;
    // Fisher-Yates over everything but the current track, which then leads the order.
    std::vector<int> rest;
    for (int i = 0; i < n; ++i) {
        if (i != cur_) rest.push_back(i);
    }
    std::mt19937_64 engine(seed_);
    for (std::size_t i = rest.size(); i > 1; --i) std::swap(rest[i - 1], rest[engine() % i]);
    order_.clear();
    if (cur_ >= 0) order_.push_back(cur_);
    order_.insert(order_.end(), rest.begin(), rest.end());
}

void Queue::set(std::vector<Track> tracks, int startIndex) {
    items_ = std::move(tracks);
    cur_ = items_.empty() ? -1 : std::clamp(startIndex, 0, size() - 1);
    reorder();
}

void Queue::setShuffle(bool on, std::uint64_t seed) {
    shuffled_ = on;
    seed_ = seed;
    reorder();
}

void Queue::setDuration(TrackId id, std::int64_t durationMs) {
    for (Track& t : items_) {
        if (t.id == id) t.durationMs = durationMs;
    }
}

int Queue::position() const {
    const auto at = std::find(order_.begin(), order_.end(), cur_);
    return at == order_.end() ? -1 : static_cast<int>(at - order_.begin());
}

const Track* Queue::current() const { return cur_ < 0 ? nullptr : &items_[static_cast<std::size_t>(cur_)]; }

bool Queue::insert(At at, std::vector<Track> tracks) {
    if (tracks.empty()) return false;
    const int added = static_cast<int>(tracks.size());
    const int first = size();
    const int place = at == At::End ? first : std::min(position() + 1, first);
    if (shuffled_) {
        items_.insert(items_.end(), tracks.begin(), tracks.end());
        std::vector<int> fresh(static_cast<std::size_t>(added));
        std::iota(fresh.begin(), fresh.end(), first);
        order_.insert(order_.begin() + place, fresh.begin(), fresh.end());
    } else {
        items_.insert(items_.begin() + place, tracks.begin(), tracks.end());
        if (cur_ >= place) cur_ += added;
        order_.resize(items_.size());
        std::iota(order_.begin(), order_.end(), 0);
    }
    if (cur_ >= 0) return false;
    cur_ = order_.front();
    return true;
}

Queue::Removal Queue::remove(int position) {
    if (position < 0 || position >= size()) return {};
    const int gone = order_[static_cast<std::size_t>(position)];
    items_.erase(items_.begin() + gone);
    order_.erase(order_.begin() + position);
    for (int& i : order_) {
        if (i > gone) --i;
    }
    Removal result;
    if (gone != cur_) {
        if (gone < cur_) --cur_;
        return result;
    }
    result.current = true;
    const int n = size();
    if (n == 0) {
        cur_ = -1;
    } else if (position < n) {
        cur_ = order_[static_cast<std::size_t>(position)];
    } else if (repeat_ == Repeat::All) {
        cur_ = order_.front();
    } else {
        cur_ = order_.back();
        result.ended = true;
    }
    return result;
}

void Queue::move(int from, int to) {
    if (from == to || from < 0 || to < 0 || from >= size() || to >= size()) return;
    auto rotateRange = [](auto& list, int a, int b) {
        if (a < b) {
            std::rotate(list.begin() + a, list.begin() + a + 1, list.begin() + b + 1);
        } else {
            std::rotate(list.begin() + b, list.begin() + a, list.begin() + a + 1);
        }
    };
    if (shuffled_) {
        rotateRange(order_, from, to);  // the order that plays changes; the list is not what the user sees
        return;
    }
    rotateRange(items_, from, to);
    if (cur_ == from) {
        cur_ = to;
    } else if (from < cur_ && cur_ <= to) {
        --cur_;
    } else if (to <= cur_ && cur_ < from) {
        ++cur_;
    }
}

bool Queue::select(int position) {
    if (position < 0 || position >= size()) return false;
    cur_ = order_[static_cast<std::size_t>(position)];
    return true;
}

void Queue::skipNext() {
    const int p = position();
    if (p < 0) return;
    cur_ = order_[static_cast<std::size_t>(p + 1 < size() ? p + 1 : 0)];
}

void Queue::skipPrevious() {
    const int p = position();
    if (p < 0) return;
    cur_ = order_[static_cast<std::size_t>(p > 0 ? p - 1 : size() - 1)];
}

bool Queue::hasNext() const { return !empty() && (position() + 1 < size() || repeat_ == Repeat::All); }

const Track* Queue::following() const {
    const int p = position();
    if (p < 0) return nullptr;
    if (repeat_ == Repeat::One) return current();
    if (p + 1 < size()) return &items_[static_cast<std::size_t>(order_[static_cast<std::size_t>(p + 1)])];
    return repeat_ == Repeat::All ? &items_[static_cast<std::size_t>(order_.front())] : nullptr;
}

Queue::Auto Queue::advance() {
    const Track* next = following();
    if (next == nullptr) return Auto::End;
    const bool same = next == current();
    cur_ = static_cast<int>(next - items_.data());
    return same ? Auto::Repeated : Auto::Advanced;
}

std::vector<TrackId> Queue::ids() const {
    std::vector<TrackId> out;
    out.reserve(order_.size());
    for (int i : order_) out.push_back(items_[static_cast<std::size_t>(i)].id);
    return out;
}

}  // namespace geode::player
