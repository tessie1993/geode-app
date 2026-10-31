#include "sound/SoundEngine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace geode::sound {

namespace {

constexpr float kDigitalSilence = 1e-4f;  // -80 dBFS RMS: below it a frame is silence and the means freeze
constexpr float kSilenceHold = 0.5f;
constexpr float kReturnAfter = 1.0f;
constexpr float kWarmSeconds = 50.0f / 30.0f;
constexpr float kRewarmAfter = 2.0f;  // silence this long re-arms the warm-up (a new track)
constexpr float kStarved = 0.1f;
constexpr float kBandFloor = 3e-4f;   // -70 dB of a full-scale sine, for the untilted EQ and drum bands
constexpr float kTiltTop = 22050.0f;
constexpr float kTimeWrap = 3600.0f;
constexpr float kCountWrap = 4096.0f;
constexpr int kWaveSlack = 32;
constexpr float kRunTau = 1.0f;
constexpr float kEase = 0.06f;
constexpr float kLn2 = 0.69314718f;

// TouchDesigner's Band EQ centres.
constexpr std::array<float, GEODE_SOUND_EQ_BANDS> kEqCentres = {
    25, 40, 60, 90, 150, 240, 370, 590, 930, 1500, 2300, 3600, 5600, 8900, 14000, 22000};

void hann(std::vector<float>& w) {
    const double n = static_cast<double>(w.size());
    for (size_t i = 0; i < w.size(); i++) {
        w[i] = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) / n));
    }
}

// 20 Hz … 20 kHz onto 0 … 1, the axis every frequency-valued output uses.
float logAxis(float hz) { return clamp01(std::log2(std::max(hz, 20.0f) / 20.0f) / std::log2(1000.0f)); }

// Asymmetric one-pole: `rise` seconds going up, `fall` going down.
float lag(float y, float x, float rise, float fall, float dt) { return follow(y, x, x > y ? rise : fall, dt); }

}  // namespace

int SoundEngine::fftSizeFor(int sampleRateHz) {
    int n = 256;
    while (static_cast<float>(n) < 0.040f * static_cast<float>(sampleRateHz)) n *= 2;
    return n;
}

GeodeSoundTuning SoundEngine::defaultTuning() {
    GeodeSoundTuning t{};
    for (int i = 0; i < 3; i++) {
        t.gain[i] = 1.0f;
        t.attack[i] = 0.03f;
        t.release[i] = 0.15f;
    }
    t.drum[0] = 0.6f;
    t.drum[1] = 0.5f;
    t.drum[2] = 0.5f;
    t.drumDecay[0] = 0.18f;
    t.drumDecay[1] = 0.15f;
    t.drumDecay[2] = 0.08f;
    t.reactivity = 1.0f;
    t.speed = 1.0f;
    t.onsetThreshold = 2.0f;
    t.hardCutLevel = 2.5f;
    t.hardCutHalfLife = 60.0f;
    t.pitchThreshold = 0.35f;
    t.noteHold = 0.08f;
    t.silenceDb = -60.0f;
    return t;
}

