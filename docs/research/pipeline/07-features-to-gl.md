# 07: Features to parameters to GL

Stage: the analysed feature frames leave the sound engine, become resolved scene params, pass the safety clamp and reach GL as uniforms.
Research only: no code was changed.
One fact per line; every line cites a file:line in this repo or a primary source URL, or says "derived" (with the proof on the line) or "unverified" or "open".
Repo state read: HEAD `b759e7c6f`; the Graphify index is at `e9fa52b`, and every file below changed since, so all citations come from the current files.
Sources that could not be fetched from the container are named in section 6 and nothing below rests on them alone.

## Sources fetched and read

- [S1] C++ working draft, [intro.races] (data races are undefined behaviour): https://eel.is/c++draft/intro.races
- [S2] C++ working draft, [atomics.fences]: https://eel.is/c++draft/atomics.fences
- [S3] C++ working draft, [atomics.order]: https://eel.is/c++draft/atomics.order
- [S4] C++ working draft, [atomics.lockfree] (`is_always_lock_free`): https://eel.is/c++draft/atomics.lockfree
- [S5] C++ working draft, [saferecl.rcu] (`<rcu>`, `rcu_domain`, grace periods): https://eel.is/c++draft/saferecl.rcu
- [S6] C++ working draft, [hardware.interference]: https://eel.is/c++draft/hardware.interference
- [S7] WG21 std-proposals reflector, J. Maurer on P1478 and seqlocks (2023-04-12): https://lists.isocpp.org/std-proposals/2023/04/6328.php
- [S8] Android NDK Choreographer reference (frame timelines, API 33): https://developer.android.com/ndk/reference/group/choreographer
- [S9] Khronos EGL registry, EGL_ANDROID_presentation_time: https://github.com/KhronosGroup/EGL-Registry/blob/main/extensions/ANDROID/EGL_ANDROID_presentation_time.txt
- [S10] Khronos EGL registry, EGL_ANDROID_get_frame_timestamps: https://github.com/KhronosGroup/EGL-Registry/blob/main/extensions/ANDROID/EGL_ANDROID_get_frame_timestamps.txt
- [S11] Khronos EGL reference page, eglSwapInterval: https://github.com/KhronosGroup/EGL-Registry/blob/main/sdk/docs/man/eglSwapInterval.xml
- [S12] Android frame pacing guide (queue stuffing, presentation timestamps): https://developer.android.com/games/sdk/frame-pacing
- [S13] Android AudioTimestamp reference: https://developer.android.com/reference/android/media/AudioTimestamp
- [S14] Solà, Deray, Atchuthan, "A micro Lie theory for state estimation in robotics", arXiv:1812.01537v9: https://arxiv.org/abs/1812.01537
- [S15] Solà, "Quaternion kinematics for the error-state Kalman filter", arXiv:1711.02508: https://arxiv.org/abs/1711.02508
- [S16] Mateu-Figueras, Pawlowsky-Glahn, Egozcue, "The normal distribution in some constrained sample spaces", arXiv:0802.2643: https://arxiv.org/abs/0802.2643
- [S17] Blelloch, "Prefix Sums and Their Applications", CMU: https://www.cs.cmu.edu/~guyb/papers/Ble93.pdf

## 1. How the app does it now

### 1a. Sound to features

- Each renderer owns its own `sound::SoundEngine`, built on the first `writeAudio` for its sample rate (`core/viz/Renderer.cpp:94-105`).
- The engine is stepped on the GL thread at the start of every frame, under `soundLock_` (`core/viz/RendererFrame.cpp:50-84`, step at `:82`).
- So N live surfaces (stage, wallpaper, dream, cast, export) run N analyses of the same ring; there is no single analysis per source and no publish step.
- The feed reads the ring up to the speaker's sample: `listenLimit` from the playout clock (`core/sound/interp/SpeakerSync.hpp:38-41`), applied per frame in `core/api/geode_viz_api.cpp:394-416`.
- The "now" that limit uses is `steady_clock::now()` when the frame starts (`core/api/geode_viz_api.cpp:396-401`), not the time the frame will be on the glass.
- Bottleneck: what is drawn matches the speaker at render start, then appears one or more vsyncs later; the gap is render time plus queue plus compose, and nothing measures or corrects it.
- Bottleneck: analysis cost sits on the GL thread before any draw, inside the frame budget, once per surface.

