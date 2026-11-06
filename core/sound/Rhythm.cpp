#include "sound/Rhythm.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace geode::sound {

namespace {

constexpr float kBandMeanSeconds = 4.0f;   // each band's level is read over its own ~4 s mean
constexpr float kLocalMeanSeconds = 0.3f;  // the onset envelope minus this keeps only its peaks
constexpr int kTempoEvery = 25;            // hops between tempo estimates (0.25 s)
constexpr int kTempoAfter = 300;           // the first estimate waits for 3 s of envelope
constexpr int kPhaseEvery = 5;             // hops between phase measurements (50 ms)
constexpr int kPhaseWindow = 400;          // the phase reads at most the last 4 s, in whole bars
// The onset envelope is librosa's onset_strength: log power in dB against one reference shared by every
// band (power_to_db with amin 1e-10 and top_db 80; the reference is the loudest band's recent peak, so a
// quiet master and a loud one give the same envelope), its positive change, and the mean over the bands.
constexpr float kDbAmin = 1e-10f;
constexpr float kDbTop = 80.0f;
constexpr float kPriorBpm = 120.0f;        // the tempo prior: log-normal around 120 BPM (Ellis 2007) ...
constexpr float kPriorOctaves = 1.0f;      // ... one octave wide
constexpr float kComb = 0.5f;              // weight of the lag's double in its score (the bar hears the beat)
constexpr float kLockOn = 0.35f;
constexpr float kLockOff = 0.2f;
constexpr float kLockAfter = 1.0f;         // seconds past the line before locking, ...
constexpr float kUnlockAfter = 2.0f;       // ... and below it before letting go
constexpr float kPhaseGain = 0.2f;         // share of the phase error corrected per measurement
// The onset envelope peaks this many hops after the onset itself: the 15 ms power envelope plus the
// 20 ms change. The measured phase is moved forward by it, so a beat lands on the hit, not after it.
constexpr float kLatencyHops = 1.5f;
constexpr float kCountWrap = 4096.0f;
constexpr float kHarmonyLatency = 0.35f;   // a harmonic change is heard this long after the chord moved

float wrapHalf(float x) { return x - std::round(x); }
float frac(double x) { return static_cast<float>(x - std::floor(x)); }

}  // namespace

void Rhythm::configure(int sampleRateHz) {
    sampleRateHz_ = std::max(8000, sampleRateHz);
    hop_ = std::max(1, static_cast<int>(std::lround(static_cast<float>(sampleRateHz_) * kHopSeconds)));
    hopHz_ = static_cast<float>(sampleRateHz_) / static_cast<float>(hop_);
    lagMin_ = static_cast<int>(std::floor(60.0f * hopHz_ / 200.0f));
    lagMax_ = std::min(static_cast<int>(std::ceil(60.0f * hopHz_ / 60.0f)), kRing / 3);
    prior_.assign(static_cast<size_t>(lagMax_ + 2), 0.0f);
    for (int lag = 1; lag < static_cast<int>(prior_.size()); lag++) {
        const float bpm = 60.0f * hopHz_ / static_cast<float>(lag);
        const float octaves = std::log2(bpm / kPriorBpm) / kPriorOctaves;
        prior_[lag] = std::exp(-0.5f * octaves * octaves);
    }
    ac_.assign(static_cast<size_t>(2 * lagMax_ + 4), 0.0f);
    reset();
}

void Rhythm::reset() {
    const int rate = sampleRateHz_;
    const int hop = hop_;
    const float hopHz = hopHz_;
    const int lagMin = lagMin_;
    const int lagMax = lagMax_;
    std::vector<float> prior = std::move(prior_);
    std::vector<float> ac = std::move(ac_);
    *this = Rhythm();
    sampleRateHz_ = rate;
    hop_ = hop;
    hopHz_ = hopHz;
    lagMin_ = lagMin;
    lagMax_ = lagMax;
    prior_ = std::move(prior);
    ac_ = std::move(ac);
    barInPhrase_ = 7;  // the first downbeat starts phrase one
}

float Rhythm::periodOrDefault() const { return period_ > 0.0f ? period_ : 0.5f * hopHz_; }

