# Stage 2: spectral front end (research, 2026-11-14)

Scope: the PCM hops of one source → the time–frequency representation every later analysis reads (onsets, drums, pitch, chroma, timbre, structure, the drawn spectrum).
Read with `docs/research/music-analysis.md` (§2 framing, §4 STFT vs CQT, §6 FFT licences, §7 verified onset findings) and `pipeline/01-ingest-framing.md` (hop from the burst, the DFT on ℤ/nℤ, polymorphism basics); none of that is repeated here.
Evidence: every line names a fetched primary source, or is marked "inference" (follows from cited facts), "secondary" (a source quoting the primary), "unverified" (not read in full) or "open" (no source settles it).
Code facts are at commit `b759e7c6f`.
Clean room: methods and maths from papers, standards and platform docs only; libraries are named only where the repo already ships them, others only by licence and URL.

## 1. How the app does it now

### Rates and sizes
- The analysis rate is the input rate halved up to three times while it is ≥ 88.2 kHz, floored at 8 kHz (`core/sound/SoundEngine.cpp:94-98`); 88.2 kHz, 8 kHz and "3" are hand-picked.
- The FFT size is the smallest power of two ≥ 40 ms of samples, starting at 256 (`SoundEngine.cpp:88-92`): 2048 at 44.1 and 48 kHz; 40 ms and 256 are hand-picked.
- The pitch and chroma FFT is fixed at twice that, 4096 at 48 kHz (`SoundEngine.cpp:116,118`; `core/sound/SoundEngine.hpp:233`).
- The hop is a constant 10 ms (`core/sound/Rhythm.hpp:36`), 480 samples at 48 kHz (`core/sound/Rhythm.cpp:49`); window ÷ hop is 4.27 for the short FFT and 8.53 for the long one, neither an integer.
- The flux lag is a constant 1/30 s rounded to hops (`SoundEngine.cpp:53,167`).

### The transform
- `Fft` accepts powers of two only and aborts otherwise (`core/sound/Fft.cpp:19`).
- It runs KISS FFT's complex transform on the real frame with zero imaginary parts (`Fft.cpp:24-25`), so half its work is redundant: a real-data DFT saves "roughly a factor of two in time" (Frigo & Johnson, Proc. IEEE 93(2) 2005, https://www.fftw.org/fftw-paper-ieee.pdf).
- The shipped header already has a real transform, `transform_real` (`third_party/kissfft/kissfft.hh:154`), unused by the app.
- `Fft` keeps magnitudes only (`Fft.cpp:26`); phase is thrown away, so complex-domain and phase-deviation onset functions cannot be built on it (inference; those functions are in `music-analysis.md` §5).
- KISS FFT has no NEON path in the tree (no `neon`/`__ARM` symbol in `third_party/kissfft`, grep); its SIMD mode is SSE and does four separate FFTs at once (`third_party/kissfft/README.simd:1-14`).
- KISS FFT's generic butterfly for prime factors > 5 resizes a `mutable` scratch vector (`kissfft.hh:330,359`): it allocates on first use and races if one plan is shared across threads; power-of-two sizes never reach it (`Fft.cpp:17-18` comment).
- KISS FFT is BSD-3-Clause (`third_party/kissfft/COPYING`).

### Windowing and reading the ring
- The window is a periodic ("DFT-even") Hann: `0.5 − 0.5·cos(2πi/n)` divides by n, not n−1 (`SoundEngine.cpp:73-78`), the form Harris recommends for DFT use (section 2).
- Each hop copies the newest n samples from a per-engine stereo ring with a modulo per sample (`SoundEngine.cpp:267-283`), then windows and transforms them (`SoundEngine.cpp:352-353`).
- The 2n pitch frame is re-read and re-transformed every hop too (`SoundEngine.cpp:761-764`, called from `melody` at `:835`); nothing of the n-point transform is reused.
- Magnitudes are scaled by 4/n and multiplied by a fixed treble tilt pinned at 22.05 kHz (`SoundEngine.cpp:136-139,354-357`).

