#pragma once

// The four places a Scene (#302) can show a generator (#471): a 2D style or a simulation rendered
// offscreen into a texture that the environment samples. Sky is the backdrop the rays miss into, Wall
// and Material are surfaces (triplanar), CameraPlane is a quad filling the view, which is how today's
// full-screen look survives as "a generator on the camera plane".
//
// The numbers are the ABI's slot numbers (geode_viz_set_generator) and lib_generator's sampler order
// (uGenSky, uGenWall, uGenMaterial, uGenPlane; uGenMix.xyzw), so they never change.

namespace geode::scene::generator {

enum class Slot : int { Sky = 0, Wall = 1, Material = 2, CameraPlane = 3 };
inline constexpr int kSlotCount = 4;

// Stable ASCII names: the tail of the slot's param ids ("scene.generators.pin.sky") and of the
// renderer's instance keys ("gen:sky:<id>"). An out-of-range slot has no name ("").
constexpr const char* slotName(Slot s) {
    switch (s) {
        case Slot::Sky: return "sky";
        case Slot::Wall: return "wall";
        case Slot::Material: return "material";
        case Slot::CameraPlane: return "plane";
    }
    return "";
}

}  // namespace geode::scene::generator
