#pragma once
#include <array>
#include <limits>

namespace geode::viz {

class UniformCache;

// The Gielis superformula, driven by the music, as the shared shape every
// fragment style can morph through:
//
//     r(theta) = ( |cos(m*theta/4) / a|^n2 + |sin(m*theta/4) / b|^n3 )^(-1/n1)
//
// One family of six numbers draws a circle (n1 = n2 = n3 = 2, any m), a
// rounded m-gon (n2, n3 above 2), an m-pointed star (n1 well below 1), and
// everything in between, and it draws them CONTINUOUSLY: every parameter is a
// real number, so the shape can travel from one to the next rather than cut.
// That is what makes it the right thing to hang a music reaction on. A level
// can only make a picture bigger; this can make it a different thing.
//
// Two curves are kept. The AZIMUTH curve is the silhouette - what shapeWarp()
// in lib_scene_uniforms folds a style's plane into, what a 2D style masks
// with. The ELEVATION curve is the second half of the 3D supershape, the
// spherical product r1(theta) * r2(phi) that sdSupershape() in
// lib_superformula marches.
//
// ---- what the music moves ---------------------------------------------------
//
//   m   (lobe count)    an INTEGER plateau. Snares walk it; a drop resets it
//                       to a bold low count; a section boundary re-rolls it.
//                       Changes are crossfades between two closed curves, never
//                       a glide of m itself - see the periodicity note below.
//   n1  (sharpness)     tonal passages sit round, percussive ones sit spiky
//                       (harmonicity sets the baseline); a kick pinches it
//                       toward a star and it relaxes back.
//   n2, n3 (lobe form)  bass fattens the cosine lobes, treble and hats the
//                       sine lobes; spectral brightness tilts the two.
//   a, b (weights)      stereo pan (azimuth) and stereo width (elevation).
//   spin                integrated: energy sets the rate, a section boundary
//                       the sense, a snare a bounded turn.
//
// ---- periodicity, and why m never glides -------------------------------------
//
// |cos(m*theta/4)| has period 4*pi/m in theta, so the curve closes over 2*pi
// only when m is EVEN; an odd m with n2 != n3 or a != b is 4*pi-periodic and
// has a seam at theta = pi. A non-integer m does not close at all. So m is
// only ever an integer, odd counts are drawn with symmetric exponents (which
// does close), and a change of count is a crossfade: the row on screen is
// frozen as the outgoing half and the incoming count is evaluated live. The
// crossfade of two closed curves is closed, and freezing what is on screen
// makes a retarget seamless whenever it lands.
//
// ---- how it reaches the GPU -----------------------------------------------
//
// Both curves are SAMPLED here, every frame, into two rows of kSamples floats
// that ShaderScene uploads as an R32F texture, exactly as it uploads the
// audio bands. A style reads them with ashape() / ashapeElev(): two texel
// fetches and a mix, cheap enough to sit inside a 128-step march where three
// pow() calls per evaluation would not be. Each row is normalised to a mean
// radius of 1, so the shape changes without the picture zooming - that is the
// whole point - and clamped to kRadiusMin..kRadiusMax so a very spiky curve
// cannot fold a style's plane to a point.
//
// The six raw parameters of each curve are uploaded too, for a style that
// wants the analytic form (see superformula() in lib_superformula).
class SuperShape {
public:
    static constexpr int kSamples = 512;
    static constexpr int kRows = 2;
    static constexpr float kRadiusMin = 0.15f;
    static constexpr float kRadiusMax = 4.0f;

    struct Curve {
        // The count the row is heading to; `blend` is how far the crossfade
        // from the frozen outgoing row has got, 0..1.
        int m = 6;
        float blend = 1.0f;
        float n1 = 2.0f;
        float n2 = 2.0f;
        float n3 = 2.0f;
        float a = 1.0f;
        float b = 1.0f;
    };

    // Everything the choreography reads, gathered by ShaderScene from its own
    // slewed envelopes and from MusicSignals. All already smoothed; the fired
    // flags are this frame's events.
    struct Drive {
        float bass = 0.0f;
        float mid = 0.0f;
        float treble = 0.0f;
        float energy = 0.0f;
        float swell = 0.0f;
        float kick = 0.0f;
        float snare = 0.0f;
        float hat = 0.0f;
        float build = 0.0f;
        float novelty = 0.0f;
        float harmonic = 0.5f;
        float brightness = 0.0f;
        float pan = 0.0f;
        float width = 0.0f;
        bool snareFired = false;
        bool dropFired = false;
        bool arrivalFired = false;
        bool sectionFired = false;
    };

    SuperShape();

    void reset();
    void step(const Drive& d, float dt);
    void upload(UniformCache& uniforms) const;

    const Curve& azimuth() const { return azimuth_; }
    const Curve& elevation() const { return elevation_; }
    float spin() const { return spin_; }
    float lipschitz2() const { return lipschitz2_; }
    float lipschitz3() const { return lipschitz3_; }
    // Row 0: the azimuth curve over theta in [-pi, pi), periodic. Row 1: the
    // elevation curve over phi in [-pi/2, pi/2], clamped. Mean-normalised.
    const std::array<float, kSamples * kRows>& samples() const { return samples_; }

    // The raw formula. `angle` in radians; a and b must be positive, n1 non-zero.
    static float radius(float angle, float m, float n1, float n2, float n3, float a, float b);

private:
    struct RowStats {
        float mean = 1.0f;
        float minimum = std::numeric_limits<float>::max();
        float maximum = 0.0f;
        float slope = 0.0f;
    };

