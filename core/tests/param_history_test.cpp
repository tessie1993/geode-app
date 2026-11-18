// Host-side checks for Customize's undo history (core/viz/ParamHistory, geode_param_history_*) and its dice's
// rules (core/viz/PanelRoll), as the Kotlin panel behaved before they moved here. No GL, no device.
//
// What is pinned:
// - edits within 700 ms fold into one entry, a gap starts a new one, a discrete write is always its own and the
//   next edit after it checkpoints again;
// - a new entry clears redo, and the first edit after an undo or a redo is always a new entry;
// - undo puts back the look before, its palette id with it, and redo the one undone; both stop at an empty stack;
// - 64 entries at most, the oldest dropped;
// - the roll without Pro adds "#PRO" to the kept set; only the roll of everything rolls the waves, never without Pro.
#include <cstdio>
#include <string>
#include <vector>

#include "api/geode_api.h"
#include "viz/PanelRoll.hpp"
#include "viz/ParamHistory.hpp"

namespace history = geode::viz::param_history;
namespace roll = geode::viz::panel_roll;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

history::Look look(float v, const char* id = "", const char* style = "") { return history::Look{{v, v + 1.0f}, id, style}; }

float valueOf(const std::optional<history::Look>& l) { return l ? l->frame[0] : -1.0f; }

