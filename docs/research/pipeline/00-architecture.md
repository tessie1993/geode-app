# 00: The architecture that holds the analysis pipeline together

Research only. Nothing here is built. One fact per line. Code facts are pinned to commit `b759e7c6f` (file:line). Every outside claim cites a source from the list below that was fetched and read for this note. A claim with no source is marked **design** (a proposal derived from cited facts), **unverified** or **open**.

## Sources (fetched and read)

- [S1] E. A. Lee, D. G. Messerschmitt, "Synchronous Data Flow", Proc. IEEE 75(9):1235–1245, 1987. Pages 1235–1238 read from the prepublished scan: http://ptolemy.berkeley.edu/publications/papers/87/synchdataflow/synchdataflow.pdf (DOI 10.1109/PROC.1987.13876).
- [S2] P. Arumí, X. Amatriain, "Time-triggered Static Schedulable Dataflows for Multimedia Systems", SPIE/MMCN 2009: https://amatria.in/pubs/parumi_mmcn09.pdf
- [S3] J. Lin, "Monoidify! Monoids as a Design Principle for Efficient MapReduce Algorithms", arXiv:1304.7544, 2013: https://arxiv.org/abs/1304.7544
- [S4] P. O. Boykin, S. Ritchie, I. O'Connell, J. Lin, PVLDB 7(13):1441–1451, 2014 (DOI 10.14778/2733004.2733016): http://www.vldb.org/pvldb/vol7/p1441-boykin.pdf
- [S5] K. Tangwongsan, M. Hirzel, S. Schneider, "In-Order Sliding-Window Aggregation in Worst-Case Constant Time", arXiv:2009.13768 (extended version of DEBS 2017, DOI 10.1145/3093742.3093925): https://arxiv.org/abs/2009.13768
- [S6] G. E. Blelloch, "Prefix Sums and Their Applications", CMU-CS-90-190, 1990: https://www.cs.cmu.edu/~scandal/papers/CMU-CS-90-190.html (PDF http://www.cs.cmu.edu/~guyb/papers/Ble93.pdf)
- [S7] J. Hughes, "Generalising Monads to Arrows", Sci. Comput. Program. 37:67–111, 2000; draft of 10 Nov 1998 read: https://www.cs.tufts.edu/~nr/cs257/archive/john-hughes/arrows.pdf
- [S8] K. Driesen, U. Hölzle, "The Direct Cost of Virtual Function Calls in C++", OOPSLA 1996 (DOI 10.1145/236337.236369): https://static.aminer.org/pdf/PDF/000/522/086/the_direct_cost_of_virtual_function_calls_in_c.pdf
- [S9] L. Dionne, "Runtime Polymorphism: Back to the Basics", CppCon 2017, slides: https://github.com/ldionne/cppcon-2017-runtime-polymorphism (slides.pdf, slides 9.1–9.3)
- [S10] C++ working draft, [mem.res.monotonic.buffer]: https://eel.is/c++draft/mem.res.monotonic.buffer
- [S11] C++ working draft, [mem.res.class]: https://eel.is/c++draft/mem.res.class
- [S12] C++ working draft, [mem.res.global]: https://eel.is/c++draft/mem.res.global
- [S13] C++ working draft, [expr.pre]: https://eel.is/c++draft/expr.pre
- [S14] C++ working draft, [temp.concept]: https://eel.is/c++draft/temp.concept
- [S15] Clang User's Manual, `-ffp-contract` and `-ffp-model`: https://clang.llvm.org/docs/UsersManual.html
- [S16] Android NDK reference, Performance Hint Manager: https://developer.android.com/ndk/reference/group/a-performance-hint
- [S17] AOSP header `include/android/performance_hint.h` (API levels): https://android.googlesource.com/platform/frameworks/native/+/refs/heads/main/include/android/performance_hint.h
- [S18] Android SDK reference, `PerformanceHintManager`: https://developer.android.com/reference/android/os/PerformanceHintManager
- [S19] Android, "Analyze thread scheduling" (CPU core affinity): https://developer.android.com/agi/sys-trace/threads-scheduling
- [S20] Android, ADPF overview: https://developer.android.com/games/optimize/adpf
- [S21] Android NDK guide, AAudio: https://developer.android.com/ndk/guides/audio/aaudio/aaudio
- [S22] R. Bencina, "Real-time audio programming 101: time waits for nothing", 2011: http://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing
- [S23] Arm, "Coding for Neon – Part 1: Load and Stores": https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/coding-for-neon---part-1-load-and-stores

Not reachable from this container, so not cited: the WG21 papers N3916 (pmr) and P0734 (concepts) on open-std.org (connection reset); the standard's draft text [S10]–[S14] is cited in their place.

## The current state (file:line)

### Where sound enters

