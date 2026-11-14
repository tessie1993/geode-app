# Stage 06: timbre and instruments (research, 2026-11-14)

Scope: from the hop's spectrum to timbre descriptors, which instruments play, whether a voice sings, and the genre or texture as slow context.
Builds on `docs/research/music-analysis.md`, whose small-tagger and NNAPI → LiteRT lines are not repeated here.
Evidence: each line names its source; "derived" means it follows from the maths shown, with no paper behind it; "unverified" means no primary source was read; "open" means nothing read here settles it.
Clean room: methods and numbers come from papers; no code, names or strings from other projects; projects are named only where a URL must point at a licence.

## 1. How the app does it now

- One `SoundEngine` per renderer computes every descriptor on its own hop, 10 ms of samples (`core/sound/SoundEngine.hpp`, `Rhythm::kHopSeconds`).
- The FFT size is the smallest power of two of at least 40 ms of samples (`SoundEngine::fftSizeFor`); 40 ms is a chosen value.
- Centroid: magnitude-weighted mean frequency over 20 Hz–20 kHz, on a log axis 0 = 20 Hz … 1 = 20 kHz (`SoundEngine::descriptors`, `SoundFrame.h` `centroid`).
- Rolloff: the bin under which 85% of the magnitude sum lies, same axis (`rolloff`); 85% is a chosen value.
- Peak frequency and its level over a slow peak (`peakFreq`, `peakMag`).
- Flatness: geometric over arithmetic mean of power over 100 Hz–10 kHz, read in dB with −30 dB as 0 (`flatness`); the −30 dB floor is a chosen value.
- Partial density: local maxima above 10% of the band's peak, divided by 48 (`densityFast`, `densitySlow`); 10% and 48 are chosen values.
- Each descriptor is eased by `follow(…, τ, dt)` with τ 0.05 s, 0.03 s, 0.1 s or 1 s; each τ is a chosen value.
- Melody: a harmonic sum of 5 partials weighted 1/h, fundamental 80 Hz–1.5 kHz, on a 2N FFT (`SoundEngine::estimatePitch`).
- `evenness`: the even harmonics' share of the first six partial amplitudes of the tracked melody (`estimatePitch`; `SoundFrame.h` `evenness`).
- `evenness` is only meaningful while a pitch is tracked; on a full mix it describes whichever harmonic series the tracker holds.
- Not computed: MFCC or any cepstrum, spectral spread, skewness, kurtosis, slope, crest, tristimulus, inharmonicity, roughness (grep of `core/sound`).
- Not computed: instrument recognition, instrument activity, vocal presence, genre (grep of `core/` for `vocal|genre|instrument|timbre`).
- `core/library/Tags` reads the file's genre tag; nothing in the visuals reads it.
- Consumers: `Form::distance2` uses centroid and flatness as a 2-number timbre group (`core/sound/Form.cpp`).
- Consumers: `Emotion` maps brightness as `(centroid − 0.35) / 0.35` (`core/sound/Emotion.cpp`); 0.35 is a chosen value.
- Consumers: `NoteResponse` k1 TEXTURE weights centroid, density, flatness and treble 0.35 / 0.25 / 0.2 / 0.2 (`core/viz/NoteResponse.cpp`); the weights are chosen values.
- Consumers: `MotionField` drives brightness from centroid and harmony from 1 − flatness (`core/viz/MotionField.cpp`).
- No ML runtime is in the repo: no LiteRT, TFLite or ONNX reference outside `.claude/skills` (grep 2026-11-14).

## 2. Optimal design, with sources and measurements

### 2.1 Layers

