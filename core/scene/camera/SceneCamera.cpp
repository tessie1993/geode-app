#include "scene/camera/SceneCamera.hpp"

#include <algorithm>
#include <cmath>

namespace geode::scene::camera {

namespace {

using viz::CameraRig;

float clampDt(float dt) { return std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f; }

Vec3 v3(const std::array<float, 3>& a) { return {a[0], a[1], a[2]}; }

constexpr float kRateEpsilon = 1e-4f;

}  // namespace

SceneCamera::SceneCamera() : store_(cameraSchema()) {}

void SceneCamera::reset() {
    rig_.reset();
    takeover_.reset();
    comfort_.reset();
    primed_ = false;
    rigCuts_ = 0;
    lastProgrammeTime_ = programmeTime_;
    spinYaw_ = spinPitch_ = spinRoll_ = 0.0f;
    slide_ = {0.0f, 0.0f, 0.0f};
    pose_ = viz::CameraPose{};
    frame_ = Pose{};
}

void SceneCamera::setMode(Mode mode) {
    if (mode == mode_) return;
    mode_ = mode;
    // Rails is forward flight from today's path: nothing SixDof integrated survives into it. The comfort
    // stage is not primed again, so the switch itself glides under the speed cap instead of cutting.
    spinYaw_ = spinPitch_ = spinRoll_ = 0.0f;
    slide_ = {0.0f, 0.0f, 0.0f};
    takeover_.reset();
}

void SceneCamera::setComfort(Comfort level) {
    const int v = static_cast<int>(level);
    const float f = (v >= 0 && v <= 2) ? static_cast<float>(v) : static_cast<float>(Comfort::Normal);
    store_.input(kComfort, signal::ParamInput::set(f, signal::Origin::User));
}

void SceneCamera::step(const FrameInput& in) {
    if (in.params == nullptr) return;
    const float dt = clampDt(in.dt);
    store_.advance(dt);
    auto param = [this](CameraParam id) { return store_.effective(id); };

    const int chosen = static_cast<int>(std::lround(param(kComfort)));
    frameComfort_ = effectiveComfort(static_cast<Comfort>(std::clamp(chosen, 0, 2)), in.reducedMotion);
    const bool sixDof = mode_ == Mode::SixDof;

    // ---- 1. Base: today's rig ---------------------------------------------------------------------------
    // In Rails the rig takes the look exactly as it always has; in SixDof the takeover layer owns it.
    const CameraRig::LookInput rigLook = sixDof ? CameraRig::LookInput{} : in.look;
    rig_.step(*in.params, rigLook, dt, in.reducedMotion, in.bankFlat, in.sound);
    const viz::CameraPose& rp = rig_.pose();
    const Quat qRig = fromBasis(v3(rp.right), v3(rp.up), v3(rp.fwd));
    // The rig's path frame before its look: rig = path * fromLook(yaw, pitch, roll).
    const Quat qPath = qnormalize(mul(qRig, conj(fromLook({rp.yaw, rp.pitch, rp.roll}))));
    const bool rigCut = rig_.cuts() != rigCuts_;
    rigCuts_ = rig_.cuts();

    Pose base{v3(rp.pos), qRig, rp.focal * param(kFocal)};
    const float yawRate = param(kYawRate);
    const float pitchRate = param(kPitchRate);
    const float rollRate = param(kRollRate);
    const float lookRoll = sixDof ? param(kLookRoll) : 0.0f;
    Pose result = base;
    if (sixDof) {
        // Speeds are integrated: a changed rate changes how fast the view turns, never where it is.
        spinYaw_ = wrapAngle(spinYaw_ + yawRate * dt);
        spinPitch_ = wrapAngle(spinPitch_ + pitchRate * dt);
        spinRoll_ = wrapAngle(spinRoll_ + rollRate * dt);
        const Look held{spinYaw_ + param(kLookYaw), spinPitch_ + param(kLookPitch), spinRoll_ + lookRoll};
        base.orient = qnormalize(mul(qRig, fromLook(held)));
        slide_ = add(slide_, scale(add(scale(rightOf(base.orient), param(kStrafe)), scale(upOf(base.orient), param(kRise))), dt));
        base.pos = add(base.pos, slide_);

        // ---- 2. Takeover ----------------------------------------------------------------------------------
        takeover_.step({in.look.yaw, in.look.pitch, in.lookActive}, dt, param(kTakeoverReach), param(kTakeoverFollow),
                       param(kTakeoverGlide));
        Pose over = base;
        over.orient = qnormalize(mul(base.orient, takeover_.offset()));
        result = blend(base, over, takeover_.weight());
    }

    // ---- 3. Keyframes -------------------------------------------------------------------------------------
    float keyWeight = 0.0f;
    bool keyCut = false;
    if (!keys_.empty()) {
        keyWeight = param(kKeyframeWeight);
        keyCut = keyWeight > 0.0f && keys_.cutBetween(lastProgrammeTime_, programmeTime_);
        result = blend(result, keys_.sample(programmeTime_), keyWeight);
    }
    lastProgrammeTime_ = programmeTime_;

    // ---- 4. Rails -----------------------------------------------------------------------------------------
    // Only what the layers above lay over the base is clamped; the base itself is today's law, untouched.
    if (!sixDof && keyWeight > 0.0f) {
        Look rel = toLook(qnormalize(mul(conj(base.orient), result.orient)));
        rel.yaw = std::clamp(rel.yaw, -CameraRig::kMaxYaw, CameraRig::kMaxYaw);
        rel.pitch = std::clamp(rel.pitch, -CameraRig::kMaxPitch, CameraRig::kMaxPitch);
        rel.roll = std::clamp(rel.roll, -CameraRig::kMaxRoll, CameraRig::kMaxRoll);
        result.orient = qnormalize(mul(base.orient, fromLook(rel)));
        result.pos = base.pos;
        // No flips on rails: hold the roll short of a quarter turn (unless the base is itself over).
        float roll = 0.0f;
        float baseRoll = 0.0f;
        if (rollFromLevel(result.orient, roll) && rollFromLevel(base.orient, baseRoll) && std::fabs(baseRoll) <= kNoFlipRoll) {
            const float fixed = std::clamp(roll, -kNoFlipRoll, kNoFlipRoll);
            if (fixed != roll) result.orient = qnormalize(mul(result.orient, axisAngle({0.0f, 0.0f, 1.0f}, fixed - roll)));
        }
    }

    // ---- 5. Comfort and the safety clamp -------------------------------------------------------------------
    ComfortStage::Input ci;
    ci.target = result;
    float rigRoll = 0.0f;
    ci.referenceRoll = wrapAngle((rollFromLevel(qRig, rigRoll) ? rigRoll : 0.0f) + lookRoll);
    ci.rollHeld = param(kHoldRoll) >= 0.5f || std::fabs(rollRate) > kRateEpsilon || std::fabs(pitchRate) > kRateEpsilon ||
                  keyWeight > 0.0f;
    ci.cut = rigCut || keyCut;
    ci.primeOnly = !primed_;
    ci.safetyOnly = !sixDof;
    const ComfortStage::Output out = comfort_.step(ci, frameComfort_, dt);
    primed_ = true;
    frame_ = out.pose;

    // ---- 6. The scenes' pose ------------------------------------------------------------------------------
    pose_ = rp;
    if (!sixDof && keyWeight <= 0.0f && out.untouched) {
        // Rails, nothing laid over: the rig's pose as it is, so every style draws exactly as before.
        pose_.focal = out.pose.focal;
    } else {
        const Quat& q = out.pose.orient;
        pose_.pos = out.pose.pos;
        pose_.right = rightOf(q);
        pose_.up = upOf(q);
        pose_.fwd = fwdOf(q);
        pose_.focal = out.pose.focal;
        // uCamLook and the flat projection read the look relative to the path, as the rig reports it.
        const Look l = toLook(qnormalize(mul(conj(qPath), q)));
        pose_.yaw = l.yaw;
        pose_.pitch = l.pitch;
        pose_.roll = l.roll;
        pose_.flat[3] = rp.flat[3] + wrapAngle(l.roll - rp.roll);
        pose_.tilt = {std::clamp(l.yaw, -1.0f, 1.0f) * CameraRig::kFlatTilt, std::clamp(l.pitch, -1.0f, 1.0f) * CameraRig::kFlatTilt};
    }
    if (rp.focal > 0.0f && pose_.focal != rp.focal) pose_.flat[2] = rp.flat[2] * (pose_.focal / rp.focal);

    store_.clearModulation();
}

}  // namespace geode::scene::camera