SoundEngine::SoundEngine(int sampleRateHz)
    : sampleRateHz_(std::max(8000, sampleRateHz)),
      n_(fftSizeFor(sampleRateHz_)),
      binHz_(static_cast<float>(sampleRateHz_) / static_cast<float>(n_)),
      tuning_(defaultTuning()),
      hann_(static_cast<size_t>(n_)),
      tilt_(static_cast<size_t>(n_ / 2 + 1)),
      pitchHann_(static_cast<size_t>(2 * n_)),
      fft_(n_),
      pitchFft_(2 * n_),
      ringL_(static_cast<size_t>(4 * n_)),
      ringR_(static_cast<size_t>(4 * n_)),
      mono_(static_cast<size_t>(n_)),
      left_(static_cast<size_t>(n_)),
      right_(static_cast<size_t>(n_)),
      windowed_(static_cast<size_t>(n_)),
      mag_(static_cast<size_t>(n_ / 2 + 1)),
      tilted_(static_cast<size_t>(n_ / 2 + 1)),
      prevLogMag_(static_cast<size_t>(kFluxSlots * (n_ / 2 + 1))),
      pitchMono_(static_cast<size_t>(2 * n_)),
      pitchMag_(static_cast<size_t>(n_ + 1)),
      waveRaw_(GEODE_SOUND_WAVEFORM_POINTS + kWaveSlack),
      wavePrev_(GEODE_SOUND_WAVEFORM_POINTS) {
    hann(hann_);
    hann(pitchHann_);
    // MilkDrop's tilt, -0.02·ln(1 - f/22.05 kHz): about +6 dB per octave, so the treble is not buried
    // under the bass's raw energy. Pinned to Hz rather than to bins so it means the same at every rate.
    for (size_t k = 0; k < tilt_.size(); k++) {
        const float f = std::min(static_cast<float>(k) * binHz_, kTiltTop * 511.0f / 512.0f);
        tilt_[k] = -0.02f * std::log(1.0f - f / kTiltTop);
    }
    bandBins_ = {bins(20, 250), bins(250, 4000), bins(4000, 16000), bins(20, 60), bins(2000, 6000)};
    for (int i = 0; i < GEODE_SOUND_EQ_BANDS; i++) {
        const float lo = i == 0 ? 20.0f : std::sqrt(kEqCentres[i - 1] * kEqCentres[i]);
        const float hi = i == GEODE_SOUND_EQ_BANDS - 1 ? 0.5f * static_cast<float>(sampleRateHz_)
                                                       : std::sqrt(kEqCentres[i] * kEqCentres[i + 1]);
        eqBins_[i] = bins(lo, hi);
    }
    const float top = std::min(20000.0f, 0.5f * static_cast<float>(sampleRateHz_));
    for (int i = 0; i < GEODE_SOUND_SPECTRUM_POINTS; i++) {
        const float lo = 20.0f * std::pow(top / 20.0f, static_cast<float>(i) / GEODE_SOUND_SPECTRUM_POINTS);
        const float hi = 20.0f * std::pow(top / 20.0f, static_cast<float>(i + 1) / GEODE_SOUND_SPECTRUM_POINTS);
        spectrumBins_[i] = bins(lo, hi);
    }
    // The filter bank: low/middle/high on the same edges as bass/mid/treble, then the drum bands.
    static constexpr std::array<std::array<float, 2>, kSlices> kSliceHz = {{
        {0, 250}, {250, 4000}, {4000, 0}, {40, 120}, {150, 300}, {2500, 6000}, {7000, 16000}, {0, 0}}};
    for (int i = 0; i < kSlices; i++) bank_[i].set(kSliceHz[i][0], kSliceHz[i][1], sampleRateHz_);
    audible_ = bins(20, 20000);
    musical_ = bins(100, 10000);
    fluxBins_ = bins(30, 16000);
    waveBlock_ = std::max(1, n_ / 512);  // 160 × block always fits the window: ~13 ms at every rate
    // Only the middle of the drawn wave counts when aligning, so the centre is what holds still.
    waveWeight_.resize(GEODE_SOUND_WAVEFORM_POINTS);
    const float c = GEODE_SOUND_WAVEFORM_POINTS;
    for (int i = 0; i < GEODE_SOUND_WAVEFORM_POINTS; i++) {
        const float tri = i < GEODE_SOUND_WAVEFORM_POINTS / 2 ? 2.0f * i / c : 2.0f * (c - 1.0f - i) / c;
        waveWeight_[i] = clamp01(5.0f * tri - 3.2f);
    }
    reset();
}

SoundEngine::BinRange SoundEngine::bins(float loHz, float hiHz) const {
    const int top = n_ / 2;
    hiHz = std::min(hiHz, 0.5f * static_cast<float>(sampleRateHz_));
    const int from = std::clamp(static_cast<int>(std::ceil(loHz / binHz_)), 1, top);
    const int to = std::clamp(static_cast<int>(std::ceil(hiHz / binHz_)), from + 1, top + 1);
    return {from, to};
}

float SoundEngine::sum(const std::vector<float>& mag, BinRange r) {
    float s = 0.0f;
    for (int k = r.from; k < r.to; k++) s += mag[k];
    return s;
}

float SoundEngine::nextRandom() {
    state_.random = state_.random * 1664525u + 1013904223u;
    return static_cast<float>(state_.random >> 8) / 16777216.0f;
}

void SoundEngine::write(const float* interleaved, int frames, int channels) {
    if (interleaved == nullptr || frames <= 0 || channels <= 0) return;
    const size_t cap = ringL_.size();
    for (int i = 0; i < frames; i++) {
        const float l = interleaved[static_cast<size_t>(i) * channels];
        const float r = channels > 1 ? interleaved[static_cast<size_t>(i) * channels + 1] : l;
        ringL_[writePos_] = l;
        ringR_[writePos_] = r;
        writePos_ = (writePos_ + 1) % cap;
        const float m = 0.5f * (l + r);
        for (auto& band : bank_) band.process(m);
    }
    written_ += frames;
    fresh_ = true;
}

void SoundEngine::readWindow(int count, float* mono, float* left, float* right) const {
    const size_t cap = ringL_.size();
    for (int i = 0; i < count; i++) {
        const int64_t age = count - i;  // 1 = newest
        float l = 0.0f;
        float r = 0.0f;
        if (!starved_ && age <= written_) {
            const size_t idx = (writePos_ + cap - static_cast<size_t>(age)) % cap;
            l = ringL_[idx];
            r = ringR_[idx];
        }
        mono[i] = 0.5f * (l + r);
        if (left != nullptr) left[i] = l;
        if (right != nullptr) right[i] = r;
    }
}

