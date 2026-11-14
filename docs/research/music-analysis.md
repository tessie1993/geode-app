# Music analysis pipeline: research (2026-11-14)

Scope: sound in (player, another app, mic on top) → one music analysis → parameters that move the GL scene.
Rule: no value is picked by hand. Each value is read from the device or comes with the algorithm the research selects, and is tuned by measurement.
Evidence: each line names its source; "secondary" means a paper or doc quoting the primary, not the primary read in full; "open" means no source settles it.
Clean room: methods and maths come from papers; no project code, names or strings enter `core/`.

## 1. Does the Oboe burst ↔ analysis hop mismatch lose or distort what the FFT sees?

- An STFT frame is defined by sample index (frame m covers samples mH … mH+N−1), not by callback boundaries.
- A FIFO between writer and reader hands the reader the identical sample sequence in order, whatever the block sizes on either side.
- So a burst/hop mismatch on its own loses nothing and distorts nothing; it only adds latency and jitter (a hop finishes inside a later callback).
- In this app loss and distortion do happen, from the frame-pull design, not from the mismatch:
- The analysis is pulled once per GL frame (`core/api/geode_viz_api.cpp` `geode_viz_feed_ring`, then `LiveFrame::run` renders).
- A take keeps only the newest `maxFrames` (`core/audio/ingest/PcmRing.cpp`, `Cursor::take`: `from = max(read_, oldest, end - maxFrames_)`).
- The player ring's runway is 2^14 frames, 341 ms at 48 kHz (`core/sound/interp/SpeakerSync.hpp`); the mic ring's is a quarter of 2^15, 171 ms at 48 kHz (`core/audio/ingest/RingKinds.hpp`).
- A GL stall longer than that (scene build, shader compile, return from background) drops samples: loss.
- `Take::skipped` is computed but read nowhere outside `PcmRing.cpp`: the samples before and after the gap are fed to the analysis as if continuous.
- That splice is a step in the waveform (a false transient) and shifts every beat phase by the gap: distortion.
- With no screen drawing nothing is analysed at all.
- Fix class: push-driven analysis that consumes every sample as it arrives (section 3).

## 2. Framing: where the numbers must come from