void Rhythm::hop(const std::array<float, kBands>& power) {
    hops_++;
    const float meanK = 1.0f - std::exp(-kHopSeconds / kBandMeanSeconds);
    float loudest = 0.0f;
    for (float p : power) loudest = std::max(loudest, p);
    dbRef_ = std::max(loudest, dbRef_ * std::exp(-kHopSeconds / kBandMeanSeconds));
    const float ref = std::max(dbRef_, kDbAmin);
    std::array<float, kBands> change{};
    float odf = 0.0f;
    for (int b = 0; b < kBands; b++) {
        const float p = power[b];
        float& mean = bandMean_[b];
        if (p > 1e-12f) mean = mean > 0.0f ? mean + meanK * (p - mean) : p;
        // Log over the band's own mean, so a quiet master and a loud one give the same envelope, and the
        // change from two hops back (20 ms), the time the 15 ms power envelope takes to rise.
        const float x = mean > 0.0f ? std::log1p(100.0f * p / mean) : 0.0f;
        change[b] = std::max(0.0f, x - bandLog_[b][1]);
        bandLog_[b][1] = bandLog_[b][0];
        bandLog_[b][0] = x;
        // The beat's evidence is the same change in dB against the shared reference: each band over its
        // own mean (above, what the drum accents and band flux read) makes a hat 20 dB down rise as far
        // as the kick, and a kick on the beat under an open hat on the "and" then cancels in the pulse.
        const float db = std::max(10.0f * std::log10(std::max(p, kDbAmin) / ref), -kDbTop);
        odf += std::max(0.0f, db - bandDb_[b][1]);
        bandDb_[b][1] = bandDb_[b][0];
        bandDb_[b][0] = db;
    }
    odf /= static_cast<float>(kBands);
    bandFluxPeak_[0] = std::max(bandFluxPeak_[0], change[kLow]);
    bandFluxPeak_[1] = std::max(bandFluxPeak_[1], change[kMiddle]);
    bandFluxPeak_[2] = std::max(bandFluxPeak_[2], change[kHigh]);
    hopKick_ = change[kKick] + 0.5f * change[kLow];
    hopSnare_ = std::min(change[kSnareBody], change[kSnareNoise]);

    localMean_ += (1.0f - std::exp(-kHopSeconds / kLocalMeanSeconds)) * (odf - localMean_);
    ring_[ringHead_] = std::max(0.0f, odf - localMean_);
    ringHead_ = (ringHead_ + 1) % kRing;

    // Predict, then correct: the oscillator steps to this hop first, so the pulse measured at this hop is
    // compared with the oscillator at the same instant (#373; compared before the step, every beat ran a
    // hop early).
    advance();
    if (hops_ >= kTempoAfter && hops_ % kTempoEvery == 0) estimateTempo();
    if (period_ > 0.0f && hops_ % kPhaseEvery == 0) measurePhase();
}