void SoundEngine::step(float dtSeconds, GeodeSoundFrame& out) {
    const float dt = std::clamp(dtSeconds, 1e-4f, 0.25f);
    starvedSeconds_ = fresh_ ? 0.0f : starvedSeconds_ + dt;
    fresh_ = false;
    starved_ = starvedSeconds_ > kStarved;
    out = GeodeSoundFrame{};
    State& s = state_;

    for (int i = 0; i < kSlices; i++) {
        const float level = bank_[i].take();
        const float punch = bank_[i].takePunch();
        sliceNow_[i] = starved_ ? 0.0f : level;
        punchNow_[i] = starved_ ? 0.0f : punch;
    }
    readWindow(n_, mono_.data(), left_.data(), right_.data());
    double squares = 0.0;
    for (float v : mono_) squares += static_cast<double>(v) * v;
    out.rms = static_cast<float>(std::sqrt(squares / n_));
    const bool silent = out.rms < kDigitalSilence;
    if (silent) {
        s.quietSeconds += dt;
    } else {
        if (s.quietSeconds >= kRewarmAfter) s.warmSeconds = kWarmSeconds;
        s.quietSeconds = 0.0f;
    }
    const bool warm = s.warmSeconds > 0.0f;
    if (!silent) s.warmSeconds = std::max(0.0f, s.warmSeconds - dt);

    // The spectrum: Hann over the window, magnitudes scaled so a full-scale sine reads 1, then tilted.
    for (int i = 0; i < n_; i++) windowed_[i] = mono_[i] * hann_[i];
    fft_.magnitudes(windowed_.data(), mag_.data());
    const float scale = 4.0f / static_cast<float>(n_);
    for (size_t k = 0; k < mag_.size(); k++) {
        mag_[k] *= scale;
        tilted_[k] = mag_[k] * tilt_[k];
    }

    levels(dt, warm, silent, out);
    spectrumPoints(dt, out.spectrum);
    waveform(out.waveform);
    descriptors(dt, warm, silent, out);
    drums(dt, silent, out);
    events(dt, warm, out);
    melody(dt, out);
    clocks(dt, out);
}

void SoundEngine::levels(float dt, bool warm, bool silent, GeodeSoundFrame& out) {
    State& s = state_;
    // The MilkDrop core: summed bands, each against its own running mean.
    for (int b = 0; b < kBands; b++) s.band[b].step(sum(tilted_, bandBins_[b]), dt, warm, silent);
    out.bass = s.band[kBass].ratio();
    out.mid = s.band[kMid].ratio();
    out.treble = s.band[kTreble].ratio();
    out.vol = (out.bass + out.mid + out.treble) / 3.0f;
    out.bassAtt = s.band[kBass].attRatio();
    out.midAtt = s.band[kMid].attRatio();
    out.trebleAtt = s.band[kTreble].attRatio();
    out.volAtt = (out.bassAtt + out.midAtt + out.trebleAtt) / 3.0f;
    out.sub = s.band[kSub].ratio();
    out.subAtt = s.band[kSub].attRatio();
    out.midHigh = s.band[kMidHigh].ratio();
    out.midHighAtt = s.band[kMidHigh].attRatio();

    // The TouchDesigner layer: the filter bank's slice levels, made relative the same way, then Lag
    // (attack/release), threshold, gain and offset into 0..1. Halving first puts "typical" (1.0) at 0.5
    // and a strong hit (2.0) at full scale.
    const GeodeSoundTuning& t = tuning_;
    float* shaped[3] = {&out.low, &out.middle, &out.high};
    float* presence[3] = {&out.bassPresence, &out.midPresence, &out.treblePresence};
    float* time[3] = {&out.bassTime, &out.midTime, &out.trebleTime};
    for (int b = 0; b < 3; b++) {
        s.slice[b].step(sliceNow_[b], dt, warm, silent, kBandFloor);
        s.shaped[b] = lag(s.shaped[b], s.slice[b].ratio(), t.attack[b], t.release[b], dt);
        const float att = s.band[b].attRatio();
        *shaped[b] = clamp01(t.reactivity * t.gain[b] * (0.5f * s.shaped[b] - t.threshold[b]) + t.offset[b]);
        // Presence: the same band over ~3 s, deaf to single hits.
        s.presence[b] = follow(s.presence[b], att, 3.0f, dt);
        *presence[b] = clamp01(0.5f * s.presence[b]);
        s.bandTime[b] = std::fmod(s.bandTime[b] + t.speed * dt * att, kTimeWrap);
        *time[b] = s.bandTime[b];
    }
    s.curvedTime = std::fmod(s.curvedTime + t.speed * dt * std::pow(out.volAtt, 2.5f), kTimeWrap);
    out.curvedTime = s.curvedTime;

    // level: the whole mix over its own slow peak (3 s half-life), so a quiet track still spans 0..1.
    s.levelPeak = std::max({out.rms, s.levelPeak * std::exp(-dt * kLn2 / 3.0f), 1e-4f});
    out.level = clamp01(out.rms / s.levelPeak);
    out.loudness = clamp01(1.0f + 20.0f * std::log10(std::max(out.rms, 1e-6f)) / 60.0f);

    for (int i = 0; i < GEODE_SOUND_EQ_BANDS; i++) {
        s.eq[i].step(sum(mag_, eqBins_[i]), dt, warm, silent, kBandFloor);
        out.eq[i] = s.eq[i].ratio();
    }
}

