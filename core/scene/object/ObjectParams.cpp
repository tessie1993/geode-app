#include "scene/object/ObjectParams.hpp"

#include <cstdio>
#include <utility>
#include <vector>

#include "scene/Math.hpp"
#include "scene/object/Object.hpp"
#include "signal/events/Event.hpp"

namespace geode::scene::object {

namespace {

using signal::ParamDef;
using signal::ParamKind;

constexpr float topOf(int count) { return static_cast<float>(count - 1); }

// Order is ObjectParam's; the static_asserts keep the counts honest.
std::vector<ParamDef> baseRows() {
    return {
        {"pos.x", ParamKind::Float, 0.0f, -64.0f, 64.0f},
        {"pos.y", ParamKind::Float, 0.0f, -64.0f, 64.0f},
        {"pos.z", ParamKind::Float, 0.0f, -64.0f, 64.0f},
        {"vel.x", ParamKind::Float, 0.0f, -16.0f, 16.0f},
        {"vel.y", ParamKind::Float, 0.0f, -16.0f, 16.0f},
        {"vel.z", ParamKind::Float, 0.0f, -16.0f, 16.0f},
        {"rot.yaw", ParamKind::Circular, 0.0f, -kPi, kPi},
        {"rot.pitch", ParamKind::Circular, 0.0f, -kPi, kPi},
        {"rot.roll", ParamKind::Circular, 0.0f, -kPi, kPi},
        {"spin.x", ParamKind::Float, 0.0f, -kTwoPi, kTwoPi},
        {"spin.y", ParamKind::Float, 0.0f, -kTwoPi, kTwoPi},
        {"spin.z", ParamKind::Float, 0.0f, -kTwoPi, kTwoPi},
        {"scale", ParamKind::Float, 1.0f, 0.0f, 8.0f},
        {"mat.palette", ParamKind::Circular, 0.0f, 0.0f, 1.0f},
        {"mat.glow", ParamKind::Float, 0.0f, 0.0f, 4.0f, true},
        {"mat.opacity", ParamKind::Float, 1.0f, 0.0f, 1.0f, true},
        {"mat.id", ParamKind::Int, 0.0f, 0.0f, 15.0f},
        {"life.birth", ParamKind::Float, 0.0f, 0.0f, 10.0f},
        {"life.lifetime", ParamKind::Float, 4.0f, 0.0f, 600.0f},
        {"life.fadeIn", ParamKind::Float, 0.25f, 0.0f, 10.0f},
        {"life.fadeOut", ParamKind::Float, 0.5f, 0.0f, 10.0f},
        {"life.fadeCurve", ParamKind::Float, 1.0f, 0.0f, 1.0f},
        {"life.ageRate", ParamKind::Float, 1.0f, 0.0f, 4.0f},
        {"life.death", ParamKind::Int, 0.0f, 0.0f, topOf(kDeathCount)},
        {"life.deathBeats", ParamKind::Int, 8.0f, 1.0f, 256.0f},
        {"life.deathEvent", ParamKind::Int, static_cast<float>(static_cast<int>(signal::EventKind::Drop)), 0.0f,
         topOf(static_cast<int>(signal::kEventKindCount))},
        {"life.entry", ParamKind::Int, 0.0f, 0.0f, topOf(kExitCount)},
        {"life.exit", ParamKind::Int, 0.0f, 0.0f, topOf(kExitCount)},
        {"life.burstSpread", ParamKind::Float, 1.5f, 0.0f, 4.0f},
        {"anchor", ParamKind::Int, 0.0f, 0.0f, topOf(kAnchorCount)},
        {"target.mode", ParamKind::Int, 0.0f, 0.0f, topOf(kTargetModeCount)},
        {"target.distance", ParamKind::Float, 3.0f, 0.2f, 32.0f},
        {"target.height", ParamKind::Float, 0.5f, -8.0f, 8.0f},
        {"target.orbitRate", ParamKind::Float, 0.4f, -kTwoPi, kTwoPi},
    };
}

std::vector<ParamDef> shapeRows() {
    std::vector<ParamDef> rows = baseRows();
    rows.push_back({"shape.type", ParamKind::Int, 0.0f, 0.0f, topOf(kShapeTypeCount)});
    rows.push_back({"shape.size.x", ParamKind::Float, 0.5f, 0.01f, 8.0f});
    rows.push_back({"shape.size.y", ParamKind::Float, 0.5f, 0.01f, 8.0f});
    rows.push_back({"shape.size.z", ParamKind::Float, 0.5f, 0.01f, 8.0f});
    rows.push_back({"shape.round", ParamKind::Float, 0.0f, 0.0f, 2.0f});
    return rows;
}

static_assert(kBaseParamCount == 34, "baseRows() has one row per ObjectParam");
static_assert(kShapeParamCount == kBaseParamCount + 5, "shapeRows() adds one row per ShapeParam");

}  // namespace

const signal::ParamSchema& objectSchema(Kind kind) {
    static const signal::ParamSchema base(baseRows());
    static const signal::ParamSchema shape(shapeRows());
    return kind == Kind::Shape ? shape : base;
}

std::size_t formatTarget(char* out, std::size_t cap, std::uint32_t instance, std::string_view paramId) {
    if (out == nullptr || cap == 0) return 0;
    out[0] = '\0';
    if (instance == 0 || paramId.empty()) return 0;
    const int n = std::snprintf(out, cap, "%.*s%u.%.*s", static_cast<int>(kTargetPrefix.size()), kTargetPrefix.data(),
                                static_cast<unsigned>(instance), static_cast<int>(paramId.size()), paramId.data());
    if (n < 0 || static_cast<std::size_t>(n) >= cap) {
        out[0] = '\0';
        return 0;
    }
    return static_cast<std::size_t>(n);
}

std::optional<TargetAddress> parseTarget(std::string_view address) {
    if (address.substr(0, kTargetPrefix.size()) != kTargetPrefix) return std::nullopt;
    std::string_view rest = address.substr(kTargetPrefix.size());
    const std::size_t dot = rest.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot > 10) return std::nullopt;
    std::uint64_t instance = 0;
    for (std::size_t i = 0; i < dot; ++i) {
        const char c = rest[i];
        if (c < '0' || c > '9') return std::nullopt;
        instance = instance * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (instance == 0 || instance > 0xFFFFFFFFull) return std::nullopt;
    const std::string_view param = rest.substr(dot + 1);
    if (param.empty()) return std::nullopt;
    return TargetAddress{static_cast<std::uint32_t>(instance), param};
}

}  // namespace geode::scene::object
