# Pipeline stage 3: rhythm and time (research, 2026-11-14)

Scope: transients and onsets, drums (kick, snare, hats), the harmonic/percussive split, beat, downbeat and meter, tempo as an output, rhythmic patterns and subdivisions.
Builds on `docs/research/music-analysis.md` (Böck 2012 online flux, the causal RNN, BeatNet, SuperFlux, the hop-from-burst framing); those findings are cited there and not repeated here.
Evidence: every line names its source; "derived" means worked out here from a cited source; "unverified" means nothing fetched settles it; "open" means it needs a measurement or a source not yet found.
Clean room: methods and maths from papers only; no code, names or strings from other projects.
Tempo rule: a tempo not yet heard is 0; examples here use 77, 93, 141 and 174; no method below may weight, centre on or start from a tempo.
Paper copies were read as text extracted from the PDFs at the URLs given.

## 1. How the app does it now

### Framing and hop
- The whole sound engine runs on one hop of 10 ms of audio: `kHopSeconds = 0.01f` (`core/sound/Rhythm.hpp:36`), hop = round(rate × 0.01) (`core/sound/Rhythm.cpp:49`).
- Hardcoded: the 10 ms hop is a convention (see `music-analysis.md` §2); it is not tied to the device burst.
- The hop is driven per sample from `SoundEngine::feed` (`core/sound/SoundEngine.cpp:246-256`): every sample goes through the filter bank and the transient detector, and every hop through `Rhythm::hop` and `analyse()`.
- Transient's statistics count hops and assume they are 10 ms: `kHistory = 100` "One second of hop peaks (Rhythm's 10 ms hop at every rate)" (`core/sound/Transient.hpp:57-58`). Hidden coupling: change the hop and the "one second" changes.

### Onset envelope for the beat
- Seven filter-bank bands feed the beat (`core/sound/Rhythm.hpp:34-35`); their edges are hardcoded in `kSliceHz` (`core/sound/SoundEngine.cpp:154-155`): 0–250, 250–4000, 4000–top, kick 40–120, snare body 150–300, snare noise 2500–6000, hat 7000–16000 Hz.
- Per band: log1p(3 · power / shared reference), positive change over two hops (20 ms), mean over bands (`core/sound/Rhythm.cpp:96-101`). Hardcoded: `kCompress = 3.0f` (`:23`), the two-hop change, the shared reference released over `kBandMeanSeconds = 4.0f` (`:12`, `:79`).
- The drum accents read a different log: log1p(100 · p / band mean) (`core/sound/Rhythm.cpp:89`). Hardcoded: the 100.
- Minus a local mean over `kLocalMeanSeconds = 0.3f` (`core/sound/Rhythm.cpp:13`, `:108-109`), half-wave rectified into a 600-hop ring (`kRing = 600`, 6 s, `core/sound/Rhythm.hpp:112`).
- Hardcoded: `kRefFloor = 1e-10f`, `kFlatEnvelope = 1e-6f` (`core/sound/Rhythm.cpp:22`, `:24`).

### Tempo (period)
- Search limits: `GEODE_SOUND_BPM_SLOWEST 30.0f`, `GEODE_SOUND_BPM_FASTEST 300.0f` (`core/sound/SoundFrame.h:40-41`); `lagMin_` from the fastest (`core/sound/Rhythm.cpp:51`), `lagMax_ = kRing / kPeriodsHeard` = 200 hops (`:52`).
- The slowest-BPM constant is welded to the ring by a static_assert: 60 / 30 / 0.01 × 3 must equal 600 (`core/sound/Rhythm.hpp:113`). The ring length, the hop, the 3 periods and the slowest limit cannot move independently.
- The limits are flat inside (no weighting), so they pass the no-BPM-bias rule as limits; but both are hand-picked numbers, not read from the device or the music.
- `Reactions.cpp` normalises the bpm source by `1 / GEODE_SOUND_BPM_FASTEST` (`core/viz/Reactions.cpp:71`): a visual scale tied to the search limit.
- First estimate after `kTempoAfter = 300` hops (3 s), then every `kTempoEvery = 25` hops (`core/sound/Rhythm.cpp:14-15`, `:116`). Hardcoded.
- Two passes of a [1/4, 1/2, 1/4] binomial smoother before the autocorrelation (`core/sound/Rhythm.cpp:129-137`). Hardcoded width.
- Normalised autocorrelation, each lag divided by the energy of its overlap (`core/sound/Rhythm.cpp:149-156`): no lag gains from how many terms it has. Good: this is the bias-free part.
- Score = (ac[lag] + 0.5 · ac[2·lag]) / 1.5 (`kComb = 0.5f`, `core/sound/Rhythm.cpp:27`, `:157`). Hardcoded share.
- The beat is the fastest local maximum reaching `kLevel = 0.45f` of the strongest (`core/sound/Rhythm.cpp:29`, `:165-169`). Names no tempo, but 0.45 is hand-picked and it decides the metrical level.
- Peak needs `acConf_ >= 0.05` (`core/sound/Rhythm.cpp:180`). Hardcoded.
- Same tempo if within 4% (`:187`), refined by 0.3 (`:188`); a new tempo needs 4 votes, a "related" one 8 (`:204`).
- Hidden assumption: "related" is the hardcoded ratio set {2, 1/2, 3/2, 2/3} with tolerances 0.08, 0.03, 0.06, 0.03 (`core/sound/Rhythm.cpp:202-203`): it names duple and triple relations only, not 4/3, 5/4 or others.