void SoundEngine::spectrumPoints(float dt, float* out) {
    float frameMax = 0.0f;
    for (int i = 0; i < GEODE_SOUND_SPECTRUM_POINTS; i++) {
        float v = 0.0f;
        for (int k = spectrumBins_[i].from; k < spectrumBins_[i].to; k++) v = std::max(v, tilted_[k]);
        out[i] = v;
        frameMax = std::max(frameMax, v);
    }
    // A slow peak (2 s half-life) rather than the long mean: textures want 0..1, and a hit should touch 1.
    state_.spectrumPeak = std::max({frameMax, state_.spectrumPeak * std::exp(-dt * kLn2 / 2.0f), 1e-6f});
    for (int i = 0; i < GEODE_SOUND_SPECTRUM_POINTS; i++) out[i] = clamp01(out[i] / state_.spectrumPeak);
}

void SoundEngine::waveform(float* out) {
    // The newest (points + slack) × block samples, block-averaged down to points + slack values.
    const int count = GEODE_SOUND_WAVEFORM_POINTS + kWaveSlack;
    const float* src = mono_.data() + (n_ - count * waveBlock_);
    for (int p = 0; p < count; p++) {
        float s = 0.0f;
        for (int j = 0; j < waveBlock_; j++) s += src[p * waveBlock_ + j];
        waveRaw_[p] = s / static_cast<float>(waveBlock_);
    }
    // Slide over the slack to the offset that best matches what was drawn last frame, so a steady
    // tone stands still instead of crawling. The earliest offset wins a tie.
    int best = kWaveSlack;
    if (state_.waveHasPrev) {
        float bestError = std::numeric_limits<float>::max();
        for (int d = 0; d <= kWaveSlack; d++) {
            float error = 0.0f;
            for (int i = 0; i < GEODE_SOUND_WAVEFORM_POINTS; i++) {
                error += waveWeight_[i] * std::fabs(waveRaw_[i + d] - wavePrev_[i]);
            }
            if (error < bestError) {
                bestError = error;
                best = d;
            }
        }
    }
    for (int i = 0; i < GEODE_SOUND_WAVEFORM_POINTS; i++) {
        out[i] = waveRaw_[i + best];
        wavePrev_[i] = out[i];
    }
    state_.waveHasPrev = true;
}

