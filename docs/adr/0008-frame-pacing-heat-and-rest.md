---
status: accepted
---

# Frame pacing, heat and rest: one native rule for every surface

On 2026-11-05 the owner answered Group E's questions about the render loop (session board #433). This ADR records those answers and where the rule lives. It is part of [ADR 0006](0006-fosfora-aligned-engine.md)'s `core/viz/frame`, and [ADR 0003](0003-engine-internals-in-cpp.md) puts it in C++.

## What the owner decided

1. **Frame rate is a user setting, set per surface.**
   - **Live** (screen, PiP, second screen) follows Settings › Performance's fps cap.
   - **The wallpaper** has its own fps choice in its options, default 30.
   - **The dream** has its own fps choice in Behaviour › Dream, default 30.
   - **Export** renders at the project's fps and is never paced. Today the wallpaper is hard-wired to `min(60, Performance cap)` and the dream has no choice at all.
2. **On a hot phone, fps goes first.** The ladder from cool to hot:
   1. fps drops to 30;
   2. then render scale drops;
   3. then march quality drops;
   4. then post FX go, last.

   Liquid keeps its own "when hot" choice (`LiquidHotPolicy`). Today the native tiers do the reverse: they lower scale first and cap fps only at Minimal. That cap (`ThermalTierInfo::fpsCap`) is never applied, because no code reads it.
3. **The wallpaper and dream in silence** ("rest when silent", 6 s): they hold a still frame. That is unchanged.
4. **Battery saver:** the wallpaper **and** the dream hold a still frame while "pause in battery saver" is on (default on). Both already pass `isPowerSaveMode` into `wallpaperRunMode`, so this keeps today's rule.
5. **Reduced motion:** either the in-app Reduce motion switch or the system's Remove animations (`ANIMATOR_DURATION_SCALE == 0`) makes the wallpaper **and** the dream hold a still frame. For these two surfaces this replaces #389's "scale motion down". The live visual's rule stays with #336.
6. **The dream with reduced motion off** keeps today's gentle flight. The owner had no preference.

## Where the rule lives

Everything below is C++ in `core/viz/frame`, host-tested in `core/tests`, behind `geode_api.h`:

- **The pacer:** the vsync period estimate, the divisor, the dt clamp and the first-frame dt. Kotlin keeps the Choreographer callback and passes each vsync's `frameTimeNanos` in. Native answers "draw, with this dt" or "skip". One dt rule replaces both `FramePacer`'s 0.001..1/15 s clamp and `Renderer::beginFrame`'s 0.001..0.1 s.
- **The effective cap:** `min(surface cap, thermal cap)`. Kotlin reads it back for `Surface.setFrameRate`.
- **One thermal state per process** (#387). The Kotlin shell reads `PowerManager` and pushes status and headroom in at most once a second. Every renderer reads the same tier.
- **The tier ladder** above: `thermalTierInfo` gains a march scale, and the order follows point 2.
- **Performance tiers** (`SAVER`, `BALANCED`, `HIGH`) and `modeForDevice`. Kotlin keeps reading RAM, `isLowRamDevice` and the media performance class, and passes them in.
- **The run rule** for the wallpaper and the dream (`STOPPED`, `STILL`, `ANIMATED`): the inputs are the surface, visibility, covered, preview, battery saver, silence, reduced motion (both switches) and the options.

Kotlin keeps the Choreographer callback, `Surface.setFrameRate`, `GLSurfaceView` and EGL, and the platform reads (RAM, performance class, `PowerManager`, the animator scale, sensors).

## Consequences

- The wallpaper's and the dream's fps choices are new settings rows (Group F's screens). The stores go in `WallpaperPrefs` and `BehaviourPrefs`.
- A hot phone's first visible change is smoothness, not sharpness. This is the owner's call, and it overrides the ordering in the current `ThermalGovernor.cpp`.
- A reduced-motion user sees a still wallpaper that no longer reacts to music. That is the owner's call for vestibular safety.