### Phase, lock
- Phase: the Fourier coefficient of the ring at the period, read at the newest instant (PLP read causally), over whole bars of `meter_ × P` hops, at most `kPhaseWindow = 400` hops (4 s) (`core/sound/Rhythm.cpp:17`, `:211-240`).
- Hidden coupling: the phase window holds whole bars, so a wrong meter changes the phase window.
- Latency shift `kLatencyHops = 1.9f`, "measured on clicks from 60 to 190 BPM" (`core/sound/Rhythm.cpp:35-38`). Measured once on clicks, then frozen; it changes if any envelope time constant or the hop changes.
- Confidence: clamp((acConf − 0.05)/0.35) × clamp((plpConf − 0.1)/0.4), smoothed over 1 s (`core/sound/Rhythm.cpp:243-244`). Hardcoded 0.05, 0.35, 0.1, 0.4, 1 s.
- Lock at 0.35 held 1 s, unlock under 0.2 held 2 s (`core/sound/Rhythm.cpp:30-33`); phase gain 0.2 (`:34`); snap after a quarter-beat miss held 1 s (`:270-271`). Hardcoded.
- Phase measured every `kPhaseEvery = 5` hops (`core/sound/Rhythm.cpp:16`). Hardcoded.

### Downbeat, meter, phrase
- Meter starts at 4: `int meter_ = 4` (`core/sound/Rhythm.hpp:150`). Hidden assumption: 4/4 until the music proves 3.
- Only 3 and 4 are ever considered: `const int other = meter_ == 4 ? 3 : 4` (`core/sound/Rhythm.cpp:400`); `slotScore_` is `std::array<float, 4>` (`core/sound/Rhythm.hpp:143`), so no meter above 4 can exist.
- `kMeterBeats = 24` "whole bars of 3 and of 4" (`core/sound/Rhythm.hpp:147`). Hardcoded to those two meters.
- Meter switches when the other explains > 0.5 of the accent spread and > 2× the current, for 2 × other beats (`core/sound/Rhythm.cpp:403-404`). Hardcoded.
- Downbeat: kicks favour the downbeat, snares the backbeat: score = kick − 0.5 · snare, leaky 0.85 per bar, mean rate 0.125 (`core/sound/Rhythm.cpp:349-352`). Hidden assumption: a rock/pop backbeat (kick on one, snare on two and four).
- Kick accent = change[kick] + 0.5 · change[low]; snare = min(body, noise) (`core/sound/Rhythm.cpp:105-106`). Hardcoded 0.5.
- A new downbeat slot must lead by 0.15 of scale for 4 evaluations (`core/sound/Rhythm.cpp:362-365`). Hardcoded.
- Harmony accent: a chord change 0.35 s back adds 0.3 · scale to its slot (`kHarmonyLatency`, `core/sound/Rhythm.cpp:40`, `:423-427`). Hardcoded; "chords change on bars" is an assumption (`core/sound/Rhythm.hpp:81`).
- Phrase = 8 bars: `barInPhrase_ = ... % 8` (`core/sound/Rhythm.cpp:320`), `/ 8.0f` (`:490`), `barInPhrase_ = 7` at reset (`:71`), `std::clamp(barsAgo, 0, 7)` (`:431`). Hidden assumption: the 8-bar phrase.
- Unlocked fallback: bar = 4 hits, phrase = 32 hits (`hitTime / 4.0`, `hitTime / 32.0`, `core/sound/Rhythm.cpp:495-496`). Hidden assumption: 4/4 and 8 bars even with no meter heard.
- `SoundFrame.h` documents both: downbeat "(the beat slot the kicks favour, 4/4)" (`core/sound/SoundFrame.h:145`), phrase "each 8-bar phrase" (`:148`).
- Form inherits them: a 32-beat ring, a 4-beat newer half "one bar", a 16-beat older half "four bars" (`core/sound/Form.hpp:47-51`), `kMinSectionBars = 8` (`core/sound/Form.cpp:18`), `kMinDropBars = 4` "builds run 4–8 bars" (`:19`).

### Transients
- Per sample, four bands, fast/slow one-pole power followers; detection = their ratio in dB (`core/sound/Transient.hpp:99-132`).
- Hardcoded: `kFastSeconds = {3, 1, 0.5, 2} ms`, `kSlowSeconds = {30, 20, 20, 20} ms` (`:46-48`), `kFloorPower = 4e-8f` (`:51`), `kMinJumpDb = 6` (`:53`), `kMarginDb = 3`, `kSpread = 3` (`:55-56`), `kRefractorySeconds = 0.03f` (`:60`), `kAttackSeconds = 0.005f` (`:62`).
- The threshold adapts from the last second's median + MAD (`core/sound/Transient.hpp:136-152`): data-driven, but the margin and spread multipliers are hand-picked.
- Report delay: 5 ms of attack measurement, then the hop that holds its end (`core/sound/Transient.hpp:119-121`, `:153-155`): up to 5 ms + one hop.