void Rhythm::estimateTempo() {
    const int n = static_cast<int>(std::min<int64_t>(hops_, kRing));
    for (int i = 0; i < n; i++) line_[i] = ring_[(ringHead_ - n + i + 2 * kRing) % kRing];
    float mean = 0.0f;
    for (int i = 0; i < n; i++) mean += line_[i];
    mean /= static_cast<float>(n);
    float energy = 0.0f;
    for (int i = 0; i < n; i++) {
        line_[i] -= mean;
        energy += line_[i] * line_[i];
    }
    if (energy < 1e-9f) {
        acConf_ = 0.0f;
        return;
    }
    // The biased autocorrelation (a plain sum, fewer terms at longer lags) leans toward the shorter of
    // two lags that fit equally, which with the comb below is what picks the beat over the bar.
    const int top = std::min(2 * lagMax_ + 2, n - 1);
    for (int lag = lagMin_ - 1; lag <= top; lag++) {
        float sum = 0.0f;
        for (int i = lag; i < n; i++) sum += line_[i] * line_[i - lag];
        ac_[lag] = sum / energy;
    }
    auto at = [&](int lag) { return lag <= top ? ac_[lag] : 0.0f; };
    auto score = [&](int lag) {
        return (std::max(0.0f, ac_[lag]) + kComb * std::max(0.0f, at(2 * lag))) * prior_[lag];
    };
    int best = -1;
    float bestScore = 0.0f;
    for (int lag = lagMin_; lag <= lagMax_; lag++) {
        const float s = score(lag);
        if (s > bestScore && s >= score(lag - 1) && s >= score(lag + 1)) {
            bestScore = s;
            best = lag;
        }
    }
    if (best < 0) {
        acConf_ = 0.0f;
        return;
    }
    // The faster metrical level wins when the envelope is nearly as periodic at half the lag: a click on
    // every beat at 174 BPM fits 87 as well (every other click), and the prior cannot tell them apart.
    const int half = static_cast<int>(std::lround(0.5f * static_cast<float>(best)));
    if (half >= lagMin_ && ac_[half] >= 0.6f * ac_[best]) {
        int peak = half;
        for (int lag = std::max(lagMin_, half - 1); lag <= half + 1; lag++) {
            if (score(lag) > score(peak)) peak = lag;
        }
        best = peak;
        bestScore = score(best);
    }
    acConf_ = std::clamp(ac_[best], 0.0f, 1.0f);
    const float a = score(best - 1);
    const float b = bestScore;
    const float c = score(best + 1);
    const float curve = a - 2.0f * b + c;
    const float found = static_cast<float>(best) + (curve < 0.0f ? std::clamp(0.5f * (a - c) / curve, -0.5f, 0.5f) : 0.0f);
    if (acConf_ < 0.05f) return;

    if (period_ <= 0.0f) {
        period_ = found;
        return;
    }
    const float ratio = found / period_;
    if (std::fabs(ratio - 1.0f) < 0.04f) {
        period_ += 0.3f * (found - period_);  // the same tempo, refined
        candidateVotes_ = 0;
        return;
    }
    // A different tempo must win several estimates in a row before it replaces the current one: one
    // second for an unrelated tempo, two for the current one's double, half, or ×1.5 (the classic
    // octave and triplet confusions, which a passing fill can suggest).
    if (candidate_ > 0.0f && std::fabs(found / candidate_ - 1.0f) < 0.04f) {
        candidateVotes_++;
        candidate_ += 0.3f * (found - candidate_);
    } else {
        candidate_ = found;
        candidateVotes_ = 1;
    }
    const bool related = std::fabs(ratio - 2.0f) < 0.08f || std::fabs(ratio - 0.5f) < 0.03f ||
                         std::fabs(ratio - 1.5f) < 0.06f || std::fabs(ratio - 2.0f / 3.0f) < 0.03f;
    if (candidateVotes_ >= (related ? 8 : 4)) {
        period_ = candidate_;
        candidateVotes_ = 0;
        missSeconds_ = 1e9f;  // re-place the phase on the next measurement
    }
}

void Rhythm::measurePhase() {
    // The predominant local pulse at the newest instant: the windowed Fourier coefficient of the onset
    // envelope at the beat period. A peak m hops ago adds e^(2πi m / P), so the angle is how far into the
    // beat we are, and the magnitude over the mass says how much of the envelope sits on one phase.
    //
    // The window is rectangular and holds whole bars (4P), or whole half bars when a bar does not fit:
    // then every note of a pattern that repeats each bar is in it equally often, and the coefficient does
    // not depend on which note came last. A window that fades with age weighs the newest notes most, so
    // with a kick on the beat and another sound on the "and" the angle swung with the bar position.
    const float p = period_;
    const int avail = static_cast<int>(std::min<int64_t>(hops_, kPhaseWindow));
    const int bars = static_cast<int>(static_cast<float>(avail) / (4.0f * p));
    const int halves = static_cast<int>(static_cast<float>(avail) / (2.0f * p));
    const int m = bars >= 1    ? static_cast<int>(std::lround(static_cast<float>(bars) * 4.0f * p))
                  : halves >= 1 ? static_cast<int>(std::lround(static_cast<float>(halves) * 2.0f * p))
                                : avail;
    const std::complex<float> rot = std::polar(1.0f, 2.0f * std::numbers::pi_v<float> / p);
    std::complex<float> z(1.0f, 0.0f);
    std::complex<float> sum(0.0f, 0.0f);
    float mass = 0.0f;
    for (int i = 0; i < m; i++) {
        const float o = ring_[(ringHead_ - 1 - i + kRing) % kRing];
        sum += o * z;
        mass += o;
        z *= rot;
    }
    plpConf_ = mass > 1e-9f ? std::clamp(std::abs(sum) / mass, 0.0f, 1.0f) : 0.0f;
    if (mass > 1e-9f) {
        measured_ = frac(std::arg(sum) / (2.0 * std::numbers::pi) + kLatencyHops / p);
    }

    const float dt = kPhaseEvery * kHopSeconds;
    const float raw = std::clamp((acConf_ - 0.05f) / 0.35f, 0.0f, 1.0f) * std::clamp((plpConf_ - 0.1f) / 0.4f, 0.0f, 1.0f);
    conf_ += (1.0f - std::exp(-dt / 1.0f)) * (raw - conf_);
    if (!locked_) {
        lockSeconds_ = conf_ > kLockOn ? lockSeconds_ + dt : 0.0f;
        if (lockSeconds_ >= kLockAfter) {
            locked_ = true;
            everLocked_ = true;
            lockSeconds_ = 0.0f;
            snapPhase();
            // Accents gathered on the free-running grid before the lock fell on arbitrary beats: start the
            // downbeat search over on the beats the music has.
            slotScore_ = {};
            kickMean_ = 0.0f;
            snareMean_ = 0.0f;
            pendingVotes_ = 0;
            pendingDownSlot_ = -1;
            return;
        }
    } else {
        lockSeconds_ = conf_ < kLockOff ? lockSeconds_ + dt : 0.0f;
        if (lockSeconds_ >= kUnlockAfter) {
            locked_ = false;
            lockSeconds_ = 0.0f;
        }
    }
    const float error = wrapHalf(measured_ - frac(theta_));
    missSeconds_ = std::fabs(error) > 0.25f ? missSeconds_ + dt : 0.0f;
    if (missSeconds_ > 1.0f) {
        snapPhase();
        return;
    }
    // Unlocked, the grid still follows the pulse, gently, so the form tracker's beats stay near the music.
    pending_ = (locked_ ? kPhaseGain : 0.5f * kPhaseGain) * error;
}

