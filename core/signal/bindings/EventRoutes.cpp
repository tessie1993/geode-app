#include "signal/bindings/EventRoutes.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace geode::signal {

namespace {
constexpr double kNever = -std::numeric_limits<double>::infinity();
// A pulse this many time constants old is below 1 % of its amount: it is retired.
constexpr float kPulseLifetimes = 5.0f;
}  // namespace

EventRoutes::EventRoutes(ParamStore& store) : store_(store), lastPulse_(store.size(), kNever) {}

EventRoutes::~EventRoutes() { detach(); }

int EventRoutes::add(const EventRoute& route) {
    if (!store_.schema().contains(route.target)) return -1;
    EventRoute r = route;
    if (r.everyNth == 0) r.everyNth = 1;
    routes_.push_back(r);
    counts_.push_back(0);
    return static_cast<int>(routes_.size()) - 1;
}

void EventRoutes::clear() {
    routes_.clear();
    counts_.clear();
    pulses_.clear();
}

void EventRoutes::attach(EventBus& bus, int priority) {
    detach();
    bus_ = &bus;
    subscription_ = bus.subscribe(EventFilter::any(), [this](const Event& e) { onEvent(e); }, priority);
}

void EventRoutes::detach() {
    if (bus_ != nullptr) bus_->unsubscribe(subscription_);
    bus_ = nullptr;
    subscription_ = kNoSubscription;
}

void EventRoutes::onEvent(const Event& e) {
    for (std::size_t i = 0; i < routes_.size(); ++i) {
        const EventRoute& r = routes_[i];
        if (!r.when.matches(e)) continue;
        counts_[i] += 1;
        if (counts_[i] % r.everyNth != 0) continue;
        act(r, e);
    }
}

void EventRoutes::act(const EventRoute& r, const Event& e) {
    const ParamDef& def = store_.schema().def(r.target);
    const float now = store_.committed(r.target);
    switch (r.action.kind) {
        case ActionKind::Set:
            store_.input(r.target, ParamInput::set(r.action.amount, Origin::Engine));
            break;
        case ActionKind::Nudge:
            store_.input(r.target, ParamInput::set(now + r.action.amount, Origin::Engine));
            break;
        case ActionKind::GlideTo:
            store_.input(r.target, ParamInput::glideTo(r.action.amount, r.action.seconds, Origin::Engine));
            break;
        case ActionKind::Toggle:
            store_.input(r.target, ParamInput::set(now >= 0.5f ? 0.0f : 1.0f, Origin::Engine));
            break;
        case ActionKind::Step: {
            // Whole steps round the range [min, max]: past max starts again at min, and back.
            const float lo = std::ceil(def.min);
            const float hi = std::max(lo, std::floor(def.max));
            const float span = hi - lo + 1.0f;
            float next = std::round(now + r.action.amount) - lo;
            next = std::fmod(next, span);
            if (next < 0.0f) next += span;
            store_.input(r.target, ParamInput::set(lo + next, Origin::Engine));
            break;
        }
        case ActionKind::Pulse:
            pulse(r.target, r.action.amount * e.strength, r.action.seconds, e.timeS);
            break;
    }
}

void EventRoutes::pulse(ParamId target, float amount, float seconds, double timeS) {
    if (!std::isfinite(amount) || amount == 0.0f) return;
    float decay = std::isfinite(seconds) && seconds > 0.0f ? seconds : 0.1f;
    const bool luminance = store_.schema().def(target).luminance;
    // An event with no real time cannot be placed against the flash clock: on a luminance param it is
    // dropped (the safe side), elsewhere it pulses and leaves the clock alone.
    if (!std::isfinite(timeS)) {
        if (luminance) {
            ++limited_;
            return;
        }
    } else {
        double& last = lastPulse_[target];
        // Engine time is not monotonic: a new render, a seek on the programme clock or an export
        // restart starts it again. Time before this param's last pulse is a new clock, so the param's
        // pulses and flash history from the old one are dropped; the limit then holds within the new
        // clock from its first pulse.
        if (timeS < last) {
            pulses_.erase(std::remove_if(pulses_.begin(), pulses_.end(), [target](const Pulse& p) { return p.target == target; }),
                          pulses_.end());
            last = kNever;
        }
        if (luminance) {
            decay = std::max(decay, kLuminanceMinDecay);
            if (timeS - last < 1.0 / static_cast<double>(kLuminanceMaxPerSecond)) {
                ++limited_;
                return;
            }
        }
        last = timeS;
    }
    if (pulses_.size() == kMaxPulses) pulses_.erase(pulses_.begin());  // the oldest has decayed most
    pulses_.push_back(Pulse{target, amount, decay, 0.0f});
}

void EventRoutes::modulate(float dt) {
    const float step = std::isfinite(dt) && dt > 0.0f ? dt : 0.0f;
    for (const Pulse& p : pulses_) store_.modulate(p.target, Combine::Add, p.amount * std::exp(-p.age / p.decay));
    for (Pulse& p : pulses_) p.age += step;
    pulses_.erase(std::remove_if(pulses_.begin(), pulses_.end(), [](const Pulse& p) { return p.age > p.decay * kPulseLifetimes; }),
                  pulses_.end());
}

}  // namespace geode::signal
