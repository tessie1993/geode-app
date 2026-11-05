#pragma once
#include <vector>

#include "sound/SoundFrame.h"
#include "sound/offline/OfflineFeatures.hpp"

namespace geode::sound::offline {

// The harmony fields OfflineFeatures leaves (#464, ADR 0011): the chord's colour, its tension, harmonic
// change and the scale, read from that analysis's CQT chroma over the whole track.
//
//   chordHue, tension   live Harmony's formulas (circle-of-fifths centre, interval-class roughness) on the
//                       centred 0.5 s chroma; tension's 0.3 s follower becomes a centred ±0.15 s mean
//   harmonicChange      the chroma of the second after each frame against the second before it (live
//                       compares the last half second with the two before, so it fires about half a second
//                       late); peaks above live's 0.15, at least 0.5 s apart, fire on the change itself
//   scale …             live's scoring (each of ScaleTable.inc's scales on each root: correlation with the
//                       scale's notes plus a pull toward a strong root and fifth) on the centred 4 s chroma,
//                       then one Viterbi path over the 46 × 12 states with a switch cost equal to live's
//                       rule (a better fit by 0.03 for two seconds), so the scale changes where the music
//                       does rather than two seconds after; brightness, tension and fit glide over ±0.5 s
//                       as live's do over a second
//
// Frames with no chroma at all (no music within the slow chroma's reach) read as live does before any
// music: scale -1, brightness 0.5, fit 0.
struct HarmonyTrack {
    double frameRate = 0.0;
    std::vector<float> chordHue, tension, scaleFit, scaleBrightness, scaleTension;
    std::vector<int> scale;      // ScaleTable.inc index, -1 for no music
    std::vector<int> scaleRoot;  // 0..11
    std::vector<double> changes; // harmonic change times, seconds, ascending
    std::vector<float> changeStrength;  // each one's trigger strength, 0..1
};

HarmonyTrack analyseHarmony(const Features& features);

// The harmony fields SoundFrame.h gives: chordHue, tension, harmonicChange, scale, scaleRoot,
// scaleBrightness, scaleTension, scaleFit. Leaves every other field as it is.
void sampleHarmony(const HarmonyTrack& harmony, double seconds, double dt, GeodeSoundFrame& out);

// ScaleTable.inc as the test reads it: how many scales, one's brightness (-1..1), and its pitch classes
// above the root (bit 0 = the root).
int harmonyScaleCount();
float harmonyScaleBrightness(int scale);
unsigned harmonyScaleMask(int scale);

}  // namespace geode::sound::offline
