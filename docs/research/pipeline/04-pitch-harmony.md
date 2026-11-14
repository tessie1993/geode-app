# Stage 4: pitch and harmony (research, 2026-11-14)

Scope: lead pitch, predominant melody, multi-pitch and note events, tuning reference, chroma, key and modulation, chords, scales and modes, tension, all causal and pushed per source.
Builds on `docs/research/music-analysis.md` (YIN/pYIN, harmonic-sum multi-F0, online melody, HPCP, key profiles, real-time chords); none of that is repeated here.
Evidence rule: each line names the primary source that was fetched and read; "derived" marks algebra worked from a cited definition (no measurement); "unverified" and "open" mark the rest.
Clean room: methods and maths only; no code, weights, names or strings from any project; systems are cited by author and venue.
Colour: every angle below (fifths phase, key angle, chord angle) is a number in 0..1 or radians; which colour it drives is the style's and the user's, and this doc prescribes none.
Tempo: no window, threshold or prior in this stage may come from a tempo; none below does.

## 1. How the app does it now

### Front end
- The analysis rate is the input rate halved while at or above 88.2 kHz (`core/sound/SoundEngine.cpp:94`).
- The window FFT is the smallest power of two at or above 40 ms of the analysis rate (`core/sound/SoundEngine.cpp:88-92`); 40 ms is a hand-picked constant.
- Pitch and harmony share one "long" spectrum, twice that FFT, Hann-windowed (`core/sound/SoundEngine.cpp:116-118`, `:761-764`): 4096 points, 85 ms at 48 kHz, bins 11.7 Hz; 92.9 ms at 44.1 kHz, bins 10.8 Hz (derived from the code).
- Harmony is configured on that spectrum's bin width (`core/sound/SoundEngine.cpp:172`) and stepped every hop from it (`core/sound/SoundEngine.cpp:951`).

### Lead pitch (`SoundEngine::estimatePitch`, `core/sound/SoundEngine.cpp:761-819`)
- A harmonic sum of the bin plus four partials weighted 1/h; `kHarmonics = 5` is hand-picked (`:758`, `:787`).
- Search range 80 Hz to 1.5 kHz, hand-picked (`:767-768`).
- A held-note continuity penalty: candidates beyond two semitones are divided by `1 + (d - 2)/24`, hand-picked (`:792`).
- Confidence is `(best/mean - 3)/7` clamped, hand-picked (`:801`).
- The bin is refined by parabolic interpolation and converted to MIDI against a fixed 440 Hz (`:808`); no tuning reference is estimated anywhere.
- Evenness: the even partials' share of the first six, six hand-picked (`:813`).

### Note events (`SoundEngine::melody`, `core/sound/SoundEngine.cpp:821-909`)
- Voiced when confidence ≥ the user slider `pitchThreshold` (default 0.35, `core/sound/Tuning.cpp:36`).
- A new note must hold `noteHold` seconds (default 0.08, `core/sound/Tuning.cpp:37`), longer for leaps: `hold·(1 + (leap - 2)/5)` (`:874`), hand-picked.
- Octave slips fold back unless held 0.25 s, within 0.75 semitone (`:842-844`), hand-picked.
- Note hysteresis: within 0.75 semitone of the held note stays that note (`:862`).
- Legato: a note change more than 0.12 s after the last attack (`:881`); re-attack: same note with a level jump and 0.1 s spacing (`:884`); the level jump is `rms > 2·silence` and punch > 0.25 (`:827`); all hand-picked.
- Glide: the lag of a 60 ms follower, snapped on jumps over two semitones, scaled to 24 semitones/s (`:855-856`, `:905`), hand-picked.
- Vibrato: deviation from a 150 ms mean, half a semitone reads 1 (`:890-891`), hand-picked.
- Pitch height maps C2..C6 to 0..1 with an 80 ms follower (`:849`); a note is dropped after 0.15 s unvoiced (`:897`); run density is changes/s over 12 (`:908`).
- `core/sound/Tuning.*` is the user's listening-tuning slider schema (gain, thresholds, holds), not a musical tuning reference.

