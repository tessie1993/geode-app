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
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "scene/generator/Assignment.hpp"
#include "scene/generator/Slot.hpp"
#include "viz/Director.hpp"
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

// Who publishes the assignment process-wide (scene::generator::publish): a live renderer the shell has
// set a generator, pin or policy on, after each setter and each director swap. Every renderer adopts
// the published assignment when it is built (Visuals builds a new one each time its view attaches, so
// leaving Visuals keeps the choices), and an export's again when it turns offscreen. An offscreen
// renderer never publishes, nor does one nobody set anything on (the wallpaper, the dream), so their
// own directors' swaps never overwrite the app's.
constexpr bool publishesAssignment(bool offscreen, bool setHere) { return !offscreen && setHere; }

// Whether a slot's instance is stepped and drawn this frame. Only when a scene on screen samples it
// (ShaderScene::samplesGenerator: its program has that slot's uGen* sampler), so assigning a sky under
// a 2D style costs nothing until a style that reads the sky comes on; and at the Minimal tier only the
// camera plane, which is the picture itself, while a sky, wall or material is decoration a hot phone
// sheds before frames. A slot skipped here keeps its instance resident (pins() still holds it), so it
// resumes without a rebuild.
constexpr bool drawsSlot(gen::Slot slot, ThermalTier tier, bool sampled) {
    if (!sampled) return false;
    return slot == gen::Slot::CameraPlane || tier != ThermalTier::Minimal;
}

// The director hook (#472): the director committed a look (Director::commits() is `commits`, the reason
// is `why`). A section or a drop is a generator moment, numbered by that commit count so a replay of
// the play (an export pins the same seed) swaps at the same commits; any other commit is none.
struct DirectorMoment {
    gen::Moment moment = gen::Moment::Section;
    std::uint32_t index = 0;
};

constexpr std::optional<DirectorMoment> directorMoment(Director::Reason why, int commits) {
    if (commits < 0) return std::nullopt;
    const auto index = static_cast<std::uint32_t>(commits);
    if (why == Director::Reason::Section) return DirectorMoment{gen::Moment::Section, index};
    if (why == Director::Reason::Drop) return DirectorMoment{gen::Moment::Drop, index};
    return std::nullopt;
}

}  // namespace geode::viz::generator_targets
