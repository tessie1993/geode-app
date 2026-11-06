#include "signal/events/EventBus.hpp"

#include <algorithm>
#include <utility>

namespace geode::signal {

namespace {
bool before(int pa, SubscriptionId ia, int pb, SubscriptionId ib) { return pa != pb ? pa > pb : ia < ib; }
}  // namespace

SubscriptionId EventBus::subscribe(const EventFilter& filter, EventHandler handler, int priority) {
    if (!handler) return kNoSubscription;
    Subscriber s;
    s.id = next_++;
    s.priority = priority;
    s.filter = filter;
    s.handler = std::move(handler);
    if (dispatching_) {
        joining_.push_back(std::move(s));
        return joining_.back().id;
    }
    const auto at = std::find_if(subs_.begin(), subs_.end(),
                                 [&](const Subscriber& o) { return before(s.priority, s.id, o.priority, o.id); });
    const SubscriptionId id = s.id;
    subs_.insert(at, std::move(s));
    return id;
}

bool EventBus::unsubscribe(SubscriptionId id) {
    for (Subscriber& s : subs_) {
        if (s.id == id && s.live) {
            s.live = false;
            if (!dispatching_) compact();
            return true;
        }
    }
    for (auto it = joining_.begin(); it != joining_.end(); ++it) {
        if (it->id == id) {
            joining_.erase(it);
            return true;
        }
    }
    return false;
}

void EventBus::dispatch(const EventQueue& queue) {
    // Not re-entrant: a handler that dispatches would reorder the frame. Events it wants to raise go
    // into the next frame's queue instead.
    if (dispatching_) return;
    dispatching_ = true;
    for (const Event& e : queue) {
        // By index: a handler's subscribe() goes to joining_, so subs_ never reallocates here.
        for (std::size_t i = 0; i < subs_.size(); ++i) {
            Subscriber& s = subs_[i];
            if (s.live && s.filter.matches(e)) s.handler(e);
        }
    }
    dispatching_ = false;
    compact();
    std::vector<Subscriber> joining = std::move(joining_);
    joining_.clear();
    for (Subscriber& s : joining) {
        const auto at = std::find_if(subs_.begin(), subs_.end(),
                                     [&](const Subscriber& o) { return before(s.priority, s.id, o.priority, o.id); });
        subs_.insert(at, std::move(s));
    }
}

std::size_t EventBus::subscribers() const {
    std::size_t n = joining_.size();
    for (const Subscriber& s : subs_) n += s.live ? 1 : 0;
    return n;
}

void EventBus::compact() {
    subs_.erase(std::remove_if(subs_.begin(), subs_.end(), [](const Subscriber& s) { return !s.live; }), subs_.end());
}

}  // namespace geode::signal