void SoundEngine::descriptors(float dt, bool warm, bool silent, GeodeSoundFrame& out) {
    State& s = state_;
    if (!silent) {
        // Centroid, rolloff and the strongest bin, over the audible range on the untilted spectrum.
        double num = 0.0;
        double den = 0.0;
        int peak = audible_.from;
        for (int k = audible_.from; k < audible_.to; k++) {
            num += static_cast<double>(mag_[k]) * k * binHz_;
            den += mag_[k];
            if (mag_[k] > mag_[peak]) peak = k;
        }
        const float hz = den > 0.0 ? static_cast<float>(num / den) : 20.0f;
        s.centroid = follow(s.centroid, logAxis(hz), 0.05f, dt);
        double below = 0.0;
        int rolloff = audible_.to - 1;
        for (int k = audible_.from; k < audible_.to; k++) {
            below += mag_[k];
            if (below >= 0.85 * den) {
                rolloff = k;
                break;
            }
        }
        s.rolloff = follow(s.rolloff, logAxis(static_cast<float>(rolloff) * binHz_), 0.05f, dt);
        s.peakFreq = follow(s.peakFreq, logAxis(static_cast<float>(peak) * binHz_), 0.03f, dt);
        s.peakMagPeak = std::max({mag_[peak], s.peakMagPeak * std::exp(-dt * kLn2 / 3.0f), 1e-6f});
        s.peakMag = follow(s.peakMag, mag_[peak] / s.peakMagPeak, 0.03f, dt);

        // Flatness and partial density over the musical middle, 100 Hz–10 kHz.
        double logSum = 0.0;
        double linSum = 0.0;
        float rangeMax = 0.0f;
        for (int k = musical_.from; k < musical_.to; k++) {
            const double p = static_cast<double>(mag_[k]) * mag_[k] + 1e-12;
            logSum += std::log(p);
            linSum += p;
            rangeMax = std::max(rangeMax, mag_[k]);
        }
        const double count = musical_.to - musical_.from;
        // Music sits far from white noise (a raw flatness of ~0.01), so it is read in dB: -30 dB is 0, 0 dB is 1.
        const double flat = std::exp(logSum / count) / (linSum / count);
        s.flatness = follow(s.flatness, clamp01(1.0f + static_cast<float>(10.0 * std::log10(flat)) / 30.0f), 0.05f, dt);
        int peaks = 0;
        for (int k = std::max(musical_.from, 1); k < std::min(musical_.to, n_ / 2); k++) {
            if (mag_[k] > mag_[k - 1] && mag_[k] >= mag_[k + 1] && mag_[k] > 0.1f * rangeMax) peaks++;
        }
        const float density = clamp01(static_cast<float>(peaks) / 48.0f);
        s.densityFast = follow(s.densityFast, density, 0.1f, dt);
        s.densitySlow = follow(s.densitySlow, density, 1.0f, dt);
    }
    out.centroid = s.centroid;
    out.rolloff = s.rolloff;
    out.peakFreq = s.peakFreq;
    out.peakMag = clamp01(s.peakMag);
    out.flatness = s.flatness;
    out.densityFast = s.densityFast;
    out.densitySlow = s.densitySlow;

    // Flux: how much louder each bin got than in the youngest stored spectrum at least kFluxLag old (or
    // the oldest one, above 240 fps), on a log scale, as a relative level so the mix's loudness divides
    // out. A fixed lag rather than "the last frame" keeps onsets the same at 30 and 60 fps.
    static constexpr float kFluxLag = 1.0f / 30.0f;
    const size_t stride = mag_.size();
    int ref = -1;
    for (int i = 0; i < kFluxSlots; i++) {
        if (fluxAge_[i] < 0.0f) continue;
        fluxAge_[i] += dt;
        const bool better = ref < 0 || (fluxAge_[ref] < kFluxLag ? fluxAge_[i] > fluxAge_[ref]
                                                                 : fluxAge_[i] >= kFluxLag && fluxAge_[i] < fluxAge_[ref]);
        if (better) ref = i;
    }
    float* now = prevLogMag_.data() + static_cast<size_t>(fluxHead_) * stride;
    const float* then = ref >= 0 ? prevLogMag_.data() + static_cast<size_t>(ref) * stride : nullptr;
    float rise = 0.0f;
    for (int k = fluxBins_.from; k < fluxBins_.to; k++) {
        const float lm = std::log1p(1000.0f * mag_[k]);
        if (then) rise += std::max(0.0f, lm - then[k]);
        now[k] = lm;
    }
    fluxAge_[fluxHead_] = 0.0f;
    fluxHead_ = (fluxHead_ + 1) % kFluxSlots;
    if (then) {
        s.flux.step(rise, dt, warm, silent);
        out.flux = s.flux.ratio();
    } else {
        out.flux = 1.0f;
    }

    const float energy = out.rms * out.rms;
    s.energyShort = follow(s.energyShort, energy, 0.4f, dt);
    s.energyLong = follow(s.energyLong, energy, 8.0f, dt);
    const float trend = s.energyLong > 1e-12f ? s.energyShort / s.energyLong : 1.0f;
    out.energyTrend = clamp01(0.5f * trend);
    // Intensity creeps up while the song is louder than its own average and drains slowly otherwise.
    s.intensity = clamp01(s.intensity + dt * (0.15f * std::max(0.0f, trend - 1.0f) - 0.03f * s.intensity));
    out.intensity = s.intensity;

    double eL = 0.0;
    double eR = 0.0;
    double eMid = 0.0;
    double eSide = 0.0;
    for (int i = 0; i < n_; i++) {
        const double l = left_[i];
        const double r = right_[i];
        eL += l * l;
        eR += r * r;
        eMid += 0.25 * (l + r) * (l + r);
        eSide += 0.25 * (l - r) * (l - r);
    }
    if (!silent) {
        s.width = follow(s.width, eMid + eSide > 0.0 ? static_cast<float>(eSide / (eMid + eSide)) : 0.0f, 0.1f, dt);
        s.pan = follow(s.pan, eL + eR > 0.0 ? static_cast<float>((eR - eL) / (eL + eR)) : 0.0f, 0.1f, dt);
    }
    out.stereoWidth = clamp01(s.width);
    out.stereoPan = std::clamp(s.pan, -1.0f, 1.0f);
}

