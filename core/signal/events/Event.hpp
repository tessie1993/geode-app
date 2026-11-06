#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>

// Events: the moments the engine reacts to (a kick, a downbeat, a drop, a key change, a style switch),
// as plain values raised once per frame into an EventQueue and handed out by an EventBus. The kinds are
// one table, EventKinds.inc.

namespace geode::signal {

enum class EventKind : std::uint8_t {
#define GEODE_EVENT(name, wire) name,
#include "signal/events/EventKinds.inc"
#undef GEODE_EVENT
};

constexpr std::size_t kEventKindCount = 0
#define GEODE_EVENT(name, wire) +1
#include "signal/events/EventKinds.inc"
#undef GEODE_EVENT
    ;

// The kind's stable wire id ("kick", "build_start"), and back.
std::string_view eventWireId(EventKind kind);
std::optional<EventKind> eventKindFromWire(std::string_view wire);

struct Event {
    EventKind kind = EventKind::Kick;
    // How strong the moment is, 0..1 (a trigger's value; 1 for events with no strength of their own).
    float strength = 1.0f;
    // Extra fact some kinds carry: the section id for SectionChange, the key (0..11 major, 12..23 minor)
    // for KeyChange, the style's hash for StyleSwitched; 0 otherwise.
    std::int32_t value = 0;
    // Engine time of the frame that raised it, seconds.
    double timeS = 0.0;
};

using EventMask = std::bitset<kEventKindCount>;

// Which events a subscriber or a route answers to.
struct EventFilter {
    EventMask kinds;
    float minStrength = 0.0f;

    static EventFilter of(std::initializer_list<EventKind> list, float minStrength = 0.0f) {
        EventFilter f;
        for (EventKind k : list) f.kinds.set(static_cast<std::size_t>(k));
        f.minStrength = minStrength;
        return f;
    }
    static EventFilter any() {
        EventFilter f;
        f.kinds.set();
        return f;
    }
    bool matches(const Event& e) const { return kinds.test(static_cast<std::size_t>(e.kind)) && e.strength >= minStrength; }
};

// The events of one frame, in the order they were raised. Fixed capacity, so raising one never
// allocates on the render thread; past capacity events are dropped and counted.
class EventQueue {
public:
    static constexpr std::size_t kCapacity = 64;

    bool push(const Event& e) {
        if (size_ == kCapacity) {
            ++dropped_;
            return false;
        }
        events_[size_++] = e;
        return true;
    }
    void clear() {
        size_ = 0;
        dropped_ = 0;
    }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    std::size_t dropped() const { return dropped_; }
    const Event& operator[](std::size_t i) const { return events_[i]; }
    const Event* begin() const { return events_.data(); }
    const Event* end() const { return events_.data() + size_; }

private:
    std::array<Event, kCapacity> events_{};
    std::size_t size_ = 0;
    std::size_t dropped_ = 0;
};

}  // namespace geode::signal
