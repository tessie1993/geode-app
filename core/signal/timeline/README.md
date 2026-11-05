# core/signal/timeline

Part of [`core/signal`](../README.md).

**Built:** `TakeTimeline` (#457, ADR 0008): a track take's choices on song time (style, preset, every param, the play's seed), recorded natively, kept as `takes/<takeId>.choices`, and read back per frame when My videos' "Render again" renders the take offline. Host test `core/tests/take_timeline_test.cpp`. The rest below is still the target.

**Owns:** everything that changes the look on programme time: style switches (by time and by beat), scene keyframes and a take's param events. Kotlin builds it once from the Studio project and sends it; native samples it per frame as a source. Studio preview and export send the same timeline, so they cannot disagree.

**Moves in (as of `cf7e88f`)**
- `core/viz/StyleSchedule` (style switches; today set only by `OffscreenSceneRenderer`)
- keyframe evaluation, today Kotlin per frame in two places that disagree: `ui/StudioPreviewController.kt:236-250` and `ui/ExportController.kt:840-845`
- take params, today re-parsed from `org.json` every export frame (`data/PerformanceTake.kt` `stateAt`)
- reactions re-resolved when a scheduled switch changes the style (export resolves the first style's only today, `OffscreenSceneRenderer.kt:142`)

The project document (`editor/`) stays Kotlin. Only its evaluation over time moves here.

**fosfora:** `headless/schedule.rs`, `headless/loop_spec.rs`.

**Test that proves it:** a project with a take, a keyframe track and a beat cut renders the same params in preview and export at the same programme times.