void SoundEngine::drums(float dt, bool silent, GeodeSoundFrame& out) {
    static constexpr float kAttack = 0.005f;
    static constexpr std::array<float, 3> kRefractory = {0.12f, 0.12f, 0.06f};
    State& s = state_;
    // Punch, measured per sample in the filter bank: each drum band's level over its own mean, over its
    // own recent past (150 ms), so a held bass note is quiet and the front of a kick is loud. A band only
    // counts while it carries a real share of the whole (-30 dB): measured against its own mean, a band
    // holding nothing but a tone's leakage would punch too.
    const float total = sliceNow_[kAll];
    std::array<float, 4> punch{};
    for (int i = 0; i < 4; i++) {
        const int band = kKick + i;
        punch[i] = sliceNow_[band] > kBandFloor && sliceNow_[band] > 0.0316f * total ? punchNow_[band] : 0.0f;
    }
    // A snare is body and noise at once: it needs both to jump, which a kick's harmonics or a sung
    // syllable alone do not. On a kick's frame its noise must also out-jump the kick, so a clap on the
    // kick still counts and the kick's own click does not.
    std::array<float, 3> drumPunch = {punch[0], std::min(punch[1], punch[2]), punch[3]};
    std::array<float, 3> hit{};
    float* env[3] = {&out.kickEnv, &out.snareEnv, &out.hatEnv};
    for (int d = 0; d < 3; d++) {
        if (d == 1 && hit[0] > 0.0f && punch[2] <= punch[0]) drumPunch[1] = 0.0f;
        hit[d] = silent ? 0.0f
                        : s.drumTrigger[d].step(drumPunch[d], std::max(0.1f, tuning_.drum[d]), kRefractory[d], dt);
        *env[d] = s.envelope[d].step(hit[d], kAttack, std::max(0.01f, tuning_.drumDecay[d]), dt);
    }
    out.kick = hit[0];
    out.snare = hit[1];
    out.hat = hit[2];
    if (hit[0] > 0.0f) {
        s.kickCount++;
        if (s.kickCount % 2 == 0) out.kick2 = hit[0];
        if (s.kickCount % 4 == 0) out.kick4 = hit[0];
        if (s.kickCount % 8 == 0) out.kick8 = hit[0];
        if (s.kickCount % 16 == 0) out.kick16 = hit[0];
        s.easedTarget[0] = 1.0f - s.easedTarget[0];
        s.easedTarget[2] = nextRandom();
    }
    if (hit[1] > 0.0f) {
        s.snareCount++;
        if (s.snareCount % 2 == 0) out.snare2 = hit[1];
        if (s.snareCount % 4 == 0) out.snare4 = hit[1];
        s.easedTarget[1] = 1.0f - s.easedTarget[1];
    }
}

void SoundEngine::events(float dt, bool warm, GeodeSoundFrame& out) {
    State& s = state_;
    const GeodeSoundTuning& t = tuning_;
    const float silenceOn = std::pow(10.0f, t.silenceDb / 20.0f);
    out.hardCut = warm || s.silent ? 0.0f : s.hardCut.step(out.vol, t.hardCutLevel, t.hardCutHalfLife, dt);
    out.onset = s.onset.step(out.flux, std::max(1.1f, t.onsetThreshold), 0.06f, dt);
    out.onsetEnv = s.envelope[3].step(out.onset, 0.005f, 0.2f, dt);

    if (!s.silent) {
        s.belowSeconds = out.rms < silenceOn ? s.belowSeconds + dt : 0.0f;
        if (s.belowSeconds >= kSilenceHold) {
            s.silent = true;
            s.silentSeconds = s.belowSeconds;
            out.silence = 1.0f;
            s.kickCount = 0;
            s.snareCount = 0;
        }
    } else {
        s.silentSeconds += dt;
        if (out.rms > 2.0f * silenceOn) {
            s.silent = false;
            s.belowSeconds = 0.0f;
            if (s.silentSeconds >= kReturnAfter) out.returned = 1.0f;
        }
    }

    // Surge: the short loudness jumps well past the long one, having been well under it within the last
    // four seconds. Coming back from silence is `returned`, not a surge.
    const float ratio = s.energyLong > 1e-12f ? s.energyShort / s.energyLong : 1.0f;
    s.quietMemory = ratio < 0.75f ? 4.0f : std::max(0.0f, s.quietMemory - dt);
    if (out.returned > 0.0f) {
        s.surgeArmed = false;
        s.quietMemory = 0.0f;
    }
    if (!s.surgeArmed && ratio < 1.1f) s.surgeArmed = true;
    if (s.surgeArmed && !s.silent && s.quietMemory > 0.0f && ratio > 1.6f) {
        out.surge = std::clamp(0.5f * ratio, 0.05f, 1.0f);
        s.surgeArmed = false;
        s.quietMemory = 0.0f;
    }
    s.presenceFade = follow(s.presenceFade, out.rms > silenceOn ? 1.0f : 0.0f, 0.7f, dt);
    out.presence = s.presenceFade;
}