- Fast layer (every hop, on the analysis owner's thread, wait-free): hand-built descriptors from the hop's spectrum and harmonic estimate.
- Slow layer (a C++ worker thread in `core/`, seconds of context): learned classifiers for instruments, voice and context.
- The slow layer never blocks the fast one: the fast layer pushes a compact per-hop feature row into an SPSC ring and reads the newest slow result from a latest-value buffer (design from `docs/research/music-analysis.md` §7, triple buffer line).
- The fast layer reads the slow result with its age in hops, so a consumer can fade a stale label instead of trusting it (derived).
- The worker runs below the audio and GL threads; the NDK performance hint API exists from API 33, our minSdk (`docs/research/music-analysis.md` §7, https://source.android.com/docs/core/perf/performance-hint-api).
- One analysis per source holds: the worker reads that source's feature rows, not its own PCM copy (pipeline rule in the brief).

### 2.2 Timbre descriptors (fast layer)

- A descriptor set should be chosen for low redundancy: across a large instrument corpus, correlation analysis and hierarchical clustering found about ten classes of relatively independent descriptors (Peeters et al., JASA 130(5) 2011, https://www.mcgill.ca/mpcl/files/mpcl/peeters_2011_jasa.pdf).
- The same paper summarises time-varying descriptors by median and interquartile range, as robust statistics, rather than mean and standard deviation (Peeters et al. 2011).
- Centroid, spread, skewness, kurtosis, flatness and crest are all shape-of-spectrum descriptors (Peeters et al. 2011).
- The same paper notes spectral slope is linearly dependent on the centroid (Peeters et al. 2011), so adding slope beside centroid adds little.
- The odd-to-even harmonic energy ratio separates sounds with predominant odd harmonics (clarinet) from smoother envelopes (trumpet) (Peeters et al. 2011); the app's `evenness` is this ratio's share form.
- Tristimulus (three energy ratios of the harmonics) and an inharmonicity coefficient are defined there too, both needing an f0 and partials (Peeters et al. 2011).
- MFCC: a DCT of log mel energies; in classifiers it is now mostly replaced by the log-mel input itself (Han et al. use 128-band log-mel, https://arxiv.org/pdf/1605.09507).
- Roughness: pairwise partial dissonance, Sethares' fit of Plomp and Levelt's curve, d* = 0.24, s1 = 0.021, s2 = 19, b1 = 3.5, b2 = 5.75 (as quoted by Hyrkas, CMMR 2023, https://zenodo.org/records/10113480/files/cmmr2023_4b-1.pdf).
- Those roughness constants are fitted to listening data, so they come with the algorithm, not a hand pick (Hyrkas 2023, citing Sethares).
- Roughness needs partials: peak-pick the spectrum, sum over pairs within a critical band; cost grows with the square of the peak count (derived from the pairwise form).
- Cap the pair count by the critical band: pairs further than the curve's support contribute almost nothing (derived from the exponential form); the exact cut-off is open.
- `evenness` on a full mix should be computed per tracked harmonic series, gated by `pitchConfidence`, and held when unvoiced (derived from §1).

### 2.3 Predominant instrument in a mix

- Best measured method read: a VGG-style CNN on 128-band log-mel (22.05 kHz, 1024-sample window, 512 hop), trained on 3 s single-label excerpts, sliding windows summed class-wise then thresholded (Han, Kim, Lee, TASLP 2017, https://arxiv.org/pdf/1605.09507).
- Measured: micro F1 0.602, macro F1 0.503 on the IRMAS test set, 11 pitched instruments (Han et al. 2017).
- Window study: 3.0, 1.5, 1.0 and 0.5 s analysis windows tried; 1.0 s was best, 0.5 s too short (Han et al. 2017).
- Threshold study: θ from 0.2 to 0.6 swept, chosen on held-out data (Han et al. 2017).
- IRMAS training data: 6,705 excerpts of 3 s from over 2,000 recordings (Han et al. 2017; Bosch et al., ISMIR 2012, https://archives.ismir.net/ismir2012/paper/000559.pdf).
- IRMAS cannot be used for training in this app (§6).
- Model size and phone latency: not reported by Han et al. (open).

### 2.4 Instrument activity over time (which instruments play now)

- Multi-label clip tagging on OpenMIC-2018: 20,000 clips of 10 s, 20 instrument classes, from Creative Commons music (Humphrey, Durand, McFee, ISMIR 2018, https://archives.ismir.net/ismir2018/paper/000248.pdf).
- Distilled MobileNetV3 taggers fine-tuned on OpenMIC reach mAP 82.0 (width 0.4) and 84.8 (width 3.0); dynamic variants 83.6 to 85.5; the task baseline is 79.5 (Schmid, Koutini, Widmer, TASLP 2024, https://arxiv.org/pdf/2310.15648, Table I).
- The width-0.4 model and its dynamic variant are below 1 M and 2 M parameters respectively (Schmid et al. 2024).
- Its pretraining release lists the width-0.4 MobileNet at 0.983 M parameters, 0.11 G MACs per 10 s clip, AudioSet mAP 43.2, at 128 mel bands and a 10 ms hop (release README, https://raw.githubusercontent.com/fschmid56/EfficientAT/main/README.md).
- Front end of those models: 32 kHz audio, 25 ms window, 10 ms hop, mel bands (Schmid et al., ICASSP 2023, https://arxiv.org/pdf/2211.04772).
- The 10 ms hop matches the app's analysis hop (§1), so the slow layer can reuse the hop's spectrum rows if mel band edges and sample rate match (derived; resampling cost open).
- Weak labels with attention over 0.96 s embedding frames localise instruments in time; the attention model improved recall and macro F1 over a random-forest baseline, shown as box plots, not a table (Gururani, Sharma, Lerch, ISMIR 2019, https://arxiv.org/pdf/1907.04294).
- That attention model has far fewer parameters than a 986,772-parameter fully connected baseline (Gururani et al. 2019, weight-shared embedding).
- Frame-level activity with frame labels: a CNN on the CQT reaches 0.887 average F1 on MusicNet (7 instruments, classical); adding a harmonic series feature from estimated pitch reaches 0.896, from true pitch 0.933 (Hung & Yang, ISMIR 2018, https://arxiv.org/pdf/1806.09587).
- The harmonic series feature shifts each active f0 up to its (n+1)-th harmonic on the CQT and sums the maps (Hung & Yang 2018); this is a translation on log-frequency (§4).
- The app already has a melody f0 (`estimatePitch`), so a pitch-aware input is available without a second pitch model (derived; multi-f0 would be needed for chords, open).
- Recommended: a small log-mel or CQT CNN tagger with time attention, trained by us on permissively licensed data (§6), emitting per-class activity once per stride.

### 2.5 Vocal presence

- A CNN on 80-band log-mel (27.5 Hz–8 kHz) with 1.6 s context (115 frames) is a standard singing voice detector (Schlüter & Grill, ISMIR 2015, https://ofai.at/~jan.schlueter/pubs/2015_ismir.pdf; input size as re-implemented by Lee, Choi, Nam, ISMIR 2018, https://arxiv.org/pdf/1806.01180).
- Measured: classification error 7.7% on the Jamendo corpus with train and test augmentation, 9.4% without (Schlüter & Grill 2015, Table 1).
- Measured: error 7.3% on RWC with train and test augmentation (Schlüter & Grill 2015, Table 2).
- Re-implementations without augmentation: feature-based 87.9%, CNN 86.8%, RNN 87.5% accuracy on the Jamendo test set (Lee, Choi, Nam 2018, Table 2).
- Those systems smooth predictions with an 800 ms median filter (Lee et al. 2018); the length is a chosen value there.
- Known failure: detectors behave partly as vibrato detectors and confuse other pitch-modulated instruments (Lee et al. 2018 hypothesis and analysis).
- The Jamendo corpus: 93 pieces, ~6 h, train/valid/test 61/16/16 (Lee et al. 2018; Zenodo record https://zenodo.org/records/2585988).
- Recommended: one vocal-activity output in the same tagger as §2.4, not a second model (derived: one shared trunk, one inference).

### 2.6 Genre or texture as context

- A scattering transform along time, then along log-frequency, gives a representation stable to time warping and invariant to frequency transposition; it reported state-of-the-art genre classification on GTZAN at the time (Andén & Mallat, IEEE TSP 2014, https://arxiv.org/pdf/1304.6763).
- GTZAN's licence is not stated in anything read here (open), so it is evaluation-only at best.
- FMA: 106,574 Creative Commons tracks, 161 genres (16 roots), per-track licence field; its own data and code under CC BY 4.0 and MIT (Defferrard et al., ISMIR 2017, https://arxiv.org/pdf/1612.01840).
- MTG-Jamendo has instrument and genre tags but is non-commercial (§6).
- Recommended: no separate genre model; use the tagger's penultimate embedding, averaged over tens of seconds, as a texture vector, and the file's genre tag as an optional hint (derived).
- Owner rule "no colour narrowing" means a context label may modulate reactions only through the user-visible reaction matrix, never pick a palette (AGENTS.md).

### 2.7 Runtime

- LiteRT runs on Android with a C++ `CompiledModel` API: create an `Environment`, a `CompiledModel` from a `.tflite` file with a chosen accelerator, then buffers and `Run` (https://developers.google.com/edge/litert/next/android_cpp).
- The C++ runtime can be taken as a prebuilt library from the LiteRT Maven package or built from source with CMake (same page).
- LiteRT is Apache-2.0 (https://raw.githubusercontent.com/google-ai-edge/LiteRT/main/LICENSE).
- Its CPU path is XNNPACK; its GPU path supports OpenCL and OpenGL on Android (LiteRT README, https://raw.githubusercontent.com/google-ai-edge/LiteRT/main/README.md).
- XNNPACK is BSD-licensed (https://raw.githubusercontent.com/google/XNNPACK/master/LICENSE).
- LiteRT in Google Play services does not support devices without Play services, and its APIs "may contact Google servers from time to time" (https://developers.google.com/edge/litert/android/play_services).
- Therefore: bundle the runtime in the APK; do not use the Play services runtime (derived from the offline rule and the page above).
- Accelerators cannot run every operation; a delegate may reject a model and fall back to CPU; it may compute at another precision (https://developers.google.com/edge/litert/performance/delegates).
- The GPU already renders the visuals; a GPU delegate would share it with the GL thread (derived); whether that costs frames is open, so default to CPU and measure.
- Measured phone CPU latency, float32, 1 s audio input: a 2.9 M-parameter MobileNetV2 tagger took 8.0 / 6.2 / 13.1 / 11.6 ms on Snapdragon 865 / 888 / Helio G90T / Dimensity 700 (Dinkel et al., ICASSP 2023, https://arxiv.org/pdf/2303.01812, Table 3).
- That measurement used a PyTorch mobile benchmark tool, not LiteRT (same paper); LiteRT latency for our model is open.
- On a Raspberry Pi 4 CPU (ONNX), width-0.5 and width-1.0 distilled MobileNets ran under 0.25 s per 10 s clip headless, slower with a UI running (https://arxiv.org/pdf/2509.14049).
- Scaling from these: a sub-1 M-parameter tagger on 1 s windows should cost a few ms per inference on a phone CPU (unverified extrapolation; measure).
- Inference every stride of S hops on 1 s of context costs latency of up to S hops plus the inference time; the label lags the music by about half the window (derived).

## 3. Polymorphic interface (signatures only)

```cpp
namespace geode::sound::timbre {

// Read-only views the stage receives each hop; owned by the SoundEngine.
struct SpectrumView;        // magnitudes, bin width, sample rate, hop index
struct HarmonicView;        // f0 (or none), confidence, partial amplitudes
struct TimbreFrame;         // fast descriptors written into the hop's frame
struct FeatureRow;          // one compact per-hop row for the slow layer (e.g. log-mel)
struct LabelSpace;          // names and count of a classifier's outputs, from the model file
struct SlowResult;          // probabilities, embedding, hop index they describe

// The stage: what SoundEngine calls. One instance per source.
class TimbreStage {
public:
    virtual ~TimbreStage() = default;
    virtual void prepare(int sampleRateHz, int hopSamples, int fftSize) = 0;  // not real-time
    virtual void onHop(const SpectrumView&, const HarmonicView&, TimbreFrame&) noexcept = 0;  // wait-free
    virtual bool latest(SlowResult& out, int& ageHops) const noexcept = 0;  // wait-free read
    virtual void reset() noexcept = 0;
};

// Fast-layer strategies, run inside onHop.
class Descriptor {
public:
    virtual ~Descriptor() = default;
    virtual void prepare(int sampleRateHz, int fftSize) = 0;
    virtual void compute(const SpectrumView&, const HarmonicView&, TimbreFrame&) noexcept = 0;
};
// Implementations: SpectralShape, Cepstrum, HarmonicRatios (evenness, tristimulus, inharmonicity), Roughness.

// Feature front end for the slow layer, run inside onHop.
class FeatureFront {
public:
    virtual ~FeatureFront() = default;
    virtual void prepare(int sampleRateHz, int fftSize, int bands) = 0;
    virtual void row(const SpectrumView&, FeatureRow&) noexcept = 0;
};
// Implementations: LogMel, Cqt, Pcen (wraps another front).

// Slow-layer strategy, run on the worker thread.
class Classifier {
public:
    virtual ~Classifier() = default;
    virtual const LabelSpace& labels() const noexcept = 0;
    virtual int windowRows() const noexcept = 0;          // context the model was trained on
    virtual bool infer(std::span<const FeatureRow> window, SlowResult& out) noexcept = 0;
};
// Implementations: Null, DescriptorLinear (a small learned map over descriptor statistics), LiteRt (CompiledModel).

}  // namespace geode::sound::timbre
```

- `infer` is `noexcept` and allocation-free after construction; LiteRT buffers are created once at load (design rule; the API's buffer wrapping is on the C++ page above).
- `LabelSpace` and `windowRows` come from the model file's metadata, never from a constant in code (derived from the owner's no-arbitrary-values rule).
- `Null` keeps the stage valid with no model shipped (derived).

## 4. Algebraic structure

- Invariance f(t(x)) ≈ f(x) and equivariance f(t(x)) ≈ t′(f(x)) for a set of transforms T (Riou et al., PESTO, ISMIR 2023, https://arxiv.org/pdf/2309.02265, Eqs. 1–2).
- A standard CNN is the group-equivariant CNN for the translation group; group convolutions extend this to other groups, and pooling and pointwise nonlinearities keep equivariance (Cohen & Welling, ICML 2016, https://arxiv.org/pdf/1602.07576).
- Pitch shift is a dilation f ↦ a·f of the multiplicative group (ℝ>0, ·); on a log-frequency axis it becomes a translation of (ℝ, +) (SPICE, Gfeller et al., TASLP 2020, https://arxiv.org/pdf/1910.11664).
- On a CQT with B bins per octave the translation is discretised to ℤ, one bin = 1/B octave (derived from SPICE's CQT framing).
- A convolution along CQT frequency is therefore equivariant to transposition by whole bins; global pooling over frequency makes the output invariant (derived from Cohen & Welling with the line above).
- Toeplitz fully connected layers preserve transposition where an ordinary dense layer would break it (Riou et al. 2023); a model of under 30 k parameters was trained this way (same paper).
- Harmonics h·f0 become translations by log h; stacking CQTs computed at h ∈ {0.5, 1, 2, 3, 4, 5} aligns harmonics across channels (Bittner et al., ISMIR 2017, https://brianmcfee.net/papers/ismir2017_salience.pdf).
- The harmonic indices {1, 2, 3, …} form a monoid under multiplication, not a group; their logs are a subset of the translation group (derived).
- `evenness` is a ratio over harmonic index parity, the quotient ℕ → ℤ/2; it is invariant under global gain and under transposition (derived: both act on all partials alike).
- Global gain is the group (ℝ>0, ·); log maps it isomorphically onto (ℝ, +) (derived).
- In MFCCs a global gain adds log g to every log-mel band, and the DCT of a constant vector is nonzero only at index 0, so only c0 moves (derived).
- Per-band gain (a filter h) is the diagonal group (ℝ>0)^F; PCEN with ε = 0 and α = 1 is invariant to filtering by h (Lostanlen et al., IEEE SPL 2019, Prop. III.3, https://par.nsf.gov/servlets/purl/10313542).
- PCEN's smoothing time T must change whenever the time–frequency resolution changes (Lostanlen et al. 2019, Prop. IV.1), so T is derived from the front end, not fixed.
- PCEN was introduced as a trainable front end (Wang et al. 2016, https://arxiv.org/pdf/1607.05666).
- Augmentation by group action: sample g in a range, apply it to the input, keep the label.
- Pitch shift ±20–30% reduced singing-detection error by up to 25% relative; time stretch had no strong effect; random loudness change did not affect results (Schlüter & Grill 2015).
- Combined pitch ±30%, stretch ±30% and filters ±10 dB gave about 6% more relative improvement than pitch alone (Schlüter & Grill 2015).
- Test-time averaging over pitch shifts of −20, −10, +10, +20% helped independently of training augmentation (Schlüter & Grill 2015); this is averaging over a finite orbit of the group (derived).
- Fine-tuning recipe in the tagger paper: waveform roll ±125 ms (time translation), gain ±7 dB, and random perturbation of the mel filterbank's min and max frequency (Schmid et al. 2024).
- The symmetry is only approximate: timbre changes with register, and a finite CQT has edges; shifts beyond ±30% stopped helping (Schlüter & Grill 2015 figure 2; derived for the edges).
- The app's easing `follow(x, target, τ, dt)` should compose as a semigroup in dt: easing over dt1 then dt2 equals easing over dt1 + dt2 when the target holds, so results do not depend on hop size (derived; holds for the exponential form exp(−dt/τ)).
- Combining independent window posteriors by summing log-odds uses the additive group on logits (derived); Han et al. summed class outputs and normalised, the probability-domain analogue.

## 5. Dynamic values and where each comes from

- Sample rate: from the source (`SoundEngine` ctor); the slow front end resamples to the model's training rate read from model metadata.
- Hop: `Rhythm::kHopSeconds` today; under ADR 0006 the hop chain's sample clock.
- FFT size: today from a 40 ms rule (§1); should come from the frequency resolution the descriptor needs, e.g. the lowest f0 the harmonic estimator must separate (derived; open which).
- Mel band count, band edges, window and hop of the slow front end: from the model file, set at training.
- Context window: from the model file; Han et al. found 1.0 s best for predominant recognition, Schlüter used 1.6 s for voice.
- Inference stride: from the measured inference time on the device at load, so worker load stays under a share of one core (derived; the share is open).
- Per-class decision threshold: from a held-out validation set at training, per class (Han et al. swept θ); shipped in the model metadata.
- Temporal smoothing of labels: from validation (Lee et al. used 800 ms, a chosen value), shipped in metadata.
- PCEN T: derived from the time–frequency resolution (Lostanlen et al. 2019).
- Descriptor easing τ: should come from the descriptor's own analysis window or from the consumer, not a constant (derived).
- Descriptor normalisation (e.g. the 85% rolloff, −30 dB flatness floor, /48 density): should come from the distribution of each descriptor over the training corpus, e.g. its median and IQR (Peeters et al. 2011 statistics), stored as data.
- Roughness constants: from Sethares' fit to Plomp and Levelt (Hyrkas 2023).
- Accelerator: CPU by default; GPU only if a load-time measurement on this device beats CPU without costing GL frames (LiteRT delegate page advises measuring; GL cost open).
- Staleness: the slow result's age in hops, from the hop counter.

## 6. Licences of data and weights

- IRMAS: CC BY-NC-SA 3.0 per a dataset loader's licence text (secondary, https://mirdata.readthedocs.io/en/latest/_modules/mirdata/datasets/irmas.html); the primary page refused the fetch (403). Non-commercial: unusable.
- OpenMIC-2018: dataset CC BY 4.0 (https://zenodo.org/records/1432913); audio from FMA, each recording with its own licence in the metadata CSV; usable only after dropping NC and ND tracks.
- FMA: own data CC BY 4.0, code MIT; audio per-track Creative Commons with a licence field (Defferrard et al. 2017); filter to CC BY and CC0 tracks (derived).
- MusicNet: CC BY 4.0 on the Zenodo record (https://zenodo.org/records/5120004); per-recording licences not checked (open).
- Slakh2100: CC BY 4.0 (https://zenodo.org/records/4599666); synthesised from MIDI, no vocals.
- NSynth: CC BY 4.0, 305,979 single notes, 1,006 instruments, 11 families including vocal, 16 kHz, 4 s (https://magenta.tensorflow.org/datasets/nsynth).
- Jamendo corpus (voice): record CC BY 4.0, 93 Creative Commons pieces; per-track licences not listed (https://zenodo.org/records/2585988; open).
- MedleyDB: CC BY-NC-SA 4.0, non-commercial research only (https://zenodo.org/records/1649325). Unusable.
- MUSDB18: educational, non-commercial only (https://zenodo.org/records/1117372). Unusable.
- MTG-Jamendo: metadata CC BY-NC-SA 4.0, non-commercial research only; commercial use needs written permission (https://raw.githubusercontent.com/MTG/mtg-jamendo-dataset/master/README.md). Unusable.
- AudioSet: the dataset is CC BY 4.0 (https://research.google.com/audioset/download.html); the audio itself is not distributed with it.
- Distilled tagger code: MIT (https://raw.githubusercontent.com/fschmid56/EfficientAT/main/LICENSE); its weights were trained on AudioSet audio, whose rights the CC BY 4.0 labels do not cover (open, legal).
- GTZAN: licence not found (open). Evaluation only, if at all.
- Runtime: LiteRT Apache-2.0, XNNPACK BSD (§2.7).
- Therefore the usable training pool read here is OpenMIC (filtered), FMA (filtered), MusicNet, Slakh2100, NSynth and the Jamendo voice corpus (per-track check pending); we train our own weights.
- A commercially licensed vocal set with frame labels in pop mixes was not found (open).

## 7. Risks and open questions

- Phone latency of our own LiteRT model on CPU and GPU: unmeasured; the only phone numbers read are PyTorch-mobile ones (§2.7).
- GPU delegate vs the GL renderer sharing one GPU: unmeasured.
- APK size of the bundled LiteRT runtime per ABI: not read (open).
- Minimum Android version of the LiteRT C++ prebuilt: not stated on the page read (open).
- The filtered training pool is small and skewed (classical in MusicNet, synthesised in Slakh, single notes in NSynth); accuracy on pop mixes is unknown.
- Weak clip labels (OpenMIC) give "present somewhere in 10 s", not "playing now"; frame accuracy needs frame labels (MusicNet, Slakh) or attention (Gururani et al.).
- Vocal detectors confuse vibrato instruments with voice (Lee et al. 2018).
- Transposition invariance is approximate; a pitch-pooled model may lose register-dependent timbre cues.
- AudioSet-trained weights: rights status open; do not ship them until settled.
- Descriptor normalisations now in code (85%, −30 dB, /48, 0.35, NoteResponse weights) are hand picks; replacing them changes the look of every style, so it needs golden updates (`core/tests/sound_golden.txt`).
- Owner rule: a classifier's label must reach visuals only through the editable reaction matrix, never as a hardcoded reaction (AGENTS.md).
- No BPM bias: no timbre or genre model may take tempo as input or be weighted by it (AGENTS.md, `tools/no-bpm-bias-gate.sh`).
- Nothing here was adversarially verified; every number is single-source.
