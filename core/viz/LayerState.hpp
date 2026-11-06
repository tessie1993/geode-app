#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace geode::viz::layer_state {

// Customize › Advanced › Layers: a second style drawn over the main one (its id, how much of it, how it
// blends), process-wide (#364). It used to live on one Renderer (the live screen's, pushed by Kotlin
// every frame), so an export, the live wallpaper and the dream each drew the base style alone. Like
// Macros, the director and the shader overrides, it is now one value every renderer reads: each
// renderer's Follower hands it the layer when the generation moves. The main thread writes; GL threads
// read, so a mutex is enough.

struct Layer {
    std::string sceneId;  // "" = no layer
    float mix = 0.5f;     // 0..1; VisualSafety still caps a flashing blend at composite time
    int blend = 1;        // BlendMode ordinal (viz/VisualSafety), Screen by default
    bool operator==(const Layer& o) const { return sceneId == o.sceneId && mix == o.mix && blend == o.blend; }
    bool operator!=(const Layer& o) const { return !(*this == o); }
};

// Sets the layer every renderer draws. A non-finite mix is 0 and the mix is clamped to 0..1. The
// generation moves only when the stored value changes, so setting the same layer again is free.
void set(const Layer& layer);
Layer get();
// Moves on every change.
uint64_t generation();
// Tests only: back to no layer (moves the generation when anything was set).
void reset();

// One renderer's view of the store. A live renderer follows it; an export pins the layer it started
// with (geode_viz_set_offscreen), so an edit made in Customize while a background render runs never
// reaches that video (the same rule as the export's pinned settings, #350). Owned by one renderer and
// called under that renderer's lock.
class Follower {
public:
    // True, with `out` filled, when the layer this renderer draws changed since the last poll (a new
    // renderer sees whatever was set before it was made). One atomic load otherwise.
    bool poll(Layer& out);
    // Latches the store's current layer; later sets never reach poll until unpin.
    void pin();
    // Follows the store again from its current value.
    void unpin();
    bool pinned() const { return pinned_; }

private:
    std::optional<uint64_t> seen_;
    bool pinned_ = false;
    bool pinDirty_ = false;
    Layer pinnedLayer_;
};

}  // namespace geode::viz::layer_state
