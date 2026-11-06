#pragma once

#include <cstddef>
#include <vector>

#include "signal/params/ParamSchema.hpp"
#include "signal/params/ParamState.hpp"

// The param store: one ParamCell (ParamState.hpp) per schema row, plus this frame's modulation, and
// effective() = base ⊕ modulation, clamped once by the schema. Modulation is never saved and is cleared
// each frame, so a stage can no longer clamp early and stop a later one from pulling a value back.
//
// Per frame, on one thread (the renderer's):
//   store.advance(dt);              // glides and trials move on
//   store.clearModulation();
//   ...every modulator and route: store.modulate(id, combine, amount, weight)...
//   store.effective(id)             // what the scene is drawn with, before safety::apply

namespace geode::signal {

// How one modulation combines with the base. Every kind accumulates across the frame, so the order in
// which modulators run never matters:
//   v = (base × Π multiply) + Σ add, then pulled toward the weighted mean of every Toward target by
//   their summed weight (capped at 1), then the one clamp. A circular param adds round its circle and
//   pulls the short way.
enum class Combine : std::uint8_t { Add, Multiply, Toward };

class ParamStore {
public:
    // Every param starts at rest on its default.
    explicit ParamStore(ParamSchema schema);

    const ParamSchema& schema() const { return schema_; }
    std::size_t size() const { return cells_.size(); }

    // The state machine's one input. An unknown id is Rejected.
    Outcome input(ParamId id, const ParamInput& in);
    void advance(float dt);

    void clearModulation();
    // Non-finite amounts or weights, and unknown ids, are dropped.
    void modulate(ParamId id, Combine combine, float amount, float weight = 1.0f);

    // A held control shows exactly what the finger set: its modulation is muted. On by default.
    void setMuteWhileHeld(bool mute) { muteWhileHeld_ = mute; }

    ParamPhase phase(ParamId id) const;
    float base(ParamId id) const;
    float committed(ParamId id) const;
    float effective(ParamId id) const;
    // effective() of every param, in schema order.
    void effectiveAll(std::vector<float>& out) const;
    const ParamCell& cell(ParamId id) const { return cells_[id]; }

private:
    struct Modulation {
        float add = 0.0f;
        float multiply = 1.0f;
        float towardSum = 0.0f;  // Σ weight × target (a circular one as an offset from the base)
        float towardWeight = 0.0f;
    };

    ParamSchema schema_;
    std::vector<ParamCell> cells_;
    std::vector<Modulation> mods_;
    bool muteWhileHeld_ = true;
};

}  // namespace geode::signal
