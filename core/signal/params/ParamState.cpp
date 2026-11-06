#include "signal/params/ParamState.hpp"

#include <algorithm>
#include <cmath>

namespace geode::signal {

namespace {

bool interpolates(const ParamDef& def) { return def.kind == ParamKind::Float || def.kind == ParamKind::Circular; }

float eased(Ease ease, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return ease == Ease::Smooth ? t * t * (3.0f - 2.0f * t) : t;
}

void land(ParamCell& cell, float value) {
    cell.phase = ParamPhase::Rest;
    cell.base = value;
    cell.committed = value;
    cell.elapsed = 0.0f;
    cell.duration = 0.0f;
    cell.trialHeld = false;
    cell.trialLeft = 0.0f;
}

// Starts a glide from wherever the base is now to `target` (already clamped). Lands at once when the
// param does not interpolate or there is no time to glide in.
void glide(ParamCell& cell, float target, float seconds, Ease ease, const ParamDef& def) {
    if (!interpolates(def) || !(seconds > 0.0f) || cell.base == target) {
        land(cell, target);
        return;
    }
    cell.phase = ParamPhase::Glide;
    cell.from = cell.base;
    cell.committed = target;
    cell.elapsed = 0.0f;
    cell.duration = seconds;
    cell.ease = ease;
    cell.trialHeld = false;
    cell.trialLeft = 0.0f;
}

void springBack(ParamCell& cell, const ParamDef& def) { glide(cell, cell.committed, kSpringBackSeconds, Ease::Smooth, def); }

float trialSeconds(float seconds) { return seconds > 0.0f && std::isfinite(seconds) ? seconds : kTrialSeconds; }

using Kind = ParamInput::Kind;

Outcome fromRestOrGlide(ParamCell& cell, const ParamInput& in, const ParamDef& def, float value) {
    switch (in.kind) {
        case Kind::Set:
            land(cell, value);
            return Outcome::Applied;
        case Kind::GlideTo:
            glide(cell, value, in.seconds, in.ease, def);
            return Outcome::Applied;
        case Kind::Reset:
            glide(cell, ParamSchema::clampTo(def, def.defaultValue), in.seconds, in.ease, def);
            return Outcome::Applied;
        case Kind::Hold:
            if (in.origin != Origin::User) return Outcome::Rejected;
            // The finger holds what is on screen: a glide stops where it is.
            cell.phase = ParamPhase::Held;
            cell.committed = cell.base;
            return Outcome::Applied;
        case Kind::Try:
            if (in.origin != Origin::User) return Outcome::Rejected;
            // committed is already the right spring-back target: the value at rest, a glide's target.
            cell.phase = ParamPhase::Trial;
            cell.base = value;
            cell.trialHeld = false;
            cell.trialLeft = trialSeconds(in.seconds);
            return Outcome::Applied;
        case Kind::Drive:
            if (in.origin != Origin::Timeline) return Outcome::Rejected;
            cell.phase = ParamPhase::Driven;
            return Outcome::Applied;
        case Kind::Release:
        case Kind::Undrive:
            return Outcome::Ignored;
    }
    return Outcome::Ignored;
}

Outcome fromHeld(ParamCell& cell, const ParamInput& in, float value) {
    switch (in.kind) {
        case Kind::Set:
            if (in.origin != Origin::User) return Outcome::Rejected;
            cell.base = value;
            cell.committed = value;
            return Outcome::Applied;
        case Kind::Release:
            cell.phase = ParamPhase::Rest;
            cell.committed = cell.base;
            return Outcome::Applied;
        case Kind::Try:
            if (in.origin != Origin::User) return Outcome::Rejected;
            // The finger was on the control already: committed is the value it held.
            cell.phase = ParamPhase::Trial;
            cell.base = value;
            cell.trialHeld = true;
            cell.trialLeft = trialSeconds(in.seconds);
            return Outcome::Applied;
        case Kind::Hold:
        case Kind::Undrive:
            return Outcome::Ignored;
        case Kind::GlideTo:
        case Kind::Drive:
        case Kind::Reset:
            return Outcome::Rejected;
    }
    return Outcome::Ignored;
}

Outcome fromTrial(ParamCell& cell, const ParamInput& in, const ParamDef& def, float value) {
    switch (in.kind) {
        case Kind::Set:
        case Kind::GlideTo:
        case Kind::Reset: {
            const float v = in.kind == Kind::Reset ? ParamSchema::clampTo(def, def.defaultValue) : value;
            if (in.origin == Origin::User && in.kind == Kind::Set) {
                cell.base = v;  // dragging the tried value
                return Outcome::Applied;
            }
            if (in.origin == Origin::Preset || (in.origin == Origin::User && in.kind == Kind::Reset)) {
                cell.committed = v;  // what it springs back to
                return Outcome::Applied;
            }
            return Outcome::Rejected;
        }
        case Kind::Try:
            if (in.origin != Origin::User) return Outcome::Rejected;
            cell.base = value;
            cell.trialLeft = trialSeconds(in.seconds);
            return Outcome::Applied;
        case Kind::Hold:
            if (in.origin != Origin::User) return Outcome::Rejected;
            cell.trialHeld = true;
            return Outcome::Applied;
        case Kind::Release:
            springBack(cell, def);
            return Outcome::Applied;
        case Kind::Drive:
            if (in.origin != Origin::Timeline) return Outcome::Rejected;
            // The trial ends without a spring back: the timeline takes the base from here.
            cell.phase = ParamPhase::Driven;
            cell.trialHeld = false;
            cell.trialLeft = 0.0f;
            return Outcome::Applied;
        case Kind::Undrive:
            return Outcome::Ignored;
    }
    return Outcome::Ignored;
}

Outcome fromDriven(ParamCell& cell, const ParamInput& in, const ParamDef& def, float value) {
    switch (in.kind) {
        case Kind::Set:
        case Kind::GlideTo:
            if (in.origin != Origin::Timeline) return Outcome::Rejected;
            cell.base = value;
            return Outcome::Applied;
        case Kind::Undrive:
            if (in.origin != Origin::Timeline) return Outcome::Rejected;
            springBack(cell, def);
            return Outcome::Applied;
        case Kind::Release:
        case Kind::Drive:
            return Outcome::Ignored;
        case Kind::Hold:
        case Kind::Try:
        case Kind::Reset:
            return Outcome::Rejected;
    }
    return Outcome::Ignored;
}

// A glide's length must be a real, non-negative number of seconds; 0 lands at once.
bool validGlide(const ParamInput& in) {
    if (in.kind != Kind::GlideTo && in.kind != Kind::Reset) return true;
    return std::isfinite(in.seconds) && in.seconds >= 0.0f;
}

}  // namespace

ParamCell restingCell(const ParamDef& def) {
    ParamCell cell;
    land(cell, ParamSchema::clampTo(def, def.defaultValue));
    return cell;
}

Outcome apply(ParamCell& cell, const ParamInput& in, const ParamDef& def) {
    if (!validGlide(in)) return Outcome::Rejected;
    const float value = ParamSchema::clampTo(def, in.value);
    switch (cell.phase) {
        case ParamPhase::Rest:
        case ParamPhase::Glide:
            return fromRestOrGlide(cell, in, def, value);
        case ParamPhase::Held:
            return fromHeld(cell, in, value);
        case ParamPhase::Trial:
            return fromTrial(cell, in, def, value);
        case ParamPhase::Driven:
            return fromDriven(cell, in, def, value);
    }
    return Outcome::Ignored;
}

void advance(ParamCell& cell, float dt, const ParamDef& def) {
    if (!(dt > 0.0f) || !std::isfinite(dt)) return;
    if (cell.phase == ParamPhase::Glide) {
        cell.elapsed += dt;
        if (cell.elapsed >= cell.duration) {
            land(cell, cell.committed);
            return;
        }
        const float k = eased(cell.ease, cell.elapsed / cell.duration);
        // A circular param glides the short way round, then wraps.
        const float span = ParamSchema::circularDelta(def, cell.from, cell.committed);
        cell.base = ParamSchema::wrap(def, cell.from + span * k);
        return;
    }
    if (cell.phase == ParamPhase::Trial && !cell.trialHeld) {
        cell.trialLeft -= dt;
        if (cell.trialLeft <= 0.0f) springBack(cell, def);
    }
}

const char* phaseName(ParamPhase phase) {
    switch (phase) {
        case ParamPhase::Rest: return "rest";
        case ParamPhase::Glide: return "glide";
        case ParamPhase::Held: return "held";
        case ParamPhase::Trial: return "trial";
        case ParamPhase::Driven: return "driven";
    }
    return "?";
}

const char* inputName(ParamInput::Kind kind) {
    switch (kind) {
        case Kind::Set: return "set";
        case Kind::GlideTo: return "glideTo";
        case Kind::Hold: return "hold";
        case Kind::Release: return "release";
        case Kind::Try: return "try";
        case Kind::Drive: return "drive";
        case Kind::Undrive: return "undrive";
        case Kind::Reset: return "reset";
    }
    return "?";
}

}  // namespace geode::signal