### 1b. Frame timing

- One process vsync thread posts `AChoreographer_postFrameCallback64` (`core/stage/android/VsyncThread.cpp:126`, `:143`) and turns a drawn vsync into a last-wins "frame due" command (`:136-161`).
- It reads only `frameTimeNanos`; it never asks for frame timelines (`AChoreographer_postVsyncCallback`, API 33 [S8]), although minSdk is 33 (AGENTS.md).
- [S8] says of the older callback that it "cannot be used to precisely plan your render timeline", and that the expected presentation time "should be used to advance any animation clocks".
- The live stage swaps with plain `eglSwapBuffers` (`core/stage/android/EglWindow.cpp:147-151`); `eglPresentationTimeANDROID` is used only on encoder surfaces (`core/viz/frame/RecordTap.cpp:111`, `core/output/android/LoopRenderHost.cpp:145`).
- No live path reads `EGL_ANDROID_get_frame_timestamps` [S10], so the real sound-to-photon delay is never measured.
- The Kotlin `Choreographer` in `app/src/main/java/dev/geode/stage/Stage.kt:334-353` only polls events; it draws nothing.

### 1c. Params in (the look)

- Every UI edit rebuilds a whole `LookSnapshot` in Kotlin from four flows (`app/src/main/java/dev/geode/ui/EnginePlumbing.kt:92-96`).
- The setter pushes it at once (`app/src/main/java/dev/geode/stage/Stage.kt:62-66`); `pushLook` sends the whole param frame, the LFO, envelope and reaction JSON texts, the tuning and the transition across JNI (`Stage.kt:300-321`).
- Native compares every part, strings included, in `LookLatch::take` (`core/viz/LookSnapshot.hpp:51-71`) and resends what differs (`core/api/geode_viz_api.cpp:776-802`).
- A changed param frame goes through `SceneParams::fromWire` over all 204 rows (`core/viz/Params.cpp`, `fromWire`; row count from `grep -c GEODE_PARAM( core/viz/ParamTable.inc`) and then `Renderer::setParams` under the `stateLock_` mutex (`core/viz/Renderer.cpp:51-54`).
- The GL thread takes the same mutex at its latch (`core/viz/RendererFrame.cpp:170-191`), so a slider drag at pointer rate contends with the render thread.
- Bottleneck: one knob turn costs a whole-look build, a whole-frame JNI copy, string compares and a mutex shared with the GL thread.

### 1d. The chain (Resolver)

- The order is documented and fixed in one place (`core/signal/Resolver.hpp:21-32`): fade, director shuffle, director look, notes, director axes and modulators, macro bank, LFOs, envelopes, reaction routes, motion field, safety.
- The fade glides every Fade row linearly (`core/signal/Resolver.cpp:71`, `core/viz/Params.cpp:95`), Circular rows included.
- Bug (derived): `colorShift` and `paletteShift` are `Circular` and `Fade` (`core/viz/ParamTable.inc:155`, `:527`), so a fade from 0.95 to 0.05 sweeps the long way through the whole wheel, which `core/viz/Circular.hpp:7-15` exists to forbid.
- Each modulator writes its result straight into the params and clamps to the row's range at once (`core/viz/Lfo.cpp:173-181`), stage after stage (`core/signal/Resolver.cpp:138-152`).
- Bug (derived): a clamp between additions breaks associativity and commutativity: with range [0,1], base 0.9, LFO +0.3, route -0.3, today's chain gives clamp(clamp(1.2)-0.3)=0.7, while the sum gives 0.9, and swapping the two stages gives 0.9.
- The director mixes two operation kinds: additive offsets (`camSmooth`, `core/viz/Director.cpp:553`) and multiplicative turns of the motion dials (`core/viz/Director.cpp:557-560`), each with its own clamp.
- Circular rows are left unclamped by the modulators (`core/viz/Lfo.cpp:179`) and the shaders wrap them (`fract`, `app/src/main/assets/shaders/lib_palette.glsl:45`; `cos`, `lib_bulb.glsl:467`), which is already the U(1) rule, done ad hoc.
- The reaction matrix smooths a circular source on its circle (`core/viz/Reactions.cpp:680-700`) and a linear one with an attack/release one-pole (`:701-704`).
- The safety clamp is last and limits params only (`core/viz/VisualSafety.cpp:132-154`), as the owner's rule asks.