### Band groupings (all computed from the same magnitude array)
- Five fixed Hz ranges (bass, mid, treble, sub, presence) (`SoundEngine.cpp:140`).
- Sixteen EQ bands on hand-picked centres 25 Hz…22 kHz (`SoundEngine.cpp:66-67,141-146`).
- 64 log-spaced "spectrum points" 20 Hz…min(20 kHz, Nyquist), taking the max bin in each (`SoundEngine.cpp:147-152,452-466`).
- The flux sums log-magnitude rises per spectrum point over 30 Hz–16 kHz (`SoundEngine.cpp:563-591`) against a maximum-filtered earlier spectrum with a semitone reach (`core/sound/OnsetFlux.hpp:308-322`).
- Chroma maps the long FFT's bins to pitch classes against a fixed A = 440 Hz (`core/sound/Harmony.cpp:199-200`).
- None of these is a mel, Bark or ERB scale; the semitone mapping exists only inside Harmony and the flux reach.

### Time-domain side
- Eight per-sample bands, each two Butterworth biquads per edge (24 dB/oct), with hand-picked Hz edges (`SoundEngine.cpp:154-157`; `core/sound/FilterBank.hpp:71-101,111-126`).
- Their envelope and mean time constants (15 ms, 0.32 s, 4.15 s, 0.15 s) are hand-picked (`FilterBank.hpp:120-125`).
- High-rate input is halved by a 47-tap Blackman windowed-sinc half-band, delay 23 input samples (`core/sound/Decimate.hpp:212-269`); the tap count is fixed, not designed from a stopband target.

### Per screen, not per source
- Each `Renderer` builds its own `SoundEngine` (`core/viz/Renderer.cpp:100`), so live, wallpaper and dream each repeat the same FFTs on the same audio (inference from `music-analysis.md` §1).

## 2. The optimal design, with sources

