#include "signal/params/ParamSchema.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace geode::signal {

ParamSchema::ParamSchema(std::vector<ParamDef> rows) : rows_(std::move(rows)) {
    for (ParamDef& row : rows_) {
        if (row.max < row.min) std::swap(row.min, row.max);
        if (!std::isfinite(row.defaultValue)) row.defaultValue = row.min;
        // Finite now, so clampTo never falls back on the default it is computing.
        row.defaultValue = clampTo(row, row.defaultValue);
    }
}

std::optional<ParamId> ParamSchema::find(std::string_view id) const {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].id == id) return static_cast<ParamId>(i);
    }
    return std::nullopt;
}

bool ParamSchema::idsUnique() const {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].id.empty()) return false;
        for (std::size_t j = i + 1; j < rows_.size(); ++j) {
            if (rows_[i].id == rows_[j].id) return false;
        }
    }
    return true;
}

float ParamSchema::clamp(ParamId id, float value) const {
    if (!contains(id)) return 0.0f;
    return clampTo(rows_[id], value);
}

float ParamSchema::wrap(const ParamDef& def, float value) {
    if (def.kind != ParamKind::Circular) return value;
    const float span = def.max - def.min;
    if (!(span > 0.0f)) return def.min;
    float t = std::fmod(value - def.min, span);
    if (t < 0.0f) t += span;
    // fmod can land exactly on span after the += for tiny negatives; the circle is half-open.
    if (t >= span) t = 0.0f;
    return def.min + t;
}

float ParamSchema::circularDelta(const ParamDef& def, float from, float to) {
    if (def.kind != ParamKind::Circular) return to - from;
    const float span = def.max - def.min;
    if (!(span > 0.0f)) return 0.0f;
    float d = std::fmod(to - from, span);
    if (d > span * 0.5f) d -= span;
    if (d < -span * 0.5f) d += span;
    return d;
}

float ParamSchema::clampTo(const ParamDef& def, float value) {
    if (!std::isfinite(value)) value = def.defaultValue;
    switch (def.kind) {
        case ParamKind::Float:
            return std::clamp(value, def.min, def.max);
        case ParamKind::Int: {
            const float lo = std::ceil(def.min);
            const float hi = std::max(lo, std::floor(def.max));
            return std::clamp(std::round(value), lo, hi);
        }
        case ParamKind::Bool:
            return value >= 0.5f ? 1.0f : 0.0f;
        case ParamKind::Circular:
            return wrap(def, value);
    }
    return value;
}

}  // namespace geode::signal