### 1e. Camera

- The rig keeps yaw, pitch and roll as three separate critically damped springs (`core/viz/CameraRig.hpp:129`, `core/viz/CameraRig.cpp:52-60`).
- It composes them as three axis rotations in a fixed order each frame (`core/viz/CameraRig.cpp:287-296`).
- Derived: three independent angle springs do not follow the geodesic between two orientations of SO(3), so a large combined turn takes a curved path whose shape depends on the Euler order; [S15] §2.7 gives the geodesic (slerp) instead.

### 1f. Hand constants found on this stage

- `morphRemainSec_ = seconds * 3.0f` (`core/signal/Resolver.cpp:17`).
- `fade <= 0.01f` snaps (`core/signal/Resolver.cpp:71`).
- `kPhaseBackSlack = 0.1f` (`core/viz/Circular.hpp:32`).
- `kTriggerFloor = 0.5f` and the 0.5 / 0.3 hysteresis on Step, Jump and Toggle (`core/viz/Reactions.cpp:287`, `:635-672`).
- `kLuminanceMinAttack = 0.08f`, `kLuminanceMinRelease = 0.35f` (`core/viz/Reactions.hpp:272-273`): these limit luminance params, so they are safety and stay the owner's.
- `kMaxYaw = 0.6f`, `kTouchLook = 0.9f` (`core/viz/CameraRig.hpp:49`, `:52`) and the roll softness `soft * 1.5f` (`core/viz/CameraRig.cpp:230`).
- `kFeedbackIdleFrames = 120` "about two seconds at 60 Hz" (`core/viz/RendererFrame.cpp:36`): a frame count standing for a time, wrong at 90 and 120 Hz.

## 2. The optimal design

### 2a. One analysis per source, published to many readers

- Move the analysis off the GL threads: one engine per source, stepped by whoever writes the source's PCM (the hop chain ADR 0006 plans), publishing one feature frame per hop.
- Stamp each frame with the sample index of its last input sample, the sample rate and a stream generation (a seek or a new track bumps it), so readers can match it to the playout clock.
- Publish into a broadcast history ring: one writer, any number of readers, each slot a sequence number plus the payload.
- Why a history ring and not one triple buffer per reader: a triple buffer hands over only the newest frame, but 2b needs the frames on both sides of the speaker's sample, which may be many hops behind the newest.
- Why not one SPSC ring per reader: the writer would copy each frame once per reader and would have to know the readers; derived, the cost grows with the reader count.
- Why not RCU: `<rcu>` is in the C++ working draft [S5], but whether the NDK's libc++ ships it is open, and a grace period is a deferred free the audio side would have to schedule.
- The seqlock trap: a reader's plain copy of a slot that the writer is rewriting is two potentially concurrent conflicting actions, at least one not atomic, so a data race and undefined behaviour [S1], even when the sequence check would throw the copy away.
- The legal form today: make each payload word a `std::atomic<uint32_t>` (or `std::atomic<float>`), read and written with `memory_order_relaxed`, with a release fence after the writer's copy and an acquire fence before the reader's second sequence load [S2]; no data race remains because every conflicting access is atomic [S1].
- That is what P1478's per-byte atomic memcpy would provide as one call (reflector discussion of the seqlock example, [S7]); the paper itself could not be fetched, so its exact wording and status are unverified.
- Payloads must be trivially copyable for any memcpy-style copy to give a live object ([S7], citing [basic.types.general]).
- Check at compile time that the word type is lock-free with `std::atomic<T>::is_always_lock_free` [S4]; whether relaxed loads compile to plain loads on ARM64 is unverified.
- Keep the sequence word and the payload of the slot being written on separate cache lines, with `hardware_destructive_interference_size` [S6].
- Readers never spin: if the slot they want is being written (odd sequence) or was lapped (sequence moved), they take the neighbouring complete slot from the history, so the GL thread's read is bounded.
- Derived: a reader that never waits cannot be blocked by a writer that was preempted mid-write, which is the failure a spinning seqlock reader has.
- Export keeps its own synchronous publisher: the analysis and the render run on one thread in sample order, so no concurrency and identical bits on every run.

