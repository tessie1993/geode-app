#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "signal/events/Event.hpp"

// The event bus: subscribers name the events they answer to (an EventFilter) and a priority, and
// dispatch() hands each of a frame's events to every matching subscriber.
//
// Order is part of the interface, so a frame is deterministic: events in the order they were raised;
// for each event, subscribers by priority (higher first), then in the order they subscribed.
//
// A handler may subscribe or unsubscribe during dispatch: a new subscriber hears from the next
// dispatch on, a removed one hears nothing more, even later in the same frame.
//
// Single-threaded, like everything in core/signal: the renderer's thread owns it.

namespace geode::signal {

using EventHandler = std::function<void(const Event&)>;
using SubscriptionId = std::uint32_t;
constexpr SubscriptionId kNoSubscription = 0;

class EventBus {
public:
    SubscriptionId subscribe(const EventFilter& filter, EventHandler handler, int priority = 0);
    // False when the id is not (or no longer) subscribed.
    bool unsubscribe(SubscriptionId id);
    void dispatch(const EventQueue& queue);
    std::size_t subscribers() const;

private:
    struct Subscriber {
        SubscriptionId id = kNoSubscription;
        int priority = 0;
        EventFilter filter;
        EventHandler handler;
        bool live = true;
    };

    void compact();

    std::vector<Subscriber> subs_;   // kept sorted: priority descending, then id ascending
    std::vector<Subscriber> joining_;  // subscribed during a dispatch; merged in after it
    SubscriptionId next_ = 1;
    bool dispatching_ = false;
};

}  // namespace geode::signal
