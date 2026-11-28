# Fixed 30 FPS Contract

## Goal

The live visualizer always targets **30 FPS from creation**. This covers the in-app stage, including its picture-in-picture and second-screen presentations. Quality remains user-controlled, but FPS is not a live setting and never adapts automatically. Export and Cast retain their own independent clocks.

## Production policy

- The only live render rate and window frame-rate vote is 30 FPS.
- Saver, Balanced, and High quality presets all use 30 FPS.
- Settings › Performance has no live FPS selector and no “Follow device” mode.
- The retained `perf_fps_cap` key is never read as a choice. Legacy values, including 30–120 FPS and Follow device, normalize to 30 on load and every save writes 30.
- 90 Hz and 120 Hz are not app frame-rate or display-mode options. Android may still run the physical panel at 90/120 Hz because of system policy or other surfaces; the app keeps rendering at 30 FPS and does not claim those panel rates as app options.
- The app does not select `preferredDisplayModeId`, set `preferredRefreshRate`, enumerate modes to choose a preferred one, or chase supported modes.
- The legacy `refreshRateFor` / `liveFpsRefresh` ABI slot remains callable but always returns `0` (no manual preference).
- `StageThread` makes the standing surface vote with `ANativeWindow_setFrameRateWithChangeStrategy(30, ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT, ANATIVEWINDOW_CHANGE_FRAME_RATE_ONLY_IF_SEAMLESS)` once per new window or revote, not per frame.
- The vote states the app’s actual render rate. Default compatibility is correct for this non-video surface. The platform may honor a multiple of the requested rate or keep a higher panel rate; neither changes the 30 FPS render target.
- SurfaceView’s explicit frame-rate vote is respected, but View/Compose votes combine and the highest can win. The live stage therefore avoids introducing a competing Compose refresh-rate vote.
- Swappy remains the preferred frame-pacing abstraction. It receives the fixed 30 FPS swap interval before each new window; where it cannot pace a stage, the existing one-shot Choreographer path remains the fallback under the same seamless-only 30 FPS vote.
- “Swappy-only” means no competing pacing authority while Swappy is active. Android's Frame Pacing docs explicitly recommend an alternative when Swappy cannot initialize, so the fallback remains a compatibility path rather than a second simultaneous pacer.
- The render loop does not use BufferQueue stuffing as a timer. Android's game-loop docs warn that swap back-pressure produces variable frame timing and initial near-zero intervals; Swappy exists to avoid that inconsistency.
- Refresh callbacks update the actual display period only. They do not change the render rate or trigger per-frame mode requests.
- One frame budget remains derived in `SwapPace.hpp`.
- ADPF CPU/GPU work hints remain scheduling aids only.
- Game Mode config sets `supportsBatteryGameMode=false`, `supportsPerformanceGameMode=false`, `allowGameDownscaling=false`, and `allowGameFpsOverride=false`.
- No thermal/load-driven FPS or quality scaling is introduced.

## Render FPS versus physical panel refresh rate

“Ratio” means whole-refresh-period pacing, not a user option:

| Physical panel timing case | Refresh periods per 30 FPS frame |
| --- | --- |
| 60 Hz | 2:1 |
| 90 Hz | 3:1 |
| 120 Hz | 4:1 |

The pure `vsyncSpan` helper accepts these whole ratios (including their nominal NTSC-rate equivalents) and returns `0` for an uneven ratio such as 75 Hz ÷ 30 FPS = 2.5. Rejecting an uneven ratio is timing validation only; it never creates another FPS choice.

The physical panel may move between these rates while the stage remains live. The app observes the active period for pacing and continues to render and vote 30 FPS.

## ABI and stored-value compatibility

- Kotlin/JNI/native signatures for `liveFpsChoices`, `liveFpsCap`, `liveFpsRefresh`, `stageSetLiveFps`, and the retained performance FPS slot are unchanged.
- `GEODE_LIVE_FPS_MAX_CHOICES` remains 12 for legacy callers; `liveFpsChoices` writes only the first slot and returns `1`.
- `GEODE_LIVE_FPS_FOLLOW` remains `-1` so old values deserialize, but it is never offered or returned.
- `GEODE_LIVE_FPS_DEFAULT` is 30, matching the native fixed-rate policy.
- `liveFpsCap` normalizes every legacy stored value to 30 regardless of the supplied display-rate list.
- `liveFpsRefresh` returns `0` regardless of its legacy arguments. The production 30 FPS vote does not depend on this slot.

## Unchanged

- Export FPS ladder: 24, 25, 30, 50, 60.
- Cast fixed-rate clock.
- User quality controls: resolution, march scale, post-FX.
- Offline export quality path.
- Existing EGL/GL ownership and StageThread lifecycle.
- Swappy tracer and ADPF timing collection.

## Validation

- Quality tier tests: every live tier reports 30 FPS, and native ABI constants match the fixed policy.
- Live FPS tests: the only offered rate is 30, all legacy caps normalize to 30, and the retired manual refresh preference returns 0.
- Whole-refresh-ratio tests: 30 FPS maps to 2/3/4 periods on 60/90/120 Hz panels and uneven ratios are rejected.
- SwapPace tests: the fixed 30 FPS interval and whole-refresh budgets on 60/90/120 Hz physical-panel timing cases.
- PerformanceStore tests: legacy and malformed stored FPS values migrate to 30; the UI has no FPS selector.
- Swappy integration review: the interval is set before `SwappyGL_setWindow`.
- Manifest test: Game Mode opt-outs are present.
- Device/CI Perfetto check: stable approximately 30 FPS with no competing Compose refresh-rate vote. The panel’s physical refresh rate may legitimately be higher.

## References

- `kb://android/media/optimize/performance/frame-rate`
- `kb://android/develop/ui/views/animations/adaptive-refresh-rate`
- `kb://android/games/optimize/display-refresh-rate-change`
- `kb://android/games/sdk/frame-pacing/opengl/add-functions`
- `kb://android/games/develop/gameloops`
- `kb://android/games/optimize/adpf/gamemode/gamemode-interventions`
- `kb://android/games/optimize/adpf/gamemode/fps-throttling`