### 2b. Picking the frame the speaker plays

- Target sample: `s* = speakerFrame(presentNs)`, where `presentNs` is the frame's expected presentation time from 2c, not the render start.
- The playout clock already maps time to a speaker sample (`core/sound/interp/SpeakerSync.hpp:38-41`); [S13] defines the platform pair it builds on: a frame position and "the estimated time when the frame was presented or is committed to be presented", on CLOCK_MONOTONIC.
- [S8] gives frame times on CLOCK_MONOTONIC too, so the two clocks need no conversion.
- Find the two published hops whose stamps bracket `s*` and blend them by `t = (s* - s0)/(s1 - s0)`; `t` comes from the stamps, so it holds at any hop size, sample rate or frame rate.
- Blend each feature by its own space: linear for a line, shortest arc for a hue, forward for a phase (the two rules of `core/viz/Circular.hpp:13-15`), hold for a discrete value.
- Events (an onset, a section change) do not blend: a renderer fires every event whose stamp falls in `(s*_previous, s*]`, so none is lost at 30 fps and none doubles at 120 fps (derived from the half-open intervals tiling the sample line).
- Never extrapolate past the newest hop: if `s*` is newer, hold the newest frame and count the miss, since an extrapolated onset would be invented.

### 2c. Latching late and timing the frame

- On API 33+ post `AChoreographer_postVsyncCallback` and read the frame timelines: count, preferred index, deadline and expected presentation time for each [S8].
- Pick the earliest timeline whose deadline the frame can meet, given the measured render time (below), as [S8] steps 1-3 describe.
- Tell EGL the chosen time with `eglPresentationTimeANDROID` before the swap; it should be on the "system monotonic up-time clock" when shown on a display [S9].
- Per [S12], presentation timestamps keep a short frame from being shown early, and frames queued faster than the display can show them add latency ("buffer-stuffing").
- Feed the same `presentNs` to 2b and to every time-based modulator, so sound, picture and clock agree on one instant.
- Measure what happened with `EGL_ANDROID_get_frame_timestamps` [S10]: `EGL_RENDERING_COMPLETE_TIME_ANDROID` minus the frame's start gives the render time, and `EGL_DISPLAY_PRESENT_TIME_ANDROID` minus the requested time gives the error to correct next frame.
- `eglGetCompositorTimingANDROID` gives the compose deadline, interval and compose-to-present latency before the first swap [S10].
- Keep the swap interval at its default of 1 [S11]; 0 swaps "as soon as the render is complete" [S11], which tears the vsync contract the pacer relies on.
- Latch the shell's knob values and touch at the last point before the param chain runs, after everything that does not depend on them (scene staging, uploads), since only their age, not the sound's, depends on when they are read.
- The sound needs no late latch: it is chosen by `presentNs`, so reading it early or late gives the same frame (derived from 2b).

### 2d. The parameter chain