void Rhythm::snapPhase() {
    // The beat nearest the oscillator that has the measured phase. Jumping back is harmless: a beat
    // already ticked never ticks again (lastBeat_).
    const double base = std::floor(theta_) + measured_;
    double best = base;
    for (double c : {base - 1.0, base + 1.0}) {
        if (std::fabs(c - theta_) < std::fabs(best - theta_)) best = c;
    }
    theta_ = best;
    pending_ = 0.0;
    missSeconds_ = 0.0f;
}

void Rhythm::advance() {
    const double step = 1.0 / periodOrDefault();
    // A correction is spread over the next hops, at most half a step each, so the oscillator never
    // stops or runs backwards and a beat cannot fire twice.
    const double c = std::clamp(pending_, -0.5 * step, 0.5 * step);
    pending_ -= c;
    theta_ += step + c;

    const int64_t nearest = static_cast<int64_t>(std::llround(theta_));
    if (nearest != accentBeat_) {
        finishBeatAccent(accentBeat_);
        accentBeat_ = nearest;
        kickAccent_ = 0.0f;
        snareAccent_ = 0.0f;
    }
    if (std::fabs(theta_ - static_cast<double>(nearest)) < 0.15) {
        kickAccent_ = std::max(kickAccent_, hopKick_);
        snareAccent_ = std::max(snareAccent_, hopSnare_);
    }

    const int64_t beat = static_cast<int64_t>(std::floor(theta_));
    while (lastBeat_ < beat) {
        lastBeat_++;
        const bool down = slotOf(lastBeat_ - downSlot_) == 0;
        bool phrase = false;
        if (down) {
            bars_++;
            barInPhrase_ = phrasePending_ ? 0 : (barInPhrase_ + 1) % 8;
            phrasePending_ = false;
            phrase = barInPhrase_ == 0;
            if (phrase) phrases_++;
        }
        if (tickCount_ < kMaxTicks) ticks_[tickCount_++] = Tick{lastBeat_, down, phrase, locked_};
        if (locked_) {
            const float strength = std::clamp(conf_, 0.05f, 1.0f);
            beatHit_ = std::max(beatHit_, strength);
            if (down) downbeatHit_ = std::max(downbeatHit_, strength);
            if (phrase) phraseHit_ = std::max(phraseHit_, strength);
        }
    }
}

void Rhythm::finishBeatAccent(int64_t beat) {
    // Kicks favour the downbeat, snares the backbeat; leaky over ~6 bars (0.85 per bar). Each beat counts
    // against the typical beat (its running mean over ~8 beats): on a log scale a kick on every beat
    // differs by little in absolute terms, and only what sets one beat apart says where the bar starts.
    const int slot = slotOf(beat);
    const float kick = kickAccent_ - kickMean_;
    const float snare = snareAccent_ - snareMean_;
    kickMean_ += 0.125f * (kickAccent_ - kickMean_);
    snareMean_ += 0.125f * (snareAccent_ - snareMean_);
    slotScore_[slot] = 0.85f * slotScore_[slot] + kick - 0.5f * snare;
    int best = 0;
    float scale = 1e-6f;
    for (int i = 0; i < 4; i++) {
        if (slotScore_[i] > slotScore_[best]) best = i;
        scale = std::max(scale, std::fabs(slotScore_[i]));
    }
    // A new downbeat slot must lead by a clear margin for a whole bar of evaluations before the bar moves.
    if (best != downSlot_ && slotScore_[best] - slotScore_[downSlot_] > 0.15f * scale) {
        pendingVotes_ = best == pendingDownSlot_ ? pendingVotes_ + 1 : 1;
        pendingDownSlot_ = best;
        if (pendingVotes_ >= 4) {
            downSlot_ = best;
            pendingVotes_ = 0;
            pendingDownSlot_ = -1;
        }
    } else {
        pendingVotes_ = 0;
        pendingDownSlot_ = -1;
    }
}