### Frame onset (flux)
- Log-magnitude flux against the spectrum `kFluxLag = 1/30` s earlier (3 hops), maximum-filtered by ±1 semitone (`core/sound/SoundEngine.cpp:53`, `:563-597`; `core/sound/OnsetFlux.hpp:36-40`), summed 30 Hz–16 kHz (`core/sound/SoundEngine.cpp:160`).
- Hardcoded: `kFluxKnee = 1500.0f` (`core/sound/SoundEngine.cpp:31`), the 1/30 s lag, the 30 Hz–16 kHz span.
- The onset trigger: `s.onset.step(out.flux, max(1.1, onsetThreshold), 0.06, dt)` (`core/sound/SoundEngine.cpp:717`); the user threshold default is 2 (`core/sound/SoundFrame.h:231`). Hardcoded 1.1 floor and 60 ms.
- Its own comment puts the onset ~70 ms late (`core/sound/Transient.hpp:7-8`).

### Drums
- Punch = band level over its own 150 ms past, per sample (`core/sound/FilterBank.hpp:66-68`, `:87-89`; `core/sound/SoundEngine.cpp:632-645`).
- Leakage floors `kLeakage = {-30, -30, -50, -50 dB}` against the whole mix (`core/sound/SoundEngine.cpp:639`). Hardcoded.
- Snare = min(body, noise); noise within 25 ms of a stronger kick is the kick's (`core/sound/SoundEngine.cpp:650-658`). Hardcoded 25 ms.
- Refractory `{50, 50, 30} ms` (`core/sound/SoundEngine.cpp:630`); attack 5 ms (`:627`). Hardcoded.
- User thresholds `drum = {0.6, 0.5, 0.5}` and decays `{0.18, 0.15, 0.08}` s (`core/sound/SoundFrame.h:227-228`): user tuning, acceptable as user-owned.
- No template, no decomposition: kick, snare and hat are band energies.

### Subdivision fields
- `kick2`, `kick4`, `kick8`, `kick16` fire on every Nth kick by a hit counter (`core/sound/SoundEngine.cpp:676-680`); `snare2`, `snare4` likewise (`:685-687`).
- They count hits, not metrical positions: they are Z → Z_16 (and Z_4 for snares) by hit index, unrelated to the beat, bar or meter.
- Hidden assumption: only the power-of-two chain 2 | 4 | 8 | 16; a bar of 3 or 5 kicks has no field; the counters reset only on silence (`core/sound/SoundEngine.cpp:727-728`), so after one missed or extra kick every field is out of step with the bar.

### Test coverage of this stage
- `core/tests/musical_test.cpp:127` plays click tracks at fixed tempos and checks tempo within 2%, phase within 5 ms, downbeats on beat one (`:188-197`). Clicks only: no annotated real music, so the app's real-world F-measure is unmeasured (open).

## 2. Optimal design per sub-task

