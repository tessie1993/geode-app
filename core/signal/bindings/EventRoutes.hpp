#pragma once

#include <cstdint>
#include <vector>

#include "signal/events/EventBus.hpp"
#include "signal/params/ParamStore.hpp"

// Event routes: when an event happens, do one thing to one param. A reaction route whose source is an
// event (GLOSSARY: Reaction route), and the native home for Customize › Music's moments → actions and the
// event recipes, which are bundles of these.
//
// An action either changes the param's base through the state machine (so a finger or a timeline still
// wins, and a preset's glide is not torn) or adds a decaying pulse to this frame's modulation.
//
// Flash safety lives here, at the one place events reach params: a pulse on a luminance param is slowed
// to at least kLuminanceMinDecay and limited to kLuminanceMaxPerSecond per param, whatever the event
// rate (WCAG 2.3.1's three flashes a second). safety::apply still clamps the level afterwards.

namespace geode::signal {

enum class ActionKind : std::uint8_t {
    Set,     // base = amount
    Nudge,   // base += amount (round its circle for a circular param)
    GlideTo, // base glides to amount over seconds
    Pulse,   // modulation += amount × strength, decaying with time constant seconds
    Toggle,  // a Bool param flips
    Step,    // base += amount, wrapping round the range: the next palette, the next fold count
};

struct Action {
    ActionKind kind = ActionKind::Pulse;
    float amount = 0.0f;
    float seconds = 0.0f;
};

struct EventRoute {
    EventFilter when;
    // Fire on every Nth matching event (1: every one); counted per route.
    std::uint16_t everyNth = 1;
    ParamId target = kNoParam;
    Action action;
};

class EventRoutes {
public:
    static constexpr float kLuminanceMinDecay = 0.35f;
    static constexpr float kLuminanceMaxPerSecond = 3.0f;
    static constexpr std::size_t kMaxPulses = 64;

    explicit EventRoutes(ParamStore& store);
    ~EventRoutes();
    EventRoutes(const EventRoutes&) = delete;
    EventRoutes& operator=(const EventRoutes&) = delete;

    // The route's index, or -1 when its target is not in the store's schema.
    int add(const EventRoute& route);
    void clear();
    std::size_t size() const { return routes_.size(); }

    // Subscribes to a bus (and leaves any bus it was on). The routes hear events through it.
    void attach(EventBus& bus, int priority = 0);
    void detach();

    // One event, as the bus delivers it. Public so a caller without a bus (a test, a replay) can feed it.
    void onEvent(const Event& e);
    // Adds the live pulses to the store's modulation and ages them by dt. Call once a frame, after
    // ParamStore::clearModulation and after the frame's events were dispatched.
    void modulate(float dt);

    std::size_t livePulses() const { return pulses_.size(); }
    // Pulses dropped by the flash limit, since construction: what a test or a debug overlay reads.
    std::uint32_t pulsesLimited() const { return limited_; }

private:
    struct Pulse {
        ParamId target;
        float amount;
        float decay;
        float age;
    };

    void act(const EventRoute& route, const Event& e);
    void pulse(ParamId target, float amount, float seconds, double timeS);

    ParamStore& store_;
    std::vector<EventRoute> routes_;
    std::vector<std::uint32_t> counts_;
    std::vector<Pulse> pulses_;
    std::vector<double> lastLuminancePulse_;  // per param: event time of its last pulse
    std::uint32_t limited_ = 0;
    EventBus* bus_ = nullptr;
    SubscriptionId subscription_ = kNoSubscription;
};

}  // namespace geode::signal
