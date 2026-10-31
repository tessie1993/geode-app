#include "sound/SoundEngine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace geode::sound {

namespace {

constexpr float kDigitalSilence = 1e-4f;  // -80 dBFS RMS: below it a frame is silence and the means freeze
constexpr float kSilenceOn = 1e-3f;       // -60 dBFS, held for kSilenceHold, fires `silence`
constexpr float kSilenceOff = 2e-3f;      // hysteresis on the way back
constexpr float kSilenceHold = 0.5f;
constexpr float kReturnAfter = 1.0f;
constexpr float kWarmSeconds = 50.0f / 30.0f;  // MilkDrop's 50 warm-up frames at its 30 fps reference
constexpr float kRewarmAfter = 2.0f;           // silence this long re-arms the warm-up (a new track)
constexpr float kStarved = 0.1f;
constexpr float kBandFloor = 3e-4f;  // -70 dB of a full-scale sine, for the untilted EQ and drum bands
constexpr float kTiltTop = 22050.0f;
constexpr float kTimeWrap = 3600.0f;
constexpr int kWaveSlack = 32;
constexpr float kVoiced = 0.35f;
constexpr float kRunTau = 1.0f;
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

}  // namespace

int SoundEngine::fftSizeFor(int sampleRateHz) {
    int n = 256;
    while (static_cast<float>(n) < 0.040f * static_cast<float>(sampleRateHz)) n *= 2;
    return n;
}

GeodeSoundTuning SoundEngine::defaultTuning() {
    return GeodeSoundTuning{{1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.08f, 0.08f, 0.08f}, {0.6f, 0.5f, 0.5f}, 1.0f, 1.0f};
}

