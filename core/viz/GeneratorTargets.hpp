#pragma once
// The renderer's side of generators (#471): which scene instance draws each slot, how big its offscreen
// target is, and which instances the residency trim must keep. Pure and GL-free (the tier is only the
// enum), so core/tests/generator_test.cpp pins every rule on the host; Renderer (RendererFrame.cpp,
// drawGenerators) does the GL.
//
// Why an instance per slot, keyed apart from the style's own id: a simulation (Silk, Fluid, Life, Acid,
// Cymatics) steps its state on every draw and sizes its grid from resize(), so the same scene cannot be
// both the style on screen and a 512² sky, nor the sky and the wall. "gen:<slot>:<id>" is its own entry
// in the renderer's scene list, built from the style id, sized to its own target and never resized back
// to the render size by a tier or scale change.

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include "scene/generator/Assignment.hpp"
#include "scene/generator/Slot.hpp"
#include "viz/ThermalGovernor.hpp"

namespace geode::viz::generator_targets {

namespace gen = scene::generator;

inline constexpr std::string_view kKeyPrefix = "gen:";

// The largest square a sky, wall or material target may be on each tier: a texture wrapped over a
// sphere or a surface is filtered down anyway, so a cool phone gets 512² and a hot one sheds pixels
// before it sheds frames.
constexpr int surfaceCap(ThermalTier tier) {
    switch (tier) {
        case ThermalTier::Full: return 512;
        case ThermalTier::Eased: return 256;
        case ThermalTier::Reduced: return 256;
        case ThermalTier::Minimal: return 128;
    }
    return 128;
}

constexpr int floorPow2(int n) {
    int p = 1;
    while (p <= n / 2) p *= 2;
    return p;
}

struct TargetSize {
    int width = 1;
    int height = 1;
};

// The camera plane fills the view, so it is drawn at the render size (already tier- and user-scaled);
// every other slot is a power-of-two square, at most the tier's cap and never larger than the render's
// longer side (a thumbnail renderer gets a small sky, not a 512² one).
constexpr TargetSize targetSize(gen::Slot slot, ThermalTier tier, int renderWidth, int renderHeight) {
    const int w = std::max(renderWidth, 1);
    const int h = std::max(renderHeight, 1);
    if (slot == gen::Slot::CameraPlane) return {w, h};
    const int side = std::min(surfaceCap(tier), floorPow2(std::max(w, h)));
    return {side, side};
}

constexpr bool validSlot(int slot) { return slot >= 0 && slot < gen::kSlotCount; }

// "gen:<slot name>:<style id>"; "" for an empty id (no generator, no instance).
inline std::string instanceKey(gen::Slot slot, std::string_view styleId) {
    if (styleId.empty()) return {};
    std::string key(kKeyPrefix);
    key += gen::slotName(slot);
    key += ':';
    key += styleId;
    return key;
}

inline bool isInstanceKey(std::string_view key) { return key.substr(0, kKeyPrefix.size()) == kKeyPrefix; }

// The style an id builds: the tail of an instance key, or the id itself for every other scene.
inline std::string styleOf(std::string_view key) {
    if (!isInstanceKey(key)) return std::string(key);
    const auto colon = key.find(':', kKeyPrefix.size());
    return colon == std::string_view::npos ? std::string() : std::string(key.substr(colon + 1));
}

using Keys = std::array<std::string, gen::kSlotCount>;

// The instance each slot wants this frame, in Slot order ("" where the slot is empty).
inline Keys wantedKeys(const gen::Assignment& a) {
    Keys keys;
    for (int i = 0; i < gen::kSlotCount; ++i) keys[static_cast<std::size_t>(i)] = instanceKey(static_cast<gen::Slot>(i), a.slots[static_cast<std::size_t>(i)].id);
    return keys;
}

// Residency: an assigned slot's instance is in use every frame, so the trim never releases it. An
// instance no slot wants any more is not pinned (the renderer releases it at once on unassigning).
inline bool pins(const Keys& wanted, std::string_view id) {
    if (id.empty()) return false;
    return std::any_of(wanted.begin(), wanted.end(), [&](const std::string& k) { return k == id; });
}

}  // namespace geode::viz::generator_targets
