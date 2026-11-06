#include "scene/object/ObjectOverlay.hpp"

#include <algorithm>

namespace geode::scene::object {

int packOverlay(const ObjectSystem& objects, const OverlayLook& look, const OverlayPalette& palette, std::size_t cap,
                OverlayFrame& out) {
    const std::size_t max = std::min(cap, OverlayFrame::kMaxShapes);
    out.count = static_cast<int>(objects.packShapes(out.shapes.data(), max));
    out.look = look;
    out.palette = palette;
    return out.count;
}

}  // namespace geode::scene::object