### Chroma (`Harmony::step`, `core/sound/Harmony.cpp:177-253`)
- Band 250 Hz to 2 kHz, fixed (`core/sound/Harmony.cpp:23-24`); the comment justifies 250 Hz from a semitone at 250 Hz being wider than the bin (`core/sound/Harmony.hpp:27-28`), but the constant is not recomputed from the actual bin width.
- Only local maxima above 3% of the frame peak count (`core/sound/Harmony.cpp:192`), hand-picked.
- Each peak goes to its nearest equal-tempered class against 440 Hz (`core/sound/Harmony.cpp:199-200`).
- A peak with a strong partner at 1/3, 1/5, 1/6, 1/7 of its frequency is weighted 0.25 unless its own lower octave sounds (`core/sound/Harmony.cpp:207-219`); 0.25 and the 0.5 partner ratio are hand-picked.
- Log compression `log1p(20·m/top)` (`core/sound/Harmony.cpp:220`); 20 is hand-picked.
- Three one-pole chromas: fast 0.1 s, medium 0.5 s, slow 4 s (`core/sound/Harmony.cpp:230-232`), hand-picked, with a running-mean warm-up (`:234-236`).
- Tonality: power on picked peaks over band power (`core/sound/Harmony.cpp:247-248`).

### Key (`core/sound/Harmony.cpp:261-291`)
- Pearson correlation of the slow chroma with 24 rotated profiles (`:264-276`); the profiles were chosen by an in-repo bench measurement (MIREX key score 0.82-0.84 vs 0.69-0.79 for probe-tone ratings, comment at `:11-14`).
- A rival key must lead by 0.03 for 2 s (`:279-283`), hand-picked.
- Major/minor mood `0.5 + 2·(bestMajor - bestMinor)` with a 2 s follower (`:291`), hand-picked.
- `keyHue` is the key's position on the circle of fifths, a minor key placed on its relative major (`:355`), via `hueOf(pc) = 7·pc mod 12` (`:78`).

### Chord, tension, harmonic change
- `chordHue`: the angle of the above-mean medium chroma on the circle of fifths (`core/sound/Harmony.cpp:295-310`); no chord is recognised.
- `tension`: a pairwise interval-class roughness table `{0, 1, .55, .15, .1, .05, .65}` (`core/sound/Harmony.cpp:21`) weighted by both levels (`:312-322`), mapped by `(t - 0.1)/0.4` with a 0.3 s follower (`:323`); the table and mapping are hand-picked.
- Harmonic change: 0.1 s snapshots, the last 5 against the 20 before (`core/sound/Harmony.hpp:43-44`, `core/sound/Harmony.cpp:327-341`), trigger thresholds 0.15 and 0.5 (`:342`), hand-picked.

### Scale and mode (`Harmony::detectScale`, `core/sound/Harmony.cpp:90-158`)
- 46 catalogue scales × 12 roots, Pearson correlation with each mask (`:107-128`), plus a "home" pull `0.5·root + 0.25·fifth` (`:129`), hand-picked.
- The chroma is capped at twice its mean first (`:100`), hand-picked.
- Every 0.25 s (`:344`); a rival must lead by 0.03 for 2 s (`:143-149`); fit mapped `(fit - 0.3)/0.5` (`:157`), hand-picked.
- Each scale's brightness and tension are design values, not measurements (`tools/musical_intelligence.py:18-20`, `core/sound/ScaleTable.inc:3-48`).

## 2. Optimal design per sub-task

