#include "scene/object/ObjectPool.hpp"

#include <algorithm>

namespace geode::scene::object {

ObjectPool::ObjectPool(std::size_t capacity) {
    capacity = std::clamp<std::size_t>(capacity, 1, kMaxShapesAndModels);
    // Every slot holds a Shape store: Shape is the one pooled kind until Model (#522) joins, which gives
    // each slot a store per pooled kind (both built here, never on the render thread).
    slots_.reserve(capacity);
    for (std::size_t i = 0; i < capacity; ++i) slots_.emplace_back(objectSchema(Kind::Shape));
    liveCap_ = capacity;
}

void ObjectPool::setLiveCap(std::size_t cap) { liveCap_ = std::clamp<std::size_t>(cap, 0, slots_.size()); }

SpawnResult ObjectPool::spawn(const ObjectSpec& spec, Origin origin, int spawner) {
    SpawnResult result;
    if (!pooled(spec.kind) || liveCap_ == 0) {
        ++refused_;
        return result;
    }
    Slot* target = nullptr;
    if (live_ < liveCap_) {
        for (Slot& s : slots_) {
            if (!s.used) {
                target = &s;
                break;
            }
        }
    }
    if (target == nullptr) {
        // Full: the oldest short-lived object makes room, or nothing does.
        Slot* oldest = nullptr;
        for (Slot& s : slots_) {
            if (s.used && s.origin == Origin::Music && (oldest == nullptr || s.serial < oldest->serial)) oldest = &s;
        }
        if (oldest == nullptr) {
            ++refused_;
            return result;
        }
        result.evicted = oldest->instance;
        result.placement = Placement::PlacedEvicting;
        ++evicted_;
        vacate(*oldest);
        target = oldest;
    } else {
        result.placement = Placement::Placed;
    }

    Slot& s = *target;
    s.used = true;
    s.instance = nextInstance_;
    nextInstance_ = nextInstance_ == 0xFFFFFFFFu ? 1u : nextInstance_ + 1u;
    s.serial = nextSerial_++;
    s.origin = origin;
    s.kind = spec.kind;
    s.spawner = static_cast<std::int16_t>(std::clamp(spawner, -1, 0x7FFF));
    writeSpec(spec, s.params);
    s.life = Life{};
    s.travel = {0.0f, 0.0f, 0.0f};
    s.orbitPhase = 0.0f;
    s.body = Body{};
    s.body.pos = {s.params.effective(kPosX), s.params.effective(kPosY), s.params.effective(kPosZ)};
    s.body.vel = {s.params.effective(kVelX), s.params.effective(kVelY), s.params.effective(kVelZ)};
    s.body.spin = {s.params.effective(kSpinX), s.params.effective(kSpinY), s.params.effective(kSpinZ)};
    s.body.scale = s.params.effective(kScale);
    s.body.age = 0.0f;
    s.body.alive = true;
    ++live_;
    result.instance = s.instance;
    return result;
}

void ObjectPool::vacate(Slot& s) {
    if (!s.used) return;
    s.used = false;
    s.body.alive = false;
    s.instance = 0;
    s.spawner = -1;
    --live_;
}

bool ObjectPool::remove(std::uint32_t instance) {
    Slot* s = find(instance);
    if (s == nullptr) return false;
    vacate(*s);
    return true;
}

void ObjectPool::clear() {
    for (Slot& s : slots_) vacate(s);
}

ObjectPool::Slot* ObjectPool::find(std::uint32_t instance) {
    if (instance == 0) return nullptr;
    for (Slot& s : slots_) {
        if (s.used && s.instance == instance) return &s;
    }
    return nullptr;
}

const ObjectPool::Slot* ObjectPool::find(std::uint32_t instance) const {
    return const_cast<ObjectPool*>(this)->find(instance);
}

int ObjectPool::aliveFrom(int spawner) const {
    int n = 0;
    for (const Slot& s : slots_) {
        if (s.used && s.spawner == spawner) ++n;
    }
    return n;
}

std::optional<TargetRef> ObjectPool::resolve(std::string_view address) const {
    const std::optional<TargetAddress> a = parseTarget(address);
    if (!a) return std::nullopt;
    return resolve(a->instance, a->param);
}

std::optional<TargetRef> ObjectPool::resolve(std::uint32_t instance, std::string_view paramId) const {
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const Slot& s = slots_[i];
        if (!s.used || s.instance != instance) continue;
        const std::optional<signal::ParamId> row = s.params.schema().find(paramId);
        if (!row) return std::nullopt;
        return TargetRef{static_cast<std::uint16_t>(i), *row, instance};
    }
    return std::nullopt;
}

bool ObjectPool::valid(const TargetRef& t) const {
    return t.slot < slots_.size() && slots_[t.slot].used && slots_[t.slot].instance == t.instance &&
           slots_[t.slot].params.schema().contains(t.param);
}

void ObjectPool::modulate(const TargetRef& t, signal::Combine combine, float amount, float weight) {
    if (valid(t)) slots_[t.slot].params.modulate(t.param, combine, amount, weight);
}

signal::Outcome ObjectPool::input(const TargetRef& t, const signal::ParamInput& in) {
    if (!valid(t)) return signal::Outcome::Rejected;
    return slots_[t.slot].params.input(t.param, in);
}

float ObjectPool::effective(const TargetRef& t) const { return valid(t) ? slots_[t.slot].params.effective(t.param) : 0.0f; }

}  // namespace geode::scene::object
