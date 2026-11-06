#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "scene/object/Object.hpp"
#include "signal/params/ParamStore.hpp"

// The object pool (#515): a fixed number of slots, every one built (its param store included) when the
// pool is, so spawning, stepping and freeing never allocate on the render thread. It holds the shapes
// and (from #522) the models together, under the spec's live cap of about 64; particles (#517) and
// lights (#518) are counted in their own pools, so this one refuses their kinds.
//
// Past its cap the pool decides the same way every time: the oldest short-lived object (Origin::Music,
// by spawn order) is dropped at once to make room, whatever is spawning; when every live object is
// persistent (a family's or a user's), the new one is refused. A persistent object is never dropped for
// another.
//
// Instances: each spawn gets the next serial from 1, never reused while the pool lives (a wrap past
// 2^32 - 1 skips 0), so a route addressed to an object that has gone resolves to nothing rather than to
// whatever took its slot.

namespace geode::scene::object {

// One row of one live object: what a route resolves its target address to.
struct TargetRef {
    std::uint16_t slot = 0xFFFF;
    signal::ParamId param = signal::kNoParam;
    std::uint32_t instance = 0;
};

enum class Placement : std::uint8_t {
    Placed,          // a free slot took it
    PlacedEvicting,  // the oldest short-lived object was dropped for it (SpawnResult::evicted)
    Refused,         // the pool is full of persistent objects, or the kind is not pooled here
};

struct SpawnResult {
    Placement placement = Placement::Refused;
    std::uint32_t instance = 0;  // 0 when refused
    std::uint32_t evicted = 0;   // the instance dropped, for PlacedEvicting
};

class ObjectPool {
public:
    // The spec's live cap of shapes and models together on a flagship (#511 § Detail).
    static constexpr std::size_t kMaxShapesAndModels = 64;

    struct Slot {
        explicit Slot(const signal::ParamSchema& schema) : params(schema) {}
        bool used = false;
        std::uint32_t instance = 0;
        std::uint64_t serial = 0;  // spawn order, for the eviction rule
        Origin origin = Origin::Music;
        Kind kind = Kind::Shape;
        std::int16_t spawner = -1;  // the rule that threw it, -1 for none
        Body body{};
        Life life{};
        Vec3 travel{0.0f, 0.0f, 0.0f};  // integrated from vel: body.pos is the pos params + this
        float orbitPhase = 0.0f;        // radians round it a camera Orbit stands at (target.orbitRate)
        signal::ParamStore params;
    };

    // capacity is clamped to 1..kMaxShapesAndModels.
    explicit ObjectPool(std::size_t capacity = kMaxShapesAndModels);

    static bool pooled(Kind kind) { return kind == Kind::Shape; }

    std::size_t capacity() const { return slots_.size(); }
    // How many may live at once, for Scene detail (a lower level caps fewer, e.g. 16). Lowering it
    // drops nothing at once: spawns past it evict or are refused as above.
    void setLiveCap(std::size_t cap);
    std::size_t liveCap() const { return liveCap_; }
    std::size_t live() const { return live_; }

    SpawnResult spawn(const ObjectSpec& spec, Origin origin, int spawner = -1);
    // Frees the object's slot now (no exit). False when it is not live.
    bool remove(std::uint32_t instance);
    void clear();

    std::size_t refused() const { return refused_; }
    std::size_t evicted() const { return evicted_; }

    Slot& slot(std::size_t i) { return slots_[i]; }
    const Slot& slot(std::size_t i) const { return slots_[i]; }
    Slot* find(std::uint32_t instance);
    const Slot* find(std::uint32_t instance) const;
    // How many live objects this rule threw.
    int aliveFrom(int spawner) const;

    // Route targets: `param.object.<instance>.<row id>` to one row of one live object, or nothing.
    std::optional<TargetRef> resolve(std::string_view address) const;
    std::optional<TargetRef> resolve(std::uint32_t instance, std::string_view paramId) const;
    // The object behind a ref must still be the one it was resolved for; otherwise these do nothing.
    void modulate(const TargetRef& t, signal::Combine combine, float amount, float weight = 1.0f);
    signal::Outcome input(const TargetRef& t, const signal::ParamInput& in);
    float effective(const TargetRef& t) const;

    // Every route target of every live object: f(instance, rowId, def), slot order then schema order.
    template <class F>
    void forEachTarget(F&& f) const {
        for (const Slot& s : slots_) {
            if (!s.used) continue;
            const signal::ParamSchema& schema = s.params.schema();
            for (std::size_t r = 0; r < schema.size(); ++r) {
                const signal::ParamDef& def = schema.def(static_cast<signal::ParamId>(r));
                f(s.instance, def.id, def);
            }
        }
    }

private:
    bool valid(const TargetRef& t) const;
    void vacate(Slot& s);

    std::vector<Slot> slots_;
    std::size_t liveCap_ = kMaxShapesAndModels;
    std::size_t live_ = 0;
    std::uint32_t nextInstance_ = 1;
    std::uint64_t nextSerial_ = 0;
    std::size_t refused_ = 0;
    std::size_t evicted_ = 0;
};

}  // namespace geode::scene::object