void SoundEngine::estimatePitch(float& midi, float& confidence, float& evenness) {
    readWindow(2 * n_, pitchMono_.data(), nullptr, nullptr);
    for (int i = 0; i < 2 * n_; i++) pitchMono_[i] *= pitchHann_[i];
    pitchFft_.magnitudes(pitchMono_.data(), pitchMag_.data());
    const float binHz = static_cast<float>(sampleRateHz_) / static_cast<float>(2 * n_);
    const int top = n_ - 1;  // last index `near` may read around
    const int kMin = std::max(2, static_cast<int>(std::ceil(80.0f / binHz)));
    const int kMax = std::min(static_cast<int>(1500.0f / binHz), top / 3 - 1);
    auto near = [&](int k) {
        if (k >= top) return 0.0f;
        return std::max({pitchMag_[k - 1], pitchMag_[k], pitchMag_[k + 1]});
    };
    // A harmonic sum: the fundamental plus what sits at twice and three times it. It prefers the
    // series' root over its loudest partial, which is what a listener calls the note.
    int best = kMin;
    float bestScore = 0.0f;
    double total = 0.0;
    for (int k = kMin; k <= kMax; k++) {
        const float score = pitchMag_[k] + 0.5f * near(2 * k) + 0.33f * near(3 * k);
        total += score;
        if (score > bestScore) {
            bestScore = score;
            best = k;
        }
    }
    const float mean = static_cast<float>(total / std::max(1, kMax - kMin + 1));
    confidence = mean > 0.0f ? clamp01((bestScore / mean - 3.0f) / 7.0f) : 0.0f;
    const float a = pitchMag_[best - 1];
    const float b = pitchMag_[best];
    const float c = pitchMag_[best + 1];
    const float curve = a - 2.0f * b + c;
    const float delta = curve < 0.0f ? std::clamp(0.5f * (a - c) / curve, -0.5f, 0.5f) : 0.0f;
    const float k0 = static_cast<float>(best) + delta;
    midi = 69.0f + 12.0f * std::log2(k0 * binHz / 440.0f);
    // Evenness: the even harmonics' share of the first six (sigmund~'s measure). Square-ish, hollow
    // tones (clarinet, square lead) read low; saw-like and vocal tones near half.
    float even = 0.0f;
    float all = 0.0f;
    for (int h = 1; h <= 6; h++) {
        const float amp = near(static_cast<int>(std::lround(k0 * h)));
        all += amp;
        if (h % 2 == 0) even += amp;
    }
    evenness = all > 0.0f ? even / all : 0.0f;
}

