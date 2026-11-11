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
// The onset envelope: each band's power against one reference shared by every band (the loudest band's
// recent peak, so a quiet master and a loud one give the same envelope), compressed, its positive change,
// and the mean over the bands. The compression is light, so a hat 20 dB under the kick adds little to
// the pulse and one nearly as loud as the kick counts nearly as much.
constexpr float kRefFloor = 1e-10f;
constexpr float kCompress = 3.0f;
constexpr float kFlatEnvelope = 1e-6f;  // mean square per hop under which there are no onsets
// A lag's evidence is its own autocorrelation plus this share of its double's: a true period repeats at
// twice itself, a chance alignment does not.
constexpr float kComb = 0.5f;
// The beat is the fastest lag whose evidence reaches this share of the strongest lag's.
constexpr float kLevel = 0.45f;
constexpr float kLockOn = 0.35f;
constexpr float kLockOff = 0.2f;
constexpr float kLockAfter = 1.0f;         // seconds past the line before locking, ...
constexpr float kUnlockAfter = 2.0f;       // ... and below it before letting go
constexpr float kPhaseGain = 0.2f;         // share of the phase error corrected per measurement
// The onset envelope peaks this many hops after the onset itself (the 15 ms power envelope plus the
// 20 ms change, measured on clicks from 60 to 190 BPM). The measured phase is moved forward by it, so a
// beat lands on the hit, not after it.
constexpr float kLatencyHops = 1.9f;
constexpr float kCountWrap = 4096.0f;
constexpr float kHarmonyLatency = 0.35f;   // a harmonic change is heard this long after the chord moved

float wrapHalf(float x) { return x - std::round(x); }
float frac(double x) { return static_cast<float>(x - std::floor(x)); }

}  // namespace

void Rhythm::configure(int sampleRateHz) {
    sampleRateHz_ = std::max(8000, sampleRateHz);
    hop_ = std::max(1, static_cast<int>(std::lround(static_cast<float>(sampleRateHz_) * kHopSeconds)));
    hopHz_ = static_cast<float>(sampleRateHz_) / static_cast<float>(hop_);
    lagMin_ = static_cast<int>(std::floor(60.0f * hopHz_ / kFastestBpm));
    lagMax_ = kRing / kPeriodsHeard;
    ac_.assign(static_cast<size_t>(2 * lagMax_ + 4), 0.0f);
    reset();
}

void Rhythm::reset() {
    const int rate = sampleRateHz_;
    const int hop = hop_;
    const float hopHz = hopHz_;
    const int lagMin = lagMin_;
    const int lagMax = lagMax_;
    std::vector<float> ac = std::move(ac_);
    *this = Rhythm();
    sampleRateHz_ = rate;
    hop_ = hop;
    hopHz_ = hopHz;
    lagMin_ = lagMin;
    lagMax_ = lagMax;
    ac_ = std::move(ac);
    barInPhrase_ = 7;  // the first downbeat starts phrase one
}