### Tuning reference
- Method: treat each peak's (or each lead-pitch frame's) deviation from the semitone grid as a unit vector, angle 2π·c/100 for c cents; the weighted vector mean's phase is the deviation and its length the confidence (Dressler & Streich, ISMIR 2007, https://archives.ismir.net/ismir2007/paper/000357.pdf).
- Causal form: an exponential moving average of those vectors, normalised by the same average of the weights (same paper, eqs. 7-10).
- Cost: one complex multiply-add per peak per hop; storage two numbers (same paper: "efficiently computable, consume only minimal storage").
- Measured: qualitative only; on ten ISMIR 2004 pieces the confidence was 0.71-0.99 on synthetic, MIDI and jazz and 0.11-0.50 on opera and pop, where sung vibrato corrupts the estimate (same paper, Table 1).
- The paper's half-lives (0.5 s and 2 s) are its choices, not derived; here the half-life is an A-parameter to be tuned on data (Pareto method, music-analysis.md §2).
- Peaks of the accompaniment gave higher confidence than the melody on opera (same paper, §3.2): feed the estimator spectral peaks weighted by stability, not the voice alone.
- Derived: the circular mean is the Fourier coefficient of the pitch distribution on the octave circle at the index equal to the number of steps per octave (12 for 12-TET, n for n-EDO); the same code serves any equal tuning by changing n.
- Open: unequal and non-octave tunings; no source here evaluates them.

