#include "signal/events/Event.hpp"

namespace geode::signal {

namespace {
constexpr std::array<std::string_view, kEventKindCount> kWireIds = {
#define GEODE_EVENT(name, wire) std::string_view(wire),
#include "signal/events/EventKinds.inc"
#undef GEODE_EVENT
};
}  // namespace

std::string_view eventWireId(EventKind kind) {
    const auto i = static_cast<std::size_t>(kind);
    return i < kWireIds.size() ? kWireIds[i] : std::string_view();
}

std::optional<EventKind> eventKindFromWire(std::string_view wire) {
    for (std::size_t i = 0; i < kWireIds.size(); ++i) {
        if (kWireIds[i] == wire) return static_cast<EventKind>(i);
    }
    return std::nullopt;
}

}  // namespace geode::signal