void Rhythm::hop(const std::array<float, kBands>& power) {
    hops_++;
    const float meanK = 1.0f - std::exp(-kHopSeconds / kBandMeanSeconds);
    float loudest = 0.0f;
    for (float p : power) loudest = std::max(loudest, p);
    dbRef_ = std::max(loudest, dbRef_ * std::exp(-kHopSeconds / kBandMeanSeconds));
    const float ref = std::max(dbRef_, kRefFloor);
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
        // The beat's evidence is the same change against the shared reference: each band over its own
        // mean (above, what the drum accents and band flux read) makes a hat 20 dB down rise as far as
        // the kick.
        const float level = std::log1p(kCompress * p / ref);
        odf += std::max(0.0f, level - bandLevel_[b][1]);
        bandLevel_[b][1] = bandLevel_[b][0];
        bandLevel_[b][0] = level;
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
    for (int i = 0; i < n; i++) line_[i] -= mean;
    // A light smoothing (binomial, 5 hops) so a period that falls between two hops correlates as well as
    // one that falls on a hop: without it the strength of a peak would depend on the tempo.
    for (int pass = 0; pass < 2; pass++) {
        float prev = line_[0];
        for (int i = 0; i < n; i++) {
            const float next = i + 1 < n ? line_[i + 1] : line_[i];
            const float cur = line_[i];
            line_[i] = 0.25f * prev + 0.5f * cur + 0.25f * next;
            prev = cur;
        }
    }
    squares_[0] = 0.0f;
    for (int i = 0; i < n; i++) squares_[i + 1] = squares_[i] + line_[i] * line_[i];
    // An envelope this flat (a held tone, silence) has no onsets to find a period in.
    if (squares_[n] < kFlatEnvelope * static_cast<float>(n)) {
        acConf_ = 0.0f;
        return;
    }
    // A lag is a candidate once kPeriodsHeard of its periods are in the window, so its double is
    // measured too.
    const int hi = std::min(lagMax_, n / kPeriodsHeard);
    const int top = 2 * hi + 2;
    for (int lag = lagMin_ - 1; lag <= top; lag++) {
        float sum = 0.0f;
        for (int i = lag; i < n; i++) sum += line_[i] * line_[i - lag];
        // Normalised by the energy of the two overlapping stretches: a periodic envelope scores the same
        // at every lag, so no tempo is favoured by how many terms its lag leaves in the window.
        const float norm = std::sqrt((squares_[n] - squares_[lag]) * squares_[n - lag]);
        ac_[lag] = norm > 1e-12f ? sum / norm : 0.0f;
    }
    auto score = [&](int lag) { return (std::max(0.0f, ac_[lag]) + kComb * std::max(0.0f, ac_[2 * lag])) / (1.0f + kComb); };
    float strongest = 0.0f;
    for (int lag = lagMin_; lag <= hi; lag++) {
        const float s = score(lag);
        if (s > strongest && s >= score(lag - 1) && s >= score(lag + 1)) strongest = s;
    }
    // The beat is the fastest level about as periodic as the strongest one: a pulse repeats at every
    // multiple of its period, so the strongest peak alone could be any of them. The rule names no tempo.
    int best = -1;
    for (int lag = lagMin_; lag <= hi && best < 0; lag++) {
        const float s = score(lag);
        if (s > 0.0f && s >= kLevel * strongest && s >= score(lag - 1) && s >= score(lag + 1)) best = lag;
    }
    if (best < 0) {
        acConf_ = 0.0f;
        return;
    }
    acConf_ = std::clamp(ac_[best], 0.0f, 1.0f);
    const float a = score(best - 1);
    const float b = score(best);
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
    const int bars = static_cast<int>(static_cast<float>(avail) / (static_cast<float>(meter_) * p));
    const int halves = static_cast<int>(static_cast<float>(avail) / (2.0f * p));
    const int m = bars >= 1    ? static_cast<int>(std::lround(static_cast<float>(bars * meter_) * p))
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
            accentSeeded_ = false;
            pendingVotes_ = 0;
            pendingDownSlot_ = -1;
            accentBeats_ = 0;
            meterVotes_ = 0;
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
    if (period_ <= 0.0f) return;
    const double step = 1.0 / period_;
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
    if (!accentSeeded_) {
        // The first beat is the typical beat so far: scored against nothing it would lead the bar
        // whichever beat the lock happened to fall on.
        accentSeeded_ = true;
        kickMean_ = kickAccent_;
        snareMean_ = snareAccent_;
    }
    const float kick = kickAccent_ - kickMean_;
    const float snare = snareAccent_ - snareMean_;
    kickMean_ += 0.125f * (kickAccent_ - kickMean_);
    snareMean_ += 0.125f * (snareAccent_ - snareMean_);
    slotScore_[slot] = 0.85f * slotScore_[slot] + kick - 0.5f * snare;
    beatAccent_[static_cast<size_t>(((beat % kMeterBeats) + kMeterBeats) % kMeterBeats)] = kick - 0.5f * snare;
    accentBeats_ = std::min(accentBeats_ + 1, kMeterBeats);
    measureMeter(beat);
    int best = 0;
    float scale = 1e-6f;
    for (int i = 0; i < meter_; i++) {
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

void Rhythm::measureMeter(int64_t beat) {
    // The last whole bars of both meters: 12 beats once heard, 24 from then on.
    const int n = accentBeats_ >= kMeterBeats ? kMeterBeats : accentBeats_ >= kMeterBeats / 2 ? kMeterBeats / 2 : 0;
    if (!locked_ || n == 0) return;
    auto accent = [&](int64_t b) { return beatAccent_[static_cast<size_t>(((b % kMeterBeats) + kMeterBeats) % kMeterBeats)]; };
    // The share of the accents' spread that a pattern repeating every m beats explains: 1 when each slot
    // of the bar always plays alike, about (m - 1) / (n - 1) for accents in no order. A pattern repeating
    // every 2 beats repeats every 4 too, so kick-snare-kick-snare says 4, and an even pulse says nothing.
    float mean = 0.0f;
    for (int i = 0; i < n; i++) mean += accent(beat - i);
    mean /= static_cast<float>(n);
    float spread = 0.0f;
    for (int i = 0; i < n; i++) spread += (accent(beat - i) - mean) * (accent(beat - i) - mean);
    if (spread < 1e-6f) return;
    auto explained = [&](int m) {
        std::array<float, 4> slot{};
        for (int i = 0; i < n; i++) slot[static_cast<size_t>(((beat - i) % m + m) % m)] += accent(beat - i);
        float sum = 0.0f;
        for (int r = 0; r < m; r++) {
            const float d = slot[static_cast<size_t>(r)] * static_cast<float>(m) / static_cast<float>(n) - mean;
            sum += static_cast<float>(n / m) * d * d;
        }
        return sum / spread;
    };
    const int other = meter_ == 4 ? 3 : 4;
    const float mine = explained(meter_);
    const float theirs = explained(other);
    meterVotes_ = theirs > 0.5f && theirs > 2.0f * mine ? meterVotes_ + 1 : 0;
    if (meterVotes_ < 2 * other) return;
    // The other meter has led on every beat for two of its bars: the bar is that long now, and its
    // downbeat is the slot whose accents lead, as the leaky score would hold it after a long run.
    meter_ = other;
    meterVotes_ = 0;
    pendingVotes_ = 0;
    pendingDownSlot_ = -1;
    slotScore_ = {};
    for (int i = 0; i < n; i++) {
        slotScore_[static_cast<size_t>(slotOf(beat - i))] += accent(beat - i) * static_cast<float>(meter_) / static_cast<float>(n) / 0.15f;
    }
    downSlot_ = 0;
    for (int r = 1; r < meter_; r++) {
        if (slotScore_[static_cast<size_t>(r)] > slotScore_[static_cast<size_t>(downSlot_)]) downSlot_ = r;
    }
}

void Rhythm::harmonyAccent(float strength) {
    if (period_ <= 0.0f) return;
    const double back = kHarmonyLatency * hopHz_ / period_;
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
    out.bpm = period_ > 0.0f ? 60.0f * hopHz_ / period_ : 0.0f;
    out.beatConf = std::clamp(conf_, 0.0f, 1.0f);
    out.beatCount = std::fmod(static_cast<float>(lastBeat_ % 4096), kCountWrap);
    out.barCount = static_cast<float>(bars_ % 4096);
    out.phraseIndex = static_cast<float>(phrases_ % 4096);
    phases(sinceHop, hitTime, out);
    out.beat = beatHit_;
    out.downbeat = downbeatHit_;
    out.phrase = phraseHit_;
}

void Rhythm::phases(int sinceHop, float hitTime, GeodeSoundFrame& out) const {
    if (locked_ && period_ > 0.0f) {
        // The instant `sinceHop` samples after the last hop; never past the next beat, which ticks on a hop.
        const double now =
            std::min(theta_ + static_cast<double>(sinceHop) / hop_ / period_, static_cast<double>(lastBeat_) + 0.999);
        out.beatPhase = frac(now);
        const int inBar = slotOf(static_cast<int64_t>(std::floor(now)) - downSlot_);
        out.barPhase = (static_cast<float>(inBar) + out.beatPhase) / static_cast<float>(meter_);
        out.phrasePhase = (static_cast<float>(barInPhrase_) + out.barPhase) / 8.0f;
    } else {
        // Unlocked: the hit clock (one hit a beat, four a bar) keeps the phases moving with the music.
        const float t = std::isfinite(hitTime) ? hitTime : 0.0f;
        out.beatPhase = frac(t);
        out.barPhase = frac(t / 4.0);
        out.phrasePhase = frac(t / 32.0);
    }
}

void Rhythm::clearFrame() {
    tickCount_ = 0;
    beatHit_ = 0.0f;
    downbeatHit_ = 0.0f;
    phraseHit_ = 0.0f;
}

}  // namespace geode::sound