    // Lobe counts. The azimuth curve is the silhouette and can afford to get
    // busy; the elevation curve is only ever seen as the profile of a solid,
    // where more than eight lobes reads as ribbing rather than as shape.
    static constexpr int kAzimuthMinM = 3;
    static constexpr int kAzimuthMaxM = 12;
    static constexpr int kElevationMinM = 2;
    static constexpr int kElevationMaxM = 8;
    // How long after a snare-driven count change the next snare may change it
    // again: at 2.5 Hz the crossfade is 97% done by then, so the count on
    // screen is readable as a count rather than as a perpetual blend. Drops,
    // sections and arrivals are rarer and bypass it; the frozen-row crossfade
    // makes that seamless too.
    static constexpr float kCountRefractorySeconds = 1.4f;
    static constexpr float kCountBlendHz = 2.5f;
    // The snare walk climbs during a buildup rather than wandering, so the
    // silhouette gets steadily busier toward the drop and simple again after.
    static constexpr float kBuildClimbLevel = 0.5f;

    // Exponent targets.
    //
    // How much the radius varies is set by how far n2 and n3 sit from 2 IN THE
    // SAME DIRECTION (both above 2 bulges the lobes, both below pinches them)
    // raised to the power 1/n1. So the two lobe exponents share one BODY
    // exponent that the spectral tilt sets on an exponential scale - bass-heavy
    // music fattens the lobes, bright music thins them to spikes, and both ends
    // are reachable - while brightness and the hats only split the pair. The
    // sharpness n1 comes from harmonicity, and a kick pinches it. Silence
    // (everything at 0) sits at a soft, slightly lobed shape rather than at a
    // circle the warp would do nothing to.
    static constexpr float kN1Percussive = 0.35f;
    static constexpr float kN1Tonal = 2.4f;
    static constexpr float kKickPinch = 0.6f;
    static constexpr float kN1Floor = 0.25f;
    static constexpr float kN1Ceiling = 6.0f;
    static constexpr float kBodyExp = 2.0f;
    static constexpr float kBodyTiltGain = 1.2f;
    static constexpr float kBrightSplit = 0.2f;
    static constexpr float kHatGain = 0.6f;
    static constexpr float kExpFloor = 0.3f;
    static constexpr float kExpCeiling = 8.0f;
    static constexpr float kPanWeight = 0.35f;
    static constexpr float kWidthWeight = 0.3f;
    static constexpr float kWeightFloor = 0.5f;
    static constexpr float kElevN1Quiet = 0.5f;
    static constexpr float kElevN1Tonal = 2.4f;
    static constexpr float kSnarePinch = 0.35f;
    static constexpr float kElevBodyTiltGain = 1.0f;
    static constexpr float kNoveltySplit = 0.15f;
    static constexpr float kMidGain = 0.4f;

    // Slew rates. Sharpness moves fastest because the kick pinch has to be
    // seen; the weights slowest because pan is a slow thing.
    static constexpr float kSharpnessHz = 6.0f;
    static constexpr float kLobeHz = 4.0f;
    static constexpr float kWeightHz = 2.0f;

    // Spin.
    static constexpr float kSpinBaseRadPerSec = 0.06f;
    static constexpr float kSpinEnergyRadPerSec = 0.35f;
    static constexpr float kTurnMinRad = 0.25f;
    static constexpr float kTurnMaxRad = 0.6f;
    static constexpr float kTurnGlideHz = 1.5f;

    // The raymarch bound is clamped: a cusp (n2 or n3 below 1) has infinite
    // slope, and near the poles of the spherical product the azimuth
    // variation is squeezed onto a vanishing circle. Past the clamp the march
    // may step inside the surface, which a signed hit test tolerates (the hit
    // lands slightly inset) and thin spikes may be skipped. See lipschitz()
    // for the derivation.
    static constexpr float kLipschitzMax = 4.0f;
    // The 3D bound is honoured to this cosine of latitude, about 70 degrees.
    static constexpr float kPoleCos = 0.35f;

    static float pick(float lo, float hi, float t);
    float nextRandom();
    int randomCount(int lo, int hi, int avoid);
    static void retarget(Curve& curve, int m, std::array<float, kSamples>& frozen, const std::array<float, kSamples>& displayed);
    static void sampleLive(const Curve& curve, float angleStart, float angleStep, std::array<float, kSamples>& out);
    RowStats finishRow(const Curve& curve, const std::array<float, kSamples>& frozen, const std::array<float, kSamples>& live,
                       std::array<float, kSamples>& displayed, int row, float angleStep, bool periodic);
    void lipschitz(const RowStats& az, const RowStats& el);
    void sampleAll();

    Curve azimuth_;
    Curve elevation_;
    float azimuthLockout_ = 0.0f;
    float spinBase_ = 0.0f;
    float spinSign_ = 1.0f;
    float turnAngle_ = 0.0f;
    float turnTarget_ = 0.0f;
    float spin_ = 0.0f;
    float lipschitz2_ = 1.0f;
    float lipschitz3_ = 1.0f;
    float meanAzimuth_ = 1.0f;
    float meanElevation_ = 1.0f;
    // Raw (un-normalised) rows: what was drawn last frame, and the outgoing
    // half of a crossfade in progress.
    std::array<float, kSamples> displayedAzimuth_{};
    std::array<float, kSamples> displayedElevation_{};
    std::array<float, kSamples> frozenAzimuth_{};
    std::array<float, kSamples> frozenElevation_{};
    std::array<float, kSamples> liveScratch_{};
    // What the GPU reads.
    std::array<float, kSamples * kRows> samples_{};
    // Deterministic: the same audio gives the same shapes, which is what makes
    // an exported frame reproducible.
    unsigned int seedState_ = 0x2545f491u;
};

}  // namespace geode::viz
