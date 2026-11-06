#include "signal/params/ParamStore.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace geode::signal {

ParamStore::ParamStore(ParamSchema schema) : schema_(std::move(schema)) {
    cells_.reserve(schema_.size());
    for (std::size_t i = 0; i < schema_.size(); ++i) cells_.push_back(restingCell(schema_.def(static_cast<ParamId>(i))));
    mods_.assign(schema_.size(), Modulation{});
}

Outcome ParamStore::input(ParamId id, const ParamInput& in) {
    if (!schema_.contains(id)) return Outcome::Rejected;
    return apply(cells_[id], in, schema_.def(id));
}

void ParamStore::advance(float dt) {
    for (std::size_t i = 0; i < cells_.size(); ++i) signal::advance(cells_[i], dt, schema_.def(static_cast<ParamId>(i)));
}

void ParamStore::clearModulation() { std::fill(mods_.begin(), mods_.end(), Modulation{}); }

void ParamStore::modulate(ParamId id, Combine combine, float amount, float weight) {
    if (!schema_.contains(id) || !std::isfinite(amount) || !std::isfinite(weight)) return;
    Modulation& m = mods_[id];
    switch (combine) {
        case Combine::Add:
            m.add += amount * weight;
            break;
        case Combine::Multiply:
            // A weight blends the factor toward 1: weight 0 leaves the value alone, 1 applies it whole.
            m.multiply *= 1.0f + (amount - 1.0f) * weight;
            break;
        case Combine::Toward: {
            if (!(weight > 0.0f)) return;
            const ParamDef& def = schema_.def(id);
            // A circular target is kept as the short way from the base, so two targets either side of
            // the wrap average to the right place.
            const float target = def.kind == ParamKind::Circular ? ParamSchema::circularDelta(def, cells_[id].base, amount) : amount;
            m.towardSum += target * weight;
            m.towardWeight += weight;
            break;
        }
    }
}

ParamPhase ParamStore::phase(ParamId id) const { return schema_.contains(id) ? cells_[id].phase : ParamPhase::Rest; }

float ParamStore::base(ParamId id) const { return schema_.contains(id) ? cells_[id].base : 0.0f; }

float ParamStore::committed(ParamId id) const { return schema_.contains(id) ? cells_[id].committed : 0.0f; }

float ParamStore::effective(ParamId id) const {
    if (!schema_.contains(id)) return 0.0f;
    const ParamCell& cell = cells_[id];
    const ParamDef& def = schema_.def(id);
    const bool held = cell.phase == ParamPhase::Held || (cell.phase == ParamPhase::Trial && cell.trialHeld);
    if (held && muteWhileHeld_) return ParamSchema::clampTo(def, cell.base);
    const Modulation& m = mods_[id];
    float v = cell.base * m.multiply + m.add;
    if (m.towardWeight > 0.0f) {
        const float pull = std::min(1.0f, m.towardWeight);
        const float mean = m.towardSum / m.towardWeight;
        if (def.kind == ParamKind::Circular) {
            // mean is an offset from the base; v has moved by its own modulation since.
            const float target = cell.base + mean;
            v += ParamSchema::circularDelta(def, v, target) * pull;
        } else {
            v += (mean - v) * pull;
        }
    }
    return ParamSchema::clampTo(def, v);
}

void ParamStore::effectiveAll(std::vector<float>& out) const {
    out.resize(cells_.size());
    for (std::size_t i = 0; i < cells_.size(); ++i) out[i] = effective(static_cast<ParamId>(i));
}

}  // namespace geode::signal