void foldsADrag() {
    history::History h;
    h.record(look(1), 10'000, false);
    h.record(look(2), 10'300, false);
    h.record(look(3), 10'900, false);
    check(h.state() == history::kCanUndo, "a drag is one undo entry");
    auto back = h.step(false, look(4));
    check(valueOf(back) == 1.0f, "undo brings back the look before the drag");
    check(!h.step(false, look(1)), "and nothing older");
    check(h.state() == history::kCanRedo, "the undone look waits for redo");
}

void gapStartsAnEntry() {
    history::History h;
    h.record(look(1), 5'000, false);
    h.record(look(2), 5'701, false);
    auto a = h.step(false, look(3));
    auto b = h.step(false, look(2));
    check(valueOf(a) == 2.0f && valueOf(b) == 1.0f, "a pause longer than 700 ms starts a new entry");
}

void exactlyTheGapFolds() {
    history::History h;
    h.record(look(1), 5'000, false);
    h.record(look(2), 5'700, false);
    h.step(false, look(3));
    check(h.state() == history::kCanRedo, "exactly 700 ms still folds");
}

void discreteIsItsOwn() {
    history::History h;
    h.record(look(1), 1'000, false);
    h.record(look(2), 1'100, true);
    h.record(look(3), 1'200, false);
    auto a = h.step(false, look(4));
    auto b = h.step(false, look(3));
    auto c = h.step(false, look(2));
    check(valueOf(a) == 3.0f && valueOf(b) == 2.0f && valueOf(c) == 1.0f,
          "a discrete write is its own entry and the edit after it checkpoints");
}

void redoAndClear() {
    history::History h;
    h.record(look(1), 0, true);
    auto undone = h.step(false, look(2));
    check(valueOf(undone) == 1.0f, "undo");
    auto redone = h.step(true, look(1));
    check(valueOf(redone) == 2.0f, "redo brings back the undone look");
    h.step(false, look(2));
    h.record(look(1), 100'000, false);
    check(h.state() == history::kCanUndo, "a new entry clears redo");
}

void foldKeepsRedo() {
    history::History h;
    h.record(look(1), 0, true);
    h.record(look(2), 0, true);
    h.step(false, look(3));
    h.record(look(2), 50'000, false);
    h.record(look(5), 50'200, false);
    check(h.state() == history::kCanUndo, "the first edit's entry clears redo");
    history::History g;
    g.record(look(1), 50'000, false);
    g.record(look(2), 50'000, true);
    g.step(false, look(3));
    g.record(look(2), 50'100, false);
    g.step(false, look(4));
    g.step(true, look(2));
    g.record(look(9), 50'150, false);
    check((g.state() & history::kCanRedo) == 0, "after undo/redo the next edit checkpoints and clears redo");
}

void paletteTravels() {
    history::History h;
    h.record(look(1, "palette-a"), 0, true);
    auto back = h.step(false, look(2, "palette-b"));
    check(back && back->paletteId == "palette-a", "undo brings back the saved palette's id");
    auto fwd = h.step(true, look(1, "palette-a"));
    check(fwd && fwd->paletteId == "palette-b", "redo too");
}

void depth() {
    history::History h;
    for (int i = 0; i < 77; ++i) h.record(look(static_cast<float>(i)), i, true);
    int steps = 0;
    float last = -1.0f;
    std::optional<history::Look> back;
    while ((back = h.step(false, look(999)))) {
        last = back->frame[0];
        ++steps;
    }
    check(steps == 64, "64 entries at most");
    check(last == 13.0f, "the oldest are dropped");
}

void api() {
    const float a[3] = {0.25f, 0.5f, 0.75f};
    const float b[3] = {1.0f, 2.0f, 3.0f};
    float out[3] = {};
    check(geode_param_history_step(0, b, 3, "", "", out) == nullptr, "nothing to undo at start");
    check(geode_param_history_record(a, 3, "kept", "", 93'000, 1) == GEODE_PARAM_HISTORY_UNDO, "record reports the bits");
    const char* id = geode_param_history_step(0, b, 3, nullptr, nullptr, out);
    check(id && std::string(id) == "kept", "the palette id comes back");
    check(out[0] == 0.25f && out[2] == 0.75f, "the frame comes back");
    check(geode_param_history_state() == GEODE_PARAM_HISTORY_REDO, "state after an undo");
    id = geode_param_history_step(1, a, 3, "kept", "", out);
    check(id && std::string(id).empty() && out[1] == 2.0f, "redo; a NULL palette id was \"\"");
}

void rollRules() {
    check(roll::locks("", false).empty(), "nothing kept, with Pro");
    check(roll::locks("", true) == "#PRO", "without Pro: #PRO alone");
    check(roll::locks("zoom\n#COLOUR", true) == "zoom\n#COLOUR\n#PRO", "without Pro: #PRO after the kept set");
    check(roll::locks("zoom", false) == "zoom", "with Pro the kept set as is");
    check(roll::bits(true, true, false) == (roll::kChanged | roll::kWaves), "a roll of everything rolls the waves");
    check(roll::bits(false, true, false) == roll::kWaves, "even when the params did not change");
    check(roll::bits(true, true, true) == roll::kChanged, "never without Pro");
    check(roll::bits(true, false, false) == roll::kChanged, "a category roll leaves the waves");
    check(roll::nextSeed() != roll::nextSeed(), "every roll its own seed");
}

}  // namespace

// A look made on one style is never put up on another: its entries are dropped, the style's own are kept.
void styleScopesTheUndo() {
    history::History h;
    h.record(look(1, "", "zoom"), 0, true);
    h.record(look(2, "", "spin"), 0, true);
    auto onSpin = h.step(false, look(3, "", "spin"));
    check(valueOf(onSpin) == 2.0f, "undo on a style puts up that style's look");
    auto onZoom = h.step(false, look(4, "", "fractal"));
    check(!onZoom, "an entry from another style is not put up");
    check(!h.step(false, look(4, "", "zoom")), "and it was dropped, not kept for later");
}

int main() {
    styleScopesTheUndo();
    foldsADrag();
    gapStartsAnEntry();
    exactlyTheGapFolds();
    discreteIsItsOwn();
    redoAndClear();
    foldKeepsRedo();
    paletteTravels();
    depth();
    api();
    rollRules();
    if (failures == 0) std::printf("param_history_test: all passed\n");
    return failures == 0 ? 0 : 1;
}
