#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace geode::viz::layer_state {

// Customize › Advanced › Layers: a second style drawn over the main one (whether it is on, where its style
// comes from, how much of it, how it blends), process-wide (#364). It used to live on one Renderer (the live
// screen's, pushed by Kotlin every frame), so an export, the live wallpaper and the dream each drew the base
// style alone. Like the macro bank, the director and the shader overrides, it is now one value every renderer
// reads: each renderer's Follower hands it the layer when the generation moves, and each renderer picks the
// style itself (viz/LayerPick), since what suits it depends on the style that renderer shows. The main thread
// writes; GL threads read, so a mutex is enough.
//
// The shell keeps it as one opaque line (encode) and hands it back (decode, edit), as it keeps the macro bank
// and the palette stack, so it is saved with the look and reaches the wallpaper and the dream after a restart.

// Where the layer's style comes from. The owner, 2026-11-12, asked what the layer should choose from: "User
// chooses. Or director or macro."
//   User      the style the user picked; none picked (or the one on screen) is one native picks to suit it
//   Director  the Auto Director picks a new one at its style moments (a section, a drop, a long stretch)
//   Macro     a macro knob's turn picks one along a native-ordered list
enum class Source { User, Director, Macro };
constexpr int kSources = 3;
const char* name(Source source);
std::optional<Source> sourceNamed(std::string_view name);

// BlendMode's count (viz/VisualSafety); geode_viz_api.cpp holds the two equal, so this file needs no GL header.
constexpr int kBlends = 8;
constexpr int kDefaultBlend = 1;  // Screen
constexpr float kDefaultMix = 0.5f;

struct Layer {
    bool on = false;
    Source source = Source::User;
    std::string sceneId;  // User: the picked style; "" lets native pick one that suits the style on screen
    int knob = 0;         // Macro: the knob's id; 0 is the first knob on screen
    float mix = kDefaultMix;  // 0..1, the Layers page's mix; waves, routes and knobs add the layerMix param to it
    int blend = kDefaultBlend;  // BlendMode ordinal; VisualSafety still caps a flashing blend at composite time
    bool operator==(const Layer&) const = default;
};

// The layer made valid: a non-finite mix is 0 and the mix is held to 0..1, a blend outside the list is Screen,
// a negative knob is 0, and a style id loses any '|' or line break (it is one field of one line).
Layer normalized(const Layer& layer);

// Sets the layer every renderer draws (normalized). The generation moves only when the stored value
// changes, so setting the same layer again is free.
void set(const Layer& layer);
Layer get();
// Moves on every change.
uint64_t generation();
// Tests only: back to the default layer, off (moves the generation when anything was set).
void reset();

// The style the on-screen renderer drew as its layer last ("" for none), so Customize › Layers can say what
// Auto Director or a knob picked. Any thread; a renderer publishes only when it changes.
void publishShown(const std::string& sceneId);
std::string shown();

// The layer as the shell stores it, one line:
//   layer|<kTextVersion>|<on 0/1>|<SOURCE>|<style id>|<knob id>|<blend ordinal>|<mix>
// decode never fails: any other text (an empty one, an older version) is the default layer, off.
constexpr int kTextVersion = 1;
std::string encode(const Layer& layer);
Layer decode(std::string_view text);
// One field changed, the rest kept: on (0/1), source (USER, DIRECTOR, MACRO), style (an id, "" for native's
// pick), knob (an id), blend (an ordinal), mix (0..1). An unknown field or a value it cannot read changes nothing.
Layer edit(const Layer& layer, std::string_view field, std::string_view value);

// One renderer's view of the store. A live renderer follows it; an export pins the layer it started with
// (geode_viz_set_offscreen), so an edit made in Customize while a background render runs never reaches that
// video (the same rule as the export's pinned settings, #350). Owned by one renderer and called under that
// renderer's lock.
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
