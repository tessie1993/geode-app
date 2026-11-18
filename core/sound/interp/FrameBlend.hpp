#pragma once

#include <cstddef>
#include <cstdint>

#include "sound/Emotion.hpp"
#include "sound/SoundEngine.hpp"
#include "sound/SoundFrame.h"
#include "sound/hop/AnalysisThread.hpp"
#include "sound/interp/Hits.hpp"

namespace geode::sound::interp {

// How a GeodeSoundFrame field reads at a sample between the two published hops that bracket it.
enum class FieldKind : uint8_t {
    Linear,   // levels, envelopes, descriptors, spectra, chroma: a straight line between the two hops
    Angle,    // hues and pitch classes on a 0..1 circle: the short way round
    Clock,    // times that only run forward (they wrap): a straight line, and the earlier hop's across the wrap
    Phase,    // beat, bar and phrase phase: computed at the sample from the earlier hop's clock (Rhythm::phases)
    Hold,     // counts, ids, states, a tempo (0 = none heard), a pitch (0 = unvoiced): the earlier hop's
    Trigger,  // one-hop pulses: never blended; each fires once, on the first read at or past its hop (FrameReader)
    Told,     // the player's fade gain: the reader sets it as the player says now
    Drawn,    // the waveform: drawn per frame from the ring at the sample (interp/Waveform), not carried by a hop
};

struct FieldSpan {
    size_t offset;  // bytes into GeodeSoundFrame
    int count;      // floats
    FieldKind kind;
};

#define GEODE_FIELD(name, kind) \
    FieldSpan { offsetof(GeodeSoundFrame, name), static_cast<int>(sizeof(GeodeSoundFrame::name) / sizeof(float)), FieldKind::kind }

// Every field of GeodeSoundFrame in declaration order with its kind: the one rule table every read follows.
// A field added to SoundFrame.h and not here fails the asserts below.
inline constexpr FieldSpan kFieldKinds[] = {
    GEODE_FIELD(bass, Linear), GEODE_FIELD(mid, Linear), GEODE_FIELD(treble, Linear), GEODE_FIELD(vol, Linear),
    GEODE_FIELD(bassAtt, Linear), GEODE_FIELD(midAtt, Linear), GEODE_FIELD(trebleAtt, Linear), GEODE_FIELD(volAtt, Linear),
    GEODE_FIELD(sub, Linear), GEODE_FIELD(subAtt, Linear), GEODE_FIELD(midHigh, Linear), GEODE_FIELD(midHighAtt, Linear),
    GEODE_FIELD(low, Linear), GEODE_FIELD(middle, Linear), GEODE_FIELD(high, Linear), GEODE_FIELD(level, Linear),
    GEODE_FIELD(bassPresence, Linear), GEODE_FIELD(midPresence, Linear), GEODE_FIELD(treblePresence, Linear),
    GEODE_FIELD(intensity, Linear),
    GEODE_FIELD(bassTime, Clock), GEODE_FIELD(midTime, Clock), GEODE_FIELD(trebleTime, Clock), GEODE_FIELD(curvedTime, Clock),
    GEODE_FIELD(hitTime, Clock), GEODE_FIELD(noteTime, Clock),
    GEODE_FIELD(presence, Linear),
    GEODE_FIELD(rms, Linear), GEODE_FIELD(loudness, Linear), GEODE_FIELD(centroid, Linear), GEODE_FIELD(rolloff, Linear),
    GEODE_FIELD(flatness, Linear), GEODE_FIELD(densitySlow, Linear), GEODE_FIELD(densityFast, Linear), GEODE_FIELD(flux, Linear),
    GEODE_FIELD(energyTrend, Linear), GEODE_FIELD(peakFreq, Linear), GEODE_FIELD(peakMag, Linear),
    GEODE_FIELD(stereoWidth, Linear), GEODE_FIELD(stereoPan, Linear),
    GEODE_FIELD(kickEnv, Linear), GEODE_FIELD(snareEnv, Linear), GEODE_FIELD(hatEnv, Linear), GEODE_FIELD(onsetEnv, Linear),
    GEODE_FIELD(kickToggle, Linear), GEODE_FIELD(snareToggle, Linear), GEODE_FIELD(kickRandom, Linear),
    GEODE_FIELD(noteRandom, Linear),
    GEODE_FIELD(kick, Trigger), GEODE_FIELD(snare, Trigger), GEODE_FIELD(hat, Trigger), GEODE_FIELD(kick2, Trigger),
    GEODE_FIELD(kick4, Trigger), GEODE_FIELD(kick8, Trigger), GEODE_FIELD(kick16, Trigger), GEODE_FIELD(snare2, Trigger),
    GEODE_FIELD(snare4, Trigger), GEODE_FIELD(hardCut, Trigger), GEODE_FIELD(onset, Trigger), GEODE_FIELD(silence, Trigger),
    GEODE_FIELD(returned, Trigger), GEODE_FIELD(surge, Trigger), GEODE_FIELD(noteChange, Trigger), GEODE_FIELD(legato, Trigger),
    GEODE_FIELD(reattack, Trigger),
    GEODE_FIELD(pitch, Hold), GEODE_FIELD(pitchClass, Angle), GEODE_FIELD(pitchHeight, Linear),
    GEODE_FIELD(pitchConfidence, Linear), GEODE_FIELD(interval, Hold), GEODE_FIELD(glide, Linear),
    GEODE_FIELD(vibrato, Linear), GEODE_FIELD(evenness, Linear), GEODE_FIELD(runDensity, Linear),
    GEODE_FIELD(bpm, Hold), GEODE_FIELD(beatPhase, Phase), GEODE_FIELD(beatConf, Linear), GEODE_FIELD(beat, Trigger),
    GEODE_FIELD(beatCount, Hold), GEODE_FIELD(downbeat, Trigger), GEODE_FIELD(barPhase, Phase), GEODE_FIELD(barCount, Hold),
    GEODE_FIELD(phrase, Trigger), GEODE_FIELD(phraseIndex, Hold), GEODE_FIELD(phrasePhase, Phase),
    GEODE_FIELD(fluxLow, Linear), GEODE_FIELD(fluxMid, Linear), GEODE_FIELD(fluxHigh, Linear), GEODE_FIELD(hfc, Linear),
    GEODE_FIELD(keyHue, Angle), GEODE_FIELD(keyRoot, Hold), GEODE_FIELD(keyMajor, Hold), GEODE_FIELD(keyClarity, Linear),
    GEODE_FIELD(chordHue, Angle), GEODE_FIELD(tension, Linear), GEODE_FIELD(harmonicChange, Trigger),
    GEODE_FIELD(scale, Hold), GEODE_FIELD(scaleRoot, Hold), GEODE_FIELD(scaleBrightness, Linear),
    GEODE_FIELD(scaleTension, Linear), GEODE_FIELD(scaleFit, Linear),
    GEODE_FIELD(novelty, Linear), GEODE_FIELD(sectionChange, Trigger), GEODE_FIELD(sectionId, Hold),
    GEODE_FIELD(sectionState, Hold), GEODE_FIELD(sectionBars, Hold), GEODE_FIELD(buildUp, Linear), GEODE_FIELD(drop, Trigger),
    GEODE_FIELD(fadeGain, Told),
    GEODE_FIELD(loudnessM, Linear), GEODE_FIELD(loudnessS, Linear), GEODE_FIELD(busy, Linear), GEODE_FIELD(drive, Linear),
    GEODE_FIELD(transient, Trigger), GEODE_FIELD(transientEnv, Linear), GEODE_FIELD(transientSharpness, Hold),
    GEODE_FIELD(eq, Linear), GEODE_FIELD(spectrum, Linear), GEODE_FIELD(waveform, Drawn), GEODE_FIELD(chroma, Linear),
    GEODE_FIELD(chromaFast, Linear), GEODE_FIELD(chromaSlow, Linear),
};

#undef GEODE_FIELD

inline constexpr int kFieldSpans = static_cast<int>(sizeof(kFieldKinds) / sizeof(kFieldKinds[0]));
// The arrays (eq, spectrum, waveform, chroma ×3) close the table; every entry before them is one scalar.
static_assert(kFieldSpans - 6 == GEODE_SOUND_SCALARS, "every scalar of GeodeSoundFrame has a kind");

constexpr bool fieldKindsCoverFrame() {
    size_t at = 0;
    int i = 0;
    for (const FieldSpan& f : kFieldKinds) {
        if (f.offset != at || (i++ < GEODE_SOUND_SCALARS && f.count != 1)) return false;
        at += static_cast<size_t>(f.count) * sizeof(float);
    }
    return at == sizeof(GeodeSoundFrame) && at == GEODE_SOUND_FRAME_FLOATS * sizeof(float);
}
static_assert(fieldKindsCoverFrame(), "kFieldKinds lists GeodeSoundFrame's fields in order, each once");

constexpr int countKind(FieldKind kind) {
    int n = 0;
    for (const FieldSpan& f : kFieldKinds) n += f.kind == kind ? f.count : 0;
    return n;
}
static_assert(countKind(FieldKind::Trigger) == SoundEngine::kTriggers, "the engine's triggers, each once");

// The kind of the float at `index` in GeodeSoundFrame.
constexpr FieldKind kindOf(int index) {
    int first = 0;
    for (const FieldSpan& f : kFieldKinds) {
        if (index < first + f.count) return f.kind;
        first += f.count;
    }
    return FieldKind::Hold;
}

// The frame at ring `position` from the hop at or before it (`a`) and the one after it (`b`, null when none is
// published yet), each field by its kind. Triggers and the waveform come out 0, fadeGain as `a` carries it.
void blendSteps(const AnalysisThread::Heard& a, const AnalysisThread::Heard* b, int64_t position, GeodeSoundFrame& out);

// One surface's reads of an AnalysisThread, frame by frame: the frame at the sample the surface draws (blendSteps),
// with every trigger of every hop at or before that sample that no earlier read delivered, at its strongest, and
// where it fired (Hits); the feeling is the earlier hop's, its degree accent collected the same way. Lock-free and
// allocation-free; one thread.
class FrameReader {
public:
    // False when no hop is readable yet or `position` is below 0: then only the triggers (and the accent) clear.
    bool read(const AnalysisThread& analysis, int64_t position, GeodeSoundFrame& out, Emotion::State& emotion,
              Hits& hits);
    // A paused speaker's frame: where the analysis stands now (its newest hop), firing only the hops a starved engine
    // ran on silence. Those repeat the position the writer stopped at and no speaker ever plays them, so each fires
    // here once and read() never fires it again. The hops the speaker has not played yet are left for read(): they
    // fire when it plays them. False as read().
    bool readStill(const AnalysisThread& analysis, GeodeSoundFrame& out, Emotion::State& emotion, Hits& hits);
    // The next read delivers no trigger of a hop at or before `position` (an export's pre-roll).
    void heardThrough(const AnalysisThread& analysis, int64_t position);
    // The next read delivers triggers from its own position on, not from history (a new ring, a new surface).
    void restart() {
        primed_ = false;
        stillAt_ = -1;
    }
    // The writer stopped and the engine runs on silence: its newest hops share one position.
    static bool starved(const AnalysisThread& analysis);

private:
    void fire(const AnalysisThread::Heard& hop, GeodeSoundFrame& out, Emotion::State& emotion, Hits& hits) const;

    int64_t lastIndex_ = -1;       // the newest hop whose triggers were delivered (Heard::index)
    int64_t lastHopPosition_ = 0;  // and its position: later hops never sit before it
    bool primed_ = false;
    // The hops heard while paused (readStill): the position they repeat, the first hop standing there (not one of
    // them: it ends the audio written) and the newest delivered. One run is remembered, the latest.
    int64_t stillAt_ = -1;
    int64_t stillFirst_ = -1;
    int64_t stillTo_ = -1;
};

}  // namespace geode::sound::interp