void SoundEngine::melody(float dt, GeodeSoundFrame& out) {
    State& s = state_;
    const GeodeSoundTuning& t = tuning_;
    const float silenceOn = std::pow(10.0f, t.silenceDb / 20.0f);
    // An attack is a drum hit or a sudden jump in level (the whole mix's punch, measured per sample in
    // the filter bank); a note change without one is legato.
    const bool levelJump = out.rms > 2.0f * silenceOn && punchNow_[kAll] > 0.25f;
    s.sinceAttack += dt;
    s.sinceReattack += dt;
    if (out.kick > 0.0f || out.snare > 0.0f || levelJump) s.sinceAttack = 0.0f;

    float midi = 0.0f;
    float confidence = 0.0f;
    float evenness = 0.0f;
    estimatePitch(midi, confidence, evenness);
    out.pitchConfidence = confidence;
    s.runRate *= std::exp(-dt / kRunTau);
    if (confidence >= t.pitchThreshold && out.rms > silenceOn) {
        out.pitch = midi;
        s.pitchClass = std::fmod(std::fmod(midi, 12.0f) + 12.0f, 12.0f) / 12.0f;
        s.pitchHeight = follow(s.pitchHeight, clamp01((midi - 36.0f) / 48.0f), 0.08f, dt);
        s.evenness = follow(s.evenness, evenness, 0.1f, dt);
        // Glide: how fast the pitch moves, as the lag of a 60 ms follower behind it (a ramp of r semitones
        // a second trails by r·τ), which reads the same at any frame rate; a frame-to-frame difference
        // over dt would read twice the jitter at 60 fps as at 30. A jump of more than two semitones is a
        // new note, not a slide: the follower snaps to it.
        static constexpr float kSlopeTau = 0.06f;
        if (s.lostSeconds > 0.0f || std::fabs(midi - s.lastMidi) > 2.0f) s.lastMidi = midi;
        s.lastMidi = follow(s.lastMidi, midi, kSlopeTau, dt);
        s.glide = follow(s.glide, std::fabs(midi - s.lastMidi) / kSlopeTau, 0.1f, dt);
        s.lostSeconds = 0.0f;
        // Within three quarters of a semitone of the current note is still that note (vibrato, a bend in
        // progress), so rounding cannot flip it back and forth across the half-semitone line.
        const int note = s.note >= 0 && std::fabs(midi - static_cast<float>(s.note)) < 0.75f
                             ? s.note
                             : static_cast<int>(std::lround(midi));
        if (note != s.candidateNote) {
            s.candidateNote = note;
            s.candidateSeconds = dt;  // this frame already heard dt of it
        } else {
            s.candidateSeconds += dt;
        }
        if (s.candidateSeconds >= t.noteHold && note != s.note) {
            const bool first = s.note < 0;
            if (!first) s.interval = std::clamp(static_cast<float>(note - s.note) / 12.0f, -1.0f, 1.0f);
            s.note = note;
            s.meanMidi = midi;
            out.noteChange = std::clamp(confidence, 0.05f, 1.0f);
            if (!first && s.sinceAttack > 0.12f) out.legato = out.noteChange;
            s.runRate += 1.0f / kRunTau;
            s.easedTarget[3] = nextRandom();
        } else if (note == s.note && levelJump && s.sinceReattack > 0.1f) {
            out.reattack = std::clamp(confidence, 0.05f, 1.0f);  // same note, struck again
            s.sinceReattack = 0.0f;
        }
        // Vibrato: the wobble around the held note's own centre (150 ms), half a semitone reads 1.
        if (note == s.note) {
            s.meanMidi = follow(s.meanMidi, midi, 0.15f, dt);
            s.vibrato = follow(s.vibrato, std::fabs(midi - s.meanMidi) / 0.5f, 0.15f, dt);
        }
    } else {
        s.lostSeconds += dt;
        s.glide = follow(s.glide, 0.0f, 0.1f, dt);
        s.vibrato = follow(s.vibrato, 0.0f, 0.15f, dt);
        if (s.lostSeconds > 0.15f) {
            s.note = -1;
            s.candidateNote = -1;
        }
    }
    out.pitchClass = s.pitchClass;
    out.pitchHeight = s.pitchHeight;
    out.interval = s.interval;
    out.glide = clamp01(s.glide / 24.0f);
    out.vibrato = clamp01(s.vibrato);
    out.evenness = s.evenness;
    out.runDensity = clamp01(s.runRate / 12.0f);
}

void SoundEngine::clocks(float dt, GeodeSoundFrame& out) {
    State& s = state_;
    if (out.kick > 0.0f || out.snare > 0.0f) s.hitCount += tuning_.speed;
    if (out.noteChange > 0.0f) s.noteCount += tuning_.speed;
    s.hitTime = follow(s.hitTime, s.hitCount, 0.05f, dt);
    s.noteTime = follow(s.noteTime, s.noteCount, 0.05f, dt);
    // Wrap the count and the clock together so the clock never runs backwards across the seam.
    if (s.hitCount >= kCountWrap && s.hitTime >= kCountWrap) {
        s.hitCount -= kCountWrap;
        s.hitTime -= kCountWrap;
    }
    if (s.noteCount >= kCountWrap && s.noteTime >= kCountWrap) {
        s.noteCount -= kCountWrap;
        s.noteTime -= kCountWrap;
    }
    out.hitTime = s.hitTime;
    out.noteTime = s.noteTime;
    for (int i = 0; i < 4; i++) s.eased[i] = follow(s.eased[i], s.easedTarget[i], kEase, dt);
    out.kickToggle = s.eased[0];
    out.snareToggle = s.eased[1];
    out.kickRandom = s.eased[2];
    out.noteRandom = s.eased[3];
}

void SoundEngine::reset() {
    std::fill(ringL_.begin(), ringL_.end(), 0.0f);
    std::fill(ringR_.begin(), ringR_.end(), 0.0f);
    writePos_ = 0;
    written_ = 0;
    fresh_ = false;
    starvedSeconds_ = 0.0f;
    starved_ = false;
    fluxAge_.fill(-1.0f);
    fluxHead_ = 0;
    std::fill(wavePrev_.begin(), wavePrev_.end(), 0.0f);
    for (auto& band : bank_) band.clear();
    sliceNow_ = {};
    punchNow_ = {};
    state_ = State{};
}

}  // namespace geode::sound
