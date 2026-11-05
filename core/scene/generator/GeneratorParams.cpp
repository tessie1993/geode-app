#include "scene/generator/GeneratorParams.hpp"

#include <utility>
#include <vector>

namespace geode::scene::generator {

signal::ParamSchema generatorSchema() {
    using signal::ParamDef;
    using signal::ParamKind;
    // Order is GeneratorParam's; the static_asserts below keep it honest. ParamDef holds a string_view,
    // so the ids are literals (static storage), spelled out rather than built from slotName.
    std::vector<ParamDef> rows{
        {"scene.generators.policy", ParamKind::Int, 0.0f, 0.0f, 1.0f},
        {"scene.generators.pin.sky", ParamKind::Bool, 0.0f, 0.0f, 1.0f},
        {"scene.generators.pin.wall", ParamKind::Bool, 0.0f, 0.0f, 1.0f},
        {"scene.generators.pin.material", ParamKind::Bool, 0.0f, 0.0f, 1.0f},
        {"scene.generators.pin.plane", ParamKind::Bool, 0.0f, 0.0f, 1.0f},
        {"scene.generators.mix.sky", ParamKind::Float, 1.0f, 0.0f, 1.0f},
        {"scene.generators.mix.wall", ParamKind::Float, 1.0f, 0.0f, 1.0f},
        {"scene.generators.mix.material", ParamKind::Float, 1.0f, 0.0f, 1.0f},
        {"scene.generators.mix.plane", ParamKind::Float, 1.0f, 0.0f, 1.0f},
    };
    return signal::ParamSchema(std::move(rows));
}

static_assert(kGeneratorParamCount == 1 + 2 * kSlotCount, "generatorSchema() has the policy, then a pin and a mix per slot");
static_assert(pinParam(Slot::CameraPlane) == kPinPlane && mixParam(Slot::CameraPlane) == kMixPlane, "rows follow Slot order");

}  // namespace geode::scene::generator
