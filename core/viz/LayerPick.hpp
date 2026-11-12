#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "viz/AutoDraw.hpp"
#include "viz/LayerState.hpp"

// Which style Customize › Layers draws over the one on screen, and what its page offers, decided here so the
// shell only shows it (ADR 0003). The layer's setting (viz/LayerState) says where the style comes from; this
// turns it into a style id each frame, per renderer, since what suits a layer depends on the style that renderer
// shows. Pure apart from the catalogue: no GL, no JNI. Host-tested in core/tests/layer_pick_test.cpp.
namespace geode::viz::layer_pick {

// The styles Auto Director and a knob pick a layer from, in the catalogue's order: Auto's own draw (the
// generative renderers and the shader styles, catalogue::Trait::AutoDraw), so a layer never starts a fluid
// solver, a cymatics plate or a MilkDrop preset on its own. Built once.
const std::vector<std::string>& pool();

// The style a layer over `activeId` starts on when none is picked: one of the same look group (catalogue::Look,
// what it looks like) from another family where there is one, so it adds a texture rather than a near copy, else
// any other in the pool. Chosen by a hash of `activeId`, not by catalogue order (which gave every user the
// catalogue's first style), so the same style always gets the same layer. Never `activeId` while the pool
// holds another.
std::string suggest(const std::string& activeId);

// The User source's style: the user's pick when it is a known style other than the one on screen, else suggest.
std::string userPick(const std::string& pickedId, const std::string& activeId);

// Which of `count` entries a knob turn (-1..1) picks: the knob's travel cut into `count` equal steps. `previous`
// (the last pick, -1 for none) holds while the turn stays within kDeadBand of a step past its own step's edges,
// so a knob resting on an edge, or a wave riding it, does not flicker between two styles.
constexpr float kDeadBand = 0.35f;
int macroIndex(float turn, int count, int previous);

// One renderer's pick. Owned by the renderer, called once a frame on its GL thread.
class Picker {
public:
    // The style to draw as the layer this frame, "" for none. `activeId` is the style on screen (the requested
    // one), `moments` the director's style moments this frame, `seed` its play seed and `turn` the Macro
    // source's knob turn with its modulation (-1..1; NaN when the bank has no such knob, which picks as User does).
    const std::string& pick(const layer_state::Layer& layer, const std::string& activeId, int moments, std::uint64_t seed,
                            float turn);
    // A new surface or a cut: the director's pick and the knob's step start over.
    void reset();

private:
    std::string picked_;
    std::string directed_;
    auto_draw::Draw draw_{0};
    std::uint64_t seed_ = 0;
    bool seeded_ = false;
    int macroIndex_ = -1;
};

// What the page calls `source` (You, Auto Director, Macro).
const char* sourceLabel(layer_state::Source source);

// What Customize › Layers draws and offers for a layer over the style on screen, so the shell decides none of it;
// geode_layer_values, _list and _shown hand it across as values and lists.
struct Editor {
    layer_state::Layer layer;  // normalized
    std::string shown;         // what the layer draws now: the User pick resolved, else `shown` as given; "" when off
    int knob = 0;              // the layer's knob's place among the knobs on screen; 0 when it is not on screen
    int family = 0;            // the style picker's family, a place in `families`
    int style = -1;            // the picked style's place in `styles`, -1 when it is not there
    std::vector<std::string> families;  // every Styles page family with a style the user can pick, by name, in order
    std::vector<std::string> styles;    // that family's styles but the one on screen, by id
};
// `family` is the picker's family as the page holds it; -1 (or a place past the list) opens on the family holding
// the style drawn now, else the first. `knobIds` are the knobs on screen, in bank order.
Editor editor(const layer_state::Layer& layer, const std::string& activeId, const std::string& shown, int family,
              const std::vector<int>& knobIds);

}  // namespace geode::viz::layer_pick