void Rhythm::harmonyAccent(float strength) {
    const double back = kHarmonyLatency * hopHz_ / periodOrDefault();
    const int slot = slotOf(static_cast<int64_t>(std::llround(theta_ - back)));
    float scale = 1e-3f;
    for (float s : slotScore_) scale = std::max(scale, std::fabs(s));
    slotScore_[slot] += 0.3f * strength * scale;
}

void Rhythm::sectionStarted(int barsAgo) {
    barsAgo = std::clamp(barsAgo, 0, 7);
    barInPhrase_ = barsAgo;
    phrasePending_ = false;
    phrases_++;
    if (barsAgo == 0 && locked_) phraseHit_ = std::max(phraseHit_, std::clamp(conf_, 0.05f, 1.0f));
}

void Rhythm::dropHere() {
    const int64_t nearest = static_cast<int64_t>(std::llround(theta_));
    const int slot = slotOf(nearest);
    float top = 0.0f;
    float scale = 1e-3f;
    for (float s : slotScore_) {
        top = std::max(top, s);
        scale = std::max(scale, std::fabs(s));
    }
    slotScore_[slot] = top + scale;  // the drop's beat is the downbeat, and stays it
    pendingVotes_ = 0;
    pendingDownSlot_ = -1;
    const bool wasDown = slotOf(nearest - downSlot_) == 0;
    downSlot_ = slot;
    if (nearest > lastBeat_) {
        phrasePending_ = true;  // the beat just ahead ticks as the downbeat that starts the phrase
        return;
    }
    const float strength = std::clamp(conf_, 0.05f, 1.0f);
    if (!wasDown) {
        bars_++;
        if (locked_) downbeatHit_ = std::max(downbeatHit_, strength);
    }
    if (barInPhrase_ != 0 || !wasDown) sectionStarted(0);
}

std::array<float, 3> Rhythm::takeBandFlux() {
    const std::array<float, 3> out = bandFluxPeak_;
    bandFluxPeak_ = {};
    return out;
}

void Rhythm::read(int sinceHop, float hitTime, GeodeSoundFrame& out) {
    const float p = periodOrDefault();
    // The instant of the frame, not of the last hop; never past the next beat, which ticks on a hop.
    const double now = std::min(theta_ + static_cast<double>(sinceHop) / hop_ / p, static_cast<double>(lastBeat_) + 0.999);
    out.bpm = period_ > 0.0f ? 60.0f * hopHz_ / period_ : 0.0f;
    out.beatConf = std::clamp(conf_, 0.0f, 1.0f);
    out.beatCount = std::fmod(static_cast<float>(lastBeat_ % 4096), kCountWrap);
    out.barCount = static_cast<float>(bars_ % 4096);
    out.phraseIndex = static_cast<float>(phrases_ % 4096);
    if (locked_) {
        out.beatPhase = frac(now);
        const int inBar = slotOf(static_cast<int64_t>(std::floor(now)) - downSlot_);
        out.barPhase = (static_cast<float>(inBar) + out.beatPhase) / 4.0f;
        out.phrasePhase = (static_cast<float>(barInPhrase_) + out.barPhase) / 8.0f;
    } else {
        // Unlocked: the hit clock (one hit a beat, four a bar) keeps the phases moving with the music.
        const float t = std::isfinite(hitTime) ? hitTime : 0.0f;
        out.beatPhase = frac(t);
        out.barPhase = frac(t / 4.0);
        out.phrasePhase = frac(t / 32.0);
    }
    out.beat = beatHit_;
    out.downbeat = downbeatHit_;
    out.phrase = phraseHit_;
}

void Rhythm::clearFrame() {
    tickCount_ = 0;
    beatHit_ = 0.0f;
    downbeatHit_ = 0.0f;
    phraseHit_ = 0.0f;
}

}  // namespace geode::sound