### Lead pitch (monophonic or dominant voice)
- A convolutional estimator on 1024 samples at 16 kHz (64 ms), 360 output bins of 20 cents, reached raw pitch accuracy 0.967 on MDB-stem-synth vs 0.919 for pYIN at 50 cents, and 0.909 vs 0.826 at 10 cents (Kim, Salamon, Li & Bello, ICASSP 2018, https://arxiv.org/pdf/1802.06182, Tables 1-2).
- The same paper: it beats pYIN whenever SNR is below 10 dB.
- Latency: one 64 ms window, no look-ahead beyond it (same paper, Fig. 1); its parameter count and phone cost were not read: open.
- For a full mix this is a monophonic method; it needs the predominant-melody stage in front (below).
- The app's harmonic sum is Klapuri's family (music-analysis.md); its constants (§1) should become B-parameters tuned on annotated data, not hand values.

### Predominant melody in polyphonic audio
- Contour method (sinusoids, salience over 600 bins of 10 cents from 55 Hz, contour creation and contour-feature selection): MIREX 2011 mean overall accuracy 0.75 (Salamon & Gómez, IEEE TASLP 2012, https://www.justinsalamon.com/uploads/4/3/9/4/4394963/salamongomezmelodytaslp2012.pdf, Table III).
- It is not causal: contour features and the melody pitch mean are computed over whole contours ; window 46.4 ms, hop 2.9 ms (same paper).
- The causal reference remains the online harmonic-cluster tracker already cited (Arora & Behera, TASLP 2013, music-analysis.md §5); its accuracy and latency were not read: open.
- Phone: the salience stage alone (harmonic summation per bin) is a fixed per-hop cost; no on-device measurement exists: open.

### Polyphonic multi-pitch and note events
- A 16,782-parameter network on a CQT with 3 bins per semitone and ~11 ms hop outputs onset, note and pitch posteriorgrams; the 3-bin pitch head carries bends (Bittner et al., ICASSP 2022, https://arxiv.org/pdf/2203.09893).
- Its harmonic stacking (CQT shifted by harmonic intervals) is what lets so few parameters work; removing it dropped every score (same paper, Table 2, ablation "−H").
- Measured note F-measure without offsets: GuitarSet .79, Molina vocals .52, MAESTRO piano .71 (vs .952 for an 18 M-parameter piano-only model) (same paper, Tables 2-3).
- Not causal as published: note creation traces forward and backward from onsets and drops notes under ~120 ms (same paper); the convolutions' look-ahead is not stated: open.
- Its CPU benchmark is a desktop (7 min 45 s file in 24 s, 951 MB peak) (same paper, §4.5); phone cost: open.
- Causal alternative with measured results: non-negative decomposition onto fixed note templates with β-divergence (β = 0.5), 50 ms frames, 10 ms hop (Dessein, Cont & Lemaitre, ISMIR 2010, https://hal.science/hal-00708682/document, §5).
- Measured on 25 MAPS piano excerpts: frame F 65.5; note F 71.1 on onsets, 28.2 with offsets; about 3× faster than real time in MATLAB on a 2.4 GHz laptop (same paper, Tables 1-2).
- Its detection thresholds (1 and 2) and 50 ms minimum duration were set empirically (same paper, §5.2): B-parameters to tune.
- Templates are instrument-specific (piano); for a mix this is a candidate, not a solution: open.

### Note articulation: re-attack, legato, vibrato, glide
- Re-attack is an onset while the same pitch is active; legato is a pitch change with no onset; both fall out of separate onset and pitch evidence (onset head plus pitch head, Bittner et al. 2022 above), not of hand timings.
- Vibrato rate and extent: fit the strongest sinusoid in the pitch curve per 0.125 s window, quarter-window step, then classify presence (Yang, Rajab & Chew, ISMIR 2016, https://archives.ismir.net/ismir2016/paper/000314.pdf).
- That window needs only past pitch frames: it is causal at 125 ms latency (derived from the paper's window).
- Its detection accuracy was not in the part read: open.
- Glide (portamento) there is detected by an HMM and fitted with a logistic curve whose inflection gives the transition time (same paper); the fit uses the whole transition, so it is offline.
- Causal glide: the slope of the pitch curve from past frames only; no measured comparison found: open.

### Chroma
- A tuned 36-bins-per-octave chroma from a constant-Q transform over 110-3520 Hz needed a 743 ms window for the lowest bins (Harte, Sandler & Gasser, ACM AMCMM 2006, https://typeset.io/pdf/detecting-harmonic-change-in-musical-audio-3ixqjqrcno.pdf; ACM venue, third-party copy).
- Derived: separating two partials a semitone apart at frequency f needs bin spacing below f·(2^(1/12) − 1) ≈ 0.0595·f; so the lowest usable chroma frequency is the bin width divided by 0.0595, computed from the live FFT, never a constant.
- A learned chroma from ±0.7 s of centred context reached 79.0% major/minor WCSR, above hand-made chromas (Korzeniowski & Widmer, ISMIR 2016, https://arxiv.org/pdf/1612.05065); it has 0.7 s look-ahead, so not causal.
- The same paper: logistic regression on a chroma derived from ground-truth labels recognised 97% of major/minor chords: given clean chroma, the classifier is not the bottleneck.
- The chroma's reference must be the tuning estimate above, not 440 Hz.

### Harmonic change and tonal centroid
- The tonal centroid maps chroma to 6-D: circle of fifths, minor thirds and major thirds with radii 1, 1, 0.5 (Harte et al. 2006 above, eq. 3).
- Derived from that equation: the three circles advance 7π/6, 3π/2 and 2π/3 per semitone, i.e. they are the DFT coefficients k = 7, 9, 4 of the chroma, the conjugates of f5 and f3, and f4, divided by the L1 norm (f0).
- The radii are copied from a symbolic model, not measured on audio (same paper: "derived using the same approach that Chew uses").
- The change function is the distance between centroids at n−1 and n+1 after Gaussian smoothing with σ = 8 frames (same paper, eq. 4): non-causal by design.
- Measured: chord-change F 64.9% at ±3 frames (278 ms) on 16 songs, precision 53%, recall 84% (same paper, Table 1).
- Causal form: compare the current centroid with a past-only average; the app's snapshot method (§1) is already of this kind; its accuracy against the centred form is unmeasured: open.

### Key and modulation
- Distributional key finding works, through the DFT, as: f5's phase picks the diatonic collection, f3's or f2's phase picks the mode (Yust, MCM 2017 "Probing questions about keys", https://people.bu.edu/jyust/tonalDist_rev.pdf, §3).
- Corpus distributions have large |f5|, then |f3|, then |f2|; |f1|, |f4|, |f6| are small and their phases inconsistent (same paper, §2).
- Correlation is biased toward major when only f5 survives; Euclidean distance is not (same paper, §3); the app uses Pearson correlation (§1), so it carries that bias.
- The tonal index Ph2 + Ph3 − Ph5 stays near 0 through tonal music, is 0 for unisons, fifths and diatonic sets and ±0.62 for major and minor triads (same paper, §4).
- An end-to-end network with time averaging scored MIREX-weighted 74.3 on GiantSteps and 83.9 on Billboard for the global key (Korzeniowski & Widmer, EUSIPCO 2017, https://arxiv.org/pdf/1706.02921).
- Its training shifted each song by −4 to +7 semitones, the full transposition orbit, multiplying data by 12 (same paper).
- Trained on pop and tested on electronic music its score fell to 57.3 (same paper): genre transfer is a risk for any learned key model.
- Sliding-window key tracking needs a fixed, predetermined window (İzmirli, ISMIR 2007, https://archives.ismir.net/ismir2007/paper/000195.pdf, §1); that paper segments offline.
- A causal key tracker with measured modulation latency was not found: open.

### Chords beyond major and minor
- Vocabulary of 170 classes: 12 roots × 14 qualities (maj, min, dim, aug, maj6, min6, 7, maj7, min7, minmaj7, dim7, hdim7, sus2, sus4) plus no-chord and unknown (McFee & Bello, ISMIR 2017, https://archives.ismir.net/ismir2017/paper/000077.pdf, §3.2).
- Decomposing the target into root, bass and active pitch classes ("structured training") raised accuracy most on sevenths and tetrads (same paper).
- Measured median weighted recall: root .861, triads .812, sevenths .729, tetrads .671, maj-min .855 (same paper, Table 1, best model).
- Its encoder uses a bidirectional recurrent layer: not causal; a forward-only version's accuracy is unmeasured: open.
- The causal real-time template method stays the reference for live use (Stark & Plumbley, ICMC 2009, music-analysis.md §5); its large-vocabulary accuracy: open.

### Scales and modes
- Derived: the seven church modes are one pitch-class set with seven choices of root; a mode is the pair (set, root ∈ set), so set detection and root detection are separate questions.
- Set detection by f5 phase and magnitude (Yust 2017 above); root (home note) by the profile or the bass, not by a hand weight.
- Derived: a scale with transpositional symmetry has fewer than 12 distinct transposes (whole-tone 2, augmented 4, diminished 3, chromatic 1), by the orbit-stabilizer theorem (Crans, Fiore & Satyendra, Amer. Math. Monthly, https://arxiv.org/pdf/0711.1873, Theorem 4.1); the catalogue loop should score each distinct set once.
- Derived: a diatonic mode's brightness order (Lydian … Locrian) is the root's position along the line of fifths within the set, readable from f5's phase relative to the root's; the hand-set brightness values (`tools/musical_intelligence.py:18-20`) can be computed instead.
- Open: whether that computed order matches listeners' brightness ratings; no perceptual source was read.

### Tension and dissonance
- Interval-content dissonance as one minus the weighted DFT magnitude norm of the chroma, weights (3, 8, 11.5, 15, 14.5, 7.5) for k = 1..6 (Ramires et al., DAFx 2020, https://arxiv.org/pdf/2008.11529; Almeida, Bernardes & Weiß, ISMIR 2022, https://archives.ismir.net/ismir2022/paper/000024.pdf, eq. 1-2).
- Those weights come from empirical dissonance ratings of intervals and triads (Almeida et al. 2022, §2.3); the DAFx authors list an optimal weighting as future work, so it is not settled.
- Derived: the app's pairwise interval-class tension is a quadratic form in the chroma's interval function; the DFT of the interval function is |f_k|² (Yust, MCM 2015, https://people.bu.edu/jyust/MCM2015.pdf, eq. 6); so the app's tension equals a weighted sum of the six |f_k|², 6 multiply-adds instead of 66 pairs.
- So both measures are invariant under transposition and inversion and cannot tell a major from a minor triad (inversion maps one to the other, Crans et al. above, §4).
- Spiral-array tension (cloud diameter, momentum, tensile strain) was shown on scores against one listener study, qualitatively (Herremans & Chew, TENOR 2016, https://www.dorienherremans.com/sites/default/files/paper_tenor_dh_preprint_small.pdf); no audio measurement: open.
- Spectral-peak roughness (Plomp & Levelt curves, Sethares) would depend on the actual timbre rather than on 12 classes; the primary papers were not read: unverified.

### What fits a phone today (derived counts, not measurements)
- Tuning circular mean, 12-bin chroma, its 6 Fourier coefficients, key by 24 phase distances, tension by 6 magnitudes: each well under a thousand multiply-adds per hop, negligible beside the FFT.
- Note templates by non-negative decomposition: one matrix-vector product per iteration (templates × bins); cost scales with template count; measured only on a laptop: open on phone.
- The 16,782-parameter multi-pitch network is small in parameters; its CQT and convolutions per frame were not costed: open; on-device runtime path per music-analysis.md §5 (LiteRT).
- Learned models need our own training on permissively licensed data; dataset licences were not checked: open.

## 3. Polymorphic interface (signatures only)

```cpp
namespace geode::sound::pitch {

struct StreamFormat { int sampleRateHz; int hopSamples; };
struct Peak { float hz; float magnitude; float stability; };
struct SpectrumView { const float* mag; int bins; float binHz; std::int64_t endSample; };
struct HopInput { const float* mono; int samples; SpectrumView spectrum; std::span<const Peak> peaks; };

template <int N> struct PcVector { std::array<float, N> v; };          // a distribution on Z_N
template <int N> struct PcFourier { std::array<std::complex<float>, N / 2 + 1> f; };

struct TuningState { float centsOffset; float confidence; int stepsPerOctave; };
struct PitchFrame { float midi; float confidence; bool voiced; };
struct PitchSalience { float midi; float salience; };
struct NoteEvent { int id; float midi; float bend; std::int64_t onSample; std::int64_t offSample; enum Kind : std::uint8_t { Onset, Reattack, Legato, Offset } kind; };
struct Articulation { float vibratoRateHz; float vibratoExtent; float glideSemitonesPerSec; };
struct KeyState { int tonic; int mode; float confidence; float fifthsAngle; };
struct ChordState { int root; int bass; std::uint16_t pcs; int quality; float confidence; };
struct ScaleState { std::uint16_t set; int root; float fit; };
struct TensionState { float value; std::array<float, 6> magnitudes; };

class Stage {
public:
    virtual ~Stage() = default;
    virtual void configure(const StreamFormat& fmt) = 0;          // allocates
    virtual void reset() = 0;
    virtual std::int64_t latencySamples() const = 0;
    virtual const char* id() const = 0;
};

class TuningEstimator : public Stage { public: virtual void push(std::span<const Peak> peaks, float dt) = 0; virtual TuningState state() const = 0; };
class LeadPitchTracker : public Stage { public: virtual void push(const HopInput& in, const TuningState& t) = 0; virtual PitchFrame current() const = 0; };
class MelodyExtractor : public Stage { public: virtual void push(const HopInput& in, const TuningState& t) = 0; virtual PitchFrame current() const = 0; };
class MultiPitchEstimator : public Stage { public: virtual void push(const HopInput& in, const TuningState& t) = 0; virtual std::span<const PitchSalience> current() const = 0; };
class NoteTracker : public Stage { public: virtual void push(std::span<const PitchSalience> pitches, float onsetEvidence, std::int64_t endSample) = 0; virtual std::span<const NoteEvent> events() const = 0; virtual Articulation articulation() const = 0; };
template <int N> class ChromaExtractor : public Stage { public: virtual void push(const HopInput& in, const TuningState& t) = 0; virtual const PcVector<N>& current() const = 0; };
template <int N> class KeyTracker : public Stage { public: virtual void push(const PcFourier<N>& f, float dt) = 0; virtual KeyState state() const = 0; };
template <int N> class ChordRecognizer : public Stage { public: virtual void push(const PcVector<N>& treble, const PcVector<N>& bass, float dt) = 0; virtual ChordState state() const = 0; };
template <int N> class ScaleEstimator : public Stage { public: virtual void push(const PcFourier<N>& f, const KeyState& k, float dt) = 0; virtual ScaleState state() const = 0; };
template <int N> class TensionMeasure : public Stage { public: virtual void push(const PcFourier<N>& f, std::span<const Peak> peaks) = 0; virtual TensionState state() const = 0; };

template <int N> PcFourier<N> fourier(const PcVector<N>& c);                  // non-virtual: the algebra is fixed
template <int N> PcVector<N> transpose(const PcVector<N>& c, int n);           // T_n
template <int N> PcVector<N> invert(const PcVector<N>& c, int n);              // I_n
template <int N> PcVector<N> multiply(const PcVector<N>& c, int unit);         // M_a, gcd(a, N) = 1

}  // namespace geode::sound::pitch
```

- Every `push` is called from the source's analysis thread as hops arrive, never allocates and never blocks (music-analysis.md §3).
- `latencySamples()` lets the frame graph align stages by sample index instead of by GL frame.
- Interchangeable implementations per interface: circular-mean tuning; harmonic-sum, learned or YIN-family lead pitch; contour or cluster melody; template-decomposition or learned multi-pitch; profile-distance, Fourier-phase or learned key; template or learned chords.

## 4. Algebraic structure

### Pitch classes and the dihedral group
- Pitch classes in 12-TET are Z_12; transposition T_n(x) = x + n and inversion I_n(x) = −x + n generate the dihedral group of order 24 (Crans, Fiore & Satyendra, https://arxiv.org/pdf/0711.1873, §3).
- Composition rules: T_m∘T_n = T_{m+n}, T_m∘I_n = I_{m+n}, I_m∘T_n = I_{m−n}, I_m∘I_n = T_{m−n} (same paper, §3).
- The same holds for any Z_N; the paper notes diatonic transposition and inversion as Z_7 (same paper, §3); for n-EDO use Z_n and the dihedral group of order 2n (derived).
- Fine chroma with b bins per semitone is Z_{12b}; transposition by one bin is a rotation there; a tuning offset is a rotation of the fine circle (derived).

### Neo-Riemannian group and the Tonnetz
- P, L, R are contextual inversions: P⟨y1,y2,y3⟩ = I_{y1+y3}, L = I_{y2+y3}, R = I_{y1+y2} applied to the triad (same paper, eqs. 3-5).
- The PLR group is generated by L and R and is dihedral of order 24 (same paper, Theorem 5.1); it acts simply transitively on the 24 major and minor triads (Corollary 5.2).
- The T/I group and the PLR group are dual: each is the centralizer of the other in Sym of the triads (same paper, Theorem 6.1).
- The PLR group's geometry is a tiling of the torus, the Tonnetz (same paper, §5).
- Consequence (derived from Theorem 6.1): the PLR element taking one recognised triad to the next commutes with every transposition and inversion, so "what kind of move" (P, L, R, LR, …) is a key-independent feature, available per chord change at no extra cost.

### Circle of fifths as an automorphism
- Derived: x ↦ 7x is an automorphism of Z_12 because gcd(7, 12) = 1; it maps the chromatic order 0,1,2,… to the fifths order 0,7,2,…; the units {1, 5, 7, 11} are all of Aut(Z_12).
- The app's `hueOf(pc) = 7·pc mod 12` (`core/sound/Harmony.cpp:78`) is exactly this map.
- Derived from the DFT definition (Yust 2015, eq. 1): the coefficients of the multiplied distribution are f_k(M_a c) = f_{a·k}(c); with a = 7 this swaps |f1| and |f5| and keeps |f2|, |f3|, |f4|, |f6|.
- For Z_N, a "circle of g" exists exactly when g is a unit mod N (derived); this generalises the circle of fifths to other tunings.

### Fourier coefficients of chroma
- f_k = Σ_j c_j e^{−i2πkj/12}; f0 is the total; components 7-11 mirror 1-5 (Yust, MCM 2015, https://people.bu.edu/jyust/MCM2015.pdf, Definition 2, Remarks 1-2).
- Adding a constant changes only f0, so mean removal is free (same paper, Remark 3).
- Transposition and inversion change phases, not magnitudes (same paper, Remark 5); transposition by n turns f_k by −2πkn/12 (derived from Definition 2).
- Qualities: |f5| diatonicity, |f4| octatonicity, |f6| whole-tone, |f3| triadicity, |f2| dyadicity, |f1| chromaticity (Yust, JMM 2018 preprint, https://people.bu.edu/jyust/parfumsVer2.pdf, §2).
- By orthogonality the qualities are independent; by Parseval their total power is fixed for a given cardinality (same paper, §2).
- Convolution of distributions multiplies magnitudes and adds phases (Yust 2015, eq. 5); correlation against all 12 rotations of a profile is one product per coefficient (Yust 2017, §3, convolution theorem).

### Invariants used
- Transposition- and inversion-invariant: |f_k| for k = 1..6 (chord quality, tension, set class).
- Transposition-invariant but inversion-sensitive: phase combinations whose indices sum to 0 mod 12, e.g. Ph2 + Ph3 − Ph5 (derived from the phase rule above); major and minor triads take opposite signs, ±0.62 (Yust 2017, §4; which sign is which was not read).
- Transposition-equivariant: each phase φ_k (key position, chord position), moving by a known amount per semitone.
- Phases are undefined where |f_k| is near 0 (Yust 2015, §3): every phase output must carry its magnitude as confidence.

### Incremental computation
- The DFT is linear, so a one-pole average of chroma has the same one-pole average of each f_k: the coefficients update per hop with 6 complex multiply-adds, without recomputing from the chroma (derived).
- The tuning estimator is the same structure on the semitone circle: a running complex mean (Dressler & Streich, eq. 7).
- Key profiles transform once at configure time; per hop the 24 key scores are phase distances on f2, f3, f5 (derived from Yust 2017, §3).

## 5. Dynamic values and where each comes from

- Tuning offset and its confidence: the circular mean of peak deviations, live (Dressler & Streich 2007).
- Pitch class boundaries: the tuning offset, never 440 Hz.
- Lowest chroma frequency: live bin width / 0.0595 (derived, §2 Chroma).
- Steps per octave N: the tuning model's choice (12 by default), passed as the template parameter.
- Pitch, multi-pitch and note thresholds (`pitchThreshold`, detection thresholds, minimum note length): B-parameters tuned on annotated data, Pareto method (music-analysis.md §2); the user's slider then offsets the tuned value.
- Window and hop of each stage: A-parameters chosen on the accuracy–latency front (same method); the stage reports the result as `latencySamples()`.
- Key and scale switching: replace the 0.03 margin and 2 s hold with a confidence derived from the coefficient magnitudes (|f5|, |f3|) and tuned hysteresis; open which statistic is best.
- Smoothing time constants (0.1/0.5/4 s): A-parameters, tuned per output on annotated chord and key changes, never from a tempo.
- Tension weights: empirical ratings (Almeida et al. 2022) until a better weighting is measured; the mapping to 0..1 from the observed range of the song (relative, as the engine does elsewhere), not a fixed `(t − 0.1)/0.4`.
- Scale brightness: the root's line-of-fifths position within the set (derived, §2 Scales), replacing hand values.
- Chord change: the PLR element and the tonal-centroid distance, both from the music.

## 6. Risks and open questions

- No causal key tracker with measured modulation latency was found.
- No causal large-vocabulary chord recogniser with measured accuracy was found; the 170-class result is bidirectional.
- The best multi-pitch and note result read (Bittner et al. 2022) is offline in its note creation; its causal accuracy is unmeasured.
- Every phone cost in this stage is unmeasured; it needs the device benchmark in music-analysis.md §8.
- Phases of weak coefficients are noise; gating by magnitude is required and its threshold is itself a B-parameter.
- Inversion invariance of magnitudes loses major/minor; any tension or quality feature built on magnitudes alone cannot tell them apart.
- Correlation key finding favours major when f2 and f3 are weak (Yust 2017); the app's Pearson key inherits this.
- Tuning estimation fails on strong vocal vibrato (Dressler & Streich 2007); weight stable peaks, and expose confidence.
- Non-12-TET and unequal tunings: the Z_N design covers equal divisions only; unequal tunings are open.
- Learned models need our own weights trained on permissively licensed data; dataset licences unchecked.
- A genre shift cost a learned key model 17 points (Korzeniowski & Widmer 2017); evaluate on the app's own genre mix.
- The TIV weights and the tonal-centroid radii are fixed constants from other studies; whether they suit audio from phones is open.
- Perceptual validity of computed mode brightness and of spectral roughness on mixes: unread, open.
- The Yust–Amiot work on non-spectral transposition-invariant phase products was seen only in a search summary: unverified.
