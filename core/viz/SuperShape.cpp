#include "viz/SuperShape.hpp"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>

#include "viz/Program.hpp"
#include "viz/Smoothing.hpp"

namespace geode::viz {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.2831853f;
constexpr int kAzimuthRow = 0;
constexpr int kElevationRow = 1;

float mixf(float a, float b, float t) { return a + (b - a) * t; }

}  // namespace

float SuperShape::radius(float angle, float m, float n1, float n2, float n3, float a, float b) {
    const float arg = m * angle * 0.25f;
    const float c = std::fabs(std::cos(arg) / a);
    const float s = std::fabs(std::sin(arg) / b);
    const float sum = std::pow(c, n2) + std::pow(s, n3);
    // sum is strictly positive (cos and sin cannot both vanish), so the floor
    // only ever guards a denormal; the reciprocal exponent is finite because
    // n1 is clamped above kN1Floor.
    return std::pow(std::max(sum, 1e-6f), -1.0f / n1);
}

float SuperShape::pick(float lo, float hi, float t) { return lo + (hi - lo) * t; }

// A 32-bit LCG, the same one ShaderScene uses to pick its plateaus.
float SuperShape::nextRandom() {
    seedState_ = seedState_ * 1664525u + 1013904223u;
    return static_cast<float>((seedState_ >> 8) & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

int SuperShape::randomCount(int lo, int hi, int avoid) {
    const int span = hi - lo + 1;
    if (span <= 1) return lo;
    int pick = lo + std::min(static_cast<int>(nextRandom() * static_cast<float>(span)), span - 1);
    // Re-rolling to a count the picture already has is not a change, so the
    // avoided count is skipped over rather than drawn again.
    if (pick == avoid) pick = pick == hi ? lo : pick + 1;
    return pick;
}

SuperShape::SuperShape() { reset(); }

void SuperShape::reset() {
    azimuth_ = Curve{};
    elevation_ = Curve{};
    elevation_.m = 4;
    azimuthLockout_ = 0.0f;
    spinBase_ = 0.0f;
    spinSign_ = 1.0f;
    turnAngle_ = 0.0f;
    turnTarget_ = 0.0f;
    spin_ = 0.0f;
    seedState_ = 0x2545f491u;
    // Seed the displayed rows so the first crossfade has something real to
    // fade from rather than a row of zeros.
    sampleAll();
    frozenAzimuth_ = displayedAzimuth_;
    frozenElevation_ = displayedElevation_;
}

// A count change fades from WHAT IS ON SCREEN to the new curve: the displayed
// row is frozen as the outgoing half, and only the incoming half is evaluated
// live. Freezing what is on screen, rather than the previous target, is what
// makes a retarget seamless at any moment - a drop that lands mid-crossfade
// starts a new crossfade from the mid-crossfade shape, not from a curve that
// was never drawn.
void SuperShape::retarget(Curve& curve, int m, std::array<float, kSamples>& frozen,
                          const std::array<float, kSamples>& displayed) {
    if (m == curve.m) return;
    frozen = displayed;
    curve.m = m;
    curve.blend = 0.0f;
}

void SuperShape::step(const Drive& d, float dt) {
    azimuthLockout_ = std::max(azimuthLockout_ - dt, 0.0f);

    // ---- the events: what re-picks a lobe count -----------------------------
    if (d.dropFired) {
        // A drop wants a bold, simple silhouette to slam into: the lowest
        // counts, then the snares build it back up from there.
        retarget(azimuth_, kAzimuthMinM + std::min(static_cast<int>(nextRandom() * 3.0f), 2), frozenAzimuth_, displayedAzimuth_);
        retarget(elevation_, kElevationMinM + std::min(static_cast<int>(nextRandom() * 3.0f), 2), frozenElevation_,
                 displayedElevation_);
        azimuthLockout_ = kCountRefractorySeconds;
    } else if (d.sectionFired || d.arrivalFired) {
        retarget(azimuth_, randomCount(kAzimuthMinM, kAzimuthMaxM, azimuth_.m), frozenAzimuth_, displayedAzimuth_);
        retarget(elevation_, randomCount(kElevationMinM, kElevationMaxM, elevation_.m), frozenElevation_, displayedElevation_);
        azimuthLockout_ = kCountRefractorySeconds;
        // A new section turns the other way. The RATE changes sign, never the
        // angle, so the shape slows to a stop and comes back rather than jumps.
        if (d.sectionFired) spinSign_ = -spinSign_;
    } else if (d.snareFired) {
        if (azimuthLockout_ <= 0.0f) {
            int stepBy;
            if (d.build > kBuildClimbLevel) {
                stepBy = 1;
            } else {
                static constexpr int kSteps[4] = {-2, -1, 1, 2};
                stepBy = kSteps[std::min(static_cast<int>(nextRandom() * 4.0f), 3)];
            }
            int next = azimuth_.m + stepBy;
            // Reflect off the ends of the range rather than sticking to them.
            if (next > kAzimuthMaxM) next = kAzimuthMaxM - (next - kAzimuthMaxM);
            if (next < kAzimuthMinM) next = kAzimuthMinM + (kAzimuthMinM - next);
            next = std::clamp(next, kAzimuthMinM, kAzimuthMaxM);
            if (next != azimuth_.m) {
                retarget(azimuth_, next, frozenAzimuth_, displayedAzimuth_);
                azimuthLockout_ = kCountRefractorySeconds;
            }
        }
        // Every snare turns the shape a little, whichever way, and the turn
        // glides in; the count refractory does not apply to it.
        const float sense = nextRandom() < 0.5f ? -1.0f : 1.0f;
        turnTarget_ += sense * pick(kTurnMinRad, kTurnMaxRad, nextRandom());
    }

    // ---- the exponents: what the envelopes do to the lobes ------------------
    const float bass = std::clamp(d.bass, 0.0f, 1.0f);
    const float mid = std::clamp(d.mid, 0.0f, 1.0f);
    const float treble = std::clamp(d.treble, 0.0f, 1.0f);
    const float swell = std::clamp(d.swell, 0.0f, 1.0f);
    const float bright = std::clamp(d.brightness, 0.0f, 1.0f);
    {
        Curve& c = azimuth_;
        const float n1 = mixf(kN1Percussive, kN1Tonal, d.harmonic) * (1.0f - kKickPinch * std::clamp(d.kick, 0.0f, 1.0f));
        // Spectral tilt, on an exponential scale: bass over treble fattens,
        // treble over bass thins.
        const float body = kBodyExp * std::exp(kBodyTiltGain * std::clamp(bass - treble, -1.0f, 1.0f));
        const float n2 = body * (1.0f + kBrightSplit - 2.0f * kBrightSplit * bright);
        const float n3 = body * (1.0f - kBrightSplit + 2.0f * kBrightSplit * bright) * (1.0f + kHatGain * std::clamp(d.hat, 0.0f, 1.0f));
        const float pan = std::clamp(d.pan, -1.0f, 1.0f);
        c.n1 = slewTo(c.n1, std::clamp(n1, kN1Floor, kN1Ceiling), dt, kSharpnessHz, kSharpnessHz);
        c.n2 = slewTo(c.n2, std::clamp(n2, kExpFloor, kExpCeiling), dt, kLobeHz, kLobeHz);
        c.n3 = slewTo(c.n3, std::clamp(n3, kExpFloor, kExpCeiling), dt, kLobeHz, kLobeHz);
        c.a = slewTo(c.a, std::max(1.0f + kPanWeight * pan, kWeightFloor), dt, kWeightHz, kWeightHz);
        c.b = slewTo(c.b, std::max(1.0f - kPanWeight * pan, kWeightFloor), dt, kWeightHz, kWeightHz);
        c.blend = slewTo(c.blend, 1.0f, dt, kCountBlendHz, kCountBlendHz);
    }
    {
        Curve& c = elevation_;
        const float n1 = mixf(kElevN1Quiet, kElevN1Tonal, d.harmonic) * (1.0f - kSnarePinch * std::clamp(d.snare, 0.0f, 1.0f));
        // The profile follows the passage: loud fattens the body, quiet thins it.
        const float body = kBodyExp * std::exp(kElevBodyTiltGain * std::clamp(2.0f * (swell - 0.5f), -1.0f, 1.0f));
        const float novelty = std::clamp(d.novelty, 0.0f, 1.0f);
        const float n2 = body * (1.0f + kNoveltySplit - 2.0f * kNoveltySplit * novelty);
        const float n3 = body * (1.0f - kNoveltySplit + 2.0f * kNoveltySplit * novelty) * (1.0f + kMidGain * mid);
        const float width = std::clamp(d.width, 0.0f, 1.0f) - 0.5f;
        c.n1 = slewTo(c.n1, std::clamp(n1, kN1Floor, kN1Ceiling), dt, kSharpnessHz, kSharpnessHz);
        c.n2 = slewTo(c.n2, std::clamp(n2, kExpFloor, kExpCeiling), dt, kLobeHz, kLobeHz);
        c.n3 = slewTo(c.n3, std::clamp(n3, kExpFloor, kExpCeiling), dt, kLobeHz, kLobeHz);
        c.a = slewTo(c.a, std::max(1.0f + kWidthWeight * width, kWeightFloor), dt, kWeightHz, kWeightHz);
        c.b = slewTo(c.b, std::max(1.0f - kWidthWeight * width, kWeightFloor), dt, kWeightHz, kWeightHz);
        c.blend = slewTo(c.blend, 1.0f, dt, kCountBlendHz, kCountBlendHz);
    }

    // ---- spin ---------------------------------------------------------------
    // Monotonic within a section: loudness sets the rate, never the sign, so
    // the silhouette can never stutter back and forth on a busy passage.
    spinBase_ = std::fmod(spinBase_ + spinSign_ * (kSpinBaseRadPerSec + kSpinEnergyRadPerSec * std::clamp(d.energy, 0.0f, 1.0f)) * dt,
                          kTwoPi);
    turnAngle_ += (turnTarget_ - turnAngle_) * (1.0f - std::exp(-dt * kTurnGlideHz));
    // Keep both turn values small; they only ever matter modulo a turn.
    if (std::fabs(turnTarget_) > kTwoPi) {
        const float wrap = std::copysign(kTwoPi, turnTarget_);
        turnTarget_ -= wrap;
        turnAngle_ -= wrap;
    }
    spin_ = std::fmod(spinBase_ + turnAngle_ + kTwoPi * 4.0f, kTwoPi);

    sampleAll();
}

// The row a curve draws for one integer count. Odd counts are drawn with
// symmetric exponents and unit weights, the only way an odd m closes over
// 2*pi (see the periodicity note in the header).
void SuperShape::sampleLive(const Curve& curve, float angleStart, float angleStep, std::array<float, kSamples>& out) {
    const bool odd = (curve.m & 1) != 0;
    const float n2 = odd ? 0.5f * (curve.n2 + curve.n3) : curve.n2;
    const float n3 = odd ? n2 : curve.n3;
    const float a = odd ? 1.0f : curve.a;
    const float b = odd ? 1.0f : curve.b;
    const float m = static_cast<float>(curve.m);
    for (int i = 0; i < kSamples; ++i) {
        out[static_cast<size_t>(i)] = radius(angleStart + angleStep * static_cast<float>(i), m, curve.n1, n2, n3, a, b);
    }
}

// Blends the frozen and live rows into the displayed row, then writes the
// normalised, clamped copy the GPU reads and returns its statistics.
SuperShape::RowStats SuperShape::finishRow(const Curve& curve, const std::array<float, kSamples>& frozen,
                                           const std::array<float, kSamples>& live, std::array<float, kSamples>& displayed,
                                           int row, float angleStep, bool periodic) {
    double sum = 0.0;
    for (int i = 0; i < kSamples; ++i) {
        const auto k = static_cast<size_t>(i);
        displayed[k] = mixf(frozen[k], live[k], curve.blend);
        sum += displayed[k];
    }
    RowStats stats;
    stats.mean = static_cast<float>(sum / kSamples);
    const float scale = 1.0f / std::max(stats.mean, 1e-6f);
    float* out = samples_.data() + static_cast<size_t>(row) * kSamples;
    for (int i = 0; i < kSamples; ++i) {
        out[i] = std::clamp(displayed[static_cast<size_t>(i)] * scale, kRadiusMin, kRadiusMax);
        stats.minimum = std::min(stats.minimum, out[i]);
        stats.maximum = std::max(stats.maximum, out[i]);
    }
    const int pairs = periodic ? kSamples : kSamples - 1;
    for (int i = 0; i < pairs; ++i) {
        const int j = (i + 1) % kSamples;
        stats.slope = std::max(stats.slope, std::fabs(out[j] - out[i]) / angleStep);
    }
    return stats;
}

// ---- the bounds a march divides by -----------------------------------------
//
// For a radial function rho(dir), the field f(p) = |p| - rho(p/|p|) has
// gradient p/|p| - grad_S(rho)/|p|, the two terms orthogonal, so
// |grad f| = sqrt(1 + |grad_S rho|^2 / |p|^2). Every point outside the surface
// has |p| > rho >= rho_min, and the segment from an outside point to its
// nearest surface point stays outside (a crossing would be a nearer point),
// so on that segment |grad f| <= L = sqrt(1 + max|grad_S rho|^2 / rho_min^2)
// and the true distance is at least f(p) / L. That is what the shaders
// divide by.
//
// In 2D grad_S is the slope of the row. For the spherical product
// r1(theta) * r2(phi), |grad_S rho|^2 = (r1' r2 / cos phi)^2 + (r1 r2')^2,
// which is unbounded at the poles; the bound below stops honouring it past
// kPoleCos, and both bounds are clamped at kLipschitzMax because a cusp has
// infinite slope. Past either limit the march may step inside the surface -
// see the note on kLipschitzMax in the header.
void SuperShape::lipschitz(const RowStats& az, const RowStats& el) {
    const float minA = std::max(az.minimum, kRadiusMin);
    const float minB = std::max(el.minimum, kRadiusMin);
    lipschitz2_ = std::clamp(std::sqrt(1.0f + (az.slope * az.slope) / (minA * minA)), 1.0f, kLipschitzMax);
    const float azimuthal = az.slope * el.maximum / kPoleCos;
    const float polar = az.maximum * el.slope;
    const float floor = minA * minB;
    lipschitz3_ = std::clamp(std::sqrt(1.0f + (azimuthal * azimuthal + polar * polar) / (floor * floor)), 1.0f, kLipschitzMax);
}

void SuperShape::sampleAll() {
    // Azimuth: theta over [-pi, pi), periodic, so sample i sits at -pi + i * step.
    // Elevation: phi over [-pi/2, pi/2] inclusive, so the last sample IS the pole.
    const float azimuthStep = kTwoPi / static_cast<float>(kSamples);
    const float elevationStep = kPi / static_cast<float>(kSamples - 1);
    sampleLive(azimuth_, -kPi, azimuthStep, liveScratch_);
    const RowStats az = finishRow(azimuth_, frozenAzimuth_, liveScratch_, displayedAzimuth_, kAzimuthRow, azimuthStep, true);
    sampleLive(elevation_, -0.5f * kPi, elevationStep, liveScratch_);
    const RowStats el = finishRow(elevation_, frozenElevation_, liveScratch_, displayedElevation_, kElevationRow, elevationStep, false);
    meanAzimuth_ = az.mean;
    meanElevation_ = el.mean;
    lipschitz(az, el);
}

void SuperShape::upload(UniformCache& u) const {
    glUniform4f(u.loc("uShapeA"), static_cast<float>(azimuth_.m), azimuth_.n1, azimuth_.n2, azimuth_.n3);
    glUniform4f(u.loc("uShapeAWeight"), azimuth_.a, azimuth_.b, azimuth_.blend, meanAzimuth_);
    glUniform4f(u.loc("uShapeB"), static_cast<float>(elevation_.m), elevation_.n1, elevation_.n2, elevation_.n3);
    glUniform4f(u.loc("uShapeBWeight"), elevation_.a, elevation_.b, elevation_.blend, meanElevation_);
    glUniform2f(u.loc("uShapeLip"), lipschitz2_, lipschitz3_);
    glUniform1f(u.loc("uShapeSpin"), spin_);
}

}  // namespace geode::viz
