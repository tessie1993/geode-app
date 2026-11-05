#pragma once
#include <cstddef>
#include <vector>

#include "sound/SoundFrame.h"
#include "sound/offline/OfflineBeats.hpp"

namespace geode::sound::offline {

// The offline analyser's form slice (#465, ADR 0011): sections, build-ups and drops for the whole track at
// once, so an export's cuts and Scene changes land on the real boundary. Live Form (core/sound/Form) can
// only hear the past: its section change fires a bar after the boundary and its drop waits for the bass to
// come back. This one sees both sides of every boundary, so it puts the event on the boundary itself.
// It is a separate implementation (ADR 0011), not a mode of live Form; what it shares is the output, the
// Form fields of GeodeSoundFrame with the same ids and ranges (sample()).
//
// The method is the recurrence segmentation librosa's tutorials and the lab use (tools/analysis_lab/lab.py),
// on the offline beat grid (OfflineBeats), with Foote's checkerboard picking the boundaries where the lab
// tells librosa's agglomerative clustering how many to find (it is an oracle-k reference; a render is told
// nothing):
//
//   STFT ─► per frame: chroma (12, loudest = 1), MFCC 0..12 of the mel spectrogram, loudness, low-band
//        (< 150 Hz) power, centroid, onset strength
//   beat grid (extended at its tempo to the track's ends) ─► per beat: median chroma, mean of the rest
//   chroma + MFCC, z-scored per dimension ─► pairwise distances ─► box checkerboard, 4 bars each side
//        ─► novelty at every beat ─► peaks 4+ bars apart over a floor ─► snapped to the downbeat
//   per beat: low band, loudness, centroid, onsets ─► at every beat: the low band jumps 6 dB+ after 2 bars
//        of rise (louder, brighter or busier) with the bass pulled out ─► drop, which is also a boundary
//   sections ─► nearest earlier section by mean features ─► sectionId (a returning chorus keeps its number)
//   sections ─► loudness and bass share against the track's ─► state (intro, verse, build, drop,
//        breakdown, outro), live Form's rules made symmetric
//
// A box kernel, not Foote's Gaussian taper, because a box exactly as long as a chord loop sees the same loop
// on both sides wherever it sits, so a four-chord groove reads no novelty at all; the taper would see each
// chord change.

struct FormSection {
    double start = 0.0;  // seconds; the first section starts at 0
    int id = 0;          // sectionId, 0..7
    float strength = 0.0f;  // the sectionChange strength at its start (0 for the first), kEventFloor..1
};

struct FormState {
    double start = 0.0;  // seconds
    int state = GEODE_SECTION_INTRO;
};

struct FormDrop {
    double at = 0.0;          // seconds, the drop's beat
    double buildStart = 0.0;  // where its build began; buildUp ramps from here to the drop
    float strength = 0.0f;    // kEventFloor..1
};

struct FormTimeline {
    double duration = 0.0;
    std::vector<FormSection> sections;  // ascending, covering [0, duration); empty for silence
    std::vector<FormState> states;      // ascending; a state can change inside a section (a build)
    std::vector<FormDrop> drops;        // ascending
    std::vector<double> boundaries;     // the sections' starts after the first: internal boundaries only
    std::vector<double> noveltyTimes;   // seconds, one per beat of the extended grid
    std::vector<float> novelty;         // 0..1 at those times
};

// The floor both events fire at or above, the same as live Form::kEventFloor (Form.hpp), so a consumer that
// reads either analyser acts on > 0 alone.
constexpr float kFormEventFloor = 0.4f;

// The whole analysis over a mono track and its beat grid (OfflineBeats::analyse on the same samples). A grid
// with too few beats is replaced by a 120 BPM pulse from 0, which gives sections but no downbeat snapping.
// Deterministic: the same samples and grid give the same timeline, bit for bit.
FormTimeline analyseForm(const float* mono, size_t samples, int sampleRateHz, const BeatGrid& grid);

// The timeline as the live engine's Form fields for the frame that ends at `seconds` and lasted `dt`:
// novelty, sectionChange, sectionId, sectionState, sectionBars, buildUp, drop, with SoundFrame.h's ids and
// ranges. sectionChange and drop fire on the one frame whose span (seconds - dt, seconds] holds the boundary
// or the drop, so every frame rate sees each once at the same song time. Unlike live, sectionChange fires
// on the boundary, not a bar after it (that is what an export is for); a drop is also a sectionChange with
// the drop's strength, as live's is.
//
// It also restarts the phrase count at each section, as live does ("a section change starts a new one"):
// call it after OfflineBeats' sample() for the same frame, and it rewrites phrase, phraseIndex and
// phrasePhase from the grid's downbeats. Leaves every other field as it is.
void sample(const FormTimeline& form, const BeatGrid& grid, double seconds, double dt, GeodeSoundFrame& out);

}  // namespace geode::sound::offline
