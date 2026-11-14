# Stage 1: ingest and framing (research, 2026-11-14)

Scope: sound in (player tap, another app's audio, the mic layered on top) → PCM ring → framed hops for the one analysis per source.
Read with `docs/research/music-analysis.md` (sections 1–3 and 7 there are not repeated here: the burst/hop mismatch, the callback rules, the triple buffer).
Evidence: every line names a fetched primary source, or is marked "inference" (follows from cited facts), "unverified" (not read in full) or "open" (no source settles it).
Code facts are at commit `b759e7c6f`.
Clean room: methods and maths from papers and platform docs only; no code from other projects.

## 1. How the app does it now

### Sources and writers
- The player tap is a Media3 `TeeAudioProcessor.AudioBufferSink` on the playback thread: `flush()` starts a ring epoch, `handleBuffer()` hands the direct buffer to native (`app/src/main/java/dev/geode/engine/audioandroid/PcmTap.kt:26-42`).
- Media3 documents only that `flush` is "called when the audio processor is flushed with a format of subsequent input" and `handleBuffer` gets "a read-only buffer" (https://developer.android.com/reference/androidx/media3/exoplayer/audio/TeeAudioProcessor.AudioBufferSink); the tap's block size is the decoder's, not a device burst (inference).
- The tap writes only while it holds the ring; `geode_pcm_ring_hand_over` flips that and begins an epoch (`core/api/geode_ingest_api.cpp:161-175`).
- Another app's audio: a Java `AudioRecord` with `AudioPlaybackCaptureConfiguration` on a Kotlin thread at `THREAD_PRIORITY_URGENT_AUDIO`, blocking reads into one direct buffer (`app/src/main/java/dev/geode/audio/AudioCapturePump.kt:82-108`, `PlaybackCapture.kt`).
- That capture asks the plan with device rate 0 ("unknown"), so it never tries the device's own rate first (`AudioCapturePump.kt:43`; `CapturePlan.cpp` `capturePlan`).
- The mic asks with `PROPERTY_OUTPUT_SAMPLE_RATE` (`app/src/main/java/dev/geode/ui/CaptureController.kt:199`).
- The mic is an Oboe input stream (exclusive then shared, `LowLatency`), callback writes the mic ring directly (`core/audio/ingest/android/OboeMic.cpp:58-98`, `core/audio/ingest/MicInput.cpp:55-67`).
- Neither mic nor capture reads `getFramesPerBurst`, `getXRunCount` or a capture timestamp; the only `getTimestamp` call in the audio code is the output's (`core/audio/output/OboeOutput.cpp:198`).
- The capture read size is a fixed 1024 frames and the AudioRecord buffer a fixed 4 platform minimums (`core/audio/ingest/CapturePlan.hpp:13,15`); `MicInput::kCapacityFrames` is their product (`MicInput.hpp:71`).

### The ring
- `PcmRing`: single writer, many readers, power-of-two capacity, a runway a reader stays behind, atomic head, epoch word, per-write rate and channel marks (`core/audio/ingest/PcmRing.hpp:41-132`).
- Sizes are constants: player 2^19 frames with a 2^14 runway (`core/sound/interp/SpeakerSync.hpp:28-29`), mic 2^15 mono, output mix 2^15 stereo (`core/audio/ingest/RingKinds.hpp:26-31`).
- `Cursor::take` keeps only the newest `maxFrames` after a stall and sets `skipped` (`core/audio/ingest/PcmRing.cpp:222-246`); `skipped` is read nowhere outside the ring and its tests (grep at `b759e7c6f`).
- The clock is the sample count; no wall time except the snapshot stitch (`core/audio/ingest/README.md`, "Clock").

### Mixing
- The mic is mixed at each reader, not in a ring: `LayerMix` queues the mic, linearly resamples it by the nominal rate ratio and adds it to the player's block (`core/audio/ingest/LayerMix.cpp:46-102`).
- Its join, cap and alone times are hand-picked constants: 0.10 s, 0.25 s, 0.30 s (`core/audio/ingest/LayerMix.hpp:29-31`).
- Drift between the two clocks is handled only by dropping the queue's oldest audio above the cap (`LayerMix.cpp:73-78`): an unmarked splice in the mic signal.
- `LayerMix::pull` grows a `std::vector` on the GL thread (`LayerMix.cpp:41`), and `trim`/`render` erase from its front (O(n) moves).

### Framing and analysis
- Analysis is pulled, once per GL frame per renderer: `geode_viz_feed_ring` takes up to 4 runs from the renderer's own cursor and writes them into that renderer's `SoundEngine` (`core/api/geode_viz_api.cpp:188,363-425`).
- So two surfaces on one source (screen and wallpaper) each run a full analysis of the same samples (inference from `geode_viz_api.cpp:150-156`: one cursor and engine per `geode_viz`).
- The player's reader stops at the speaker's sample from the playout clock (`geode_viz_api.cpp:401`, `SpeakerSync.hpp:38-41`).
- The hop is a constant 10 ms (`core/sound/Rhythm.hpp:36`), the window 2048 frames (`core/sound/SoundEngine.hpp:62`); neither reads the device.
- No thread is created for analysis, no ADPF session exists, and no code sets affinity (grep for `APerformanceHint`, `sched_`, `affinity`: none).

## 2. The optimal design, with sources

### Push, one analysis per source
- Owner's model: analysis is pushed as audio arrives, one per source, never per screen; today's pull per GL frame breaks both halves (section 1).
- Target: each source's writer publishes into its ring; one hop worker per active source consumes every sample once and publishes feature frames stamped with their sample position (inference; matches the dataflow split of synchronous sample streams and asynchronous control in Bray & Tzanetakis, ISMIR 2005, cited in music-analysis.md §3).
- Renderers then read feature frames by presentation sample (the existing `core/sound/interp` plan), so the speaker sync moves from PCM reads to feature reads (inference from `core/sound/interp/README.md`).
- With analysis at the tap rather than the speaker, the player ring's 2^19 frames only serve readers that need raw PCM behind the sink (Record, Cast); its size should then come from those readers (inference).

### Device facts read at runtime
- `AAudioStream_getFramesPerBurst`: "the number of frames that the application should read or write at one time for optimal performance" (https://developer.android.com/ndk/reference/group/audio).
- The same entry: "For some endpoints, the burst size can vary dynamically. But these tend to be devices with high latency." So the burst is a hint, never a contract.
- Without `setFramesPerDataCallback`, "numFrames can vary" per callback (same page).
- `PROPERTY_OUTPUT_FRAMES_PER_BUFFER` is the HAL buffer; "construct your audio buffers so that they contain an exact multiple of this number" for regular callbacks (https://developer.android.com/ndk/guides/audio/audio-latency).
- The same page: "Don't assume that the actual sample rate exactly matches the nominal sample rate" and that capture and playback rates differ; independent clocks need asynchronous rate conversion.
- `AudioRecord.getBufferSizeInFrames` (API 23) may be "enlarged ... on creation or if the AudioRecord is subsequently rerouted" (https://developer.android.com/reference/android/media/AudioRecord): read it after open and after a route change.
- Open: no documented burst getter exists for `AudioRecord`; whether `PROPERTY_OUTPUT_FRAMES_PER_BUFFER` also bounds the capture path is unverified.

### Framing from those facts
- An STFT frame m covers samples mH … mH+N−1 by sample index; block boundaries do not change it (music-analysis.md §1).
- The window N and hop H in seconds come from the analysis algorithm and its latency/accuracy Pareto tuning (Stefani & Turchet, DAFx 2021, in music-analysis.md §2), never from the device.
- The device only quantises: H_frames = a multiple of the source's natural block when one exists (mic: burst; tap and capture: none), N_frames = the nearest FFT-friendly length 2^a·3^b·5^c at or above the window (music-analysis.md §2) (inference).
- Synchronous dataflow: a periodic schedule exists only if the topology matrix has rank s−1, and the firings per period q lie in its null space (Lee & Messerschmitt, IEEE Trans. Computers C-36(1) 1987, Theorem 1 and Fig. 6, https://ptolemy.berkeley.edu/publications/papers/87/staticscheduling/).
- For one edge (callback makes B frames, hop eats H) the balance is q_cb·B = q_hop·H, so q = (H/g, B/g) with g = gcd(B, H); H = k·B gives one hop every k callbacks, ending on a callback boundary (inference from the theorem).
- Because the burst may vary and the tap has no burst, alignment is to the ring's sample lattice, not to callbacks: hops fire when head ≥ next hop end, whatever the block sizes (inference).
- Aligning hops to callbacks only lowers latency and jitter on the mic; it never changes what the analysis sees (music-analysis.md §1).

### Multirate schedule
- Each analysis family has its own (N_i, H_i): transients short, pitch and chroma long (Holighaus et al. 2013 and Böck et al. DAFx 2012, music-analysis.md §4 and §7).
- With all hops multiples of a base hop h0 (h0 = gcd of the H_i), one worker runs a static cyclic schedule whose period is lcm(H_i) (inference from Lee & Messerschmitt's periodic schedule).
- Right-aligned windows of different lengths sharing the newest sample are evidenced for causal onsets (Böck et al., DAFx 2012, music-analysis.md §7).

### Gaps and epochs
- An input overrun "will occur if the stream is not read in time ... the incoming data ... is discarded" and some input devices always return 0 (`AAudioStream_getXRunCount`, NDK audio reference above).
- `AAudioStream_get*()` calls are allowed in the data callback (same page), so the mic callback can read the xrun count per block.
- `AAudioStream_getTimestamp` "can also be used to align a recorded stream with a playback stream"; positions are monotonic and valid only while started (same page).
- `AudioRecord.getTimestamp` (API 24) gives frame delivery "at the earliest point available in the capture pipeline", in MONOTONIC or BOOTTIME (AudioRecord reference above); the class lists no overrun counter.
- So a capture gap is detectable as timestamp frame position advancing more than frames read (inference; open: verify on devices).
- Ring lapping (a slow reader) already yields `Take::skipped` with a known frame count (`PcmRing.cpp:245-246`).
- A gap of known length n is a time shift: clock-like state (beat and bar phase, onset age) advances by n exactly; signal-holding state (window, filter memory, flux history) is invalid and restarts (inference; section 4).
- A gap of unknown length, a flush or a source change is an epoch: everything restarts (current behaviour for epochs, `PcmRing.hpp:141`).

### Mixing sources
- The two sources run on independent clocks whose ratio is not known a priori and drifts (Android audio-latency page above).
- The established control: model written and read sample counts W(t), R(t) as continuous functions of time, evaluate W − R − Δ each period, drive a variable resampling ratio with a delay-locked loop on timestamps; buffer fill level alone is unreliable (Adriaensen, "Controlling adaptive resampling", §3.1, https://kokkinizita.linuxaudio.org/papers/adapt-resamp.pdf).
- Both inputs carry the timestamps that model needs (AAudio and AudioRecord `getTimestamp` above; the player's `PlayoutClock` already does this for the sink).
- Under the owner's one-analysis-per-source rule the mic gets its own analysis, and sources merge as features, not PCM; only Record and Cast still need a PCM mix (inference; open: owner's choice).
- If a PCM mix stays, its jitter buffer and drift correction come from the measured timestamp loop, replacing the 0.10/0.25/0.30 s constants (inference).

### Threads and wake-up
- The data callback must not use "mutexes or other synchronization primitives" and must not sleep or allocate (NDK audio reference above).
- `sem_post` "shall be async-signal-safe" (POSIX, https://pubs.opengroup.org/onlinepubs/9699919799/functions/sem_post.html); the tap's playback thread and the capture's read thread may post.
- Open: whether posting a semaphore from the Oboe mic callback breaks the "no synchronization primitives" rule in practice; the alternative is the worker waking on its own period (H/fs) and reading the head atomically.
- The hop worker runs H/fs behind at most; it needs no real-time priority of its own if the ring holds N + H + measured worst lateness (inference).

### ADPF and big.LITTLE
- `APerformanceHint_createSession(manager, threadIds, size, initialTargetWorkDurationNanos)`; `setThreads` replaces the thread list (https://developer.android.com/ndk/reference/group/a-performance-hint).
- The API is "intended for periodic workloads": report the actual duration each cycle and the framework steers toward the target (same page).
- `notifyWorkloadIncrease/Reset/Spike` are for sudden changes, rate-limited per app (same page); Android 16 adds them (Oboe ADPF wiki, https://github.com/google/oboe/wiki/Using-ADPF-for-High%E2%80%90Quality-Audio-Performance).
- For an audio callback the target is "the period of your callback" from rate and frames per callback (Oboe ADPF wiki above).
- So each hop worker's target is its hop period H/fs, and it reports its measured work per hop (inference from both).
- "Your app shouldn't try to set the CPU core affinity"; big cores may be thermally unsustainable; the hint API decides placement (https://source.android.com/docs/core/perf/performance-hint-api).
- The same page: the governor "can take ~200ms to ramp up or down CPU frequency".
- Linux places a task by capacity fitness, task_util < capacity, and moves "misfit" tasks to bigger CPUs (https://docs.kernel.org/scheduler/sched-capacity.html).
- uclamp is set by `sched_setattr()` or cgroups (same page); open: whether an app may set it on Android.
- ADR 0008 forbids lowering fps, scale or quality for heat (AGENTS.md); ADPF only reports work and never trades quality, so it is compatible (inference).

## 3. The polymorphic interface (signatures only)

### Choice of polymorphism
- A virtual call costs about a call through a function pointer, 20–25% over a direct call in the measured implementations (ISO/IEC TR 18015:2004 §5.3.3, https://stroustrup.com/performanceTR.pdf).
- The same report: templates and virtual functions "are often complementary techniques".
- A concept "is a template that defines constraints on its template arguments" and "is not instantiated" (C++ draft [temp.concept], https://eel.is/c++draft/temp.concept): zero run-time cost.
- A `std::variant`'s contained value "is nested within" the variant object ([variant.variant.general], https://eel.is/c++draft/variant.variant.general): a closed set of algorithms switchable at run time without a heap (no-allocation claim is inference from "nested").
- Rule (inference): virtual at the source edge (open, start, stop: rare), concepts inside the per-hop loop (inlined), a `std::variant` where the user switches algorithms live, and never a dispatch per sample.
- Open: data-oriented layout (structure of arrays across bands and sources) was not researched from a primary source.

### Sketch
- `struct DeviceFacts { int rateHz; int framesPerBurst; int halFramesPerBuffer; int bufferFrames; }`: read once per open and after each route change; 0 means unknown.
- `struct BlockStamp { int64_t firstSample; int64_t epoch; int rateHz; int channels; };`
- `struct Gap { int64_t frames; bool lengthKnown; };`
- `class PcmSource` (virtual): `DeviceFacts open(const SourceRequest&)`, `bool start()`, `void stop()`, `SourceKind kind() const`; implementations Tap, PlaybackCapture, Mic, a test fake.
- `concept BlockSink`: `s.write(std::span<const float>, const BlockStamp&)` and `s.gap(const Gap&)` and `s.epoch(int64_t)`; the ring satisfies it.
- `struct HopSpec { int windowFrames; int hopFrames; int offsetFrames; };` built by `HopSpec derive(const AlgorithmTiming&, const DeviceFacts&)`.
- `struct Hop { std::span<const float> window; int64_t endSample; int rateHz; bool afterGap; int64_t gapFrames; };`
- `concept HopConsumer`: `c.spec() -> HopSpec`, `c.consume(const Hop&)`, `c.shift(int64_t frames)` (known gap), `c.reset()` (epoch).
- `template <HopConsumer... C> class Framer`: one per source; owns the cursor, builds every consumer's window from the ring by sample index, runs the static lcm schedule.
- `class HopWorker`: one thread per active source; wakes per hop, runs its `Framer`, reports work to its ADPF session, publishes feature frames.
- `class SourceClockMap` (one per non-player source): sample ↔ monotonic time from `getTimestamp`, its ratio to the player's tracked by a DLL; `PlayoutClock` is the player's instance.

## 4. Algebraic structure

### Block concatenation: the free monoid and its action
- Finite blocks under concatenation form a monoid (associative, identity = empty block) (definition; Tangwongsan et al. use the same monoid vocabulary, arXiv:2009.13768 §2.1, https://arxiv.org/abs/2009.13768).
- The analysis state is acted on by that monoid: state·(a‖b) = (state·a)·b (inference).
- Consequence: any split of the stream into blocks must give the same features; the property test is random split points against one whole write (`core/tests/pcm_ring_test.cpp` already does this for the ring).
- A gap is not the empty block: it is a separate element Gap(n), and the analysis must define its action (shift or reset), which today's splice does not (section 2).

### Sliding-window statistics
- Window aggregations "can usually be expressed as binary operators that are associative" but "not necessarily commutative nor invertible" (Tangwongsan, Hirzel & Schneider, arXiv:2009.13768, abstract).
- Subtract-on-evict "only works when subtraction is well-defined" (same paper, §8.3): that is when the values form a group.
- Two-stacks does each operation in amortized O(1) and worst-case O(n) combines; DABA in worst-case O(1): "at most one time per query, four times per insert, and three times per evict" (same paper, §3 and Theorem 5.2).
- Sum, sum of squares and count are group-valued (inverse = subtraction): O(1) sliding mean and energy (inference).
- Max, min and peak hold are only monoids: they need two-stacks or DABA, never subtraction (inference from the paper).
- Floating-point addition is not associative: ((2·10^-30 + 10^30) − 10^30) − 10^-30 computes to −10^-30 instead of 10^-30 (Goldberg, ACM Computing Surveys 1991, https://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html).
- So float subtract-on-evict drifts; a true group is unsigned integer arithmetic, "performed modulo 2^N" ([basic.fundamental], https://eel.is/c++draft/basic.fundamental).
- Design: accumulate fixed-point (quantised at the source's bit depth) in uint64 for exact O(1) inverse, or use the monoid (DABA) path for floats (inference).
- Open: compensated (Kahan) summation as a float alternative; Higham's analysis (SIAM J. Sci. Comput. 14(4) 1993) was not read.

### The sample clock as a group of time shifts
- Sample positions form the group (ℤ, +); a shift by n acts on the stream; an epoch picks a new origin (inference).
- A hop lattice Hℤ is a subgroup; its cosets ℤ/Hℤ are the hop phases; aligning hops to callbacks is choosing a coset representative (inference).
- Several hops: H1ℤ + H2ℤ = gcd(H1,H2)ℤ (the base hop) and H1ℤ ∩ H2ℤ = lcm(H1,H2)ℤ (the schedule period) (standard subgroup facts of ℤ; inference).
- A known gap of n samples is the action of n ∈ ℤ: phase-like state shifts exactly; this is what lets `shift(n)` replace a reset (inference).

### The DFT as the cyclic group's Fourier transform
- The DFT computes the Fourier coefficients "of a function on the cyclic group Z/nZ"; the Fourier matrix "is effectively the character table of a cyclic group" (Maslen & Rockmore, "The Cooley–Tukey FFT and Group Theory", MSRI Publ. 46, 2003, https://library.slmath.org/books/Book46/files/11maslen.pdf).
- "The DFT diagonalizes any group invariant operator" (same paper): shifts become per-bin phase factors.
- Cooley–Tukey for n = pq is a factorisation g = y·h over the subgroup H = qℤ/nℤ and coset representatives (same paper, §2): mixed-radix lengths 2^a·3^b·5^c are subgroup chains, which is why non-power-of-two windows cost little.
- The sliding DFT updates bins per sample by that shift property (Jacobsen & Lyons, IEEE SPM 20(2) 2003, DOI 10.1109/MSP.2003.1184347; unverified: not read, paywalled).
- Open: the sliding DFT's subtract-on-evict inherits the float drift above; whether a per-bin recursive form stays stable over a song was not checked.

## 5. Dynamic values and where each comes from

- Sample rate fs: the stream's actual rate (Oboe `getSampleRate` after open, `AudioRecord.getSampleRate`, the tap's `flush` rate); already carried per write (`PcmRing::writeFloat`).
- Requested capture rate: `PROPERTY_OUTPUT_SAMPLE_RATE` first for both mic and capture (fix `AudioCapturePump.kt:43`, which passes 0).
- Burst B: `getFramesPerBurst()` per open and per reroute; may vary (NDK reference).
- HAL buffer: `PROPERTY_OUTPUT_FRAMES_PER_BUFFER` (audio-latency page).
- Capture read size: the hop H (consumer demand under the balance equation), replacing the 1024 constant (inference).
- Capture buffer: start at the platform minimum and grow on measured gaps, as AAudio suggests tuning size with `getXRunCount` "at run-time for each device" (`AAudioStream_setBufferSizeInFrames`, NDK reference), replacing the 4× constant.
- Window N and hop H in seconds: each algorithm's own, tuned on the latency–accuracy Pareto front (music-analysis.md §2); quantised to frames by fs and B.
- Base hop h0 and schedule period: gcd and lcm of the hops (section 4).
- Ring capacity: next power of two ≥ N_max + H_max + fs × measured worst worker lateness + the writer's largest block (inference), replacing 2^15 and 2^19.
- Mix jitter buffer and drift ratio: the DLL on timestamps (Adriaensen above), replacing 0.10/0.25/0.30 s.
- ADPF target: hop period H/fs per worker (Oboe ADPF wiki, inference).
- Gap length: xrun delta (mic), timestamp vs frames read (capture), `skipped` count (ring lap).
- None of these values reads or weights a tempo (no-BPM-bias rule).

## 6. Risks and open questions

- The tap's block size and timing come from Media3's decode loop; any hop alignment there is to samples only (Media3 documents no block size).
- `getXRunCount` returns 0 on some input devices (NDK reference): gap detection must also use timestamps.
- Posting a semaphore from the Oboe callback may count as a forbidden synchronization primitive (open).
- Float sliding statistics drift without exact integer groups or periodic re-summation (section 4).
- Moving analysis from GL threads to per-source workers changes thread count; each must join an ADPF session or risk the ~200 ms governor ramp (performance-hint page).
- PCM mixing of mic and player at one rate needs a DLL; without it the queue keeps splicing (`LayerMix.cpp:73-78`).
- The capture's `getTimestamp` accuracy on real devices, and whether playback capture reports one at all, are unverified.
- Whether the mic should feed its own analysis or stay a PCM layer is the owner's call (one analysis per source suggests its own).
- Whether a known-length gap should shift or reset the rhythm state needs a test with annotated audio with dropouts (open).
- Data-oriented layout, uclamp access and the sliding DFT's stability were not researched from primary sources (open).
