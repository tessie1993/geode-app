#include "viz/world/MorphDriver.hpp"

#include <algorithm>
#include <cmath>

#include "viz/world/WorldAssets.hpp"

namespace geode::viz::world {

namespace {

constexpr float kTau = 6.2831853f;
// CameraRig::kTravelWrap: the eye's z jumps back by this much when travel wraps.
constexpr float kTravelWrap = 8192.0f;

float clamp01(float x) { return std::clamp(std::isfinite(x) ? x : 0.0f, 0.0f, 1.0f); }

// Exponential approach with time constant tau, exact for any dt.
float ease(float v, float target, float tau, float dt) { return v + (target - v) * (1.0f - std::exp(-dt / tau)); }

float wrap01(float x) { return x - std::floor(x); }

}  // namespace

DriverInput DriverInput::fromSound(const GeodeSoundFrame& f, float audioDrive) {
    DriverInput in;
    const float drive = std::isfinite(audioDrive) ? std::max(audioDrive, 0.0f) : 1.0f;
    in.presence = clamp01(f.presence);
    in.energy = clamp01(drive * (0.6f * f.loudness + 0.4f * f.intensity) * (0.4f + 0.6f * in.presence));
    in.mood = clamp01(0.5f * f.scaleBrightness + 0.5f * f.keyMajor);
    in.density = clamp01(0.6f * f.densitySlow + 0.4f * f.runDensity);
    in.bass = std::clamp(drive * (std::isfinite(f.bassAtt) ? f.bassAtt : 0.0f), 0.0f, 2.0f);
    in.kick = clamp01(drive * f.kickEnv);
    in.chordHue = wrap01(std::isfinite(f.chordHue) ? f.chordHue : 0.0f);
    in.buildUp = clamp01(f.buildUp);
    in.section = f.sectionChange > 0.5f;
    in.drop = f.drop > 0.5f;
    return in;
}

void MorphDriver::reset(uint64_t seed) {
    *this = MorphDriver{};
    seed_ = seed;
    rng_ = mix64(seed ^ 0x576F726C64447276ull);
    gazeRng_ = mix64(seed ^ 0x47617A6553747265ull);
}

float MorphDriver::draw() {
    rng_ = mix64(rng_);
    return static_cast<float>((rng_ >> 40) & 0xFFFFFFu) / 16777216.0f;
}

float MorphDriver::drawGaze() {
    gazeRng_ = mix64(gazeRng_);
    return static_cast<float>((gazeRng_ >> 40) & 0xFFFFFFu) / 16777216.0f;
}

void MorphDriver::retarget(const DriverInput& in, bool drop) {
    (void) in;
    Constraints c;
    // Morph is read slowly (morph_), never this beat's value: the target should not depend on where
    // in the bar the section landed.
    c.mutation = std::clamp(0.08f + 0.55f * morph_ + (drop ? 0.15f : 0.0f), 0.0f, 1.0f);
    c.energy = energy_;
    c.mood = mood_;
    c.density = density_;
    c.avoid = toSeed_;
    rng_ = mix64(rng_);
    int picked = 0;
    from_ = now_;
    to_ = random(rng_, c, &picked);
    toSeed_ = picked;
    progress_ = 0.0f;
    glide_ = drop ? kDropGlide : kVerseGlideSlow + (kVerseGlideFast - kVerseGlideSlow) * energy_;
    ++retargets_;
}

void MorphDriver::step(const DriverInput& in, float dt) {
    dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f;
    if (!started_) {
        started_ = true;
        Constraints c;
        c.mutation = 0.1f;
        c.energy = 0.5f;
        rng_ = mix64(rng_);
        to_ = random(rng_, c, &toSeed_);
        from_ = to_;
        now_ = to_;
        progress_ = 1.0f;
        energy_ = in.energy;
        mood_ = in.mood;
        density_ = in.density;
        morph_ = clamp01(in.morph);
    }

    // The song, read slowly.
    energy_ = ease(energy_, clamp01(in.energy), 3.0f, dt);
    mood_ = ease(mood_, clamp01(in.mood), 6.0f, dt);
    density_ = ease(density_, clamp01(in.density), 4.0f, dt);
    morph_ = ease(morph_, clamp01(in.morph), 4.0f, dt);
    warp_ = ease(warp_, clamp01(in.warp), 1.2f, dt);
    turb_ = ease(turb_, std::clamp(in.turbulence, 0.0f, 1.5f) / 1.5f, 1.2f, dt);
    bass_ = ease(bass_, std::clamp(in.bass, 0.0f, 1.5f), 0.25f, dt);
    kick_ = ease(kick_, clamp01(in.kick), 0.08f, dt);
    buildUp_ = ease(buildUp_, clamp01(in.buildUp), 1.0f, dt);
    hueX_ = ease(hueX_, std::cos(kTau * in.chordHue), 1.5f, dt);
    hueY_ = ease(hueY_, std::sin(kTau * in.chordHue), 1.5f, dt);
    const float t = std::clamp((in.detail - 0.25f) / 1.25f, 0.0f, 1.0f);
    iterCap_ = std::round(2.0f + 3.0f * t);

    // When: a drop always turns the world; a section turns it by chance, likelier the louder the
    // song is, so a calm one can keep its world. Edges only: a trigger held two frames is one moment.
    const bool dropEdge = in.drop && !lastDrop_;
    const bool sectionEdge = in.section && !lastSection_;
    lastDrop_ = in.drop;
    lastSection_ = in.section;
    if (dropEdge) {
        retarget(in, true);
    } else if (sectionEdge) {
        const float chance = std::clamp(0.12f + 0.78f * energy_, 0.12f, 0.9f);
        if (draw() < chance) retarget(in, false);
    }
    // The gaze's moments: every section and drop draws a new look (wider when the song is loud);
    // a drop first holds the view at centre while it lands.
    if (dropEdge || sectionEdge) {
        const float reach = 0.45f + 0.55f * energy_;
        lookX_ = (drawGaze() * 2.0f - 1.0f) * reach;
        lookY_ = (drawGaze() * 2.0f - 1.0f) * reach;
        if (dropEdge) dropHold_ = kDropGazeHold;
    }

    // How fast: FLOW (k2) scales every glide, 0.5x .. 1.5x.
    const float flow = 0.5f + clamp01(in.knobs[2]);
    if (progress_ < 1.0f) progress_ = std::min(1.0f, progress_ + dt * flow / std::max(glide_, 0.1f));
    now_ = lerp(from_, to_, progress_);

    // The live wobble on top, summed into one copy and clamped once.
    const auto& k = in.knobs;
    Genome g = now_;
    g[IfsScale] += 0.06f * bass_ * (0.5f + k[0]);
    g[InvRadius] += 0.12f * bass_;
    g[Ifs] += 0.35f * turb_;
    g[IfsIter] += 1.2f * (k[1] - 0.5f);
    g[IfsAngA] += 0.25f * (k[1] - 0.5f);
    g[Twist] += 0.8f * warp_;
    g[PalOffset] += 0.3f * (k[3] - 0.5f);
    g[Emission] += 0.22f * kick_ * (0.5f + k[0]) + 0.3f * (k[4] - 0.5f) + 0.25f * buildUp_ + 0.2f * (in.intensity - 1.0f);
    g[Corridor] += 0.6f * (k[5] - 0.5f);
    live_ = clampFields(g);

    // The brightness envelope: hold the frame's level while the world's own brightness moves,
    // rate-limited so no retarget, drop or not, ever steps the picture's level.
    const float want = std::clamp(0.85f / std::max(brightness(live_), 0.2f), 0.6f, 1.5f);
    const float eased = ease(gain_, want, 1.5f, dt);
    const float limit = kGainRatePerSecond * dt;
    gain_ = std::clamp(eased, gain_ - limit, gain_ + limit);

    // The view's roll: the world's own rate, a little faster with the energy, kept in one turn.
    roll_ += dt * live_[Roll] * (0.7f + 0.6f * energy_);
    roll_ -= kTau * std::floor(roll_ / kTau + 0.5f);
    swayPhase_ = wrap01(swayPhase_ + dt * 0.07f);
    lookAround(dt);
    publish();
}

void MorphDriver::lookAround(float dt) {
    bassSlow_ = ease(bassSlow_, bass_ / 1.5f, 2.0f, dt);
    // The live world's reach: it glides with the genome, so a retarget widens or narrows it smoothly.
    const float yawRange = std::max(live_[GazeYaw], 0.0f);
    const float pitchRange = std::max(live_[GazePitch], 0.0f);
    float nx = 0.0f;
    float ny = 0.0f;
    float tau = 0.3f;
    if (dropHold_ > 0.0f) {
        // The drop: back to centre, as fast as the rate allows (about a second from the widest look).
        dropHold_ = std::max(0.0f, dropHold_ - dt);
    } else {
        // The section's look, the chord's sideways drift, the build-up's rise, the bass's weight.
        nx = lookX_ + 0.15f * hueY_;
        ny = lookY_ + 0.8f * buildUp_ * (1.0f - lookY_) - 0.1f * bassSlow_;
        tau = 2.2f;
    }
    const float tx = std::clamp(nx, -1.0f, 1.0f) * yawRange;
    const float ty = std::clamp(ny, -1.0f, 1.0f) * pitchRange;
    const float limit = kGazeRatePerSecond * dt;
    gazeX_ = std::clamp(ease(gazeX_, tx, tau, dt), gazeX_ - limit, gazeX_ + limit);
    gazeY_ = std::clamp(ease(gazeY_, ty, tau, dt), gazeY_ - limit, gazeY_ + limit);
    gazeX_ = std::clamp(gazeX_, -yawRange, yawRange);
    gazeY_ = std::clamp(gazeY_, -pitchRange, pitchRange);
}

void MorphDriver::travel(const std::array<float, 3>& eye, float dt) {
    dt = std::isfinite(dt) ? std::clamp(dt, 0.0f, 0.1f) : 0.0f;
    if (!hasEye_) {
        hasEye_ = true;
        eye_ = eye;
    }
    std::array<float, 3> d{eye[0] - eye_[0], eye[1] - eye_[1], eye[2] - eye_[2]};
    // The rig's travel wraps at 8192; the world must not see the jump.
    if (d[2] > kTravelWrap * 0.5f) d[2] -= kTravelWrap;
    if (d[2] < -kTravelWrap * 0.5f) d[2] += kTravelWrap;
    eye_ = eye;
    // The phase is kept in periods of the live world (two mirrored cells), so a period that morphs stretches the world
    // about the eye instead of sliding it past (z / CellZ at a large z would race).
    zCells_ = wrap01(zCells_ + d[2] / std::max(2.0f * live_[CellZ], 1.0f));
    if (dt > 0.0f) {
        for (int i = 0; i < 3; ++i) {
            const float v = std::clamp(d[i] / dt, -20.0f, 20.0f);
            vel_[i] = ease(vel_[i], v, 0.4f, dt);
        }
    }
    publish();
}

Way MorphDriver::way() const {
    Way w;
    w.zPhase = uniforms_[ZPhase];
    w.eye = eye_;
    w.wake = {uniforms_[WakeX], uniforms_[WakeY], uniforms_[WakeZ]};
    w.iterCap = static_cast<int>(uniforms_[IterCap]);
    return w;
}

void MorphDriver::publish() {
    for (int f = 0; f < kFieldCount; ++f) uniforms_[static_cast<size_t>(f)] = live_[f];
    uniforms_[ZPhase] = zCells_ * 2.0f * live_[CellZ];
    uniforms_[Gain] = gain_;
    uniforms_[IterCap] = iterCap_;
    uniforms_[HueTurn] = 0.2f * std::atan2(hueY_, hueX_) / kTau;
    // The wake: where the eye will be in about a second, at most four units ahead.
    std::array<float, 3> wake{vel_[0] * 1.2f, vel_[1] * 1.2f, vel_[2] * 1.2f};
    const float len = std::sqrt(wake[0] * wake[0] + wake[1] * wake[1] + wake[2] * wake[2]);
    if (len > 4.0f) {
        for (float& c : wake) c *= 4.0f / len;
    }
    uniforms_[WakeX] = wake[0];
    uniforms_[WakeY] = wake[1];
    uniforms_[WakeZ] = wake[2];
    uniforms_[Assets] = hasShapes() && texturesBound_ ? 1.0f : 0.0f;
    // The bank: the roll, a slow sway (deeper when the song is loud), and a lean into sideways motion.
    const float sway = (0.05f + 0.1f * energy_) * std::sin(kTau * swayPhase_);
    const float lean = std::clamp(-0.03f * vel_[0], -0.2f, 0.2f);
    uniforms_[RollAngle] = roll_ + sway + lean;
    // The lens: the world's, punched in a touch on the kick.
    uniforms_[LensNow] = std::clamp(live_[Lens] * (1.0f + 0.05f * kick_), 0.7f, 1.45f);
    // The gaze: yaw about the view's up, pitch about its right (lib_world's worldCamera).
    uniforms_[GazeX] = gazeX_;
    uniforms_[GazeY] = gazeY_;
}

}  // namespace geode::viz::world