SoundEngine::SoundEngine(int sampleRateHz)
    : sampleRateHz_(std::max(8000, sampleRateHz)),
      n_(fftSizeFor(sampleRateHz_)),
      binHz_(static_cast<float>(sampleRateHz_) / static_cast<float>(n_)),
      tuning_(defaultTuning()),
      ringL_(static_cast<size_t>(4 * n_)),
      ringR_(static_cast<size_t>(4 * n_)),
      mono_(static_cast<size_t>(n_)),
      left_(static_cast<size_t>(n_)),
      right_(static_cast<size_t>(n_)),
      windowed_(static_cast<size_t>(n_)),
      hann_(static_cast<size_t>(n_)),
      mag_(static_cast<size_t>(n_ / 2 + 1)),
      tilted_(static_cast<size_t>(n_ / 2 + 1)),
      tilt_(static_cast<size_t>(n_ / 2 + 1)),
      prevLogMag_(static_cast<size_t>(n_ / 2 + 1)),
      fft_(n_),
      pitchFft_(2 * n_),
      pitchMono_(static_cast<size_t>(2 * n_)),
      pitchHann_(static_cast<size_t>(2 * n_)),
      pitchMag_(static_cast<size_t>(n_ + 1)) {
    hann(hann_);
    hann(pitchHann_);
    // MilkDrop's tilt, -0.02·ln(1 - f/22.05 kHz): about +6 dB per octave, so the treble is not buried
    // under the bass's raw energy. Pinned to Hz rather than to bins so it means the same at every rate.
    for (size_t k = 0; k < tilt_.size(); k++) {
        const float f = std::min(static_cast<float>(k) * binHz_, kTiltTop * 511.0f / 512.0f);
        tilt_[k] = -0.02f * std::log(1.0f - f / kTiltTop);
    }
    bandBins_ = {bins(20, 250), bins(250, 4000), bins(4000, 16000)};
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
    drumBins_ = {bins(40, 120), bins(180, 300), bins(2000, 5000), bins(7000, 16000)};
    waveBlock_ = std::max(1, n_ / 512);  // 160 × block always fits the window: ~13 ms at every rate
    waveRaw_.assign(GEODE_SOUND_WAVEFORM_POINTS + kWaveSlack, 0.0f);
    wavePrev_.assign(GEODE_SOUND_WAVEFORM_POINTS, 0.0f);
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

float SoundEngine::sum(const std::vector<float>& mag, BinRange r) const {
    float s = 0.0f;
    for (int k = r.from; k < r.to; k++) s += mag[k];
    return s;
}

void SoundEngine::write(const float* interleaved, int frames, int channels) {
    if (interleaved == nullptr || frames <= 0 || channels <= 0) return;
    const size_t cap = ringL_.size();
    for (int i = 0; i < frames; i++) {
        const float l = interleaved[static_cast<size_t>(i) * channels];
        ringL_[writePos_] = l;
        ringR_[writePos_] = channels > 1 ? interleaved[static_cast<size_t>(i) * channels + 1] : l;
        writePos_ = (writePos_ + 1) % cap;
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

    readWindow(n_, mono_.data(), left_.data(), right_.data());
    double squares = 0.0;
    for (float v : mono_) squares += static_cast<double>(v) * v;
    out.rms = static_cast<float>(std::sqrt(squares / n_));
    const bool silent = out.rms < kDigitalSilence;
    if (silent) {
        quietSeconds_ += dt;
    } else {
        if (quietSeconds_ >= kRewarmAfter) warmSeconds_ = kWarmSeconds;
        quietSeconds_ = 0.0f;
    }
    const bool warm = warmSeconds_ > 0.0f;
    if (!silent) warmSeconds_ = std::max(0.0f, warmSeconds_ - dt);

    // The spectrum: Hann over the window, magnitudes scaled so a full-scale sine reads 1, then tilted.
    for (int i = 0; i < n_; i++) windowed_[i] = mono_[i] * hann_[i];
    fft_.magnitudes(windowed_.data(), mag_.data());
    const float scale = 4.0f / static_cast<float>(n_);
    for (size_t k = 0; k < mag_.size(); k++) {
        mag_[k] *= scale;
        tilted_[k] = mag_[k] * tilt_[k];
    }

    // The MilkDrop core: three summed bands, each against its own running mean.
    for (int b = 0; b < 3; b++) band_[b].step(sum(tilted_, bandBins_[b]), dt, warm, silent);
    out.bass = band_[0].ratio();
    out.mid = band_[1].ratio();
    out.treble = band_[2].ratio();
    out.vol = (out.bass + out.mid + out.treble) / 3.0f;
    out.bassAtt = band_[0].attRatio();
    out.midAtt = band_[1].attRatio();
    out.trebleAtt = band_[2].attRatio();
    out.volAtt = (out.bassAtt + out.midAtt + out.trebleAtt) / 3.0f;

    // The TouchDesigner layer on top: smooth, threshold, gain, into 0..1. Halving first puts "typical"
    // (1.0) at 0.5 and a strong hit (2.0) at full scale.
    float* td[3] = {&out.low, &out.middle, &out.high};
    float* time[3] = {&out.bassTime, &out.midTime, &out.trebleTime};
    for (int b = 0; b < 3; b++) {
        tdSmoothed_[b] = follow(tdSmoothed_[b], band_[b].attRatio(), tuning_.smooth[b], dt);
        *td[b] = clamp01(tuning_.reactivity * tuning_.gain[b] * (0.5f * tdSmoothed_[b] - tuning_.threshold[b]));
        bandTime_[b] = std::fmod(bandTime_[b] + tuning_.speed * dt * band_[b].attRatio(), kTimeWrap);
        *time[b] = bandTime_[b];
    }

    for (int i = 0; i < GEODE_SOUND_EQ_BANDS; i++) {
        eq_[i].step(sum(mag_, eqBins_[i]), dt, warm, silent, kBandFloor);
        out.eq[i] = eq_[i].ratio();
    }
    spectrumPoints(dt, out.spectrum);
    waveform(out.waveform);
    descriptors(dt, warm, silent, out);
    drums(dt, warm, silent, out);
    events(dt, warm, out);
    melody(dt, out);
    clocks(dt, out);
}

void SoundEngine::clocks(float dt, GeodeSoundFrame& out) {
    static constexpr float kEase = 0.05f;  // ~150 ms to settle on the next whole count
    static constexpr float kWrap = 4096.0f;
    if (out.kick > 0.0f || out.snare > 0.0f) hitCount_ += tuning_.speed;
    if (out.noteChange > 0.0f) noteCount_ += tuning_.speed;
    hitTime_ = follow(hitTime_, hitCount_, kEase, dt);
    noteTime_ = follow(noteTime_, noteCount_, kEase, dt);
    // Wrap the count and the clock together so the clock never runs backwards across the seam.
    if (hitCount_ >= kWrap && hitTime_ >= kWrap) {
        hitCount_ -= kWrap;
        hitTime_ -= kWrap;
    }
    if (noteCount_ >= kWrap && noteTime_ >= kWrap) {
        noteCount_ -= kWrap;
        noteTime_ -= kWrap;
    }
    out.hitTime = hitTime_;
    out.noteTime = noteTime_;
    presence_ = follow(presence_, out.rms > kSilenceOn ? 1.0f : 0.0f, 0.7f, dt);
    out.presence = presence_;
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
    spectrumPeak_ = std::max({frameMax, spectrumPeak_ * std::exp(-dt * kLn2 / 2.0f), 1e-6f});
    for (int i = 0; i < GEODE_SOUND_SPECTRUM_POINTS; i++) out[i] = clamp01(out[i] / spectrumPeak_);
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
    if (waveHasPrev_) {
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
    waveHasPrev_ = true;
}

void SoundEngine::descriptors(float dt, bool warm, bool silent, GeodeSoundFrame& out) {
    if (!silent) {
        const BinRange all = bins(20, 20000);
        double num = 0.0;
        double den = 0.0;
        for (int k = all.from; k < all.to; k++) {
            num += static_cast<double>(mag_[k]) * k * binHz_;
            den += mag_[k];
        }
        const float hz = den > 0.0 ? static_cast<float>(num / den) : 20.0f;
        centroid_ = follow(centroid_, clamp01(std::log2(std::max(hz, 20.0f) / 20.0f) / std::log2(1000.0f)), 0.05f, dt);

        // Flatness and partial density over the musical middle, 100 Hz–10 kHz.
        const BinRange mid = bins(100, 10000);
        double logSum = 0.0;
        double linSum = 0.0;
        float rangeMax = 0.0f;
        for (int k = mid.from; k < mid.to; k++) {
            const double p = static_cast<double>(mag_[k]) * mag_[k] + 1e-12;
            logSum += std::log(p);
            linSum += p;
            rangeMax = std::max(rangeMax, mag_[k]);
        }
        const double count = mid.to - mid.from;
        flatness_ = follow(flatness_, clamp01(static_cast<float>(std::exp(logSum / count) / (linSum / count))), 0.05f, dt);
        int peaks = 0;
        for (int k = std::max(mid.from, 1); k < std::min(mid.to, n_ / 2); k++) {
            if (mag_[k] > mag_[k - 1] && mag_[k] >= mag_[k + 1] && mag_[k] > 0.1f * rangeMax) peaks++;
        }
        const float density = clamp01(static_cast<float>(peaks) / 48.0f);
        densityFast_ = follow(densityFast_, density, 0.1f, dt);
        densitySlow_ = follow(densitySlow_, density, 1.0f, dt);
    }
    out.centroid = centroid_;
    out.flatness = flatness_;
    out.densityFast = densityFast_;
    out.densitySlow = densitySlow_;

    // Flux: how much louder each bin got since the last frame, on a log scale, as a relative level so
    // the frame rate (smaller steps at 120 fps) and the mix's loudness both divide out.
    const BinRange fluxRange = bins(30, 16000);
    float rise = 0.0f;
    for (int k = fluxRange.from; k < fluxRange.to; k++) {
        const float lm = std::log1p(1000.0f * mag_[k]);
        rise += std::max(0.0f, lm - prevLogMag_[k]);
        prevLogMag_[k] = lm;
    }
    if (fluxPrimed_) {
        flux_.step(rise, dt, warm, silent);
        out.flux = flux_.ratio();
    } else {
        out.flux = 1.0f;
        fluxPrimed_ = true;
    }

    const float energy = out.rms * out.rms;
    energyShort_ = follow(energyShort_, energy, 0.4f, dt);
    energyLong_ = follow(energyLong_, energy, 8.0f, dt);
    out.energyTrend = energyLong_ > 1e-12f ? clamp01(0.5f * energyShort_ / energyLong_) : 0.5f;

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
        width_ = follow(width_, eMid + eSide > 0.0 ? static_cast<float>(eSide / (eMid + eSide)) : 0.0f, 0.1f, dt);
        pan_ = follow(pan_, eL + eR > 0.0 ? static_cast<float>((eR - eL) / (eL + eR)) : 0.0f, 0.1f, dt);
    }
    out.stereoWidth = clamp01(width_);
    out.stereoPan = std::clamp(pan_, -1.0f, 1.0f);
}

void SoundEngine::drums(float dt, bool warm, bool silent, GeodeSoundFrame& out) {
    static constexpr float kRefTau = 0.15f;
    static constexpr float kAttack = 0.005f;
    static constexpr std::array<float, 3> kRefractory = {0.12f, 0.12f, 0.06f};
    static constexpr std::array<float, 3> kDecay = {0.18f, 0.15f, 0.08f};
    for (int i = 0; i < 4; i++) drumLevel_[i].step(sum(mag_, drumBins_[i]), dt, warm, silent, kBandFloor);
    const std::array<float, 3> level = {drumLevel_[0].ratio(), 0.5f * (drumLevel_[1].ratio() + drumLevel_[2].ratio()),
                                        drumLevel_[3].ratio()};
    std::array<float, 3> hit{};
    float* env[3] = {&out.kickEnv, &out.snareEnv, &out.hatEnv};
    for (int d = 0; d < 3; d++) {
        // Punch: the band's relative level over its own recent past (150 ms), so a held bass note is
        // quiet and the front of a kick is loud.
        const float punch = level[d] - drumRef_[d];
        drumRef_[d] = follow(drumRef_[d], level[d], kRefTau, dt);
        hit[d] = silent ? 0.0f : drumTrigger_[d].step(punch, std::max(0.1f, tuning_.drum[d]), kRefractory[d], dt);
        *env[d] = drumEnv_[d].step(hit[d], kAttack, kDecay[d], dt);
    }
    out.kick = hit[0];
    out.snare = hit[1];
    out.hat = hit[2];
    if (hit[0] > 0.0f) {
        kickCount_++;
        if (kickCount_ % 2 == 0) out.kick2 = hit[0];
        if (kickCount_ % 4 == 0) out.kick4 = hit[0];
        if (kickCount_ % 8 == 0) out.kick8 = hit[0];
        if (kickCount_ % 16 == 0) out.kick16 = hit[0];
    }
    if (hit[1] > 0.0f) {
        snareCount_++;
        if (snareCount_ % 2 == 0) out.snare2 = hit[1];
        if (snareCount_ % 4 == 0) out.snare4 = hit[1];
    }
}

void SoundEngine::events(float dt, bool warm, GeodeSoundFrame& out) {
    out.hardCut = warm || silent_ ? 0.0f : hardCut_.step(out.vol, dt);
    out.onset = onset_.step(out.flux, 2.0f, 0.06f, dt);

    if (!silent_) {
        belowSeconds_ = out.rms < kSilenceOn ? belowSeconds_ + dt : 0.0f;
        if (belowSeconds_ >= kSilenceHold) {
            silent_ = true;
            silentSeconds_ = belowSeconds_;
            out.silence = 1.0f;
            kickCount_ = 0;
            snareCount_ = 0;
        }
    } else {
        silentSeconds_ += dt;
        if (out.rms > kSilenceOff) {
            silent_ = false;
            belowSeconds_ = 0.0f;
            if (silentSeconds_ >= kReturnAfter) out.returned = 1.0f;
        }
    }

    // Surge: the short loudness jumps well past the long one, having been well under it within the last
    // four seconds. Coming back from silence is `returned`, not a surge.
    const float ratio = energyLong_ > 1e-12f ? energyShort_ / energyLong_ : 1.0f;
    quietMemory_ = ratio < 0.75f ? 4.0f : std::max(0.0f, quietMemory_ - dt);
    if (out.returned > 0.0f) {
        surgeArmed_ = false;
        quietMemory_ = 0.0f;
    }
    if (!surgeArmed_ && ratio < 1.1f) surgeArmed_ = true;
    if (surgeArmed_ && !silent_ && quietMemory_ > 0.0f && ratio > 1.6f) {
        out.surge = std::clamp(0.5f * ratio, 0.05f, 1.0f);
        surgeArmed_ = false;
        quietMemory_ = 0.0f;
    }
}

void SoundEngine::estimatePitch(float& midi, float& confidence) {
    readWindow(2 * n_, pitchMono_.data(), nullptr, nullptr);
    for (int i = 0; i < 2 * n_; i++) pitchMono_[i] *= pitchHann_[i];
    pitchFft_.magnitudes(pitchMono_.data(), pitchMag_.data());
    const float binHz = static_cast<float>(sampleRateHz_) / static_cast<float>(2 * n_);
    const int kMin = std::max(2, static_cast<int>(std::ceil(80.0f / binHz)));
    const int kMax = std::min(static_cast<int>(1500.0f / binHz), n_ / 3 - 2);
    auto near = [&](int k) { return std::max({pitchMag_[k - 1], pitchMag_[k], pitchMag_[k + 1]}); };
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
    const float hz = (static_cast<float>(best) + delta) * binHz;
    midi = 69.0f + 12.0f * std::log2(hz / 440.0f);
}

void SoundEngine::melody(float dt, GeodeSoundFrame& out) {
    // An attack is a drum hit or a sudden jump in level; a note change without one is legato.
    const float previousRef = rmsRef_;
    rmsRef_ = follow(rmsRef_, out.rms, 0.08f, dt);
    sinceAttack_ += dt;
    if (out.kick > 0.0f || out.snare > 0.0f || (out.rms > kSilenceOff && out.rms > 1.4f * previousRef)) {
        sinceAttack_ = 0.0f;
    }

    float midi = 0.0f;
    float confidence = 0.0f;
    estimatePitch(midi, confidence);
    out.pitchConfidence = confidence;
    runRate_ *= std::exp(-dt / kRunTau);
    if (confidence >= kVoiced && out.rms > kSilenceOn) {
        out.pitch = midi;
        pitchHeight_ = follow(pitchHeight_, clamp01((midi - 36.0f) / 48.0f), 0.08f, dt);
        lostSeconds_ = 0.0f;
        const int note = static_cast<int>(std::lround(midi));
        if (note != candidateNote_) {
            candidateNote_ = note;
            candidateSeconds_ = 0.0f;
        } else {
            candidateSeconds_ += dt;
        }
        // A note counts once it has held for 40 ms, which keeps vibrato and transients from chattering.
        if (candidateSeconds_ >= 0.04f && note != note_) {
            const bool first = note_ < 0;
            note_ = note;
            out.noteChange = std::clamp(confidence, 0.05f, 1.0f);
            if (!first && sinceAttack_ > 0.06f) out.legato = out.noteChange;
            runRate_ += 1.0f / kRunTau;
        }
    } else {
        lostSeconds_ += dt;
        if (lostSeconds_ > 0.15f) {
            note_ = -1;
            candidateNote_ = -1;
        }
    }
    out.pitchHeight = pitchHeight_;
    out.runDensity = clamp01(runRate_ / 12.0f);
}

void SoundEngine::reset() {
    std::fill(ringL_.begin(), ringL_.end(), 0.0f);
    std::fill(ringR_.begin(), ringR_.end(), 0.0f);
    writePos_ = 0;
    written_ = 0;
    fresh_ = false;
    starvedSeconds_ = 0.0f;
    starved_ = false;
    for (auto& b : band_) b.reset();
    tdSmoothed_ = {};
    bandTime_ = {};
    hitCount_ = 0.0f;
    hitTime_ = 0.0f;
    noteCount_ = 0.0f;
    noteTime_ = 0.0f;
    presence_ = 0.0f;
    warmSeconds_ = kWarmSeconds;
    quietSeconds_ = 0.0f;
    for (auto& e : eq_) e.reset();
    spectrumPeak_ = 1e-6f;
    std::fill(wavePrev_.begin(), wavePrev_.end(), 0.0f);
    waveHasPrev_ = false;
    std::fill(prevLogMag_.begin(), prevLogMag_.end(), 0.0f);
    flux_.reset();
    fluxPrimed_ = false;
    centroid_ = 0.0f;
    flatness_ = 0.0f;
    densitySlow_ = 0.0f;
    densityFast_ = 0.0f;
    energyShort_ = 0.0f;
    energyLong_ = 0.0f;
    width_ = 0.0f;
    pan_ = 0.0f;
    for (auto& d : drumLevel_) d.reset();
    drumRef_ = {1.0f, 1.0f, 1.0f};
    for (auto& t : drumTrigger_) t.reset();
    for (auto& e : drumEnv_) e.reset();
    kickCount_ = 0;
    snareCount_ = 0;
    hardCut_.reset();
    onset_.reset();
    silent_ = false;
    belowSeconds_ = 0.0f;
    silentSeconds_ = 0.0f;
    surgeArmed_ = true;
    quietMemory_ = 0.0f;
    rmsRef_ = 0.0f;
    sinceAttack_ = 1e9f;
    candidateNote_ = -1;
    candidateSeconds_ = 0.0f;
    note_ = -1;
    lostSeconds_ = 0.0f;
    pitchHeight_ = 0.0f;
    runRate_ = 0.0f;
}

}  // namespace geode::sound
