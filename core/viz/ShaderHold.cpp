#include "viz/ShaderHold.hpp"

#include <cstring>

namespace geode::viz::shader_hold {

namespace {

bool sameShapes(const shapes::Packed& a, const shapes::Packed& b) {
    return a.shapes == b.shapes && a.count == b.count && a.mix == b.mix && a.fold == b.fold && a.melt == b.melt;
}

// A frame that may count toward holding: still, live, with no new samples.
bool settles(const Inputs& in) { return in.still && !in.exporting && !in.newPcm; }

}  // namespace

bool same(const Inputs& a, const Inputs& b) {
    return a.still == b.still && a.exporting == b.exporting && a.newPcm == b.newPcm &&
           std::memcmp(&a.sound, &b.sound, sizeof a.sound) == 0 && a.touch == b.touch && a.pose == b.pose &&
           a.notes.knobs == b.notes.knobs && a.notes.amount == b.notes.amount && sameShapes(a.shapes, b.shapes) &&
           a.marchScale == b.marchScale && a.playSeed == b.playSeed && a.overrides == b.overrides &&
           a.paletteRamp == b.paletteRamp;
}

bool Held::holds(const Inputs& now) const { return settles(now) && any_ && settled_ >= kSettleDraws && same(last_, now); }

void Held::drew(const Inputs& now) {
    if (!settles(now)) {
        settled_ = 0;
    } else if (any_ && same(last_, now)) {
        if (settled_ < kSettleDraws) settled_++;
    } else {
        settled_ = 1;
    }
    last_ = now;
    any_ = true;
}

}  // namespace geode::viz::shader_hold