- One native PCM ring per audio source, single writer, many readers, no locks or allocation after construction: `core/audio/ingest/PcmRing.hpp:8-15`.
- The ring's clock is the sample count, with no wall time: `core/audio/ingest/PcmRing.hpp:27`.
- Every write carries its own sample rate and channel count, so a reader never pairs new samples with an old rate: `core/audio/ingest/PcmRing.hpp:69-73`.
- A new stream (track change, seek, source switch) starts an epoch that restarts every cursor: `core/audio/ingest/PcmRing.hpp:63-65`.
- The ingest README already names the missing piece: "The ring is here; the hop thread that should read it is not": `core/audio/ingest/README.md:3`.
- Which ring a resting surface hears (the app's ring or the output-mix capture) is a pure policy stepped on a millisecond clock: `core/sound/SoundSource.hpp:5-24`, `:56-94`.
- Its hysteresis times are compile-time constants (`kStallMs` 1000, `kReturnMs` 500, `kRetryMs` 10000): `core/sound/SoundSource.hpp:27-31`.

### Where analysis runs today

- A live renderer takes from its ring with its own cursor on the GL thread, then calls `Renderer::writeAudio`: `core/api/geode_viz_api.cpp:363-420`.
- The take loop is bounded at `kFeedRunsPerFrame = 4` takes per frame: `core/api/geode_viz_api.cpp:188`, `:404`.
- `Renderer::writeAudio` takes `soundLock_` (a `std::mutex`) around the engine write: `core/viz/Renderer.cpp:94-106`.
- It builds a new `SoundEngine` with `std::make_unique` when the rate changes, on the GL thread: `core/viz/Renderer.cpp:99-101`.
- The renderer owns one engine: `core/viz/Renderer.hpp:352`.
- The engine says so itself: "One engine per surface (screen, wallpaper, export), never shared between threads": `core/sound/SoundEngine.hpp:56-57`.
- So two surfaces that hear the same ring (the screen and the wallpaper on the player's ring) each run the whole analysis: this is the "per screen" analysis the owner's model rules out.
- The frame step takes the same mutex, latches settings, forwards player signals and calls `sound_->step(dt, soundFrame_)`: `core/viz/RendererFrame.cpp:57-85`.
- The resolver then runs one step per frame on that frame: `core/viz/RendererFrame.cpp:217`.

### The engine is already a hand-scheduled multirate dataflow

- The engine is "two analyses side by side ... both on one sample clock": `core/sound/SoundEngine.hpp:20-21`.
- Its own diagram is a dataflow graph: write → ring and filter bank per sample → every hop band powers → Rhythm, and `analyse()` → window → Hann → FFT → levels → Harmony → Form → Emotion: `core/sound/SoundEngine.hpp:38-44`.
- Per-sample nodes: input sanitising, decimation, DC blocker, trim: `core/sound/SoundEngine.cpp:201-234`.
- Per-sample nodes: ring write, filter bank, transient detector, loudness meter: `core/sound/SoundEngine.cpp:236-245`.
- The hop fires when `hopFill_` reaches `rhythm_.hopSamples()`: `core/sound/SoundEngine.cpp:246-256`.
- The hop is a compile-time 10 ms (`Rhythm::kHopSeconds = 0.01f`): `core/sound/Rhythm.hpp:36`.
- The FFT size is derived at runtime from the rate (the smallest power of two ≥ 40 ms, from 256): `core/sound/SoundEngine.cpp:88-92`.
- The analysis rate is derived at runtime (halved up to three times while ≥ 88.2 kHz): `core/sound/SoundEngine.cpp:94-98`.
- The rhythm ring's length is fixed at compile time by a `static_assert` over the slowest tempo and the hop: `core/sound/Rhythm.hpp:113`.
- `step()` copies the newest hop's frame, folds in triggers held since the last step, moves phases to the frame's sample and draws the waveform: `core/sound/SoundEngine.cpp:284-311`.
- Held triggers are a per-reader fold (`held_`), reset at each step: `core/sound/SoundEngine.hpp:259`, `core/sound/SoundEngine.cpp:296-297`.
- When the writer stops for > 0.25 s, `step(dt)` feeds `dt` worth of silence, so the starved path is clocked by the frame's `dt`, not by samples: `core/sound/SoundEngine.cpp:259-265`, `:286-291`.
- All hop state is one `State` struct, so a reset is one assignment: `core/sound/SoundEngine.hpp:117-188`.
- Per-hop scratch is `std::vector`s sized at construction: `core/sound/SoundEngine.hpp:264-282`.
- The FFT holds a shared, cached plan and vectors allocated in its constructor: `core/sound/Fft.hpp:14-26`.
- The engine claims "Allocation-free after construction": `core/sound/SoundEngine.hpp:57`.

### The small stages

- `core/sound/Stages.hpp` holds per-hop kernels, each taking `dt`: `perFrame`, `due`, `follow`, `softLimit`, `RelativeLevel`, `Trigger`, `Envelope`, `HardCut`: `core/sound/Stages.hpp:9-163`.
- `follow()` is a one-pole recurrence `y ← x + (y − x)·e^(−dt/τ)`: `core/sound/Stages.hpp:26-29`.
- `due()` keeps a cadence by accumulating float seconds with a 1 ms tolerance: `core/sound/Stages.hpp:18-23`.
- `RelativeLevel` keeps an attack envelope and a running mean as one-pole recurrences: `core/sound/Stages.hpp:46-66`.
- Its time constants are compile-time constants: `core/sound/Stages.hpp:79-83`.
- None of these declares an interface; they are concrete classes with a `step` and a `reset`.

### The consumer chain

- `signal::Resolver` is the per-frame chain from requested params to drawn params, in a fixed order: `core/signal/Resolver.hpp:21-32`.
- It is single-threaded, takes everything as plain values, and two resolvers with the same inputs give the same bits: `core/signal/Resolver.hpp:34-42`.
- Its one entry point: `Resolver::step(requested, sound, dt, settings, style, heard, fadeDt)`: `core/signal/Resolver.hpp:84-86`.
- Modulators (LFOs, ADSRs, reactions, director, macro bank, motion field) are concrete members, not an interface: `core/signal/Resolver.hpp:104-114`.

### Build flags that bear on determinism

- C++20, `-O2` and `-flto` in Release, no `-ffp-contract` flag, no `-ffast-math`: `CMakeLists.txt:12-22`.
- There is no `std::pmr`, no `APerformanceHint` and no `sched_setaffinity` anywhere in `core/` or `app/src/main/cpp` (grep at this commit).

## The optimal architecture

### The shape

- **design** One analysis graph per sound source (player tap, mic, other-app capture, an export's decoded track); never one per surface.
- **design** The graph is built at runtime from the source's format, the device and the chosen algorithm for each stage; nothing in its shape is a compile-time constant.
- **design** Surfaces (screen, wallpaper, dream, Cast, export) read the graph's published features; each keeps its own `Resolver`, which stays per frame and per surface.
- **design** Modulators and the director stay on the consumer side, behind interfaces (`Modulator`, `DirectorPolicy`) with virtual dispatch at frame rate.

### The graph is synchronous dataflow

- SDF is dataflow in which "the number of data samples produced or consumed by each node on each invocation is specified a priori" [S1, abstract].
- Because of that, nodes "can be scheduled statically (at compile time) onto single or parallel programmable processors so the run-time overhead usually associated with data flow evaporates" [S1, abstract].
- "Multiple sample rates within the same system are easily and naturally handled" [S1, abstract].
- A delay of d samples on an arc is "implemented ... by initializing the arc buffer with d zero samples" [S1, p. 1236].
- The schedule's repetition counts are the solution of the balance equations, "equivalent to finding a vector in the null-space of the graph topology matrix" [S2, §3].
- "Any rate-consistent graph will have a schedule unless the graph has loops, and there is a lack of initial delays in the looping arcs" [S2, §3].
- **design** "At compile time" becomes "at graph build": the topology matrix is filled from runtime rates (the device rate, the decimation count, the hop and window each algorithm asks for), and the schedule is solved then.
- **design** Today's engine maps onto three rates: per-sample nodes (1:1), hop nodes (consume `hopSamples`, produce 1 token), and the frame consumer (variable, outside the graph).
- **design** An overlapped FFT window that reads n samples and advances by `hop` is an SDF arc that consumes `hop` per firing with n − hop initial tokens (the delay of [S1]); this is what `readWindow` does by hand today (`core/sound/SoundEngine.cpp:267-282`).
- **design** Slower musical layers (tempo, harmony, form, emotion) are nodes with lower rates (consume k hop tokens, produce 1), so the scheduler sees the true rate of each layer instead of running everything every hop.

### Time-triggered activations, and why the ring changes the picture

- Plain SDF "lacks the notion of time: executing non-trivial schedules of multi-rate dataflows in a time-triggered callback architecture ... causes jitter, excessive latency and run-time inefficiencies" [S2, abstract].
- TTSDF adds time-triggered input and output actors and a static scheduler whose periodic schedule "can be split among several callback activations" [S2, abstract].
- In TTSDF "the concatenation of one or more callbacks activations must form a periodic scheduling" [S2, §4.1.1].
- An l-latency schedule starts with a prologue of l empty callback activations [S2, Def. 7].
- The minimum-latency schedule is found by trying l = 0 … period until one does not deadlock [S2, §4.2 listing].
- TTSDF "enables parallelization and run time load balancing between callback activations" [S2, abstract].
- TTSDF's model assumes each input actor reads a callback buffer of a known token count [S2, §4.1.1].
- **design** Android does not give the analysis that: the tap, the mic and the capture hand the ring blocks of varying size (`core/audio/ingest/PcmRing.hpp:69-73`), and AAudio's burst size is system-controlled [S21, "Tuning buffers"].
- **design** So the ring is the graph's only input actor, and the analysis fires on the SDF firing rule alone ("enough tokens on every input arc"), counted in samples, never on a callback boundary.
- **design** This makes every firing a function of the sample index, which is the property determinism needs (see Determinism below).
- **design** The TTSDF idea that survives is the split: one period of the static schedule is cut into activations, each with a bounded cost, and slow nodes are spread over activations with a fixed added latency l.

### Push, pull and never waiting

- **design** Push in: the writer pushes samples into the ring and returns; it never runs analysis.
- **design** Push through: the source's analysis thread drains the ring and pushes tokens through the static schedule.
- **design** Pull out: a surface reads the newest published feature snapshot at its frame's sample position (from the playout clock, `core/sound/interp/SpeakerSync.hpp`), as `step()` does today.
- **design** Fast layers never wait on slow ones: a slow node's output for hop k is consumed at hop k + L, with L fixed at graph build (TTSDF's l-latency prologue [S2, Def. 7]), so a fast node reads a value that is always there.
- **design** L is never "whenever the worker finishes": that would make the features depend on thread timing and break export equality.
- **design** Publication is a sample-stamped snapshot (seqlock or triple buffer), so readers never lock the writer; the lock-free claim of the ring (`core/audio/ingest/PcmRing.hpp:12-15`) extends to the snapshot.

### What becomes per reader

- **design** Held triggers become a per-reader fold over the hops since that reader's last read; it is the max semigroup (`held_` today, `core/sound/SoundEngine.hpp:259`).
- **design** The waveform drawing stays per reader, since it depends on the frame's own sample.
- **design** Beat, bar and phrase phases are moved to the reader's sample by the reader, as `rhythm_.phases(hopFill_, …)` does now (`core/sound/SoundEngine.cpp:306`).

## Interfaces: signatures only

```cpp
namespace geode::graph {

// Algebra (core/graph/Algebra.hpp)
struct Laws { bool associative; bool commutative; bool invertible; bool idempotent; bool exactInFloat; };
template <class M> concept Semigroup = requires(const typename M::T& a, const typename M::T& b) {
    { M::combine(a, b) } -> std::same_as<typename M::T>; { M::laws } -> std::convertible_to<Laws>; };
template <class M> concept Monoid = Semigroup<M> && requires { { M::identity() } -> std::same_as<typename M::T>; };
template <class M> concept Group = Monoid<M> && requires(const typename M::T& a) { { M::inverse(a) } -> std::same_as<typename M::T>; };
template <class R> concept Semiring = Monoid<typename R::Add> && Monoid<typename R::Mul>;
template <Monoid M> class Fold;                 // reader-side fold since last read
template <Monoid M> class Window;               // FIFO sliding window; picks subtract-on-evict when Group, two-stack/DABA-style otherwise
template <class A> struct Affine;               // y -> a*y + b, a monoid under composition

// Streams and rates
struct Rate { int64_t samplesPerToken; };       // derived at build, never a literal
template <class T> struct Port { int consumes; int produces; int initialTokens; };

// A kernel: statically dispatched inside a node
template <class K> concept Kernel = requires(K k, std::span<const typename K::In> in, std::span<typename K::Out> out) {
    { K::laws } -> std::convertible_to<Laws>;
    { k.consumes() } -> std::same_as<int>; { k.produces() } -> std::same_as<int>;
    { k.fire(in, out) } noexcept; { k.reset() } noexcept; };

// Composition at the type level (inside one node)
template <Kernel F, Kernel G> auto then(F, G);          // F >>> G
template <Kernel F> auto first(F);                      // run F on the first of a pair
template <Kernel F, Kernel G> auto fanout(F, G);        // F &&& G, F before G

// A node: type-erased, local storage, built once
struct NodeVTable { void (*fire)(void*, const Buffers&) noexcept; void (*reset)(void*) noexcept; };
class NodeRef { void* self; const NodeVTable* vt; };

// Build
struct DeviceCaps { int sampleRateHz; int channels; int cpuCount; int64_t preferredUpdateRateNs; int framesPerBurst; };
struct StageChoice { std::string_view stage; std::string_view algorithm; };
class GraphBuilder {
public:
    explicit GraphBuilder(std::pmr::memory_resource* buildArena);
    template <Kernel K, class... Args> NodeId add(std::string_view name, Args&&...);
    void connect(NodeId from, int outPort, NodeId to, int inPort);
    std::expected<Plan, BuildError> plan(const DeviceCaps&) const;      // balance equations, schedule, buffer sizes, L
    std::unique_ptr<AnalysisGraph> build(const Plan&, std::pmr::memory_resource* graphArena) const;
};
struct Plan { std::span<const int> repetitions; std::span<const Firing> period; std::size_t arenaBytes; int slowLatencyHops; };

// Run
class AnalysisGraph {
public:
    void advance(const audio::ingest::Cursor::Take&) noexcept;   // fires every firing whose tokens are ready
    int64_t samplesConsumed() const noexcept;
    void reset() noexcept;                                         // new epoch
};
struct FeatureSnapshot { int64_t sample; GeodeSoundFrame frame; sound::Emotion::State emotion; };
class FeatureBus {
public:
    void publish(const FeatureSnapshot&) noexcept;                 // analysis thread only
    bool read(FeatureSnapshot& out, int64_t atOrBeforeSample) const noexcept;  // any reader, wait-free
};
class ReaderView { public: void step(int64_t frameSample, GeodeSoundFrame& out) noexcept; };  // per surface: held triggers, phases, waveform

// Drivers: the same graph under both
class LiveDriver    { public: LiveDriver(std::shared_ptr<const audio::ingest::PcmRing>, AnalysisGraph&, FeatureBus&); void runOnce() noexcept; };
class OfflineDriver { public: OfflineDriver(AnalysisGraph&, FeatureBus&); void feed(const float* interleaved, int frames, int channels, int rateHz) noexcept; };

// Consumers on top (frame rate, inheritance)
class Modulator      { public: virtual ~Modulator() = default; virtual void step(const FeatureSnapshot&, float dt, viz::SceneParams&) noexcept = 0; };
class DirectorPolicy { public: virtual ~DirectorPolicy() = default; virtual void step(const FeatureSnapshot&, float dt, viz::SceneParams&) noexcept = 0; };

}  // namespace geode::graph
```

## The algebraic framework

### Monoids as the interface for partial results

- A monoid is "an algebraic structure with a single associative binary operation and an identity element" [S3, §2].
- The design principle: "create a monoid out of the intermediate value emitted by the mapper" [S3, §2].
- A mean is not associative, but (sum, count) under element-wise sum is, "with a division at the end" [S3, §2].
- The final division "destroys the monoid, but this is a one-time termination operation that can be treated as post-processing" [S3, fn. 3].
- [S3, §4] proposes "declaring ... objects as monoids (for example, via an interface)" so an optimisation layer can apply combining automatically.
- [S4, §3] defines semigroup, monoid and commutative semigroup, and requires values to be at least commutative semigroups for correct aggregation.
- [S4, §3]: with one processor consuming one ordered input, "semigroups are sufficient for correctness, i.e., we do not need commutativity".
- [S4, §3]: commutativity is needed "as soon as we introduce partitioned input", because partial results "may be aggregated in arbitrary order".
- [S4, §4]: max and min are semigroups (no identity over the integers); sets under union are monoids; monoids compose into tuples.
- **design** One source's samples arrive in one order, so a per-source chain needs only associativity; commutativity is asked for only when a stage splits its work across workers (per band, per bin).
- **design** Examples in the engine today: band energy sums over a hop (`sum`, `core/sound/SoundEngine.hpp:191`) are a commutative monoid; held triggers and punch peaks (`held_`, `punchNow_`, `core/sound/SoundEngine.hpp:251`, `:259`) are the max semigroup, which is also idempotent.

### Groups: invertible results and sliding windows

- [S5, Table 1] sorts aggregations by algebra: sum-like (sum, count, mean, geomean, stddev) are invertible, associative and commutative; max-like (max, min, argMax) are associative and commutative but not invertible.
- [S5, §2] defines a left inverse as a "known and reasonably cheap" `inv` with `a⊗b⊗inv(a) = b`.
- With an inverse, a FIFO window evicts by combining the inverse (subtract-on-evict).
- Without one, DABA keeps O(1) worst-case aggregation steps per insert, evict and query, requiring only associativity and FIFO order [S5, §1].
- DABA Lite needs space for n + 2 partial aggregates against DABA's 2n [S5, abstract].
- Worst case, not average, is what a real-time path needs: "you should always consider the worst-case execution time of your code" [S22].
- **design** A `Window<M>` picks its algorithm from `M::laws` at build: subtract-on-evict for a `Group` whose laws say `exactInFloat`, a de-amortised two-stack window otherwise.
- **design** Float sums are a group only in exact arithmetic: subtract-on-evict in `float` drifts; such windows use an integer or fixed-point accumulator, or the non-invertible algorithm (risk below).
- **design** Beat, bar and phrase positions are counters modulo n: the cyclic group Z/nZ; moving a phase by a number of samples is that group acting on the position, which is why `rhythm_.phases` can move a hop's phase to any frame exactly (`core/sound/SoundEngine.cpp:303-306`).

### Semirings and scans

- The all-prefix-sums operation "takes a binary associative operator ⊕" [S6, §1.1].
- Its tree implementation's "correctness ... relies on ⊕ being associative" [S6, §1.2].
- A first-order recurrence `x_i = (x_{i−1} ⊗ a_i) ⊕ b_i` reduces to a scan when ⊕ is associative, ⊗ is semiassociative, and ⊗ distributes over ⊕ [S6, §1.4].
- The reduction pairs `c_i = [a_i, b_i]` with `c_i • c_j = [c_i.a ⊙ c_j.a, (c_i.b ⊗ c_j.a) ⊕ c_j.b]`, which is associative [S6, eq. 1.7].
- **design** Every one-pole smoother in the engine (`follow`, the `att_` and `mean_` of `RelativeLevel`, `core/sound/Stages.hpp:26-29`, `:48-57`) is such a recurrence over (+, ×), so a block of steps composes into one affine map `(a, b)`.
- **design** That lets a smoother advance by a whole hop in one combine and lets a worker compute a slow layer's catch-up as a tree; the tree's shape must still be fixed by the sample index (Determinism).
- **design** The same scan over the (max, +) semiring is the shape of a dynamic-programming beat or path tracker; whether a chosen tracker fits is for the algorithm agents (01..07) to say: **open**.

### How a stage declares its laws, and what the scheduler may do with each

- **design** Each kernel carries `static constexpr Laws laws`; the builder reads it, nothing at run time does.
- **design** `associative`: the builder may block a stage (fold n samples into one partial) or tree-reduce it, with a tree fixed at build.
- **design** `commutative`: the builder may partition the stage across workers and merge partials.
- **design** `invertible`: `Window<M>` may evict by inverse.
- **design** `idempotent`: overlapping partials may be merged twice without harm (max over overlapping windows), so a reader that misses or repeats a hop still holds the right peak.
- **design** `exactInFloat`: false for every float `+`; then reassociation is forbidden and only the declared, fixed order is used.
- The compiler itself may regroup operators "only where the operators really are associative or commutative" [S13, ¶ on regrouping], so source order is the evaluation order the builder controls.

### Arrows: composing stages

- Hughes' `Arrow` class has `arr :: (b → c) → a b c` and `(>>>) :: a b c → a c d → a b d` [S7, §4].
- `first` lifts an arrow to act on the first component of a pair; `second`, `***` and `&&&` are defined from it [S7, §4.1].
- `first` was chosen as primitive because "evaluation order makes a difference", and `&&&` defined from it is "explicit about this: the effects of f are composed with the effects of g in that order" [S7, §4.1].
- For arrows with effects, product laws such as `(f &&& g) >>> arr fst = f` "are simply false" [S7, §4.1].
- [S7, §6] models stream processors as arrows.
- **design** In C++, `then`, `first` and `fanout` are templates that fuse kernels into one statically dispatched kernel inside a node; the runtime graph only connects nodes.
- **design** Because stages hold state (effects), `fanout(F, G)` always runs F before G, and no rewrite drops a branch whose output is unused.

## Polymorphism for real time

### Measurements

- Driesen and Hölzle measured that C++ programs "spend a median of 5.2% of their time and 3.7% of their instructions in dispatch code" [S8, abstract].
- With every function virtual, "the median overhead rises to 13.7%" [S8, abstract].
- Those numbers come from simulated 1990s superscalar processors and whole programs [S8, abstract]; they bound nothing on a current Arm core: **unverified** for this device class.
- Dionne's benchmark: creating many 16-byte objects costs about 55 ns each with inheritance or heap-backed type erasure, and about 2–3 ns with small-buffer or local storage [S9, slide 9.1, read off the chart].
- Calling three methods on each of ten objects costs about 45–49 ns for every storage choice, inheritance included [S9, slide 9.2, read off the chart].
- Dionne's guideline: "Use local storage whenever you can afford it" [S9, slide 9.3].
- **design** So the cost to avoid is allocation at construction, not the indirect call; a graph built once and called per hop pays one indirect call per node firing.
- No source fetched here measures concepts against CRTP; both resolve calls at compile time, but a measured difference is **unverified**.

### Which dispatch at which rate

- **design** Per sample (tens of thousands per second): no dispatch inside the loop; kernels fused by `then`/`fanout` and called once per block with a loop inside.
- **design** Per hop (the hop rate the plan derives, 100 per second at today's 10 ms): one call per node firing through `NodeRef`'s function-pointer table with local storage.
- **design** Per frame (modulators, director): virtual inheritance, as the owner asks for there; the cost is negligible at frame rate on [S8]/[S9]'s evidence.
- **design** The algorithm for a stage is picked at build from a registry keyed by `StageChoice`; switching an algorithm builds a new graph off the real-time thread and swaps it by an atomic pointer at an epoch.
- A concept "is a template that defines constraints on its template arguments" [S14]; `Kernel` above is checked at compile time and costs nothing at run time.

### Data layout for NEON

- Arm's structure loads "pull in data from memory and simultaneously separate values into different registers" (`VLD2/3/4`), while `VLD1` loads "with no deinterleaving" [S23].
- **design** Interleaved PCM can be split into channels on load; state that is updated per band in lockstep (the `RelativeLevel` fields across bands, `core/sound/SoundEngine.hpp:119`, `:130`) is laid out as one array per field (SoA) so one vector instruction steps four bands.
- The ring already keeps left and right in separate arrays: `core/sound/SoundEngine.hpp:236-237`.
- Whether the compiler vectorises those loops at `-O2` is **unverified** (no build in an agent container).

## Memory

- `monotonic_buffer_resource::deallocate` has "Effects: None", and memory "increases monotonically until its destruction" [S10].
- `release()` returns all memory to the upstream resource [S10].
- The buffer constructor starts from a caller-supplied buffer; the growth factor of later buffers is implementation-defined [S10].
- `memory_resource` is "an abstract interface to an unbounded set of classes encapsulating memory resources" with virtual `do_allocate`, `do_deallocate`, `do_is_equal` [S11].
- `null_memory_resource()`'s `allocate()` "always throws bad_alloc" [S12].
- General-purpose allocators "have unpredictable time behaviour, even if they don't use locks" [S22].
- **design** `GraphBuilder::plan` computes every buffer's size from the schedule (static buffering [S1, abstract]) and returns `arenaBytes`.
- **design** `build` allocates one block of `arenaBytes` on a non-real-time thread, wraps it in a `monotonic_buffer_resource` whose upstream is `null_memory_resource()`, and builds every node and arc inside it.
- **design** Any allocation the plan did not count then throws at build, in a host test, instead of reaching the real-time path.
- **design** Teardown and `release()` happen on the thread that built the graph, after the swap.
- **design** The FFT plan and window tables are built inside the same arena, so `Fft`'s shared cached plan (`core/sound/Fft.hpp:23`) becomes graph-owned.

## Determinism and testability

- **design** The graph's only clock is the sample index; every firing, window edge and slow-layer hand-off is a function of it.
- **design** Hence the result does not depend on how the input was chunked; this is directly testable on the host by feeding the same PCM in random chunk sizes, as `core/tests/pcm_ring_test.cpp` already does for the ring.
- **design** Live and export run the same `AnalysisGraph`; only the driver differs (`LiveDriver` drains a ring, `OfflineDriver` is fed by `TrackFeed`).
- **design** A slow layer's fixed latency L applies in export too, even when export runs it inline, so both give the same bits.
- **design** The starved path must feed silence by samples, not by the frame's `dt` (today `core/sound/SoundEngine.cpp:259-265`); an export never starves, so this only matters for live/host-test equality.
- The C++ draft lets floating results "be represented in greater precision and range than that required by the type" [S13].
- Clang's default `-ffp-contract=on` enables "standard compliant fusion in the same statement" (FMA); `-ffp-model=precise` is the default and keeps contraction on [S15].
- **design** With the same binary on the same device, the same inputs and the same fixed evaluation order, export equals a re-render bit for bit.
- **design** Host tests (x86-64) and the device (arm64) may differ in the last bits wherever a statement is contracted to FMA on one target and not the other; host goldens stay host-only unless the build adds `-ffp-contract=off` (cost **open**).
- Already in place: two resolvers with the same inputs give the same bits (`core/signal/Resolver.hpp:38-39`), and `director_reach_test.cpp:168` checks same seed → same bits.

## Threads on Android

- **design** One analysis thread per live source; it is not an audio callback, so it may sleep while the ring is empty, but it never takes a lock a reader or writer holds.
- **design** The writer (tap, mic callback, capture) never runs analysis and never wakes anything that can block it; how the analysis thread learns of new samples (a timed wake each hop period vs a futex wake from the writer) is **open**.
- AAudio itself "avoids using mutexes, which can cause thread preemption and glitches" [S21, Thread safety].
- Locks in an audio callback cause priority inversion [S22, "Locking"].
- **design** A worker pool, sized from the device's core count at build, runs slow nodes when their cost per period exceeds the analysis thread's budget; their results land at hop k + L.
- A performance hint session "represents a group of threads with an inter-related workload such that hints for their performance should be considered as a unit" [S16].
- Session threads "should be long-lived and not created or destroyed dynamically" [S16].
- `APerformanceHintSession_*` methods "are not thread safe so callers must ensure thread safety" [S16].
- Timings are to come from `std::chrono::steady_clock` or `CLOCK_MONOTONIC` [S16].
- `APerformanceHint_createSession`, `reportActualWorkDuration`, `updateTargetWorkDuration` and `getPreferredUpdateRateNanos` are API 33 (`__ANDROID_API_T__`) [S17], which is this app's minSdk.
- `APerformanceHint_setThreads` is API 34 [S17].
- `setPreferPowerEfficiency`, `reportActualWorkDuration2` and `AWorkDuration_*` are API 35 [S17].
- `createSessionUsingConfig`, graphics-pipeline mode and `notifyWorkload*` are API 36 [S17].
- The Java `PerformanceHintManager.createHintSession` is API 31 [S18].
- Power-efficiency mode tells the session its threads "can be safely scheduled to prefer power efficiency over performance" [S20].
- **design** One session for each source's analysis thread plus the workers it feeds, with the hop period (or a whole schedule period) as target; the GL thread keeps its own session; calls above API 33 behind availability checks (the build already fails on `-Werror=unguarded-availability`, `CMakeLists.txt:19`).
- **design** ADPF's thermal API is not used: ADR 0008 removed every heat-driven throttle (AGENTS.md).
- Android's guidance on manual CPU affinity: the platform "can't dynamically adjust task placement for runtime factors such as load and thermal throttling", forcing big cores "may unnecessarily increase battery drain and thermal load", and "it's generally best to avoid manually setting CPU affinities" [S19].
- **design** So no thread is pinned; placement is left to the scheduler and the hint sessions.

## Dynamic values

- **design** From the device at build: input sample rate and channel count (from the ring's take, `core/audio/ingest/PcmRing.hpp:69-73`), core count, `getPreferredUpdateRateNanos` [S16].
- **design** From the chosen algorithm at build: window length, hop, and each node's consumption and production counts.
- **design** From the balance equations at build: the repetition vector, the period, every arc's buffer size, the arena's size [S1], [S2].
- **design** From the TTSDF latency search at build: the smallest L that schedules [S2, §4.2].
- **design** From the worker budget at build: how many workers, from core count and the schedule's measured or declared cost (**open**: how a cost is declared without a device benchmark).
- Values in the code today that are compile-time and would become plan outputs: the 10 ms hop (`core/sound/Rhythm.hpp:36`), the rhythm ring length (`core/sound/Rhythm.hpp:113`), `kFeedRunsPerFrame` (`core/api/geode_viz_api.cpp:188`), the 0.25 s starvation cut (`core/sound/SoundEngine.hpp:53`), `RelativeLevel`'s time constants (`core/sound/Stages.hpp:79-83`).
- **open** Whether a constant that is an algorithm's own design parameter (a smoother's time constant in seconds) counts as "arbitrary"; the design keeps such parameters as the algorithm's declared inputs, converted to per-hop factors at build from the derived hop.

## The migration order (smallest step first; the app works after each)

1. Add host tests that pin today's bits: chunk-size invariance of `SoundEngine::write` and export-equals-rerender over a fixed PCM fixture (`core/tests/`). No production change.
2. Add `core/graph/Algebra.hpp` (the concepts, `Laws`, `Fold`, `Affine`) with host tests of the laws; no users.
3. Replace `held_` with a `Fold<MaxSemigroup>` inside `SoundEngine`; same bits (test 1 holds).
4. Extract the per-sample front end (clean, decimate, DC block, trim, filter bank, meter, transient; `core/sound/SoundEngine.cpp:201-245`) into one fused kernel called by `SoundEngine`; same bits.
5. Extract the hop stages of `analyse()` into kernels called in today's order by a hand-written schedule inside `SoundEngine`; same bits.
6. Add `GraphBuilder::plan` and assert in a host test that it derives exactly the hand-written schedule from declared rates; still no runtime change.
7. Build the engine's buffers from the plan inside a monotonic arena with a null upstream; host test proves no allocation after build.
8. Feed silence by samples in the starved path; this may change bits, so it lands alone with its own golden update.
9. Introduce `FeatureBus` and `ReaderView`; `Renderer` still owns its engine but reads through the bus.
10. Move the engine from the renderer to an owner keyed by ring (`geode_pcm_ring`), still advanced on the first reader's GL thread; the screen and the wallpaper on the player's ring now share one analysis; export keeps its own source and graph.
11. Move each live source's graph onto its own analysis thread (`core/sound/hop`, as the ingest README plans); drop `soundLock_` from the audio path.
12. Move slow layers (Harmony, Form, Emotion, tempo) to lower-rate nodes with fixed latency L, then onto workers; one ADPF session per source thread group.
13. Register alternative algorithms per stage and choose them at build; each new algorithm arrives with its laws and a host test.

## Risks and open questions

- **open** TTSDF's latency and jitter results assume time-triggered inputs of known size [S2, §4.1.1]; with the ring as the only input they do not transfer as proved, and a new argument (or test) is needed for the analysis thread's latency bound.
- **open** Who wakes the analysis thread without the writer making a blocking or costly call.
- **open** Sound tuning is set per renderer today (`core/viz/Renderer.cpp:101`, `:136`); one analysis per source needs it per source, and two surfaces with different trims on one source would conflict.
- **open** MilkDrop's raw PCM is per renderer (`core/viz/Renderer.cpp:107-111`); it stays a per-reader tap on the ring, outside the graph.
- **open** Record hears "exactly what the engine just heard, the mic layer included" (`core/api/geode_viz_api.cpp:414-415`); with a shared analysis the layer mix must also move to the source, or Record keeps its own mix.
- **open** The wallpaper may switch between the ring and the output-mix capture (`core/sound/SoundSource.hpp`); each is a source with its own graph, and a switch is an epoch on the reader, not a rebuild.
- **open** When a worker misses L live, the choices are to publish late (live then differs from export) or to run it inline (a deadline risk); export always runs inline and is unaffected. Owner's call.
- **risk** Float reductions are not associative; any tree or partition must be fixed at build or bits change between runs [S13], [S15].
- **risk** Subtract-on-evict windows in `float` drift; group-based windows need exact accumulators [S5, Table 1].
- **risk** Host goldens may not match device bits under FMA contraction [S15].
- **risk** The dispatch-cost figures are old [S8] or from a desktop benchmark [S9]; no measurement on an Android device was made here (agents may not build or run benchmarks).
- **risk** The 10 ms hop is tied to Rhythm's onset envelope (`core/sound/SoundEngine.hpp:46-51`); making it a derived value touches the beat tracker's tuning and the rhythm ring's `static_assert`.
- **risk** Rebuilding the engine on a rate change allocates on the GL thread today (`core/viz/Renderer.cpp:99-100`); the graph swap must be done off the real-time thread from step 10 on.
- **open** Concepts versus CRTP has no fetched measurement; both are taken as zero-cost static dispatch only by design.
- **open** The WG21 papers behind pmr and concepts could not be fetched from this container; the draft standard is cited instead.