- Each param row declares its space in the schema (`core/viz/ParamTable.inc` gets a column): a line, an open interval, a positive scale, a circle, or a discrete set.
- Each continuous space has a chart `phi` onto the reals: identity for a line, `logit((x-lo)/(hi-lo))` for an interval, `ln x` for a positive scale, the angle for a circle.
- Base: the user's (or the style's) value `b`, never rewritten, as `core/viz/MacroBank.hpp:17` and `core/signal/Resolver.cpp:127-128` already state for the knobs.
- Modulation: every source (LFO, envelope, reaction route, macro link, director offset, motion field, note response) contributes a real offset `d_k` in chart units for each param it reaches.
- Resolved value: `x = phi^-1( phi(b) + sum_k d_k )`, one chart round trip per param per frame.
- For a circle, the sum is wrapped once at the end, because `Exp(a + b) = Exp(a) o Exp(b)` on S1 and SO(2) [S14, eq. 120].
- For an interval, the result cannot leave the range, because `phi^-1` maps all of R into it (derived), so no stage needs a clamp and the 1d bug cannot happen.
- For a positive scale, the director's "turn by a factor" becomes an additive offset `ln(factor)` [S16 §3: on R+, `x (+) y = x * y`], so it commutes with every other modulator.
- Discrete rows (Int, Bool, pickers) take no modulation sum; they keep their base, as the macro bank already refuses them (`core/viz/MacroBank.hpp:50-52`).
- The director's shuffle, which replaces a value rather than offsetting it, becomes a change of base under the director's hand, ahead of the sum (open whether every shuffle can be written this way).
- The safety clamp stays last, on the resolved params only (`core/viz/VisualSafety.cpp:132-154`); it is a projection (clamp of a clamp is the same clamp), not a group element, so it sits outside the algebra and cannot be undone by any modulator.
- Uniforms are written from the clamped params; how they are uploaded (per-name `glUniform*` today, `core/viz/SoundUniforms.cpp:43-57`) belongs to stage 08 and is not judged here.

### 2e. One knob, one small call

- Add `geode_viz_set_param(geode_viz*, int slot, float value)` to `core/api/geode_api.h` and a Kotlin `Stage.setParam(slot, value)` that the slider calls directly.
- Native keeps the base as one `std::atomic<float>` per slot plus an atomic dirty bitset: the shell stores the value (relaxed) and sets the bit (release); the GL thread exchanges the bitset (acquire) at its latch and reads only the changed slots ([S2], [S3]).
- Derived: both sides are wait-free; a drag of 100 events between two frames costs 100 stores and one exchange, and the last value wins.
- The whole-look path stays for what is truly whole: a preset load, a style switch, a new surface.
- A preset morph stays a base change with a fade, run in chart space (2f), so a hue morph takes the short arc.

### 2f. Smoothing and mapping without hand constants

