#include "scene/object/Object.hpp"

#include <algorithm>
#include <cmath>

namespace geode::scene::object {

namespace {

using signal::ParamId;
using signal::ParamInput;
using signal::ParamStore;

void put(ParamStore& store, ParamId id, float v) {
    // Let go of whatever the slot's last object left on the row (a finger, a timeline), then set it.
    // Each input is a no-op in a phase it means nothing in, so the three always leave the row at rest.
    store.input(id, ParamInput::release());
    store.input(id, ParamInput::undrive());
    store.input(id, ParamInput::set(v, signal::Origin::Engine));
}

float asFloat(int v) { return static_cast<float>(v); }

// How much of the object a fade has taken away (g, 0..1, after the curve), by the fade's kind.
void applyGone(Exit kind, float g, float spread, LifeLook& look) {
    switch (kind) {
        case Exit::Fade:
            look.opacity *= 1.0f - g;
            break;
        case Exit::Collapse:
            look.scale *= 1.0f - g;
            look.collapse = std::max(look.collapse, g);
            break;
        case Exit::Burst:
            look.scale *= 1.0f + spread * g;
            look.opacity *= 1.0f - g;
            look.burst = std::max(look.burst, g);
            break;
    }
}

Exit exitOf(float v) { return static_cast<Exit>(std::clamp(static_cast<int>(v), 0, kExitCount - 1)); }

}  // namespace

void writeSpec(const ObjectSpec& s, ParamStore& store) {
    put(store, kPosX, s.pos[0]);
    put(store, kPosY, s.pos[1]);
    put(store, kPosZ, s.pos[2]);
    put(store, kVelX, s.vel[0]);
    put(store, kVelY, s.vel[1]);
    put(store, kVelZ, s.vel[2]);
    put(store, kRotYaw, s.rot.yaw);
    put(store, kRotPitch, s.rot.pitch);
    put(store, kRotRoll, s.rot.roll);
    put(store, kSpinX, s.spin[0]);
    put(store, kSpinY, s.spin[1]);
    put(store, kSpinZ, s.spin[2]);
    put(store, kScale, s.scale);
    put(store, kPalette, s.palette);
    put(store, kGlow, s.glow);
    put(store, kOpacity, s.opacity);
    put(store, kMaterial, asFloat(s.material));
    put(store, kBirth, s.birth);
    put(store, kLifetime, s.lifetime);
    put(store, kFadeIn, s.fadeIn);
    put(store, kFadeOut, s.fadeOut);
    put(store, kFadeCurve, s.fadeCurve);
    put(store, kAgeRate, s.ageRate);
    put(store, kDeath, asFloat(static_cast<int>(s.death)));
    put(store, kDeathBeats, asFloat(s.deathBeats));
    put(store, kDeathEvent, asFloat(static_cast<int>(s.deathEvent)));
    put(store, kEntry, asFloat(static_cast<int>(s.entry)));
    put(store, kExit, asFloat(static_cast<int>(s.exit)));
    put(store, kBurstSpread, s.burstSpread);
    put(store, kAnchor, asFloat(static_cast<int>(s.anchor)));
    put(store, kTargetMode, asFloat(static_cast<int>(s.target)));
    put(store, kTargetDistance, s.targetDistance);
    put(store, kTargetHeight, s.targetHeight);
    put(store, kTargetOrbitRate, s.targetOrbitRate);
    if (store.size() >= kShapeParamCount) {
        put(store, kShapeType, asFloat(static_cast<int>(s.shape)));
        put(store, kShapeSizeX, s.size[0]);
        put(store, kShapeSizeY, s.size[1]);
        put(store, kShapeSizeZ, s.size[2]);
        put(store, kShapeRound, s.round);
    }
    store.clearModulation();
}

float fadeCurve(float t, float curve) {
    if (!std::isfinite(t)) t = 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float c = std::clamp(std::isfinite(curve) ? curve : 1.0f, 0.0f, 1.0f);
    const float smooth = t * t * (3.0f - 2.0f * t);
    return t + (smooth - t) * c;
}

LifeLook lifeLook(const ParamStore& p, const Life& life, float age) {
    LifeLook look;
    const float birth = p.effective(kBirth);
    if (life.phase == Phase::Waiting || age < birth) return look;
    look.opacity = 1.0f;
    look.scale = 1.0f;
    const float curve = p.effective(kFadeCurve);
    const float spread = p.effective(kBurstSpread);
    const float fadeIn = p.effective(kFadeIn);
    const float sinceBirth = age - birth;
    const float in = fadeIn > 0.0f ? fadeCurve(sinceBirth / fadeIn, curve) : 1.0f;
    applyGone(exitOf(p.effective(kEntry)), 1.0f - in, spread, look);
    if (life.phase == Phase::Exiting) {
        const float fadeOut = p.effective(kFadeOut);
        const float out = fadeOut > 0.0f ? fadeCurve(life.exitAge / fadeOut, curve) : 1.0f;
        applyGone(exitOf(p.effective(kExit)), out, spread, look);
    }
    return look;
}

}  // namespace geode::scene::object