### 2.1 Transients and onsets (causal, low latency)
- Keep two detectors: a per-sample time-domain one for timing and a spectral one for reliability; a hybrid of the two is the published recommendation (Turchet, DAFx 2018, cited in `music-analysis.md` §5).
- Spectral detector: Böck 2012 online flux with the SuperFlux maximum filter and the causal peak picker; F 80.3% at ±25 ms (already verified, `music-analysis.md` §7). The app already has the flux and the maximum filter; what it lacks is the strictly causal peak picker in place of the 60 ms trigger, and the hop from the device.
- Latency budget per detector = window right-alignment + hop + peak picker look-back; the RT-PLP paper counts the latency of a frame-based system as one frame period (11.61 ms at 512 / 44.1 kHz) (Meier, Chiu & Müller, TISMIR 2024, https://www.audiolabs-erlangen.com/content/05_fau/professor/00_mueller/03_publications/2024_MeierCM_RealTimePLP_TISMIR_ePrint.pdf).
- Which window and hop: chosen on the accuracy–latency Pareto front of an annotated set (Stefani & Turchet, DAFx 2021, `music-analysis.md` §2); the A-parameters (window, hop) are bounded below by the device burst, the B-parameters (thresholds) have no latency cost.
- The time-domain detector's thresholds (6 dB, 3 dB, 3 MAD) are B-parameters: tune them on the same Pareto run instead of fixing them (derived).
- Cost: the spectral detector is one real FFT per hop plus O(bins) flux; the time-domain one is O(bands) per sample (derived from the code).

### 2.2 Drums (kick, snare, hats)
- Option A, single-frame NMF with fixed templates: one basis per drum, each frame decomposed on its own, KL divergence, multiplicative update of activations only (Dittmar & Gärtner, DAFx 2014, https://dafx14.fau.de/papers/dafx14_christian_dittmar_real_time_transcription_a.pdf).
- Its best average F 0.95 at H = 512, N = 2048, K = 25 iterations, threshold boost 1.25, ±50 ms (same paper, §5.2).
- Its measured systemic delay: about 6 ms at those settings, depending only on the audio input delay and the hop (same paper, §5.4).
- Caveat: its test set is 33 drum-set-only recordings, 3471 onsets; no accompaniment, so full-mix accuracy is unmeasured (same paper, §5.1).
- Fixed and semi-adaptive bases gave almost the same F; semi-adaptive helps separation quality, not transcription (same paper, §5.2–5.3).
- Templates there come from isolated hits recorded in advance (same paper, §4); this app has no isolated hits, so templates must be learned at run time (proposal: average the spectra at the band detector's most confident hits; unverified, needs a measurement).
- Per-hop cost of option A (derived): K × 2 × bins × drums multiply-adds, e.g. 25 × 2 × 1025 × 3 ≈ 154 k per hop: small next to one FFT on a phone; open until measured.
- Option B, a causal RNN per drum: unidirectional RNN mean F 0.816 (kick 0.884, snare 0.833, hat 0.729) on IDMT-SMT-Drums, online peak picker over the previous ρ frames, 2048 window, 512 hop (Southall, Stables & Hockman, ISMIR 2016, https://archives.ismir.net/ismir2016/paper/000217.pdf).
- A forward RNN: 96.3% on IDMT-SMT-Drums, 69.3% on ENST-Drums at 100 Hz frames; a 25 ms time-shifted forward RNN: 98.2% and 73.1%, staying real-time with 25 ms delay; bidirectional ones are not real-time (Vogl, Dorfer & Knees, ISMIR 2016, https://archives.ismir.net/ismir2016/paper/000269.pdf).
- On ENST-Drums an HMM baseline scored 81.5%, above every RNN there (same paper, Table 2): the harder, more expressive set is where learned causal models still lag.
- Recommendation (derived): option A as the first template detector (own code, no trained weights, licence-clean); option B only once a permissively licensed training set and a device cost measurement exist (open).
- The current band-punch detector stays as the template bootstrapper and the fallback implementation of the same interface.

### 2.3 Harmonic/percussive split and its latency
- Median filtering across time removes percussion, across frequency removes harmonics (FitzGerald, DAFx 2010, https://www.audiolabs-erlangen.com/resources/aps-w23/papers/2010_FitzGerald_HarmonicPercussiveSep_DAFx.pdf).
- The percussion-enhancing filter works on one frame along frequency: P_i = M{S_i, l_perc} (same paper, eq. 6). So the percussive estimate adds no latency beyond its own frame (derived).
- The harmonic estimate needs a median across frames; a causal version sees only past frames (secondary, `music-analysis.md` §5); its quality loss is unmeasured (open).
- A single-frame decomposition (Tikhonov-regularised, one 4096 frame, 512 hop) separated bass from a mix "similar to the NMF-based high-latency approach at a lower computational cost" (Marxer & Janer, DAFx 2013, https://dafx.de/paper-archive/2013/papers/11.dafx2013_submission_13.pdf): frame-level latency, no numeric latency given.
- Design (derived): a soft mask from the per-frame frequency median (zero added latency) feeds the drum stage; the harmonic (time-median) side runs only on the slow path for harmony, never in front of onsets or drums.

### 2.4 Beat (period and phase)
- Recommended: real-time PLP, the app's current approach done fully: tempogram per frame, optimal windowed sinusoid kernel, overlap-add of the past kernels up to now (Meier, Chiu & Müller, TISMIR 2024, URL above).
- Measured: F1 74.72% at 11.61 ms latency on GTZAN; 74.68% at 0 ms with a one-frame lookahead offset (same paper, Table 2).
- The activation is the bottleneck: the same post-processor on a ground-truth activation reaches 91.93% online (same paper, Table 2).
- Kernel size: with the RNN activation, F1 barely changes until kernels fall under 3 s; they fixed 6 s (same paper, §6.3). So the app's 4–6 s windows sit on the flat part; tune by the Pareto method rather than fix (derived).
- Lookahead trades accuracy for early beats: 10 frames (116.1 ms) cost 0.41 F1 points on GTZAN, 100 frames 5.23 (same paper, Table 3). A visual can request beats early by a measured amount instead of a fixed offset (derived).
- Bias check, RT-PLP: its tempo set Θ = [30 : 240] is a flat search range, no weight inside (same paper, §5.5). Keep it as a limit only; derive its ends from the window and hop (section 5).
- Bias check, RT-PLP: their "TR40" variant restricts the range to ±40% around the track's known mean tempo (same paper, Table 2). That is an oracle tempo: reject.
- Bias check, RT-PLP: the paper suggests narrowing Θ to steer the output pulse level (same paper, §4.5). That is a chosen tempo band: reject for this app.
- Bias check, RT-PLP: its RNN activation was pretrained on Ballroom (same paper, §5.2), whose beats average 129.7 ± 39.7 BPM (Table 1). A learned activation can carry the training set's tempo distribution; unmeasured (open). The app's flux activation has no training set, so this risk does not arise there.
- Alternative: the bar-pointer state space with the tempo-change model f = exp(−λ |φ̇_k / φ̇_{k−1} − 1|), applied only at beat positions, uniform initial distribution (Krebs, Böck & Widmer, ISMIR 2015, https://archives.ismir.net/ismir2015/paper/000239.pdf).
- Bias check, Krebs 2015: the change model depends only on the ratio of successive tempi, so it centres on the last heard tempo, never on a fixed one (same paper, eq. 10); λ = 0 makes every tempo equally likely, they used λ in [1, 300] (same paper, §2.3.3). Pass, if λ is tuned by measurement.
- Bias check, Krebs 2015: the initial distribution is uniform; the paper notes it is where knowledge "about tempo distributions" could go (same paper, §2.1.1). Keep it uniform.
- Bias check, Krebs 2015: states between tempo limits T_min, T_max, spaced logarithmically "to mimic the JNDs" (same paper, §2.3.2). Uniform over log-spaced states is uniform in log period: scale-invariant (derived, section 4). Pass, as a limit.
- Bias check, Krebs 2015: the text assumes four beats per bar "to simplify notation" (same paper, §2.3). Remove: meter is its own state (2.5).
- Alternative: BeatNet's particle filters (already verified in `music-analysis.md` §7). The tempo jump is exp(−λ_b |φ̇_k / φ̇_{k−1}|)-shaped, only at φ_b = 0, particles start uniform over the state space (Heydari, Cwitkowitz & Duan, ISMIR 2021, https://arxiv.org/pdf/2108.03576, eqs. 4–5, Algorithm 1). This answers the open question in `music-analysis.md` §7: no fixed-tempo centre, but the state-space tempo limits are not given in the paper (open).
- BeatNet cost: 5.23 s of inference for a 30 s excerpt with 1000 particles on a desktop Ryzen 9 3900X, linear in particles; 1750 particles were used (same paper, §3.3). That is ~17–30% of real time on a desktop core: likely too heavy beside rendering on a phone (derived; open until measured).
- Rejected: the real-time beat tracker of Stark, Davies & Plumbley (DAFx 2009, https://dafx.de/paper-archive/2009/papers/paper_65.pdf). Its tempo stage weights a comb filterbank by a fixed curve over tempo and searches one octave, 80 to 160 BPM, and its beat stage multiplies by a Gaussian on the log of the period ratio (same paper, §2.2–2.3). The octave limit and the curve cannot be removed without redesigning the method.
- Rejected: any method that takes a target tempo, a start tempo or a count-in (Stark 2009's "SDP+tempo" variant, same paper, §4).

### 2.5 Downbeat and meter, without priors
- BeatNet's second filter: meter φ̇_d ∈ {2, 3, …, φ̇_max}, uniform start, meter may change only at the downbeat with probability λ_d (same paper, eqs. 7–8). Downbeat F 46.49 on GTZAN (same paper, Table 2): downbeats are far from solved online.
- Bias check, BeatNet meter: the upper limit φ̇_max is a limit, flat inside; λ_d is a B-parameter; pass if both are set from the window (how many beats it holds) and by measurement.
- Recommended for this app (derived): meter as the period of the per-beat accent sequence, found by the DFT on Z_L, L = beats in the accent window, with every divisor of L a candidate (section 4).
- The app's `explained(m)` (`core/sound/Rhythm.cpp:390-399`) is already that quantity: the share of the accent variance in the m-periodic part, which equals the energy of the DFT coefficients at multiples of L/m (derived from the regular-polygon theorem: a regular d-gon in Z_c has nonzero DFT exactly at multiples of d, Amiot, arXiv math/0604210, https://arxiv.org/pdf/math/0604210, Prop. 1).
- So the generalisation is free: compute |Â(k)|² once per beat, read every meter m | L off it, compare each against its chance level (m − 1)/(L − 1) (the chance level the app's comment already states, `core/sound/Rhythm.cpp:381-382`).
- Nesting: if m | m′ then every m-periodic pattern is m′-periodic, so explained(m′) ≥ explained(m) always (derived); the choice must therefore be the smallest m whose excess over chance is not beaten significantly by a multiple of it. How to set "significantly" without a constant is open (an F-test on the variance split is one candidate; unverified).
- L itself is dynamic: the beats that fit the window chosen in 2.4; with L = lcm of the candidates heard so far, all candidates divide it (derived).
- Downbeat (the coset): once m is chosen, the downbeat is the residue class r in Z_m whose accents lead; equivalently the phase of the DFT coefficient at L/m (Amiot's phase lemma: shifting a set by t rotates coefficient k by −2πkt/c, arXiv 1208.4774, https://arxiv.org/pdf/1208.4774, Lemma 2).
- What counts as an accent must not be the backbeat rule: use the same feature vector for every slot (low-band change, drum-stage activations, harmonic change) and let the periodicity find which slot differs (derived; the kick-on-one/snare-on-two rule is an assumption to remove).
- Phrase: the same machinery one level up, over per-bar features on Z_P; no source found for causal phrase-length detection (open; also open in `music-analysis.md` §8). Until measured, phrase length is unknown, not 8.

### 2.6 Tempo as an output only
- bpm = 60 · hop rate / period (`core/sound/Rhythm.cpp:471`) stays a read-out; nothing upstream reads it (rule).
- Report the whole periodicity set (tatum, beat, bar periods with strengths), not one number; the pulse level a listener taps is ambiguous, and evaluation metrics accept double/half and off-beat (AMLt, Krebs 2015 §3.2).
- The cyclic tempogram folds tempi differing by a power of two into one class, a circle (Grosche, Müller & Kurth, ICASSP 2010, https://www.audiolabs-erlangen.com/content/05_fau/professor/00_mueller/03_publications/2010_GroscheMuellerKurth_TempogramCyclic_ICASSP.pdf, eq. 1–2). Its reference ρ only labels the circle, it weights nothing; a visual that wants an octave-free tempo can read that class.
- The bpm visual normalisation by the search limit (`core/viz/Reactions.cpp:71`) must follow the limit once the limit is dynamic (open: what the reaction matrix should scale bpm by).

### 2.7 Rhythmic patterns and subdivisions
- Tatum: the finest strong level of the periodicity set; subdivisions per beat n = beat period / tatum period, an integer read off the lattice, not assumed 2 or 4 (derived).
- Onset position: each onset's phase θ mod 1 on the circle group; quantised to Z_n per beat, Z_{nm} per bar (derived).
- Pattern descriptors from the DFT of the bar's onset set on Z_N: magnitudes are invariant to rotation (time shift) and inversion, |F_{A+α}| = |F_A| (Amiot, arXiv math/0604210, §1.3 (3)); phases locate the pattern (Amiot 2012, Lemma 2).
- Interval content is the autocorrelation on Z_c, and its DFT is |F_A|² (Lewin's remark, stated as a theorem in Amiot, arXiv math/0604210, Thm. 3).
- |F_A(d)| ≤ d with equality iff A is a regular d-gon (Amiot, arXiv math/0604210, Thm. 5): how close a pattern is to an even pulse at d per bar.
- Balance = 1 − |mean of e^{2πi x_k}|, evenness from the next coefficient; perfect balance = centre of gravity at the circle's centre (Milne, Herff, Bulger & Sethares, NIME 2016, https://zenodo.org/api/records/1176082/files/nime2016_paper0077.pdf/content, eq. 1, §2).
- For N with at most two distinct prime factors, every perfectly balanced pattern is a sum of regular K-gons with K prime and K | N (same paper, §2.1).
- Euclidean rhythms are the unique patterns maximising the sum of pairwise chord distances on the circle (Demaine et al., arXiv 0705.4085, https://arxiv.org/pdf/0705.4085, abstract): a reference for "as even as k onsets in n slots can be".
- Proposal for the `kickN` fields (derived; owner decision): keep the hit-count fields as they are (users bind them), and add metrical counterparts that fire on the kick nearest a lattice level (beat, bar, phrase) so a 3- or 5-beat bar gets a field too.

## 3. Polymorphic interfaces (signatures only)

```cpp
namespace geode::sound::rhythm {

// What every stage is told at configure(): all from the device or the chain, nothing hand-set.
struct HopClock {
    int sampleRateHz;      // the stream's rate
    int burstFrames;       // the device burst (AAudio framesPerBurst)
    int hopSamples;        // k × burstFrames, k from the Pareto tuning
    int64_t hopIndex;      // hops since the source began
};

struct OnsetHop { float strength; float sharpness; int offsetSamples; };
class OnsetDetector {
public:
    virtual ~OnsetDetector() = default;
    virtual void configure(const HopClock& clock) = 0;
    virtual void samples(std::span<const float> mono) = 0;                 // per block, time domain
    virtual OnsetHop hop(std::span<const float> logMagnitude) = 0;          // per hop, spectral
    virtual int latencySamples() const = 0;                                 // measured or derived, never a literal
    virtual void reset() = 0;
};

enum class Drum : int { Kick, Snare, Hat, Count };
struct DrumHop { std::array<float, static_cast<int>(Drum::Count)> activation; std::array<bool, static_cast<int>(Drum::Count)> hit; };
class DrumDetector {
public:
    virtual ~DrumDetector() = default;
    virtual void configure(const HopClock& clock, int bins) = 0;
    virtual DrumHop hop(std::span<const float> magnitude, std::span<const float> percussive) = 0;
    virtual void learn(Drum drum, std::span<const float> magnitude, float confidence) = 0;  // run-time templates
    virtual int latencySamples() const = 0;
    virtual void reset() = 0;
};

class PercussiveSplit {
public:
    virtual ~PercussiveSplit() = default;
    virtual void configure(const HopClock& clock, int bins) = 0;
    virtual void hop(std::span<const float> magnitude, std::span<float> percussive, std::span<float> harmonic) = 0;
    virtual int percussiveLatencyHops() const = 0;
    virtual int harmonicLatencyHops() const = 0;
    virtual void reset() = 0;
};

struct PulseLevel { float periodHops; float strength; };
struct Periodicity { std::span<const PulseLevel> levels; int beatLevel; };   // beatLevel −1: none heard (bpm 0)
class PeriodEstimator {
public:
    virtual ~PeriodEstimator() = default;
    virtual void configure(const HopClock& clock, int windowHops) = 0;
    virtual void push(float novelty) = 0;
    virtual Periodicity estimate() = 0;
    virtual void reset() = 0;
};

struct BeatState { double theta; float confidence; bool locked; };          // theta: beats as a real count
class PhaseTracker {
public:
    virtual ~PhaseTracker() = default;
    virtual void configure(const HopClock& clock) = 0;
    virtual void hop(float novelty, const Periodicity& periods) = 0;
    virtual BeatState at(int samplesAfterHop) const = 0;
    virtual void reset() = 0;
};

struct MeterState { int beatsPerBar; int downbeatResidue; float confidence; };  // beatsPerBar 0: unknown
class MeterEstimator {
public:
    virtual ~MeterEstimator() = default;
    virtual void configure(int maxBeatsHeld) = 0;
    virtual MeterState beat(int64_t beatIndex, std::span<const float> accentFeatures) = 0;
    virtual void reset() = 0;
};

struct PatternState { int slots; float balance; float evenness; std::span<const float> spectrumMagnitude; };
class PatternAnalyzer {
public:
    virtual ~PatternAnalyzer() = default;
    virtual void configure(int maxSlots) = 0;
    virtual void onset(double theta, float strength, int voice) = 0;
    virtual PatternState bar(const MeterState& meter, int tatumsPerBeat) = 0;
    virtual void reset() = 0;
};

}  // namespace geode::sound::rhythm
```

- Implementations per interface: OnsetDetector {EnvelopeRatio (today's Transient), OnlineFlux (Böck 2012 + max filter)}; DrumDetector {BandPunch (today), FixedTemplateNmf (Dittmar 2014), CausalRnn (Southall/Vogl 2016, later)}; PercussiveSplit {None, FrequencyMedian (FitzGerald, percussive side only), CausalMedian (both sides)}; PeriodEstimator {NormalisedAutocorrelation (today), FourierTempogram}; PhaseTracker {CausalPlp (Meier 2024), BarPointerFilter (Krebs 2015 / BeatNet)}; MeterEstimator {ZnSpectrum (2.5), ParticleMeter (BeatNet)}; PatternAnalyzer {ZnDft}.
- Real-time rule: one virtual call per block or per hop, never per sample; per-sample loops stay inside one implementation (derived). All buffers sized at configure(), no allocation in hop() (`music-analysis.md` §3).
- Every implementation reports its latency so the chain can sum it and shift phases by the measured total, replacing `kLatencyHops` (derived).

## 4. Algebraic structure

- Time: hop indices form the group (Z, +); a time shift is the group acting on signals; every linear time-invariant filter in the chain commutes with it (equivariance; standard, derived).
- The analysis ring of N hops is Z_N; a circular autocorrelation on it is the group convolution r = x ∗ x̃ with x̃(n) = x(−n), and the DFT on Z_N diagonalises it: F(r) = |F(x)|² (Wiener–Khinchin on Z_N; Lewin's interval-content theorem, Amiot, arXiv math/0604210, Thm. 3).
- The app's autocorrelation is linear, not circular, and normalised per lag (`core/sound/Rhythm.cpp:149-156`); the circular form is exact only for a window of whole periods; this mismatch is why it normalises by the overlap (derived).
- Shift invariance: |F_A| is invariant under translation and inversion (Amiot, arXiv math/0604210, §1.3 (3)); so tempo and pattern shape (magnitudes) do not depend on when the window starts, and only phase does (derived).
- Shift equivariance: a shift by t rotates coefficient k by −2πkt/c (Amiot, arXiv 1208.4774, Lemma 2); the beat phase is that rotation read at the beat's coefficient; this is exactly the PLP phase (`core/sound/Rhythm.cpp:211-240`).
- Phase lives in the circle group T = R/Z; the oscillator keeps a lift θ ∈ R; `frac` is the quotient map R → T and `wrapHalf` picks the representative in [−1/2, 1/2) for the error (`core/sound/Rhythm.cpp:42-43`, `:269`).
- Beat → slot in the bar is the homomorphism Z → Z_m, b ↦ b mod m (`slotOf`, `core/sound/Rhythm.hpp:94`); bar → slot in the phrase is Z → Z_P.
- Meter lattice: the subgroups of Z_L are exactly dZ_L for d | L, one per divisor, ordered by divisibility (the regular polygons of Amiot, arXiv math/0604210, Prop. 1 and Thm. 5; Milne et al. NIME 2016 §2.1 for K | N).
- An m-periodic accent sequence on Z_L (m | L) is a function on the quotient Z_L / mZ_L, and its DFT lives on the annihilator: the multiples of L/m (derived from Prop. 1); meter detection is projection onto these subspaces.
- Subdivisions: n tatums per beat, m beats per bar give Z_{nm} ⊃ the beat subgroup n·Z_{nm} ≅ Z_m; the app's `kick2 | kick4 | kick8 | kick16` is the 2-power chain of subgroups of Z_16 applied to hit counts, not to positions (derived).
- Tempo: positive periods form the multiplicative group (R>0, ·); tempo octaves are the subgroup 2^Z; the cyclic tempogram is the quotient R>0 / 2^Z, a circle (Grosche, Müller & Kurth 2010, eq. 1–2).
- Scaling time acts on tempo by multiplication; Krebs 2015's change model depends only on φ̇_k / φ̇_{k−1}, so it is invariant under that action (derived from eq. 10).
- No-bias statement (derived): the only measure on R>0 invariant under scaling is uniform in log period (Haar measure); a tracker that must not weight any tempo should be uniform in log period inside its limits; this is what Krebs's log-spaced uniform states give (standard measure theory; no music source fetched; unverified as a citation).
- Streaming statistics are monoids: (sum, sum of squares, count) under addition, the running maximum under max with identity −∞; associative merges let a hop's statistics be built from any block split of the samples (Blelloch, "Prefix Sums and Their Applications", CMU 1990, https://www.cs.cmu.edu/~guyb/papers/Ble93.pdf, §1).
- One-pole smoothers are first-order linear recurrences x_i = a x_{i−1} + b_i, solvable by an associative scan (Blelloch, same report, §1.4); k steps of a one-pole with coefficient a equal one step with a^k, so a time constant stated in seconds gives the same smoothing at any hop (derived; the app already converts seconds at configure).
- Not a monoid: the median (Transient's threshold); it needs the stored history, so it cannot be merged across blocks (derived).

## 5. Dynamic values and where each comes from

- Sample rate: the stream (AAudio/AudioRecord), at configure().
- Hop: k × the device burst, k chosen on the Pareto front of an annotated set (`music-analysis.md` §2); replaces `kHopSeconds`.
- Every statistic counted in hops (Transient's history, tempo cadence, phase cadence): stated in seconds and converted with the actual hop at configure(); replaces `kHistory = 100` and the hop counts in `Rhythm.cpp:14-17`.
- Analysis window (tempo ring, PLP kernel): Pareto tuning; RT-PLP is flat above 3 s with a real activation (Meier 2024, §6.3); replaces `kRing = 600` and `kPhaseWindow = 400`.
- Slowest period: window / periods needed for an estimate (today 3, `kPeriodsHeard`; itself a B-parameter to tune); replaces `GEODE_SOUND_BPM_SLOWEST` and the static_assert.
- Fastest period: the resolution limit of the novelty sequence, 2 hops (sampling theorem; derived); which pulse level is "the beat" is then a level choice in the periodicity set, not a search limit; replaces `GEODE_SOUND_BPM_FASTEST`.
- Thresholds (onset δ, lock lines, comb share, level share, meter margin): B-parameters, tuned on the annotated set, no latency cost (Stefani & Turchet 2021); replace `kComb`, `kLevel`, `kLockOn/Off`, the 0.05/0.35/0.1/0.4 confidence ramps, `kMinJumpDb`, `kMarginDb`, `kSpread`.
- Adaptive thresholds stay adaptive: Transient's median + MAD, Dittmar's compressed moving average (Dittmar 2014 §4.4), Böck's past-mean + δ.
- Latency compensation: the sum of each stage's reported latency, measured at configure() by an impulse through the chain (derived); replaces `kLatencyHops = 1.9f` and `kHarmonyLatency = 0.35f`.
- Drum templates: learned at run time from confident hits (proposal, unverified); band edges for the bootstrap detector from the templates' measured centroids once learned (proposal).
- Tempo-change rate λ (if a bar-pointer tracker is used): tuned by measurement; Krebs's range [1, 300] is where to search (Krebs 2015 §2.3.3).
- Particle counts (if particles are used): from the device's measured per-hop budget (APerformanceHint durations, `music-analysis.md` §7).
- Meter candidates: every divisor of the beats held (L), not {3, 4}; replaces `meter_ = 4`, `kMeterBeats = 24`, `std::array<float, 4>`.
- Phrase length: unknown until measured on Z_P over bars; replaces `% 8`, `/ 8.0f`, `barInPhrase_ = 7`, Form's 32/16/4 beats and `kMinSectionBars = 8`.
- Unlocked fallback phases: none for bar and phrase until a meter is heard (bar and phrase phase hold, as bpm stays 0); replaces `hitTime / 4.0` and `hitTime / 32.0`.

## 6. Risks and open questions

- No annotated test set on this repo: every accuracy above is a paper's, on its datasets; the app's own F-measure is unmeasured (open; the Pareto tuning needs one, with a permissive licence).
- Learned activations (RNN, CRNN) may carry their training sets' tempo distributions (GTZAN 119.4 ± 39.6, Ballroom 129.7 ± 39.7 BPM means, Meier 2024 Table 1); no paper fetched measures this effect (open). Non-learned flux avoids the question.
- Downbeat tracking online is weak in the best published system (F 46.49, BeatNet); the Z_L spectrum design here is unmeasured (open).
- Drum results are on drum-only or drum-plus-accompaniment sets (IDMT-SMT-Drums, ENST); full commercial mixes unmeasured (open).
- Run-time templates may lock onto the wrong sound (a bass note learned as a kick); needs a confidence gate measured on real mixes (open).
- Causal HPSS harmonic quality and the percussive mask's effect on drum F are unmeasured (open).
- BeatNet-class particle filters may not fit the phone's budget beside rendering (open, needs a device benchmark).
- Meter selection on the subgroup lattice needs a significance rule that is not a constant (open; F-test candidate unverified).
- Phrase length from the music, causally: no source found (open, carried from `music-analysis.md` §8).
- Changing the hop or windows shifts every downstream consumer that counts hops (Form, Transient, SoundEngine triggers); the change must convert all of them at configure() in one PR (risk).
- Making the search limits dynamic changes the bpm reaction's scale (`core/viz/Reactions.cpp:71`) and the frame's documented range (`core/sound/SoundFrame.h:139-140`) (risk).
- The no-bpm-bias gate reads `docs/`: this file avoids the gate's phrases; new edits must too.
- Not read here: other published meter and tempo models (Klapuri et al. 2006 and later); each needs the same bias check (range, start distribution, jump model, any curve over tempo) before use (open).
