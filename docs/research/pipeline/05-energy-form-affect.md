# Stage 5: energy, dynamics, form and affect (research, 2026-11-14)

Scope: from the hop features (loudness, bands, flux, chroma) to the slow, musical fields: loudness, intensity, silence, surge, build, drop, sections, section states, normalisation and emotion.
Builds on `docs/research/music-analysis.md` (BS.1770 already native, Yadati's drop definition, novelty peaks, lag matrices, real-time segmentation delay); none of that is repeated here.
Evidence: each line names its source; "read" means the primary was fetched and read; "secondary" means a search summary or a third party; "unverified" or "open" means no source read settles it.
Clean room: methods and maths only; no other project's code, names or strings.

## 1. How the app does it now (file:line; HARDCODED = picked by hand, no source)

### Loudness
- `core/sound/LufsMeter.hpp:6-9`: BS.1770 K-weighting per channel, L²+R² summed, momentary 0.4 s and short-term 3 s as running sums per 10 ms hop.
- `LufsMeter.hpp:24-25`: the 0.4 s and 3 s windows are EBU Tech 3341's (sourced).
- `LufsMeter.hpp:67-73`: the sliding windows subtract the leaving hop's power from a running sum (a group inverse in (ℝ,+)); `:77-78` clamp the sum at 0 to hide float drift.
- `LufsMeter.hpp:77`: windows still filling are averaged over the hops they hold, so the first 3 s are not a 3 s measure (a deliberate deviation from Tech 3341, not flagged on the output).
- No integrated loudness and no loudness range (LRA) live; `core/audio/dsp/Loudness.hpp:22-24` computes integrated loudness only for exported files, offline.
- `LufsMeter.hpp:20`, `:93`: HARDCODED −60 LUFS floor mapped to 0, 0 LUFS to 1.
- `LufsMeter.hpp:26`, `:97`: HARDCODED trend full scale 8 LU (M − S of 8 LU reads 1).
- `SoundEngine.cpp:443`: `loudness` is unweighted RMS in dBFS over a HARDCODED 60 dB span.

### Intensity, drive, energy trend
- `SoundEngine.cpp:601-604`: `energyTrend` = one-pole RMS² at 0.4 s over one at 8 s, halved; the 8 s τ is HARDCODED (not a standard window) and the 0.4 s one is a one-pole, not Tech 3341's rectangular window.
- `SoundEngine.cpp:41-44`, `:993-995`: `drive` = loudnessS × (0.35 + 0.65·busy), eased 0.5 s up / 1.5 s down: all four HARDCODED.
- `SoundEngine.cpp:37-38`, `:991`: `busy` = 8 onsets/s and 4 hits/s full scale, weights 0.5/0.25/0.25, beat-confidence knee 0.35/0.5: HARDCODED.
- `SoundEngine.cpp:998-999`: `intensity` = 0.75·drive + 0.25·rise, rise eased 1 s: HARDCODED.
- `DriveGain.hpp:22-23`: drive gain floor 0.15 and full scale 0.5: HARDCODED.
- `DriveGain.hpp:34-35`: motion still/full at presence 0.05/0.5: HARDCODED.
- `SoundEngine.cpp:35`: hit-drive floor 0.4: HARDCODED.

### Silence, return, surge
- `LufsMeter.hpp:21-23`: the one silence gate opens at −55 LUFS momentary, closes after 0.25 s under −60 LUFS: HARDCODED (Tech 3341's "silence" gate is −70 LUFS, below).
- `SoundEngine.cpp:17-18`, `:721-736`: a second silence detector on unweighted RMS: 0.5 s under `silenceDb` (default −60 dBFS, `Tuning.cpp:38`), exit at 2× that level, `returned` after ≥ 1 s silent: HARDCODED (user-tunable default).
- `SoundEngine.cpp:753`: `presence` eases with τ 0.7 s: HARDCODED.
- `SoundEngine.cpp:739-752`: `surge` = short/long energy ratio > 1.6 after < 0.75 within 4 s, re-arm < 1.1, strength 0.5·ratio: HARDCODED.
- `Silence.hpp:21`, `:24`: resting surfaces: peak 0.008 (≈ −42 dBFS) and 6 s rest: HARDCODED.
- `Silence.hpp:51-52`: capture refusal: 1e-6 peak and 4 s: HARDCODED.

### Build-up and drop
- `Form.cpp:12`, `:15`: an 8 s arc of 0.25 s samples (32), skipped for the first 4 s: HARDCODED.
- `Form.cpp:168-177`: build = weighted slopes of loudness (6 dB/8 s), brightness (0.08), onset rate (4), bass pulled out; weights 0.3/0.25/0.25/0.2, agreement count ≥ 2 above 0.3, gain 1.4: HARDCODED.
- `Form.cpp:21-22`: build on 0.55 / off 0.25: HARDCODED.
- `Form.cpp:74-75`: bass short τ 0.1 s, long τ 16 s: HARDCODED.
- `Form.cpp:105-107`: drop = bass back above max(2× its 2 s minimum, 0.6× long), > 0.05 share, after a build held ≥ 2 s of 4: HARDCODED.
- `Form.cpp:26-27`: drop refractory 8 s, 8 held samples: HARDCODED.
- `Form.cpp:94-95` cites Yadati et al. for the bass-return cue only; the numbers are not from that paper.

### Novelty and sections
- `Form.hpp:47-50`: 32-beat ring, checkerboard 16 beats before / 4 after, 8 stored sections: HARDCODED, and "4 beats = one bar" assumes 4/4 (owner's rule: no fixed meter).
- `Form.cpp:18`, `:19`: a section lasts ≥ 8 bars, a drop may cut it at ≥ 4: HARDCODED phrase lengths (owner's rule: no fixed phrase length).
- `Form.hpp:29`, `Form.cpp:17`: event floor 0.4, novelty over 1.6× its running mean: HARDCODED.
- `Form.cpp:125`, `:321`: novelty divided by a HARDCODED 0.5 full scale.
- `Form.cpp:292`, `:296`: the similarity scale and novelty mean are EMAs with a HARDCODED rate 0.05 per beat (so their time constant changes with tempo).
- `Form.cpp:50-60` (`distance2`): three feature groups of equal weight; the weighting itself is a choice with no source.
- `Form.cpp:20`, `:233` (`identify`): a return matches if d² < 0.3 × the largest inter-section d²: HARDCODED.
- `Form.cpp:337`: centroids learn with a count capped at 16: HARDCODED.

### Section states
- `Form.cpp:254-267` (`classifyAtSectionStart`) and `:346-358`: rules on bar loudness vs a slowly released peak (−0.125 dB/bar, `:309`), bass ratio, thresholds −2.5/−4/−6 dB, 0.4/0.7/0.8, 16 intro bars, 4 quiet bars, −4 dB/8 s slope: all HARDCODED.

### Emotion
- `Emotion.hpp:12-19`: key + scale fingerprint (table) plus live cues, summed per axis, citing Eerola, Friberg & Bresin 2013 for additivity.
- `EmotionTable.inc:1`: generated by `tools/music_emotion.py`; the per-mode valence/energy/tension and GEMS weights are that script's; their primary sources were not re-read here (open).
- `Emotion.cpp:27-36`: trust 0.2/0.2, hold 1 s, glide τ 3/3/1.5/1/2 s: HARDCODED.
- `Emotion.cpp:45-58`: Huron scale-degree qualia as numbers: the magnitudes are HARDCODED (the source is qualitative).
- `Emotion.cpp:180-195`: loudness window −33…−6 dBFS, busy 6/s, brightness 0.35–0.7, cue weights 0.3/0.2/0.1/0.2/0.15/0.05/0.35/0.3: HARDCODED.
- `Emotion.cpp:208-212`: GEMS adjustments 0.3 × products of axes: HARDCODED.
- `Emotion.cpp:150`, `:159`: a leading degree needs 1.6× the chroma mean and 0.12 s: HARDCODED.
- The tempo is correctly excluded (`Emotion.cpp:181-183`).

## 2. Optimal design per sub-task

### 2.1 Loudness: momentary, short-term, integrated, LRA
- Momentary: "sliding rectangular time window of length 0.4 s … not gated" (EBU Tech 3341 v4 2023 §2.2, read: https://tech.ebu.ch/docs/tech/tech3341.pdf).
- Short-term: "sliding rectangular time window of length 3 s … not gated"; live update rate "at least 10 Hz" (same §2.2).
- Integrated: BS.1770 gating; live update rate "at least 1 Hz" (same §2.2).
- No extra attack/release smoothing after the windows in EBU Mode (same §2.2); so `energyTrend`'s one-pole 0.4 s is not EBU momentary loudness.
- Gating: 400 ms blocks, 75% overlap, absolute gate −70 LKFS, relative gate 10 below the absolute-gated level; incomplete blocks unused (ITU-R BS.1770-5 §2, eqs 4–7, read: https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-E.pdf).
- The −0.691 constant cancels the K-weighting gain at 997 Hz (BS.1770-5 Note 1, read).
- LRA: short-term (3 s) loudness sampled at ≥ 10 Hz (≥ 2.9 s overlap), absolute gate −70 LUFS, relative gate −20 LU, LRA = 95th − 10th percentile of what survives (EBU Tech 3342 2023 §3.1, read: https://tech.ebu.ch/docs/tech/tech3342.pdf).
- Tech 3342's reason for the percentiles: the 10% keeps a fade-out from dominating, the 95% keeps one loud event from setting the range (same §3.1).
- Compliance tolerance: ±0.1 LU on the test signals (Tech 3341 §2.9 Table 1, read).
- Best causal design: keep the per-hop K-weighted power (`LufsMeter.hpp:57-61`) and add two streaming summaries on the 100 ms block grid BS.1770 already defines:
- (a) a block-loudness histogram whose bins hold (count, Σ block power), bin width ≤ 0.1 LU (from the ±0.1 LU tolerance); integrated loudness and its relative gate are a sum over bins above Γ (exact to the bin width).
- (b) a short-term-loudness histogram for LRA, same bin width; the 10th/95th percentiles are a cumulative-count walk.
- Both are commutative monoids (bin-wise addition) and groups (bin-wise subtraction), so a sliding "integrated over the last T" is exact with subtract-on-evict (section 4).
- Cost: two biquads per channel per sample (existing); per 100 ms block one histogram increment; integrated and LRA reads O(bins) at ≥ 1 Hz / ≥ 10 Hz.
- Latency: M is 0.4 s of history, S 3 s, I and LRA are programme-long by definition; none needs look-ahead.
- Open: the histogram's upper bound must come from the K-filter's peak gain on two full-scale channels (compute it from the filter, do not pick it).

### 2.2 Perceived intensity and energy trend
- Loudness change, more than level, drives continuous tension: Farbood's model sums weighted slopes of feature curves over a moving attentional window, multiplied by a memory constant β when the preceding memory window's slope has the same sign (Farbood, Music Perception 29(4) 2012, eq. 1, read: https://bpb-us-e1.wpmucdn.com/wp.nyu.edu/dist/f/11865/files/2020/08/Farbood_2012_Musical_Tension.pdf).
- Fitted there: attentional window 3 s and memory window 3 s best; step h = 0.25 s; β = 5 ("values less than 5 resulted in much poorer results") (same paper).
- Feature weights there: dynamics and melody pitch height 3, tempo and onset frequency 2, harmony and other pitch heights 1, normalised (same paper); under the no-BPM-bias rule the tempo term is dropped, onset frequency stays.
- Fit: Pearson r .67 to .99 on 10 excerpts (Table 7, same paper).
- No memory window → accuracy hardly depends on the attentional window; any memory window helps sharply (same paper, Fig. 14).
- Design: intensity trend = Farbood-style slope of short-term LUFS (and onset rate, brightness) over the last 3 s, β-weighted by the previous 3 s; causal, cost O(1) per 0.25 s step with running least-squares sums (section 4).
- The 3 s windows agree with Tech 3342's short-term window; the overlap is a coincidence, not a derivation (stated so as not to over-claim).
- Arousal is dominated by loudness change in continuous ratings (Schubert, Music Perception 21(4) 2004, abstract via secondary summary; full text not read: https://sciencegate.app/document/10.1525/mp.2004.21.4.561). Unverified beyond the abstract.

### 2.3 Silence, return and surge
- Silence: Tech 3341 calls −70 LUFS the "absolute 'silence' gating threshold" (§2.3, read); BS.1770-5 uses the same −70 LKFS (read).
- Design: silence = momentary loudness below −70 LUFS (a standard number, not tuned); hysteresis and hold from measurement of the playback path's noise floor, never picked.
- Digital-zero capture test (`Silence.hpp:51`): the threshold should be the source format's 1 LSB (e.g. 2⁻¹⁵ for 16-bit PCM), a derived value, not 1e-6 (design inference).
- Return: the first block above the gate after a silent run; how long a run counts as silence should come from the run-length posterior in 2.5 (no hand-set 1 s).
- Surge: momentary loudness above the 95th percentile of the short-term distribution so far (Tech 3342's upper percentile), after the short-term was below its 10th percentile within the memory window; both bounds are the track's own (design inference from Tech 3342's percentiles; no surge paper found: open).
- Gain invariance: percentiles shift with the same dB offset as the signal, so "above the 95th percentile" is unchanged by playback volume (section 4).

### 2.4 Build-up and drop
- Definition (prior doc): a drop follows a build of tension and re-introduces the full bassline (Yadati et al., ISMIR 2014).
- Yadati's method, read now (https://archives.ismir.net/ismir2014/paper/000297.pdf): stage 1 chroma-based unsupervised segmentation (209 ms window, 139 ms hop) over the whole track; stage 2 a linear SVM on spectrogram statistics, 13 MFCCs (50 ms, 50% overlap) and rhythm features around each boundary.
- Its segmentation alone lands 2.5 s from the true drop on average and misses < 8% of drops (same paper).
- Data: 100 EDM tracks, 190 drops, 60/20/20 split (same paper).
- Result: mean F1 0.61 at ±3 s tolerance, 0.62 at 5 s, 0.71 at 15 s; best track 0.96, worst 0.20–0.52 (Table 3, same paper).
- It is offline: the segmentation reads the whole track; no causal latency was reported.
- A 2026 EDM structure model labels intro, build-up, drop, breakdown, outro, silence, end on 98 tracks: HR@0.5 0.616, HR@3 0.635, label accuracy 0.883; offline, real-time listed as future work; arXiv licence CC BY-NC-ND (https://arxiv.org/html/2603.08759v1, read). No per-label build/drop figures.
- Causal design: build = a sustained positive Farbood tension slope (2.2) whose feature set includes the bass band's share falling; drop = a changepoint (2.5) in the bass-band and short-term loudness that follows a build within the memory window.
- Latency of the causal design: the changepoint posterior needs some blocks after the change to commit (measure it; BOCPD gives the posterior every step).
- Tolerance to evaluate against: Yadati's ±3…15 s windows; ±5 s is what that paper calls "acceptable to listeners" (same paper, conclusion).
- Open: no causal drop detector with measured F1 was found.

### 2.5 Causal novelty and section boundaries
- Prior doc: novelty peaks (Chew 2014), causal lag matrices (Grill & Schlüter 2015), real-time listeners segment later and coarser (Hartmann et al.).
- Foote's checkerboard novelty (ICME 2000, pp. 452–455; secondary description: https://audiolabs-erlangen.de/resources/MIR/FMP/C4/C4S4_NoveltySegmentation.html) needs future frames for its "after" half; causal use delays every boundary by that half (as `Form.hpp:18-20` notes).
- Bayesian online changepoint detection: exact online posterior over the run length since the last change by message passing (Adams & MacKay 2007, read: https://arxiv.org/pdf/0710.3742).
- Its cost per step is linear in data seen; pruning run lengths whose tail mass is below a threshold gives constant average cost of order the expected run length (same paper, §2.4).
- Its hazard H(τ) is a prior on segment length; a constant hazard is a geometric gap prior (same paper, eq. 5).
- Under the owner's no-fixed-phrase rule the hazard must not encode 8 bars; options: a constant hazard learned online from the track's own past run lengths, or a hierarchical hazard (open: not evaluated on music here).
- Feature for the changepoint: the beat- or block-mean vector already built in `Form.cpp` (bands, centroid, flatness, chroma), with a conjugate-exponential (Gaussian) likelihood so each run's sufficient statistics are the (n, μ, M2) monoid of section 4.
- Boundaries then need no novelty full scale (`Form.cpp:125`), no 1.6× mean and no minimum bar count: the posterior P(r_t = 0) is the event strength.
- Pruning threshold: Adams & MacKay's example is 10⁻⁴ of tail mass; it bounds cost, not detection (same paper).

### 2.6 Recognising a section again (a returning chorus)
- Causal prediction with repetition detection: a self-distance matrix over past observations only, cosine distance; combined with autoregressive and N-gram predictors, 13% average improvement over a linear predictive baseline on 180 songs (Foster, Klapuri & Plumbley, ISMIR 2011, read: https://archives.ismir.net/ismir2011/paper/000108.pdf).
- Transposed returns: take the minimum over the 12 cyclic chroma shifts, which correspond to the 12 transpositions (Müller & Clausen, ISMIR 2007, read: https://archives.ismir.net/ismir2007/paper/000047.pdf).
- Chorus detection over whole songs, with modulated choruses: 80 of 100 songs (Goto, IEEE TASLP 14(5) 2006; secondary record only, offline).
- Design: on each changepoint, compare the new run's statistics with every stored run's (n, μ, M2) under the Z12 shift minimum; a match is "this new run is as close to a stored one as runs are to themselves", judged from the within-run spread the monoid already carries (e.g. a two-sample distance in units of pooled within-run variance).
- The cut-off on that distance is a B-parameter (no latency cost) to set on the accuracy–latency Pareto front of an annotated set (Stefani & Turchet, DAFx 2021, in the prior doc), not by hand.
- Stored runs merge with the Chan/Pébay pairwise formula when matched, so the centroid is an exact pooled mean, not a capped-count EMA (`Form.cpp:337`).
- Storage: one (n, μ, M2) per stored section; bound the count by memory, evicting the least recently matched (as today).

### 2.7 Section states (intro, verse, build, drop, breakdown, outro)
- Only offline labelled models were found (2.4); no causal state classifier with measured accuracy (open).
- Design without hand thresholds: each run's loudness as a percentile of the track's short-term distribution (2.3), its bass share as a percentile of its own history, the tension slope sign (2.2) and the changepoint posterior (2.5).
- Intro: runs before the first changepoint; outro: a run whose loudness percentile falls below the 10th with a negative slope (Tech 3342's 10th percentile names fade-outs).
- Build: positive tension slope with the β memory confirmation; drop: the changepoint after a build with the bass percentile back up.
- Breakdown vs verse: low vs middle loudness percentile with bass share low vs present; the split points are B-parameters for the Pareto tuning, not constants.
- Open: a causal HMM/forward filter over states needs transition probabilities; learning them online from the track risks a cold start, training them offline needs labelled data with a licence.

### 2.8 Adaptive normalisation and AGC without hand-set constants
- Running percentile rank (the empirical CDF of the feature's own past) maps any feature to 0..1, is unchanged by any gain (a monotone map), and needs no full scale (design inference; statistics, not a music paper).
- Its memory is the only choice: use the programme (integrated) for "how loud is this song" and the short-term 3 s window for "what is changing now", the two EBU time scales.
- Absolute "how much" must stay absolute (the owner's split, `DriveGain.hpp:5-10`): drive should read LUFS, with the range ends from standards: −70 LUFS (silence gate) and the integrated level of the track.
- Onset-style adaptive whitening (Stowell & Plumbley, ICMC 2007) normalises each bin by a decaying peak; full text not reachable (unverified).
- Loudness models with attack/release smoothing: short-term attack αa 0.045, release αr 0.02, long-term 0.01 / 0.0005, per 1 ms step, "chosen to give reasonable predictions" of loudness vs duration and AM sounds (Moore et al., Trends in Hearing 2016, read: https://pmc.ncbi.nlm.nih.gov/articles/PMC5318944): these are psychoacoustically fitted ballistics, a sourced alternative to the HARDCODED 0.5 s / 1.5 s drive lag.
- Conversion: α = 1 − exp(−Tᵢ/T) gives T ≈ 22 ms attack, 50 ms release (short-term); the long-term values give ≈ 100 ms and 2 s at Tᵢ = 1 ms (my arithmetic from the paper's formula; the paper gives coefficients, not ms, for the long-term stage).

### 2.9 Music emotion recognition (valence, arousal), real-time and continuous
- Benchmark: 1,802 songs/excerpts, dynamic arousal and valence at 2 Hz, metrics Pearson r and RMSE (DEAM; Aljanaki, Yang & Soleymani, PLoS ONE 12(3) 2017, read: https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0173392).
- There, LSTM-RNN solutions were best for both dimensions in 2013 and 2015 (same paper).
- "Valence is much more difficult to model than arousal"; for valence "satisfactory performance was not achieved by any of the algorithms" (same paper).
- Annotations converge around 13 s; the first 15 s are dropped; earlier work found median orientation times of 8 s (valence) and 12 s (arousal) (same paper).
- Consequence: a continuous emotion estimate has a measured human settling time of ~13 s; an output earlier than that has no ground truth to be judged against.
- Online model: an online (low-latency) RNN regression reached R² up to .70 arousal and .50 valence on the 2013 corpus (Weninger, Eyben & Schuller, ICASSP 2014, abstract via search summary; full text glyph-encoded, not read: https://opus.bibliothek.uni-augsburg.de/opus4/files/72577/72577.pdf). Secondary.
- Cue model (in use): six cues additive, no interactions after correction, adjusted R² .77–.89; mode sr² .48 happy, .54 sad; dynamics .14 and timbre .13 for peaceful; tempo second by median sr² (Eerola, Friberg & Bresin, Frontiers in Psychology 4 2013, read: https://www.frontiersin.org/journals/psychology/articles/10.3389/fpsyg.2013.00487/full).
- Those stimuli were systematically varied, perceived discrete emotions, not continuous ratings of real recordings; the weights do not transfer to arousal/valence as numbers.
- The tempo cue must stay out (owner's rule); the variance it explains there is lost, not reassigned by hand.
- Design: arousal from causal features with measured weights (loudness change per Farbood/Schubert, onset rate, brightness), fitted on DEAM's dynamic labels (licence: check per track, CC licences vary: open); valence from mode/scale with the DEAM-measured ceiling stated in the output's confidence.
- Model class for C++: a small unidirectional recurrent or linear-state-space regressor run every 0.5 s (DEAM's 2 Hz), weights shipped as data; cost and on-device latency unmeasured (open).

## 3. Polymorphic interface (signatures only; C++20, no allocation after `configure`)

```cpp
namespace geode::sound::form {

struct HopClock { int sampleRate; int hopSamples; };            // all windows derive from these
struct Block { double kPower; float bands[GEODE_SOUND_EQ_BANDS]; float centroid, flatness, onsetRate; const float* chroma; };

// Every running summary: a monoid with identity, optional inverse.
template <class S> concept Summary = requires(S a, const S b) {
    { S::identity() } -> std::same_as<S>;
    { a.combine(b) } -> std::same_as<S&>;                         // associative
};
template <class S> concept InvertibleSummary = Summary<S> && requires(S a, const S b) {
    { a.remove(b) } -> std::same_as<S&>;                          // (a ⊕ b) ⊖ b = a
};

class LoudnessMeter {                                             // BS.1770 / Tech 3341 / 3342
public:
    virtual ~LoudnessMeter() = default;
    virtual void configure(const HopClock&) = 0;
    virtual void pushSamples(const float* left, const float* right, int frames) = 0;
    virtual double momentaryLufs() const = 0;
    virtual double shortTermLufs() const = 0;
    virtual double integratedLufs() const = 0;
    virtual double loudnessRangeLu() const = 0;
    virtual double percentileOfShortTerm(double lufs) const = 0;  // 0..1, gain-invariant
};

class TrendEstimator {                                            // Farbood-style slope with memory
public:
    virtual ~TrendEstimator() = default;
    virtual void configure(const HopClock&, double attentionSeconds, double memorySeconds) = 0;
    virtual void push(std::span<const float> features, double dtSeconds) = 0;
    virtual float trend() const = 0;                              // signed
};

class ChangeDetector {                                            // e.g. online Bayesian run-length
public:
    virtual ~ChangeDetector() = default;
    virtual void configure(int dims, std::size_t maxRunHypotheses) = 0;
    virtual void push(std::span<const float> x) = 0;
    virtual float changeProbability() const = 0;                  // P(run length = 0)
    virtual double expectedRunSeconds() const = 0;
};

class SectionMemory {                                             // returning sections
public:
    virtual ~SectionMemory() = default;
    virtual int match(const RunStats& run) = 0;                   // id, or a new id
    virtual void absorb(int id, const RunStats& run) = 0;         // pairwise merge
};

class SectionStateModel {
public:
    virtual ~SectionStateModel() = default;
    virtual int step(const RunStats& run, float trend, float changeProbability, double loudnessPercentile) = 0;
};

class Normaliser {                                                // gain-invariant 0..1
public:
    virtual ~Normaliser() = default;
    virtual void push(float x) = 0;
    virtual float rank(float x) const = 0;
};

class AffectEstimator {                                           // continuous arousal / valence
public:
    virtual ~AffectEstimator() = default;
    virtual void configure(double outputRateHz) = 0;
    virtual void push(const GeodeSoundFrame& f, double dtSeconds) = 0;
    virtual float arousal() const = 0;
    virtual float valence() const = 0;
    virtual float confidence() const = 0;                         // 0 until the measured settling time
};

}  // namespace geode::sound::form
```

- Interchangeable implementations: `LoudnessMeter` {exact histogram, quantile sketch}; `TrendEstimator` {Farbood slopes, two-EMA ratio}; `ChangeDetector` {run-length posterior, causal checkerboard}; `Normaliser` {fixed-bin histogram, quantile sketch}; `AffectEstimator` {cue regression, recurrent regressor}.
- `RunStats` is the (n, μ, M2) vector monoid of section 4; virtual dispatch is per block (≥ 10 Hz), not per sample, so its cost is negligible (design inference).

## 4. Algebraic structure

- A monoid is an associative binary operation with an identity; the mean becomes associative as the pair (sum, count) with element-wise sum and identity (0, 0) (Lin, "Monoidify!", arXiv:1304.7544, read: https://arxiv.org/pdf/1304.7544).
- Mean and variance as a monoid: for sets S₁, S₂ with n = n₁ + n₂ and δ = μ₂ − μ₁, μ = μ₁ + n₂δ/n and M₂ = M₂,₁ + M₂,₂ + n₁n₂δ²/n (Chan, Golub & LeVeque 1979, as given in Pébay, SAND2008-6212 eqs 1.3–1.4, read: https://www.osti.gov/servlets/purl/1028931).
- Welford's update is the special case where S₂ is a single point (Pébay eqs 1.1–1.2, read); naive one-pass moments are numerically unstable (Meng, arXiv:1510.04923, read).
- Pairwise formulas exist for arbitrary-order central moments and covariance (Pébay, read), so a section's feature covariance is also mergeable.
- Sliding windows: an invertible combine "enables handling deletions as inverse insertions", the standard incremental approach; associativity alone allows O(log n) per update with a balanced tree for non-invertible ones such as max (Tangwongsan, Hirzel, Wu et al., PVLDB 8 2015, read: https://www.vldb.org/pvldb/vol8/p702-tangwongsan.pdf).
- Power sums (`LufsMeter`) and histogram counts are groups, so subtract-on-evict is exact in exact arithmetic; in float it drifts (hence `LufsMeter.hpp:77`'s clamp); an integer fixed-point accumulator makes the group exact (design inference).
- Peak holds and maxima (`SoundEngine.cpp:467`) are a semilattice, not a group: windowed maxima need the tree or a two-stack scheme, not subtraction.
- Exponential smoothing is an affine map y ↦ a·y + (1 − a)·x; first-order recurrences xᵢ = (xᵢ₋₁ ⊗ aᵢ) ⊕ bᵢ reduce to a scan with the associative pair operator [a, b]•[a′, b′] = [a·a′, b·a′ + b′] (Blelloch, "Prefix Sums and Their Applications", 1990, §1.4 eqs 1.5–1.7, read: https://www.cs.cmu.edu/~guyb/papers/Ble93.pdf).
- With a = exp(−Δt/τ) the decay factors form the semigroup (ℝ≥0, +) acting on the state: two steps Δt₁, Δt₂ of constant input equal one step Δt₁ + Δt₂, so the output does not depend on hop or frame size (`Stages.hpp:26` already has this form).
- Per-beat EMAs with a fixed rate (`Form.cpp:292`, `:296`) break that invariance: their time constant is in beats, so tempo changes it (design inference; a mild BPM coupling).
- Gain: a gain g acts by the multiplicative group (ℝ>0, ×) on amplitude; log maps it to (ℝ, +), so every LUFS value shifts by 20·log₁₀ g and every difference (M − S, LRA, percentile rank) is invariant.
- Percentile rank is invariant under every strictly increasing map, of which gain in dB is one: the strongest invariance available for a normaliser.
- Time shift: a lag representation compares each moment with its own past at fixed lags, so a repeated passage gives the same pattern wherever it occurs (translation by ℤ on the time index; prior doc's lag matrices).
- Transposition: the cyclic group ℤ₁₂ acts on chroma by rotation; the minimum over the 12 shifts is a transposition-invariant similarity (Müller & Clausen 2007, read).
- Section matching is thus invariant under ℤ (time) × ℤ₁₂ (key) × ℝ>0 (gain).
- Run-length posterior: each run hypothesis carries a conjugate sufficient statistic, which is the (n, μ, M2) monoid; extending a run is the Welford step (Adams & MacKay §2.3 conjugate-exponential models, read).

## 5. Dynamic values and where each comes from

- Momentary window 0.4 s: Tech 3341 §2.2.
- Short-term window 3 s, update ≥ 10 Hz: Tech 3341 §2.2.
- Integrated update ≥ 1 Hz: Tech 3341 §2.2.
- Gating block 400 ms, 75% overlap (100 ms step), absolute −70 LUFS, relative −10 LU: BS.1770-5 §2.
- LRA: 3 s windows at ≥ 10 Hz, −70 LUFS, −20 LU, 10th–95th percentiles: Tech 3342 §3.1.
- Histogram bin width ≤ 0.1 LU: from the ±0.1 LU compliance tolerance (Tech 3341 §2.9).
- Silence level −70 LUFS: Tech 3341 §2.3 "silence" gate.
- Digital-zero threshold: 1 LSB of the source sample format (derived).
- Tension windows 3 s + 3 s, step 0.25 s, β = 5: Farbood 2012 (fitted to listener data).
- Loudness ballistics, if smoothing is wanted outside EBU Mode: Moore et al. 2016 coefficients (chosen to fit loudness data).
- Surge and section-state split points: the track's own percentiles; which percentile is a B-parameter from Pareto tuning (prior doc's method).
- Changepoint hazard: learned online from the track's own run lengths (open: method not yet chosen).
- Run-pruning mass 10⁻⁴: Adams & MacKay's example; affects cost only.
- Section-match cut-off: within-run spread from the monoid, scaled by a Pareto-tuned B-parameter.
- Emotion output rate 2 Hz and settling time ~13 s: DEAM (measured).
- Emotion weights: fitted on DEAM dynamic labels (measurement), never typed in.
- Nothing above reads the tempo; nothing assumes a meter or a phrase length.

## 6. Risks and open questions

- Tech 3341 forbids extra smoothing in EBU Mode; any eased drive is a separate, documented non-EBU field.
- Integrated loudness on a live stream has no programme start; a track change or a long silence must reset it (the app already detects both).
- LRA needs minutes of programme to settle; early in a track percentile-based events are noisy (measure how long).
- Float subtract-on-evict drifts over hours; prefer integer accumulation or periodic recomputation.
- Changepoint posterior latency on music is unmeasured; it may exceed the one-bar delay of today's checkerboard.
- A learned hazard can still carry a soft prior on section length; check that it adapts to non-4/4 and irregular forms.
- No causal drop or section-state detector with measured F1 was found; Yadati's 0.61 at ±3 s is offline.
- The 2026 EDM structure model is CC BY-NC-ND (paper) with no code or data licence: method only, nothing to ship.
- DEAM audio licences differ per track; check before any training use.
- Valence ceiling: DEAM's authors call valence performance unsatisfactory; the app should not present valence as reliable.
- The EmotionTable's generator inputs were not re-verified in this pass.
- The Weninger R² figures, Schubert's arousal–loudness result, Goto's 80/100 and the adaptive-whitening rule were read only as abstracts or secondary summaries.
- Farbood's β and weights were fitted on 10 excerpts, partly classical; transfer to popular and electronic music is untested.
- Removing the tempo cue from emotion and tension loses explained variance that the literature attributes to it; that loss is accepted, not compensated.
- On-device cost of a recurrent affect model and of a run-length posterior with many dimensions is unmeasured.
