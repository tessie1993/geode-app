#pragma once
// How a scene's generator textures (Scene::setGenerators, #471) reach lib_generator.glsl: which
// texture unit each slot samples on, the sampler each slot is named by, and the uGenMix vec4 that
// is uploaded beside them. Pure and GL-free so core/tests/generator_binding_test pins it on the host.
//
// Slot order is geode::scene::generator::Slot's (Sky, Wall, Material, CameraPlane = 0..3).
// Units 1, 2 and 3 are the flow field, the palette LUT and the sound texture (SoundUniforms), so the
// generators take 4..7; ES 3.0 guarantees 16 fragment units.
//
// The mix rule is what keeps today's look: a slot uploads 0 unless a texture is bound to it, and a
// mix that is out of range or not a number (a broken route) is clamped to 0..1 or dropped to 0.
// lib_generator returns the style's own colour, untouched, wherever its channel is 0.

#include <algorithm>
#include <array>
#include <iterator>

#include "scene/generator/Slot.hpp"

namespace geode::viz::generator_binding {

inline constexpr int kSlots = 4;
static_assert(kSlots == scene::generator::kSlotCount, "one texture unit per generator slot");
static_assert(static_cast<int>(scene::generator::Slot::CameraPlane) == 3, "uGenMix.w is the camera plane");
inline constexpr int kFirstUnit = 4;

constexpr int unit(int slot) { return kFirstUnit + slot; }

constexpr const char* samplerName(int slot) {
    switch (slot) {
        case 0: return "uGenSky";
        case 1: return "uGenWall";
        case 2: return "uGenMaterial";
        default: return "uGenPlane";
    }
}

inline bool anyBound(const unsigned (&tex)[kSlots]) {
    return std::any_of(std::begin(tex), std::end(tex), [](unsigned t) { return t != 0; });
}

inline std::array<float, kSlots> uploadMix(const unsigned (&tex)[kSlots], const float (&mix)[kSlots]) {
    std::array<float, kSlots> out{};
    for (int i = 0; i < kSlots; ++i) {
        const float m = mix[i];
        // NaN fails both comparisons and lands on 0.
        out[i] = tex[i] != 0 && m > 0.0f ? std::min(m, 1.0f) : 0.0f;
    }
    return out;
}

}  // namespace geode::viz::generator_binding
