# core/signal/timeline

Part of [`core/signal`](../README.md). **Landed:** `Keyframes` (the bezier solve, ease presets and `Track::valueAt`, ported from `editor/Keyframes.kt` with the same float maths) and `SpeedRamp` (a keyframed clip speed as Media3's piecewise speeds, plus the clamp a saved clip speed lacked), behind `geode_api.h`'s export section and tested by `core/tests/timeline_test.cpp`. The Kotlin callers still evaluate keys themselves until they switch.

**Owns:** everything that changes the look on programme time: style switches (by time and by beat), scene keyframes and a take's param events. Kotlin builds it once from the Studio project and sends it; native samples it per frame as a source. Studio preview and export send the same timeline, so they cannot disagree.

**Moves in (as of `cf7e88f`)**
- `core/viz/StyleSchedule` (style switches; today set only by `OffscreenSceneRenderer`)
- keyframe evaluation, today Kotlin per frame in two places that disagree: `ui/StudioPreviewController.kt:236-250` and `ui/ExportController.kt:840-845`
- take params, today re-parsed from `org.json` every export frame (`data/PerformanceTake.kt` `stateAt`)
- reactions re-resolved when a scheduled switch changes the style (export resolves the first style's only today, `OffscreenSceneRenderer.kt:142`)

The project document (`editor/`) stays Kotlin. Only its evaluation over time moves here.

**fosfora:** `headless/schedule.rs`, `headless/loop_spec.rs`.

**Test that proves it:** a project with a take, a keyframe track and a beat cut renders the same params in preview and export at the same programme times.
