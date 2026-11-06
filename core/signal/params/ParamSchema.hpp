#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// The param schema: one row per scene param (its stable id, kind, default and range), and the one clamp
// every value passes through. core/signal/README.md says where it sits; ADR 0006 says why. Today the rows
// are built by whoever owns the store (a test, later the resolver from the native field table); the
// schema itself knows nothing of SceneParams, so it can be fed from a generated ParamSchema.inc without
// change.

namespace geode::signal {

// Index of a row in a ParamSchema: the position the row was given in, stable for that schema.
using ParamId = std::uint16_t;
constexpr ParamId kNoParam = 0xFFFF;

enum class ParamKind : std::uint8_t {
    Float,    // clamped to [min, max]
    Int,      // rounded, then clamped to [min, max]
    Bool,     // 0 or 1: anything at or above 0.5 is on
    Circular, // a value on a circle [min, max) (a hue, an angle): wrapped, never clamped
};

struct ParamDef {
    // Stable ASCII id: the save key and the wire name. English labels are never ids.
    std::string_view id;
    ParamKind kind = ParamKind::Float;
    float defaultValue = 0.0f;
    float min = 0.0f;
    float max = 1.0f;
    // Brightness-like (brightness, intensity, flash, bloom…): event pulses on it are rate-limited and
    // slowed (see EventRoutes), so no event can turn a drum into a strobe.
    bool luminance = false;
};

class ParamSchema {
public:
    ParamSchema() = default;
    // Rows keep their order: row i is ParamId i. A row whose max is below its min has the two swapped,
    // and its default is clamped into the range, so a schema is always usable.
    explicit ParamSchema(std::vector<ParamDef> rows);

    std::size_t size() const { return rows_.size(); }
    bool contains(ParamId id) const { return id < rows_.size(); }
    const ParamDef& def(ParamId id) const { return rows_[id]; }
    // The first row with this id, or nothing.
    std::optional<ParamId> find(std::string_view id) const;
    // True when every id is non-empty and unique.
    bool idsUnique() const;

    // The one clamp, by kind: a non-finite value becomes the default; Int rounds; Bool snaps to 0 or 1;
    // Circular wraps into [min, max); Float clamps.
    float clamp(ParamId id, float value) const;
    static float clampTo(const ParamDef& def, float value);
    // Wraps a circular value into [min, max); any other kind is returned unchanged.
    static float wrap(const ParamDef& def, float value);
    // The signed shortest way round the circle from `from` to `to` (Circular only; else to - from).
    static float circularDelta(const ParamDef& def, float from, float to);

private:
    std::vector<ParamDef> rows_;
};

}  // namespace geode::signal