- AAudio: "For the lowest possible latency, do not call [setFramesPerDataCallback]… AAudio may need to add additional buffering" (https://developer.android.com/ndk/reference/group/audio).
- So the analysis adapts to the device burst; the stream is never forced to the analysis.
- framesPerBurst is listed as set by the stream (Oboe Full Guide, https://android.googlesource.com/platform/external/oboe/+/HEAD/docs/FullGuide.md); the adversarial check refuted "set only by the stream" 0-3 (AAudio may scale the burst), so the app reads it at runtime and never assumes it.
- Typical native buffer sizes are 96, 128, 160, 192, 240, 256 or 512 frames, "other values are possible" (https://developer.android.com/ndk/guides/audio/audio-latency).
- A non-native rate adds a resampler and a block-adapting buffer (Oboe implementation notes, https://chromium.googlesource.com/external/github.com/google/oboe/+/e5c061bb06657c9b953b0af33ba1651421661414/src/common/README.md).
- Multirate graphs run inside time-triggered callbacks through buffering add jitter, latency and run-time cost; a static schedule split across callback activations fixes it (CLAM time-triggered SDF, secondary: https://scite.ai/authors/pau-arumi-XeZWw).
- The FFT need not be a power of two: KissFFT has optimized radix 2, 3, 4, 5, and its real FFT needs an even length (https://github.com/mborgerding/kissfft).
- A NEON FFT with BSD-like terms takes N = 2^a·3^b·5^c, a multiple of 32 for real transforms (https://github.com/marton78/pffft).
- So a hop of k bursts (e.g. 480 = 2^5·3·5) is a valid FFT-friendly size, and hops can end exactly on callbacks.
- The current 10 ms hop (`core/sound/Rhythm.hpp` `kHopSeconds`) is a convention, not a requirement: the literature uses 86 Hz (512 at 44.1 kHz), 100 Hz, 200 Hz and 250 Hz onset grids.
- Ellis 2007 calls its 4 ms grid "some suitable grid" (Ellis, JNMR 36(1), https://hajim.rochester.edu/ece/sites/zduan/teaching/ece472/reading/Ellis_2007.pdf).
- Böck & Widmer use 100 fps in tempo work (ISMIR 2015, https://www.ismir2015.uma.es/articles/196_Paper.pdf) and 200 fps for SuperFlux (ISMIR 2013, https://archives.ismir.net/ismir2013/paper/000175.pdf).
- How to pick each analysis's window and hop without hand-picking: tune the latency parameters against an annotated set and take a point on the accuracy–latency Pareto front (Stefani & Turchet, DAFx 2021, https://iris.unitn.it/retrieve/a254bb18-4efc-450e-ae46-514667f4a777/DAFx20in21_paper_20.pdf).
- That paper separates "A-parameters" (they set detection latency: window, hop) from "B-parameters" (thresholds, no latency cost).

## 3. Pipeline architecture

- Real-time callback rules: no allocation, no locks, no file or network I/O, no sleep; move data out "using non-blocking techniques such as an atomic FIFO" (AAudio reference, link above).
- A rare processing spike longer than the buffer period glitches the stream (Bencina 2011, secondary: https://danielappelt.github.io/talk-real-time-audio-programming).
- Lock-free queues between the real-time callback and non-real-time threads are the established pattern (Bencina & Dannenberg, ICMC 2005, secondary via the same summary).
- A streaming analysis library's default scheduler favours throughput and "does not take into account the latency of the network"; real-time needs a latency-first scheduler (Bogdanov et al., ISMIR 2013, https://www.justinsalamon.com/uploads/4/3/9/4/4394963/bogdanov_essentia_ismir13.pdf).
- The same paper's model: algorithms joined by typed streams; each declares the block size it consumes and runs as soon as that many tokens are waiting.
- Dataflow systems split synchronous dataflow (samples, frames) from asynchronous control (Bray & Tzanetakis, ISMIR 2005, https://archives.ismir.net/ismir2005/paper/000125.pdf).
- Android can move a thread between core types by recent load; the Performance Hint API takes target and actual work durations per thread id (https://developer.android.com/games/optimize/adpf/performance-hint-api).
- Devices are "STRONGLY RECOMMENDED" to keep CPU performance consistent while audio is active (CDD 5.10, https://android.googlesource.com/platform/compatibility/cdd/+/refs/heads/pie-gsi/5_multimedia/5_10_professional-audio.md).
- Publishing features to many readers without locks: seqlock or triple buffer (Doumler, ADC 2024 seqlock talk, title only: https://irc.pcper.com/wait-free-thread-synchronisation-with-the-seqlock-timur-doumler-adc-2024.html). Open: the talk's content was not read.

## 4. Time–frequency representation

- The STFT has uniform resolution; a constant-Q transform has finer frequency resolution low and finer time resolution high (Holighaus et al., IEEE 2013, https://arxiv.org/pdf/1210.0084).
- A real-time CQT must be sliced, which trades delay against the finest frequency resolution (same paper).
- So no single representation serves transients and pitch at once: transients want short frames, pitch and chroma long ones.
- Multi-resolution onset detection has been compared with fixed resolution (DAFx, https://dafx.de/paper-archive/details/oyzBdAE-tMScuANtoQclrQ). Open: results not read.

## 5. Methods per musical aspect (real-time, causal)

### Transients and onsets
- Spectral flux, weighted phase deviation and complex domain reach comparable top performance (Dixon, DAFx 2006, https://dafx.de/paper-archive/details/_kscRgr98aSFW4l-j1YJuA).
- A maximum filter along frequency before the flux cuts vibrato false positives by up to 60% without missing events; it is causal (Böck & Widmer, DAFx 2013, https://dafx.de/paper-archive/details/0oee-99Z88WL7pSo749gcA).
- Hybrid: a time-domain detector is precise in timing but prone to false positives; a spectral one is reliable but less precise; combine both (Turchet, DAFx 2018, https://dafx.de/paper-archive/details/15UE-H-WV64jQVPL_RQUxQ).
- Sounds less than 30 ms apart are generally perceived as simultaneous (Stefani & Turchet 2021, citing their ref. [8]).
- 10 ms is cited as the action-to-sound target for percussion (Martelloni & McPherson, ISMIR 2023, https://archives.ismir.net/ismir2023/paper/000013.pdf, citing their ref. [10]).
- Offline-style onset schemes in one real-time evaluation had 1,024 samples of latency (EURASIP JASP 2011, https://link.springer.com/doi/10.1186/1687-6180-2011-68).

### Drums (kick, snare, hats)
- Real-time transcription and separation of drums with NMF and drum templates runs "with very small systemic delay" (Dittmar & Gärtner, DAFx 2014, https://dafx14.fau.de/papers/dafx14_christian_dittmar_real_time_transcription_a.pdf).
- Semi-adaptive templates did not beat fixed ones there (Wu & Lerch, ISMIR 2015, secondary: https://musicinformatics.gatech.edu/wp-content_nondefault/uploads/2015/10/Wu_Lerch_2015_Drum-Transcription-using-Partially-Fixed-Non-Negative-Matrix-Factorization-With.pdf).
- Today's code finds drums from band energies; no template or decomposition stage.

### Harmonic/percussive split
- Median filtering across time removes percussion, across frequency removes harmonics (FitzGerald, DAFx 2010, https://www.audiolabs-erlangen.com/resources/aps-w23/papers/2010_FitzGerald_HarmonicPercussiveSep_DAFx.pdf).
- A causal version has half the filter and costs delay; one implementation reports ~167 ms for a filter long enough to separate well (secondary: https://github.com/sevagh/Zen).
- So HPSS belongs on the slow path, never in front of transients.

### Pitch, lead melody, polyphonic notes
- YIN: one F0 per frame with one threshold; pYIN outputs several candidates with probabilities and smooths them with an HMM (Mauch & Dixon, ICASSP 2014, https://qmro.qmul.ac.uk/xmlui/handle/123456789/6040?show=full).
- Open: Viterbi smoothing needs look-ahead; an online decoder's latency was not found.
- Online predominant melody in polyphonic audio by harmonic cluster tracking beats the authors' offline baseline (Arora & Behera, IEEE TASLP 2013, https://pure.ulster.ac.uk/en/publications/on-line-melody-extraction-from-polyphonic-audio-using-harmonic-cl-3/).
- Real-time melody and bass lines by tracking F0 trajectories (Goto & Hayamizu, IJCAI-99 workshop, https://staff.aist.go.jp/m.goto/PAPER/CASA99.pdf).
- Multiple F0: salience as a weighted sum of harmonic amplitudes, polyphony estimated after; 93 ms frames; efficient (Klapuri, ISMIR 2006, https://archives.ismir.net/ismir2006/paper/000125.pdf).
- Today's code tracks one lead pitch only.

### Chroma, key, chords
- Harmonic pitch class profile: chroma that counts harmonics (Gómez, INFORMS J. Computing 18(3) 2006; Gómez & Herrera, ISMIR 2004, https://archives.ismir.net/ismir2004/paper/000164.pdf).
- Key from chroma against Krumhansl probe-tone profiles (same 2004 paper).
- Real-time chords for live performance: mask the expected notes in the chromagram and minimise the residual energy; more than major/minor triads (Stark & Plumbley, ICMC 2009, https://quod.lib.umich.edu/i/icmc/bbp2372.2009.019/--real-time-chord-recognition-for-live-performance?view=image).
- Today's `chordHue` is a chroma centre, not a recognised chord.

### Beat, meter
- Beat tracking as dynamic programming over an onset envelope (Ellis 2007, link above). Its formulation starts from a target tempo given in advance; under the no-BPM-bias rule the period must come from the music alone and nothing may weight a tempo.
- Causal real-time beat tracking with performance comparable to offline methods (Stark, Davies & Plumbley, DAFx 2009, https://dafx.de/paper-archive/details/XiEJjIc0a2Hb_KTJon0SaA).
- Open: today's downbeat assumes 4/4 and a phrase is 8 bars; no source here justifies either.
- Evaluation conventions: onsets matched within 50 ms (secondary: https://arxiv.org/pdf/2408.13734); beats by F-measure within ±70 ms (secondary: https://arxiv.org/html/2605.12287). Davies & Böck say objective beat evaluation overestimates performance (ISMIR 2014, https://archives.ismir.net/ismir2014/paper/000238.pdf).

### Energy, build-up, drop, structure
- Loudness: ITU-R BS.1770 (already native, `core/sound/LufsMeter.hpp`).
- A drop follows a build of tension and re-introduces the full bassline; detection in two stages: sound-based candidates, then temporal structure (Yadati et al., ISMIR 2014, https://archives.ismir.net/ismir2014/paper/000297.pdf).
- Open: that paper's accuracy was not read; F1 0.71 is a third party's figure.
- Nearly all perceived boundaries lie on peaks of novelty functions (Chew et al. 2014, https://qmro.qmul.ac.uk/xmlui/bitstream/123456789/22602/1/Chew%20Audio%20properties%20of%20perceived%202014%20Accepted.pdf).
- Causal grouping by smoothing novelty and tracking its peaks (Jensen, LNCS 2005, secondary: https://core.ac.uk/download/60395784.pdf).
- Lag matrices compare each moment only with the past: causal-friendly (Grill & Schlüter, EUSIPCO 2015, https://new.eurasip.org/Proceedings/Eusipco/Eusipco2015/papers/1570104539.pdf).
- Listeners segmenting in real time respond later and at fewer levels than offline annotators (Hartmann, Lartillot & Toiviainen, JNMR, https://jyx.jyu.fi/bitstream/123456789/52654/1/978-951-39-6903-5_vaitos17012017.pdf).

### Instruments
- No hand-designed method found that recognises instruments in a mix in real time.
- Small distilled audio taggers exist: 4.88 M parameters, 0.54 G multiply-accumulates per 10 s clip, AudioSet mAP up to .487 for the largest released (Schmid et al. 2022, https://ar5iv.arxiv.org/html/2506.23437). Open: on-phone latency and instrument accuracy unmeasured.
- Embedded instrument detection is necessarily delayed; one report gives 56.5% accuracy at 30.7 ms (secondary, unverified).
- On-device runtime: NNAPI is deprecated in Android 15; the C/C++ path is LiteRT with the GPU or XNNPACK CPU delegate (https://developer.android.com/ndk/guides/neuralnetworks/migration-guide, https://developers.google.com/edge/litert/android/gpu_native).

## 6. Libraries

- The C/C++ music analysis libraries surveyed are GPL-2, GPL-3 or AGPL-3 (non-commercial; commercial licence on request): none can ship in this closed app. The owner chose: our own code, methods learned from papers (2026-11-14).
- FFTs with permissive terms: KissFFT (revised BSD, in `third_party/`, no NEON path) and a NEON FFT with BSD-like terms (links in section 2). A 2015 Arm benchmark found the NEON-optimized FFTs well ahead of KissFFT on Cortex-A9/A53 (https://community.arm.com/arm-community-blogs/b/operating-systems-blog/posts/ne10-fft-feature-radix-3-and-radix-5-fft-are-supported-neon-optimization-significant-performance-improvement-by-neon-optimization).

## 7. Adversarially verified (deep-research run 2026-11-14, 3 voters per claim, 2 refutes kill)

- The callback must not block: no allocation, I/O, mutex or sleep, no heavy one-time work; even-paced; hand data out lock-free (3-0; Oboe Full Guide, https://developer.android.com/games/sdk/oboe/low-latency-audio, https://source.android.com/docs/core/audio/latency/app).
- Light fixed-cost work per buffer (a ring copy) is fine in the callback; heavy or variable-cost analysis belongs outside it (3-0, same sources; the split itself is an inference).
- A non-native rate takes a slower path: OboeTester round trip 160 ms (44.1 kHz via AAudio) vs 23 ms (Oboe resampler) vs 20 ms (all advice followed), one unnamed device (3-0, https://developer.android.com/games/sdk/oboe/low-latency-audio).
- EXCLUSIVE/MMAP bypasses the mixer but silently falls back to the legacy path when unavailable; the app cannot normally tell (3-0, https://source.android.com/devices/audio/aaudio).
- Another app's audio: Java AudioRecord with AudioPlaybackCaptureConfiguration only; no AAudio/Oboe path documented (3-0, https://developer.android.com/media/platform/av-capture).
- Input latency figures in the CDD are recommendations; the only hard limit is cold input ≤ 500 ms (2-1, https://android.googlesource.com/platform/compatibility/cdd/+/refs/tags/android-9.0.0_r30/5_multimedia/5_6_audio-latency.md).
- Without performance hints the governor can take ~200 ms to ramp; the NDK APerformanceHint API exists from API 33 (our minSdk), later calls need runtime checks (3-0, https://source.android.com/docs/core/perf/performance-hint-api, https://github.com/google/oboe/wiki/Using-ADPF-for-High%E2%80%90Quality-Audio-Performance).
- A wait-free SPSC triple buffer suits "latest feature frame only"; one serves one reader, so N renderers need N buffers or a multi-reader scheme (3-0, design shown in an ISC-licensed audio library: https://github.com/mozilla/cubeb/commit/304ef97245244e98cf8e3324eb348d9c01717f59; clean room: the idea only).
- Onsets: half-wave-rectified flux on a log, semitone-spaced filterbank (82 bands, 27.5 Hz–16 kHz), 2048-sample Hann, strictly online peak picking: F 80.3% on 27,774 onsets at ±25 ms; online within ~0.25% F of offline picking (3-0, Böck, Krebs & Schedl, ISMIR 2012, https://archives.ismir.net/ismir2012/paper/000049.pdf).
- Its causal peak picker: local max over the past w1 frames, above the past-w3 mean plus δ, a minimum gap of w5 frames, no look-ahead (same paper).
- A causal RNN on three right-aligned STFTs (512/1024/2048 samples) of 24 log-Bark bands plus differences: F 0.817 with no look-ahead (3-0, Böck et al., DAFx 2012, https://www.dafx.de/paper-archive/2012/papers/dafx12_submission_4.pdf): evidence for a shared multi-resolution, right-aligned causal front end.
- Beat, downbeat and meter with no time signature given: causal CRNN plus two particle filters, meter a state in {2, 3, …} that changes only at downbeats; beat F 75.44, downbeat F 46.49 on unseen GTZAN at ±70 ms, ahead of the online baselines (3-0, Heydari, Cwitkowitz & Duan, ISMIR 2021, https://arxiv.org/pdf/2108.03576).
- Open under the no-BPM-bias rule: whether that particle filter's tempo-state range, initial distribution or jump model carries a tempo prior.
- Refuted (excluded): ±1 ms audio timestamp accuracy; a specific ADPF target-duration formula.
- Not verified by that run (kept above as single-source or secondary): drums, HPSS, melody and multi-pitch, chroma/key/chords, loudness, build-up/drop, structure, instruments, CQT vs STFT, FFT libraries, MIR library licences.

## 8. Open questions

- The latency of an online pYIN-style decoder.
- Measured cost and accuracy of each candidate on this app's target phones (needs a device benchmark and an annotated test set; the Pareto method in section 2).
- Instrument recognition accuracy and latency on device.
- Meter and phrase length from the music, causally: no source found yet.
- The seqlock / triple-buffer trade-off for many readers (talk not read).