- A one-pole step `k = 1 - exp(-dt/tau)` is the exact solution over `dt` of `dx/dt = (u - x)/tau` for a held input (derived), so it gives the same curve at any frame rate; the reaction matrix already uses this form (`onePoleK`, `core/viz/Reactions.cpp:686`, `:704`).
- Smooth in chart space: a hue follows its arc, an interval follows its logit, so a smoothed value can never overshoot its range (derived from 2d).
- `tau` comes from the user (a route's attack and release, `core/viz/Reactions.cpp:674-679`), or from the measured period of what feeds it.
- Derived: a feature published every hop of `h` samples at `fs` changes at most every `h/fs` seconds, so a time constant below `h/fs` smooths nothing; that period is a measured floor, not a chosen number.
- The frame period from the chosen timeline [S8] is the other measured bound: smoothing finer than one frame is invisible.
- Mapping curves stay the user's per route (`r.curve`, `core/viz/Reactions.cpp:633`); the curve maps the source into a chart offset, so the same curve means the same thing on every param.
- The trigger thresholds (`core/viz/Reactions.cpp:287`, `:635-672`) could come from each source's own running distribution (an edge is a crossing of its own upper quantile); the estimator and its window are open.
- The fade's 3x morph tail and 0.01 s snap (1f) could become "until the chart distance is below one quantum of the output" (derived idea; the quantum per param is open).

### 2g. Camera rotation on SO(3)

- Keep the camera's orientation as a unit quaternion and its look offsets as a tangent vector; compose with `q * Exp(d)` [S14, eq. 25].
- Interpolate between orientations with slerp, `q(t) = q0 * (q0* * q1)^t` [S15, eq. 127], after flipping `q1` when `q0 . q1 < 0` so the path is the short one [S15 §2.7].
- Run the springs on the tangent vector `Log(q_target^-1 * q)` [S14, eq. 26], which keeps their critical damping and makes the path a geodesic.
- Yaw only, or pitch only, then behaves exactly as today, since rotations about one axis commute [S14 §IV].

## 3. Polymorphic interfaces (signatures only)

- Real-time rule: no allocation, no lock and no exception in any `noexcept` member below; configuration changes arrive through the latch, not mid-frame.
- Open sets (user modulators) use virtual dispatch once per modulator per frame; closed sets (param spaces) use `std::variant` and one pass over all slots, so there is no virtual call per param.

```cpp
namespace geode::signal {

// When a feature frame was heard: its last input sample, the rate, and the stream it belongs to.
struct FeatureStamp {
    std::int64_t endSample;
    std::int32_t sampleRateHz;
    std::uint32_t generation;
};

// One reader's view of a published stream. Never blocks; false when nothing usable is there.
class FeatureReader {
public:
    virtual ~FeatureReader() = default;
    virtual bool bracket(std::int64_t sample, std::uint32_t generation,
                         GeodeSoundFrame& before, FeatureStamp& beforeStamp,
                         GeodeSoundFrame& after, FeatureStamp& afterStamp) noexcept = 0;
    virtual bool newest(GeodeSoundFrame& out, FeatureStamp& stamp) noexcept = 0;
    virtual std::uint64_t misses() const noexcept = 0;
};

// One writer per source; readers are made before the stream starts.
class Publisher {
public:
    virtual ~Publisher() = default;
    virtual void publish(const GeodeSoundFrame& frame, FeatureStamp stamp) noexcept = 0;
    virtual std::unique_ptr<FeatureReader> reader() = 0;  // not real-time: allocates
};
// Implementations: SeqRingPublisher (live, many readers), SyncPublisher (export, one thread),
// TripleBufferPublisher (a reader that only ever wants the newest frame).

// One param's geometry: the chart to the reals and back, and how two values blend.
class ParamSpace {
public:
    virtual ~ParamSpace() = default;
    virtual float toChart(float value) const noexcept = 0;
    virtual float fromChart(float chart) const noexcept = 0;
    virtual float blend(float from, float to, float t) const noexcept = 0;  // geodesic in the space
    virtual float follow(float current, float target, float k) const noexcept = 0;
    virtual bool modulable() const noexcept = 0;  // false for discrete rows
};
// Implementations: LineSpace, IntervalSpace (logit), ScaleSpace (log), CircleSpace (hue: shortest arc),
// PhaseSpace (forward only), DiscreteSpace (hold).

// What a frame gives every modulator.
struct ModContext {
    float dtSeconds;            // the music-scaled step
    float wallDtSeconds;        // the wall step
    std::int64_t presentNs;     // the chosen frame timeline's expected presentation
    std::int64_t speakerSample; // s*
    const GeodeSoundFrame* sound;
    const sound::Emotion::State* feeling;
    std::uint32_t moments;      // events in (s*_previous, s*]
};

// Anything that adds chart offsets: an LFO, an envelope, a macro link, a director offset, the motion field.
class Modulator {
public:
    virtual ~Modulator() = default;
    virtual void tick(const ModContext& ctx) noexcept = 0;
    // Adds this modulator's offsets, in chart units, into offsets[slot]; touches only its own slots.
    virtual void contribute(std::span<float> offsets) const noexcept = 0;
};

// A reaction-matrix route: a Modulator whose source is a named feature and whose shape is the user's.
class Reaction : public Modulator {
public:
    virtual ReactSource source() const noexcept = 0;
    virtual LfoTarget target() const noexcept = 0;
};

// The chain: base plus the sum of every modulator, mapped back, then the safety projection.
class ParamResolver {
public:
    virtual ~ParamResolver() = default;
    virtual void setBase(int slot, float value) noexcept = 0;
    virtual void resolve(const ModContext& ctx, std::span<Modulator* const> modulators,
                         viz::SceneParams& out) noexcept = 0;
};

}  // namespace geode::signal
```

## 4. The algebraic structure

- Group axioms used throughout: closure, identity, inverse, associativity [S14, eqs. 1-4].
- Modulation offsets live in (R^n, +), an abelian group: any grouping and any order give the same sum (derived from the field axioms of R).
- Because the operation is associative, partial sums can be formed in any tree and reused, which is the condition [S17] states for reduce and scan ("The correctness of the result relies on (+) being associative").
- So a frame can sum the modulators that do not depend on the sound once, and add the sound-driven ones later, with the same bits as one long sum (derived; float rounding order must still be fixed for bit-identical export, see section 6).
- Transport of structure: for any bijection `h : E -> R^d`, `x (+) y = h^-1(h(x) + h(y))` makes E a vector space [S16 §2]; with `h = logit` of the normalised value, every open param interval becomes a group isomorphic to (R, +).
- On R+ the same construction with `h = ln` gives `x (+) y = x * y` and `a (.) x = x^a` [S16 §3], which is the director's "turn by a factor".
- Base versus modulation is a group action of (R^n, +) on the param space: `g . b = phi^-1(phi(b) + g)`, with `0 . b = b` and `g . (h . b) = (g + h) . b` (derived from the transport above).
- The action is free and transitive on each continuous component (derived: `g = phi(x) - phi(b)` is the one offset that takes b to x), so "how far the modulation moved this param" is always a well-defined chart offset; the second arc on a macro knob can show it.
- Circular params live on U(1) = S1, isomorphic to SO(2); composition is commutative there and `Log(Q o R) = Log(Q) + Log(R)` [S14, eqs. 119-121], so offsets on a hue add as reals and wrap once.
- A beat or bar phase is the same circle with a forward-only follower (`core/viz/Circular.hpp:36-39`); the follower is a rule on paths, not a different group.
- Per-stage transforms of the form `x -> a*x + c` form a monoid under composition, with `(a1, c1) o (a2, c2) = (a1*a2, a1*c2 + c1)` (derived); it is not commutative, so today's mixed add and multiply stages depend on their order.
- Writing every stage as an offset in chart space collapses that monoid to the abelian group above, which is why the order of modulators stops mattering.
- A clamp is idempotent and not invertible, so it is not in any of these groups; applying it once, last, keeps the algebra before it exact.
- Camera orientation lives on SO(3) (unit quaternions S3 double-cover it [S15 §2.7]); composition there does not commute, so the right and left plus operators differ [S14, eqs. 25-28] and the rig must pick one (local frame: right plus).
- Interpolation on any of these groups is `X(t) = X0 o Exp(t * Log(X0^-1 o X1))` [S14, eq. 25-26; S15, eq. 126]; on R it is the plain lerp, on S1 the short arc, on S3 slerp.

## 5. What is dynamic and where it comes from

- Sample rate: from the stream that wrote the ring (`Cursor::take`'s `sampleRateHz`, `core/api/geode_viz_api.cpp:405-414`).
- Hop size and hop period: from the analysis that publishes (stage 05/06), carried in each `FeatureStamp`.
- History depth of the broadcast ring: the most hops the speaker can trail the newest frame, from the playout clock's measured sink delay plus the hop (`core/sound/interp/SpeakerSync.hpp:21-28` sizes the PCM ring the same way).
- Target sample `s*`: the playout clock at the expected presentation time [S8], [S13].
- Expected presentation time, deadline and frame period: the chosen Choreographer frame timeline [S8].
- Refresh rate changes: `AChoreographer_registerRefreshRateCallback` [S8].
- Render time and present error: `EGL_ANDROID_get_frame_timestamps` [S10], measured each frame.
- Interpolation weight between hops: from the two stamps and `s*` (2b).
- Param ranges, kinds and spaces: the schema rows (`core/viz/ParamTable.inc`).
- Base values: the user's and the style's look, through `geode_viz_set_param` and the look.
- Modulation depths, curves, attack and release: the user's routes, waves and envelopes, or the style's DNA (`core/viz/ReactionResolve.hpp:40-51`).
- Which feature drives which param: the reaction matrix alone (owner's rule); nothing in this stage adds a route.
- Colour: the palette and hue rows are base values the style and the user own; this stage only adds the user's modulation and never narrows them (owner's rule).
- Tempo: a tempo-locked LFO reads the heard tempo when there is one; nothing here prefers or weights a tempo (owner's rule).
- Smoothing floors: the hop period and the frame period, both measured (2f).
- Safety limits: the owner's constants in `core/viz/VisualSafety.hpp:78-84`, the one place fixed numbers belong on this stage.

## 6. Risks and open questions

- Unverified: P1478's current wording and status; open-std.org reset every connection from the container, so only the reflector thread [S7] was read.
- Unverified: Boehm's paper on seqlocks and language memory models could not be fetched; nothing above depends on it.
- Open: whether `<rcu>` [S5] is in the NDK's libc++; the design does not need it.
- Unverified: that `std::atomic<float>` relaxed loads and stores are plain loads and stores on the shipped ARM64 targets; `is_always_lock_free` [S4] proves only lock-freedom, so the cost needs a device measurement.
- Risk: element-wise atomics turn a block copy into one atomic op per word; a GeodeSoundFrame is hundreds of floats (`core/sound/SoundFrame.h:21-26`), times two bracketing frames per reader per frame.
- Risk: [S9] says the exact meaning of the presentation time "depends on the native window system's use" of it; the live surface's behaviour must be checked per device.
- Risk: frame timelines are predictions; under GPU load the frame misses its timeline and the sound it chose is one vsync early; the [S10] present-time feedback corrects the next frame, not this one.
- Risk: not every device supports every [S10] timestamp ("Not all implementations may support all of the above timestamp queries"); the design must fall back to the choreographer's expected time alone.
- Open: the logit chart changes how a modulation of fixed depth feels near the ends of a range (it eases instead of clipping); whether that is the wanted feel for every row is the owner's call.
- Open: saved looks store depths in today's linear units; a chart change needs a migration that keeps a saved look's motion the same near the middle of its range.
- Open: a `logit` of a base exactly at `lo` or `hi` is infinite; the base must be held inside the open interval by one float step, or the chart must treat the end points as fixed points.
- Open: bit-identical export needs a fixed summation order even though the group sum is order-free in exact arithmetic; float addition is not associative, so the order must be pinned (for example slot order).
- Open: whether the director's shuffle and the motion field can all be expressed as chart offsets; any stage that cannot stays a base change ahead of the sum.
- Open: the per-source analysis moves the engine off the GL thread; who owns its thread for each source (player, mic, capture, export) is stage 05/06's decision.
- Open: one analysis per source changes what each surface hears today (band trims, `core/viz/RendererFrame.cpp:217`, are per renderer); per-surface trims must become a reader-side step, never a second analysis.
- Risk: a writer preempted mid-slot leaves that slot odd; readers fall back to an older slot, so the picture can lag one hop more for that frame, but never stalls.
- Open: the trigger thresholds and the morph tail (1f) have no measured replacement yet; the quantile estimator and the output quantum each need their own source before a change.
