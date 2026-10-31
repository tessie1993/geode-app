#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "sound/Fft.hpp"
#include "sound/SoundFrame.h"
#include "sound/Stages.hpp"

namespace geode::sound {

// The sound engine, built the way MilkDrop builds its audio path: audio is written into a ring as it
// arrives, and once per rendered frame the renderer steps the engine, which reads the newest window out
// of the ring, transforms it and turns it into one GeodeSoundFrame. No clock of its own and no hop: the
// frame is the unit, and each stage scales its smoothing by the frame's dt.
//
//   write() ─► ring ─► step(dt): newest window ─► waveform (aligned to last frame)
//                                              └► Hann ─► FFT ─► tilt ─► bands ─► relative levels ─► TD layer
//                                                             └► descriptors, drums, triggers, melody
//
// The export path writes exactly the audio up to each video frame and steps with dt = 1/fps, through
// this same code; one engine per surface (screen, wallpaper, export), never shared between threads.
class SoundEngine {
public:
    explicit SoundEngine(int sampleRateHz);

    // About 40 ms of audio at any rate: 2048 at 44.1/48 kHz, 4096 at 88.2/96, 8192 at 176.4/192.
    static int fftSizeFor(int sampleRateHz);

    int sampleRateHz() const { return sampleRateHz_; }
    int fftSize() const { return n_; }

    // Interleaved float PCM; mono feeds both channels, channels past the second are ignored.
    void write(const float* interleaved, int frames, int channels);
    void step(float dtSeconds, GeodeSoundFrame& out);
    void setTuning(const GeodeSoundTuning& tuning) { tuning_ = tuning; }
    const GeodeSoundTuning& tuning() const { return tuning_; }
    static GeodeSoundTuning defaultTuning();
    void reset();

private:
    struct BinRange {
        int from;
        int to;  // exclusive
    };
    BinRange bins(float loHz, float hiHz) const;
    float sum(const std::vector<float>& mag, BinRange r) const;
    void readWindow(int count, float* mono, float* left, float* right) const;
    void waveform(float* out);
    void spectrumPoints(float dt, float* out);
    void descriptors(float dt, bool warm, bool silent, GeodeSoundFrame& out);
    void drums(float dt, bool warm, bool silent, GeodeSoundFrame& out);
    void events(float dt, bool warm, GeodeSoundFrame& out);
    void melody(float dt, GeodeSoundFrame& out);
    void clocks(float dt, GeodeSoundFrame& out);
    void estimatePitch(float& midi, float& confidence);

    int sampleRateHz_;
    int n_;
    float binHz_;
    GeodeSoundTuning tuning_;

    // The ring: two channels, newest sample at writePos_ - 1.
    std::vector<float> ringL_;
    std::vector<float> ringR_;
    size_t writePos_ = 0;
    int64_t written_ = 0;
    bool fresh_ = false;
    float starvedSeconds_ = 0.0f;
    bool starved_ = false;  // nothing written for a while: the window reads as silence, not a frozen loop

    // Per-frame scratch.
    std::vector<float> mono_;
    std::vector<float> left_;
    std::vector<float> right_;
    std::vector<float> windowed_;
    std::vector<float> hann_;
    std::vector<float> mag_;       // |X|·2/Σw: a full-scale sine reads 1
    std::vector<float> tilted_;    // mag_ × MilkDrop's tilt, for bands and the spectrum texture
    std::vector<float> tilt_;
    std::vector<float> prevLogMag_;
    Fft fft_;
    Fft pitchFft_;                 // twice as long, for pitch resolution down to 80 Hz
    std::vector<float> pitchMono_;
    std::vector<float> pitchHann_;
    std::vector<float> pitchMag_;

    // MilkDrop core.
    std::array<RelativeLevel, 3> band_;
    std::array<BinRange, 3> bandBins_;
    std::array<float, 3> tdSmoothed_{};
    std::array<float, 3> bandTime_{};
    float hitCount_ = 0.0f;   // whole hits so far; the eased clock chases it
    float hitTime_ = 0.0f;
    float noteCount_ = 0.0f;
    float noteTime_ = 0.0f;
    float presence_ = 0.0f;
    float warmSeconds_ = 0.0f;
    float quietSeconds_ = 0.0f;

    // Band EQ.
    std::array<RelativeLevel, GEODE_SOUND_EQ_BANDS> eq_;
    std::array<BinRange, GEODE_SOUND_EQ_BANDS> eqBins_;
    std::array<BinRange, GEODE_SOUND_SPECTRUM_POINTS> spectrumBins_;
    float spectrumPeak_ = 1e-6f;

    // Waveform alignment.
    std::vector<float> waveRaw_;
    std::vector<float> wavePrev_;
    std::vector<float> waveWeight_;
    int waveBlock_ = 4;
    bool waveHasPrev_ = false;

    // Descriptors.
    RelativeLevel flux_;
    bool fluxPrimed_ = false;
    float centroid_ = 0.0f;
    float flatness_ = 0.0f;
    float densitySlow_ = 0.0f;
    float densityFast_ = 0.0f;
    float energyShort_ = 0.0f;
    float energyLong_ = 0.0f;
    float width_ = 0.0f;
    float pan_ = 0.0f;

    // Drums.
    std::array<RelativeLevel, 4> drumLevel_;  // kick, snare low, snare high, hat
    std::array<BinRange, 4> drumBins_;
    std::array<float, 3> drumRef_{};
    std::array<Trigger, 3> drumTrigger_;
    std::array<Envelope, 3> drumEnv_;
    uint32_t kickCount_ = 0;
    uint32_t snareCount_ = 0;

    // Events.
    HardCut hardCut_;
    Trigger onset_;
    bool silent_ = false;
    float belowSeconds_ = 0.0f;
    float silentSeconds_ = 0.0f;
    bool surgeArmed_ = true;
    float quietMemory_ = 0.0f;

    // Melody.
    float rmsRef_ = 0.0f;
    float sinceAttack_ = 1e9f;
    int candidateNote_ = -1;
    float candidateSeconds_ = 0.0f;
    int note_ = -1;
    float lostSeconds_ = 0.0f;
    float pitchHeight_ = 0.0f;
    float runRate_ = 0.0f;
};

}  // namespace geode::sound
