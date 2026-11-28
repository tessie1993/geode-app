# Whole-App E2E Migration

## Goal

Migrate the complete instrumented/journey suite from a Compose-only `MainActivity` assumption to `GeodeGameActivity : GameActivity`, its direct native callback host, the single StageThread game/render loop, and the Compose sibling overlay. Tests must prove user-visible behavior and native surface/input/window contracts, not merely that Compose semantics exist.

## Current baseline

- 33 Kotlin files under `app/src/androidTest`: 23 test classes, 10 helpers, 65 `@Test` methods.
- Nine test classes launch `MainActivity` through `createAndroidComposeRule<MainActivity>()`.
- `UiTour`/store rules use concrete `ActivityScenarioRule<MainActivity>` aliases.
- Strong Compose/navigation/control coverage; weak native surface, input, recreation, multi-window, second-display, export, and Cast coverage.
- Current PR CI runs JVM tests but no connected instrumented suite.

## New test layers

### JVM/Robolectric

Keep host-independent tests local:

- Compose components and state mapping.
- `LiveStageRetainTest` generic retention contract.
- Cast/link/deep-link route mapping.
- Theme, glass contrast, app state, widget/presentation state.

Do not force GameActivity/SurfaceView through Robolectric.

### Compose overlay semantics

Use the real migrated Activity for:

- App launch smoke.
- Shell tabs and navigation.
- Player/library journeys.
- Visuals journey.
- Paywall.
- Export reachability.

Semantic selectors remain the primary assertions. These tests do not prove native input delivery.

### Native surface/input integration

New harness:

- `GeodeGameActivityHostRule` — ActivityScenario/Compose setup and deterministic teardown.
- `GameActivityHostProbe` — direct callback installation, Activity/window generation, pending-window replay, synchronous destroy/redraw acknowledgement, and Swappy attach/detach ownership.
- `StageProbe` — the one StageThread identity, native window attached, size, display ID, first frame, and input acknowledgement.
- `WindowProbe` — bounds/location/insets/orientation/PiP/multi-window/display ID.
- `DeviceShell` — checked commands, captured/restored device settings.

Required production debug-only hooks:

- Direct native host generation/state snapshot.
- Pending window replay count.
- Native window attached/detached acknowledgement.
- Redraw completion acknowledgement.
- First-frame-presented event.
- Input sequence acknowledgement.
- StageThread identity token across recreation.
- Proof that no glue thread/`android_main` or second StageThread exists.

Hooks must be debug-only and must not add per-frame JNI in release.

### Device/window/service lane

Add focused tests:

- `GameActivityLifecycleTest`: cold/warm launch, direct callback installation, background/foreground, recreation, process death, surface reattachment, retained Stage/StageThread identity, synchronous window-destroy and redraw callbacks.
- `GameActivityInputTest`: touch, multi-touch, key, D-pad, IME, overlay-vs-native routing, no duplicate/lost events.
- `WindowModeTest`: runtime rotation, PiP enter/exit/auto-enter/actions, split-screen, free-form resize, rapid resize, focus loss.
- `SecondDisplayTest`: display add/remove/change, Presentation/fallback, same View restoration.
- `DeepLinkJourneyTest`: cold and `onNewIntent`, confirmation/cancel, malformed links, audio URI grants, voice search.
- `ExportPipelineTest`: short real export, output validation, rotation/background/recreate, cancellation/failure.
- `CastJourneyTest`: no-route/locked UI on emulator; real receiver on physical hardware.

### Physical-device lane

Remain user-controlled:

- Real Cast receiver/network changes.
- HDMI/USB-C/wireless display and removal.
- Foldable posture/rotation.
- Real GPU/SurfaceFlinger screenshots and gesture edges.
- Long export/thermal stability.
- Bluetooth/USB audio, Android Auto/TV.
- Final visual golden review.

## Migration sequence

1. Add host/probe/checked-shell harness.
2. Add GameActivity/native packaging smoke.
3. Keep deterministic audio fixtures; add `awaitStageReady`.
4. Replace direct font/rotation mutation with capture-and-restore rules.
5. Replace concrete `TourRule`; remove explicit `MainActivity` restart; compute window geometry; await first frame.
6. Convert touch/layout helpers to correct display/window coordinates and native input acknowledgement.
7. Migrate app launch, shell tabs, player, visuals, paywall, and export reachability tests.
8. Make critical UiTour steps assertive or separate audit/artifact mode.
9. Move host-independent Compose tests to JVM.
10. Migrate store/thumbnail fixtures and validate artifacts.
11. Add lifecycle/input/window/second-display/deep-link/export/Cast tests.
12. Add emulator CI lane and physical-device matrix.

## Assertions

- Exactly one GameActivity visual surface and one StageThread game/render loop.
- Compose is a sibling above it.
- The direct GameActivity callback table is installed; no glue thread or `android_main` exists.
- Native window state received before Stage attach replays once through the direct host.
- `onNativeWindowDestroyed` and `onNativeWindowRedrawNeeded` return only after StageThread acknowledgement.
- Stale Activity/window generations cannot clear the new surface.
- Recreation retains Stage/StageThread identity and replaces only the native host/controller/surface.
- PiP continues rendering while Activity is paused and stops on stop.
- Visible split-screen keeps rendering; hidden/alpha-zero does not.
- Presentation moves the same View and restores it on every exit path.
- Touch/key/D-pad/IME reach exactly one owner.
- Predictive Back uses the Compose/AndroidX dispatcher.
- Export produces a decodable file with expected duration/orientation.
- Cast teardown does not race Recomposer cancellation.

## CI

- Existing build/lint workflow remains the fast PR gate.
- Add a connected instrumented emulator job for host/lifecycle/input/window/deep-link/short-export tests.
- Keep long UiTour/store capture opt-in.
- Upload screenshots, recordings, logs, and device metadata as artifacts.
- Do not treat permissive artifact generators as regression gates without assertions.

## References

- `kb://android/guide/components/activities/testing`
- `kb://android/training/testing/different-screens/tools`
- `kb://android/training/testing/other-components/ui-automator`
- `kb://android/develop/ui/compose/testing/interoperability`
- `kb://android/develop/adaptive-apps/guides/support-multi-window-mode`
- `kb://android/develop/ui/views/picture-in-picture`
- `kb://android/training/testing/local-tests/robolectric`