### One shared front end per source
- Every analysis reads one front end owned by the source's hop worker (stage 1); no analysis owns an FFT (inference from the "one analysis per source" rule and the cost duplication above).
- Multiple right-aligned STFTs on one frame grid are an established online input: three Hann windows of 512, 1024 and 2048 samples whose "right edges … are aligned at the current position", "normalized to have equal area", 10 ms apart (Böck, Arzt, Krebs & Schedl, DAFx 2012, §2.1, https://www.dafx.de/paper-archive/2012/papers/dafx12_submission_4.pdf).
- That paper filters each STFT to 24 Bark bands with area-normalised triangles and takes the log (same, §2.1).
- Its difference lag per resolution is set "such that the overlap of the two frames is 0.5": τ = 1, 2, 4 hops for 512, 1024, 2048 (same paper): a rule from the window, not a constant.
- Right-aligned windows have intrinsic latency of half the window for a symmetric window (Su & Wu, arXiv:1606.09047, Table 2, https://arxiv.org/abs/1606.09047; preprint, venue not stated).
- A minimum-phase window keeps the symmetric window's magnitude spectrum and has "the maximal concentration of energy toward the observation time" (same paper).
- Measured: on real-time piano onsets a minimum-phase flat-top reached F 83.76% at 13.18 ms latency versus 84.25% at 21.95 ms for the symmetric one (same paper); the gain holds for synchrosqueezing, not for reassignment.

### Constant-Q: where it fits
- A full constant-Q NSGT needs a Fourier transform of the entire signal; slicing into 2N-sample slices with Tukey partition windows makes it real-time and keeps perfect reconstruction, at a "trade-off between delay and finest possible frequency resolution" (Holighaus, Dörfler, Velasco & Grill, arXiv:1210.0084 §1 and §4, https://arxiv.org/abs/1210.0084).
- The sliced transform costs O(L) overall for a fixed slice length (same, §5).
- A slice must be complete before it is transformed, so the delay grows with 2N (inference from the slicing algorithm, §4.2).
- The efficient CQT computes each bin as an inner product with a sparse spectral kernel, by Parseval: ∑ x(n)a*(n) = (1/N)∑ X(j)A*(j) (Schörkhuber & Klapuri, SMC 2010, eq. 7, https://zenodo.org/records/849741).
- Octave by octave: compute the top octave, low-pass and downsample by two, repeat "using exactly the same DFT block size and spectral kernel" (same, §3.2).
- So a real-time constant-Q front end is one FFT size + one sparse kernel + a half-band decimation cascade; the app already has the half-band stage (`Decimate.hpp`) (inference).
- Its Q and window length follow from B bins per octave: Q = q / (Δω(2^{1/B} − 1)), N_k = q·fs / (f_k(2^{1/B} − 1)), with Δω ≈ 1.50 bins for Hann (same, eqs. 4–6).
- Hop per bin: 0 < H_k ≤ N_k/2 for adequate analysis (same, §2); the top bins set the overall rate in a one-kernel design (same, §3.2).
- Constant-Q (fine low, fast high) suits pitch and chroma; short uniform STFTs suit transients; no single grid serves both (`music-analysis.md` §4).

### Recommended shape (inference from the sources above)
- Level A, transient path: one short right-aligned STFT at the source's hop, length from the onset Pareto tuning (`music-analysis.md` §2), real FFT, magnitudes and phase kept.
- Level B, tonal path: an octave pyramid of the same mixed-radix FFT size on successively half-band-decimated signals, each octave's hop doubled (bandwidth halves), with a sparse spectral kernel to B bins per octave.
- The pyramid's cost per second is a geometric series: octave j costs 2^{-j} of the top in samples and 2^{-j} in hop rate, so ≤ 4/3 of the top octave in total (inference from the halved rate and doubled hop).
- Every band scale (semitone, Bark, ERB, mel, the 16 EQ bands, the 64 drawn points) is a sparse linear map applied to those magnitudes once per hop and cached in the frame (section 4).
- Level C, per-sample bins: a sliding DFT only for a small set of bins that need sample-rate updates; the per-sample IIR bands stay for timing (Turchet's hybrid, `music-analysis.md` §5).

### Window choice
- For DFT processing, use DFT-even windows: "a conventional even sequence with the right end point removed" (Harris, Proc. IEEE 66(1) 1978, §II, https://amst.ece.iastate.edu/paper/comparative_study/window_1.pdf); the app's Hann already is.
- The Hann window's DFT has only three non-zero samples, at −2π/N, 0, +2π/N (Harris, p. 62): windowing equals a 3-tap circular convolution of the rectangular-window spectrum.
- Resolution: Δf = β·fs/N with β the window's equivalent noise bandwidth in bins (Harris, eq. 16); so the window length follows from the resolution a consumer needs, not from a time constant.
- Overlap of 50 or 75% is the norm: "The overlap is almost always 50 or 75 percent" (Harris, "Overlap Correlation").
- A minimum-phase variant of the chosen window cuts latency at the same magnitude response (Su & Wu, above); which window wins is a measurement on the app's onset and pitch sets (open).

### Hop from the window, not a constant
- Each STFT bin is the output of a low-pass filter with impulse response w(n), so it must be sampled at least at twice the window's bandwidth (Allen & Rabiner, Proc. IEEE 65(11) 1977, §VII, https://jontalle.web.engr.illinois.edu/Public/Allen-pdf/AllenRabinerProc77.pdf).
- For an N-point Hamming window at a 42 dB criterion B = 2Fs/N, so the hop is at most N/4 (same, eq. 7.1–7.4).
- Hann's main lobe spans the same ±2 bins as Hamming's (both are two-term cosine sums; Harris p. 62 places Hann's side kernels at ±2π/N), so the same N/4 bound applies (inference; Hann's sidelobes are higher than Hamming's first, so the dB criterion differs: open).
- Today the 2048 frame at a 480 hop meets N/4 = 512; the 4096 frame would meet it at 1024 but is recomputed every 480 (inference): about twice the work the sampling theorem asks for.
- Final hop = a multiple of the device burst (stage 1) that satisfies the smallest window's bound and the onset grid the Pareto tuning picks (inference).

### FFT library and size rules
- All arm64 Android devices support NEON, and the NDK enables it by default for both Arm ABIs (https://developer.android.com/ndk/guides/cpu-arm-neon).
- The same page: Clang's portable vector types "will automatically use Neon instructions" and intrinsics are "not … any faster", so a hand-written backend can be portable C++ vectors.
- A permissive NEON FFT (BSD-like, FFTPACK-derived) takes N = 2^a·3^b·5^c, "a multiple of 32 for real or 16 for complex transforms"; on 64-bit ARM it has "NEON for float and double, with fused multiply-add" (https://github.com/marton78/pffft).
- The same page: on 32-bit ARM double "runs the scalar code", and gcc "emits very inefficient assembly" there.
- An Arm NEON library with radix 3 and 5 FFTs is BSD-3-Clause (https://raw.githubusercontent.com/projectNe10/Ne10/master/LICENSE); it "detects whether the size of FFT is multiple of 3 or 5" (Wang, Arm blog, 2015-01-16, https://developer.arm.com/community/arm-community-blogs/b/operating-systems-blog/posts/ne10-fft-feature-radix-3-and-radix-5-fft-are-supported-neon-optimization-significant-performance-improvement-by-neon-optimization).
- That blog benchmarked four FFTs on Cortex-A9 (1.0 GHz) and Cortex-A53 (850 MHz) and calls the NEON gain "obvious", with numbers only in figures (same; figures not read: unverified magnitude).
- A BSD-3 C++ FFT handles any length (codelets 2, 3, 4, 5 for real data; Bluestein for large primes) but vectorises only multi-dimensional transforms, not one 1-D transform (https://github.com/mreineck/pocketfft/tree/cpp).
- KISS FFT: radix 2, 3, 4, 5 butterflies; its real FFT "only works for even length" and does two half-length FFTs packed into one complex one (`third_party/kissfft/README.md:214-226`).
- A measuring planner "works by measuring the actual run time of many different plans and by selecting the fastest one", with "no manual intervention" (Frigo & Johnson 2005, §I); the method, not that GPL library, is what to reuse.
- So: backend and size are picked at configure time by timing each permissive backend on the device at the sizes the hop grid allows (inference).
- Open: measured ns per transform for each backend on the app's target phones; no primary source gives current-phone figures.

### Sliding DFT, Goertzel and per-sample bins
- The sliding DFT updates bin k per sample by adding the newest-minus-oldest sample and multiplying by e^{j2πk/N}: one complex multiply-add per bin per sample, only for the bins wanted (Jacobsen, DSPRelated article 776, https://www.dsprelated.com/showarticle/776.php; secondary to Lyons/Jacobsen, IEEE SPM 20(2) 74–80, 2003, DOI 10.1109/MSP.2003.1184347, not read).
- The same article: its output is valid only after N samples, and it has a latency advantage in all cases.
- The plain recursion is not numerically safe: "the variance of the numerical error is unbounded" (Covell & Richardson 1991, as cited in arXiv:1707.08213, https://arxiv.org/abs/1707.08213; secondary).
- Replacing resonators by modulators improves things but "stability is not guaranteed"; a fading-memory design places all poles inside the unit circle (Kennedy, arXiv:1408.2294, abstract, https://arxiv.org/abs/1408.2294).
- The modulated SDFT removes the twiddle factor from the feedback path (Duda, IEEE SPM 27(6) 124–127, 2010, DOI 10.1109/MSP.2010.938088; abstract via an index only, secondary).
- A Hann-windowed SDFT is the rectangular SDFT followed by the 3-tap frequency kernel (Harris p. 62, above; inference for the SDFT case).
- Non-recursive sliding transforms that reuse FFT butterflies in a tree give O(n) per shift with FFT-identical output (arXiv:1707.08213, abstract and §1).
- Goertzel computes one bin per block; the crossover against an FFT by bin count is open (no primary source read).

### Decimation
- Downsampling by two after a half-band low-pass is the octave step of the efficient CQT (Schörkhuber & Klapuri §3.2).
- The stage count follows from the device rate and the highest frequency any consumer reads: halve while the next Nyquist stays above it (inference; replaces the 88.2 kHz rule).
- The half-band length follows from the stopband attenuation and transition width the kernel needs (open: no filter-design primary was read; the 47 taps are a hand pick).
- Every stage adds its group delay; the frame stamp must carry each level's effective time (inference; see section 6).

### Compute once, reuse everywhere
- One ring read per hop per level, one real FFT per level, then everything else is a view or a sparse map over those spectra (inference).
- Shared products per hop: complex spectrum, power, log-power, and each band scale's vector, each computed at most once and handed out as `const` spans (inference).
- Prefer contiguous structure-of-arrays storage per level so every consumer streams the same memory (inference; no primary source on data-oriented layout was read, as in stage 1).
- The export path must run the same front end on the same samples, which is the reason `Fft` exists (`core/sound/Fft.hpp:11-13`).

### Filter scales (inputs to the band maps)
- ERB(f) = 24.7(4.37f/1000 + 1) Hz, Glasberg & Moore's recommendation, quoted with its derivation (Slaney, Apple TR #35, 1993, p. 2, https://engineering.purdue.edu/~malcolm/apple/tr35/PattersonsEar.pdf).
- Bark: z = 26.81/(1 + 1960/f) − 0.53, invertible, within ±0.05 Bark of the table over 0.2–6.7 kHz (Traunmüller, JASA 88(1) 97–100, 1990, DOI 10.1121/1.399849; abstract only, secondary).
- Mel: the formula's constants were not traced to a primary (open).
- Semitone: f = f_ref·2^{m/12}; a semitone-spaced log filterbank gave the best online onset F in the verified study (`music-analysis.md` §7).
- The semitone grid's reference should come from a tuning estimate, not a fixed 440 Hz (open: no tuning-estimation primary read here).

## 3. The polymorphic interface (signatures only)

### Choice of polymorphism (adds to stage 1 §3)
- The front end runs per hop (~100–200 Hz); one virtual call there is amortised over an N log N transform (inference).
- Measured dispatch overhead in call-heavy C++ programs: median 5.2% of time, 13.7% if every call is virtual (Driesen & Hölzle, OOPSLA 1996, abstract, https://static.aminer.org/pdf/PDF/000/522/086/the_direct_cost_of_virtual_function_calls_in_c.pdf): it matters per sample, not per hop (inference).
- `std::function` only "should avoid" heap for small callables and may throw `bad_alloc` ([func.wrap.func.con], https://eel.is/c++draft/func.wrap.func.con): not for the audio path.
- So: concepts for per-sample kernels (decimator, SDFT, IIR band), a `std::variant` over the measured FFT backends (closed set, picked at configure), and a type-erased front end handle at the source edge (inference).

### Sketch
```cpp
namespace geode::sound::spectral {

struct FrameStamp { int64_t endSample; double centreSample; int64_t epoch; int rateHz; };
enum class WindowShape : uint8_t { Hann, MinPhaseHann, Measured };
struct LevelSpec { int windowLength; int fftLength; int hop; int decimation; WindowShape window; };
struct FrontEndSpec { std::span<const LevelSpec> levels; int binsPerOctave; float lowestHz; float highestHz; };

template <class T> concept RealFft = requires(T& f, const float* in, std::complex<float>* out, int n) {
    { T::supports(n) } -> std::same_as<bool>;
    { f.size() } -> std::same_as<int>;
    f.forward(in, out);                       // n real in, n/2+1 complex out, no allocation
};
using AnyRealFft = std::variant</* the measured permissive backends */>;
AnyRealFft pickFastest(int n, const DeviceFacts&);   // timed on device at configure

template <class T> concept Decimator = requires(T& d, float x, float& y) {
    { d.push(x, y) } -> std::same_as<bool>;
    { d.groupDelay() } -> std::same_as<double>;
    d.reset();
};

template <class T> concept BinTracker = requires(T& t, float x, std::span<const float> hz) {
    t.track(hz);                               // which bins, set per hop by a consumer
    t.push(x);                                 // per sample
    { t.bins() } -> std::same_as<std::span<const std::complex<float>>>;
};

struct BandMap {                               // sparse linear map: bins → bands
    std::span<const int> rowStart, column; std::span<const float> weight;
};
enum class Scale : uint8_t { Semitone, Bark, Erb, Mel, Eq, Drawn };
BandMap buildBandMap(Scale, const LevelSpec&, int rateHz, float referenceHz);

struct LevelView {
    FrameStamp stamp;
    std::span<const std::complex<float>> spectrum;
    std::span<const float> power, logPower;
    std::span<const float> bands(Scale) const;
};

class SpectralFrame {                          // what every consumer reads, published once per hop
public:
    int levels() const;
    const LevelView& level(int) const;
    std::span<const float> constantQ() const;  // pyramid + kernel, B bins per octave
};

template <class T> concept SpectralFrontEnd = requires(T& fe, std::span<const float> mono, const FrameStamp& s) {
    fe.configure(std::declval<const FrontEndSpec&>());
    { fe.push(mono, s) } -> std::same_as<bool>; // true when a hop completed
    { fe.frame() } -> std::same_as<const SpectralFrame&>;
    fe.shift(int64_t{});                       // known gap: advance phases, keep state
    fe.reset();                                // epoch
};

class AnyFrontEnd;                             // type-erased owner per source; HopConsumer of stage 1
}
```

## 4. Algebraic structure

### The DFT as the Fourier transform on ℤ/Nℤ
- The DFT expands f on ℤ/nℤ in the irreducible characters χ_k(j) = e^{2πijk/n} (Maslen & Rockmore 2003, https://library.slmath.org/books/Book46/files/11maslen.pdf); stage 1 §4 covers the basics.
- Cooley–Tukey is a DFT on the subgroup qℤ/nℤ ≅ ℤ/pℤ then over coset representatives; mixed radix follows a chain of subgroups (same, §2): so the valid FFT sizes are exactly the n whose subgroup chain has small prime steps (2, 3, 5 in the shipped and candidate libraries).
- For prime order there is no proper subgroup; Rader turns the DFT into a convolution on ℤ/(p−1)ℤ (same, "chirp-z" section): the reason libraries fall back to slower code for large primes.
- The DFT turns cyclic convolution into pointwise multiplication (Rockmore, CiSE 2(1) 2000, https://gauss.dartmouth.edu/~m56s13/Rockmore1999CSE_FFT_review.pdf).

### Convolution theorem in the front end
- Windowing (a product in time) is a circular convolution in frequency; for Hann it is the 3-tap kernel (Harris p. 62): it lets one rectangular SDFT state serve as a Hann spectrum, and lets a short window be applied after the FFT (inference).
- Parseval makes any linear band or CQT bin an inner product computable in either domain (Schörkhuber & Klapuri eq. 7); the sparse spectral kernel is the frequency-domain side (same, §3.1).

### Translation equivariance and invariants
- χ_k(j + m) = χ_k(j)·χ_k(m), so a cyclic shift by m multiplies bin k by a unit phase (from the character definition, Maslen & Rockmore; inference).
- So |X[k]| is invariant under cyclic shifts, and phase carries the position (inference); a hop shift of a real stream is not cyclic, so magnitude is only approximately invariant (inference).
- Consequence: features built on magnitudes are hop-phase robust; anything that needs timing (complex-domain onsets, instantaneous frequency) must keep phase, which `Fft` drops today (inference).
- Chroma is a function on ℤ/12ℤ; transposition is its cyclic shift; `Harmony.cpp:40` already uses that action when it rotates key profiles (code fact); the DFT magnitudes of a chroma vector are transposition invariant by the same shift theorem (inference; belongs to the harmony stage).

### The sliding DFT via the shift group
- One step of the SDFT is X ↦ χ_k(1)·(X + x_new − x_old): the shift action plus an update (DSPRelated 776 recursion; group reading is inference).
- Each step is an affine map; affine maps compose associatively, so h steps fold into one map; first-order recurrences x_i = (x_{i−1}⊗a_i)⊕b_i reduce to a scan with the pair operator [a,b]·[a',b'] = [a⊙a', (b⊗a')⊕b'] (Blelloch, "Prefix sums and their applications", 1993, §1.4.1, https://www.cs.cmu.edu/~guyb/papers/Ble93.pdf).
- So a hop of h samples advances every tracked bin in one combine, and blocks can be advanced in parallel (inference); the same holds for one-pole envelopes in `FilterBank.hpp` (inference).
- The group inverse (subtract the leaving sample) is exact only in exact arithmetic; in float the error grows without bound (Covell & Richardson via arXiv:1707.08213), so the stable forms trade the group for a contraction (Kennedy 2014) (inference).

### Overlap-add as a monoid, and the hop as a subgroup
- Allen & Rabiner: if w is densely enough sampled, ∑_m w(m − n) = W(e^{j0}) "independent of the window offset n" (eq. 4.5), and the dual statement holds in frequency (§VIII).
- Shifted, weighted frames under addition form a commutative monoid (identity: the zero buffer), so partial overlap-add sums combine in any grouping (inference; Blelloch's reduce needs only associativity).
- Hops form the subgroup Hℤ (stage 1 §4); the sum of a window over its H-translates is constant exactly when the window's transform vanishes on the non-zero multiples of N/H (inference from Allen & Rabiner's §VIII duality; the annihilator reading is not from a read source).
- For analysis only, the same duality says hop ≤ N/4 keeps a Hann/Hamming STFT un-aliased in time (Allen & Rabiner §VII); that is the group-level reason the hop is set by the window.
- Hops with H | N give phase advances χ_k(H) that repeat with period N/gcd(kH, N), so per-bin phase increments are exact and precomputable (inference).

### Band maps as monoid homomorphisms
- Power summed over disjoint bin sets adds (Parseval): a coarse band that is a union of fine bands is the sum of their powers (inference).
- So the finest scale is computed once and every coarser scale whose edges are unions of its edges is a further sum, never a second pass over bins (inference); overlapping triangular filters are a sparse matrix product instead.
- Max (the drawn points, the flux reference) is a monoid without inverse; its sliding form needs the two-stacks/DABA methods of stage 1 §4, not subtraction.

### Decimation as restriction to a subgroup
- Keeping every second sample restricts a signal on ℤ/2Nℤ to the subgroup 2ℤ/2Nℤ; its DFT is the sum of the two halves of the original spectrum, which is why the half-band filter must empty the upper half first (inference from the Cooley–Tukey subgroup split, Maslen & Rockmore §2).
- The half-band filter's zero taps at even offsets (`Decimate.hpp:223-224`) are the polyphase split by the same subgroup (inference).

## 5. Dynamic values and where each comes from

- Input rate: the stream's rate, read at open (stage 1 §5).
- Hop H: a multiple of the device burst (stage 1), ≤ N_min/4 for Hann/Hamming (Allen & Rabiner §VII), and the onset grid chosen on the accuracy–latency Pareto front (`music-analysis.md` §2).
- Short window N_A: from the onset Pareto tuning (Stefani & Turchet via `music-analysis.md` §2), with Böck 2012's 512/1024/2048 as the starting candidates, scaled by rate.
- Tonal window per octave: N_k = q·fs/(f_k(2^{1/B} − 1)) (Schörkhuber & Klapuri eq. 6).
- Bins per octave B and scale q: chosen by measurement on the pitch and chroma sets; 12–96 per octave is the range that paper targets (abstract).
- Lowest analysed frequency: the lowest any consumer reads (open: should follow the music's measured range, not a fixed 80 Hz as in `SoundEngine.cpp:767`).
- Highest analysed frequency and decimation stages: from Nyquist of each level and the top frequency consumers read (inference).
- FFT length: the smallest length ≥ window in the backend's supported set (2^a3^b5^c, ×32 for real in the NEON candidate), zero-padded (pffft page; inference).
- FFT backend: the fastest measured on the device at configure (Frigo & Johnson's measuring planner).
- Window shape: Hann or its minimum-phase form, picked by the onset and pitch Pareto measurement (Su & Wu).
- Flux lag per level: the hop count at which consecutive frames overlap by half, τ = round(N/(2H)) (Böck et al. DAFx 2012 §2.1); replaces `kFluxLag`.
- Flux maximum-filter reach: ±1 band on a quarter-tone bank at a 10 ms lag (Böck & Widmer, DAFx 2013, `music-analysis.md` §5), scaled to the actual lag as `OnsetFlux.hpp` already does.
- Half-band taps: from a stopband/transition target (open).
- SDFT damping or modulator form: from the error bound the float format allows (Kennedy 2014; open, measure drift over a whole track).
- Band edges: from the ERB, Bark or semitone formulas and the level's bin width, not hand-picked Hz (section 2).
- Semitone reference: from a tuning estimate (open).
- Values with no source and no rule today, to replace or move to the parameter stage: 40 ms, the 2× pitch FFT, 88.2 kHz, 256, 1/30 s, the 22.05 kHz tilt, the band and EQ edges, 64 drawn points, 47 taps, 440 Hz (section 1).

## 6. Risks and open questions

- Latency differs per level and grows with each decimation stage; consumers that combine levels need each level's `centreSample`, not one frame time (inference).
- Changing the FFT backend or the order of float sums changes outputs; `core/tests/sound_golden_test.cpp` pins the whole frame and will move (code fact; whether to re-pin or to add a tolerance is open).
- Live and export must pick the same backend and sizes on the same device; an export on another device may pick another backend and differ in the last bits (inference).
- The NEON candidate's exact licence text was not read ("BSD-like" only) (open).
- The Arm NEON library's maintenance status and arm64 FFT coverage were not established (open).
- No current-phone FFT timings from a primary source; the 2015 Arm figures are for Cortex-A9/A53 and were not read as numbers (open: device benchmark).
- 32-bit ARM devices get scalar double in the NEON candidate (pffft page); the app should stay in float (inference).
- A real FFT with packing needs an even length (KISS FFT README) or a multiple of 32 (pffft): the burst-derived hop does not constrain the FFT length, the window does, so zero-padding keeps both free (inference).
- KISS FFT's generic radix path allocates and is not thread-safe on a shared plan (`kissfft.hh:330,359`); any non-2/3/4/5 size must be refused or given a per-thread plan.
- Sliding DFT drift over hours of playback is unmeasured (open).
- The minimum-phase window result is a preprint on piano onsets; transfer to mixed music is open.
- Whether the octave pyramid's ≤ 4/3 cost bound holds after kernel sparsity and cache effects on device is open (measure).
- The Bark formula and Duda's mSDFT were read only through abstracts; the mel formula's primary was not found (open).
- One front end per source replaces one engine per renderer (`Renderer.cpp:100`); every renderer then needs a read-only subscription, which is stage 1's publication question (open there).
