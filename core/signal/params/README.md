# core/signal/params

Part of [`core/signal`](../README.md). Each scene param's value has two parts: what it *is* (its base, moved only through a state machine) and what moves it *this frame* (its modulation). One clamp combines them.

| File | Owns |
|---|---|
| `ParamSchema.hpp/.cpp` | One row per param: stable ASCII id, kind (Float, Int, Bool, Circular), default, range, `luminance` flag. Holds **the one clamp**: non-finite values go to the default, Int rounds, Bool snaps, Circular wraps. |
| `ParamState.hpp/.cpp` | **The param state machine.** Phases: **Rest**, **Glide** (easing to a new committed value), **Held** (a finger is on the control), **Trial** (a value the user may not keep, which springs back), **Driven** (a timeline owns it). Inputs are Set, GlideTo, Hold, Release, Try, Drive, Undrive and Reset, each carrying an `Origin` (User, Preset, Timeline, Engine). Only the User holds or tries and only the Timeline drives or undrives; a GlideTo or Reset of negative or non-finite seconds is rejected (0 lands at once). The full transition table is in the header. |
| `ParamStore.hpp/.cpp` | One cell per row, plus this frame's modulation (`Add`, `Multiply`, `Toward`, each weighted). `effective()` is the base ⊕ the modulation, clamped once. A held control shows exactly the finger's value. |

**The rules the machine enforces:**
- A finger outranks the engine.
- A timeline outranks both while it drives.
- A preset can change what a trial springs back to, but never takes a param from a finger.
- Every input's value is clamped before it is stored.
- Int and Bool params never interpolate.

**Why a machine:** these behaviours were scattered. Today they live in `paramFadeSec` and morph lerps in `core/viz/RendererFrame.cpp`, `ProLock`'s spring-back in Kotlin, and keyframe evaluation in two Kotlin places that disagree. Each is now one phase, with every transition tested.

**Still to come (ADR 0006):** `ParamSchema.inc`, generated from the native field table, which also generates `Params.hpp`, `SceneParamsCodec.kt`, slider ranges and uniform uploads (#346). Wiring the store into the scene params resolver (#344).

**fosfora:** `params/types.rs` (`ParamDef`), `params/store.rs` (`values` vs `modulated`, `clear_modulation`, `effective`).

**Test that proves it:** `core/tests/signal_test.cpp`. It checks:
- every cell of the transition table
- glides (linear halfway, landing, a finger stopping one, Int landing at once, a hue going the short way)
- a trial timing out, a held trial following the finger, a preset moving the spring-back target
- `effective()` of the base plus a +100 modulation equals the max, and a later −100 still pulls it back
- that the order modulators run in doesn't matter
