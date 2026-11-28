# GameActivity Host and Shader Boundary Rebuild — Specification

## Objective

Rebuild BeatRealms 3D’s first screen around GameActivity’s real native `SurfaceView`, while keeping the existing native `StageThread`, EGL context, GLES renderer, Swappy pacing, recording, Cast, export, PiP, multi-window, and second-display behavior. At the same time, establish a clean three-layer rendering boundary so UI-derived values are resolved outside GLSL and shaders contain only pixel math without redundant work.

This is an intentional architectural rebuild, not a compatibility shell. GameActivity must own the main visual surface; Compose must be a sibling UI overlay; there must never be two live renderer surfaces or two frame-pacing authorities.

## Product functionality baseline

BeatRealms 3D is an offline, no-account music player, real-time GPU music visualizer, and video studio. The migration must preserve the product, not merely the current Activity implementation:

- Local music playback: tracks, albums, artists, folders, playlists, search, queue, shuffle, repeat, sleep timer, ReplayGain, EQ, fades, lyrics, history, and favourites.
- Audio-reactive visuals: 333 advertised styles, 700+ bundled looks, audio feature extraction, Auto mode, customization, presets, textures, gestures, tilt, touch smear, liquid mode, still capture, and live recording.
- Video studio: loop renders, single/batch queue/playlist renders, 720p/1080p/4K, 24–60 FPS, H.264/HEVC, aspect-ratio templates, watermarking, cancellation, background execution, and saved output.
- Beyond the Activity: Google Cast, browser/WebRTC link, connected-display `Presentation`, PiP, home-screen widget, Quick Settings tile, playback service/MediaSession, Android Auto/TV entry points, deep links, audio-file intents, and voice search.
- Product shell: safety/photosensitivity notice, setup, tutorial, free/Pro entitlement, trial, paywall, themes, reduced motion, accessibility, and multi-pane/adaptive layouts.

The store listing advertises live wallpaper and screensaver output, but `origin/main` has no `WallpaperService`, `DreamService`, or manifest component for either. This rebuild preserves implemented behavior only; restoring wallpaper/dream requires a separate feature and surface-lifecycle design.

## Commands

Local Gradle and full suites are prohibited in this workspace. CI remains the build/test gate.

- `./gradlew :app:assembleDebug --stacktrace --no-daemon` — CI debug build.
- `./gradlew :app:testDebugUnitTest --continue --stacktrace --no-daemon` — CI JVM tests.
- `./gradlew :app:lintDebug --stacktrace --no-daemon` — CI Android lint.
- `./gradlew ktlintCheck --continue --stacktrace --no-daemon` — CI formatting.
- `./gradlew :app:detekt --continue --stacktrace --no-daemon` — CI static analysis.
- `bash tools/jni-parity-gate.sh` — local/CI JNI name parity check.
- `bash tools/check_shaders.py <glslc>` — CI shader compilation check.
- Instrumented journey/E2E tests — CI emulator/device gate after the suite is migrated.

## Project Structure

- `app/src/main/java/dev/geode/ui/GeodeGameActivity.kt` — target GameActivity subclass host, Compose overlay, PiP/intent/lifecycle integration; replaces the `MainActivity` name.
- `app/src/main/java/dev/geode/stage/StageSurfaceController.kt` — non-View layout/display/sensor/performance controller; `StageView.kt` may retain a compatibility alias.
- `app/src/main/java/dev/geode/stage/Stage.kt` — retained native stage handle and main-thread event bridge.
- `app/src/main/java/dev/geode/ui/AppShell.kt`, `Crystal.kt`, `ConnectedDisplays.kt`, `StageRun.kt`, `EnginePlumbing.kt` — Compose UI and surface placement/policy.
- `app/src/main/cpp/geode_game_activity.cpp` — direct `GameActivity_onCreate` plus `GameActivityCallbacks` host; owns native window lifecycle, with no `native_app_glue` or `android_main`.
- `core/stage/android/StageThread.cpp`, `SwappyHost.cpp`, `EglWindow.cpp` — the one native game/render loop and ownership of GL thread, EGL, Swappy, Choreographer, and ADPF.
- `core/viz/gl/Program.*`, `UniformCache.*`, `Renderer*`, `UiBlurPass.*`, `CompositePass.*` — typed uniform upload and pass selection.
- `core/viz/glsl/**` — clean GLSL includes and shaders; UI policy and redundant per-pixel work removed.
- `app/src/androidTest/**` — redesigned whole-app journey/E2E suite for the GameActivity host.

## Code Style

- Kotlin, Jetpack Compose, Hilt, Kotlin coroutines/Flow, Material 3.
- C++20 native engine; JNI name-based bridge; no second `JNI_OnLoad`.
- GameActivity 4.4.2 static Prefab library; AppCompat theme; early `libgeode.so` loading.
- Swappy remains the only live frame-pacing authority at fixed 30 FPS. No second sleep loop or Compose refresh-rate vote.
- No automatic FPS or quality reduction. ADPF CPU/GPU hints remain scheduling aids only.
- Performance Tuner, when added, is process-wide, manually ticked, and never used as a game-asset cache. Its upload path is a product-visible privacy/network change: the privacy policy/store description and the Cast/browser-only network gate must be updated narrowly for the Android Performance Parameters endpoint before release.
- GLSL is limited to pixel/shader math. UI/layout/policy calculations stay in Kotlin/C++ and cross as typed uniforms.

## Rendering Boundary

### 1. UI/controller layer

Responsible for:

- Compose layout, bounds, insets, sheet geometry, glass opacity, blur region, safe areas.
- User look, palette stack, quality, post-FX choices, screen rotation, display mode.
- Converting those values into stable renderer input structs/uniform payloads.

Must not:

- Duplicate palette/LUT evaluation per pixel.
- Embed UI policy in shader source.
- Recompute invariant values in fragment shaders.
- Upload unused feature data every frame.

### 2. Renderer/uniform layer

Responsible for:

- Resolving CPU-side values once per frame or on change.
- Typed uniform blocks/structs with explicit layouts.
- Uniform location caching and change detection.
- Enabling/disabling passes on the CPU so inactive features cost no fragment work.
- Keeping live, export, Cast, and still paths on the same look semantics.

### 3. GLSL layer

Responsible only for:

- Texture sampling, interpolation, procedural pixel math, lighting, compositing.
- Reading precomputed uniforms.
- Early-out/branch elimination for feature flags where appropriate.

Must not:

- Decide UI visibility or lifecycle.
- Reconstruct LUTs/palette curves already supplied as uniforms.
- Repeat invariant trigonometric/color-space/layout math per pixel when it can be precomputed.
- Carry dead legacy uniform paths after migration.

## Android migration guidance applied

- `GameActivity` is an `AppCompatActivity` with a built-in `SurfaceView`; the final theme must descend from `Theme.AppCompat` (`kb://android/games/agdk/game-activity/index`, `kb://android/games/agdk/game-activity/get-started`).
- A bare GameActivity cannot be the startup Activity. `GeodeGameActivity : GameActivity` is the required app-specific host (`kb://android/games/agdk/game-activity/migrate-native-activity`).
- The selected AGDK native path is the direct `GameActivity` struct/callback API. `GameActivity_onCreate` fills the framework-owned `GameActivityCallbacks`; callbacks run on the app main thread. `native_app_glue`/`android_main` are optional and are not linked because StageThread already is the single native game/render loop (`https://developer.android.com/reference/games/game-activity/struct/game-activity`, `https://developer.android.com/reference/games/game-activity/struct/game-activity-callbacks`).
- GameActivity 4.4.2 exposes protected `mSurfaceView` and `contentViewId`; the content root is `findViewById<FrameLayout>(contentViewId)`. Its default `onSetUpWindow()` uses RGBA_8888 and `SOFT_INPUT_ADJUST_RESIZE`, not RGB_565.
- A `ComposeView` can be added as a sibling View above the native surface. `ComponentActivity.setContent` replaces the content View and is therefore not the hosting mechanism for this rebuild (`kb://android/develop/ui/compose/migrate/interoperability-apis/compose-in-views`).
- Direct native window callbacks own `ANativeWindow` lifecycle. `onNativeWindowDestroyed` must synchronously stop StageThread/EGL use before returning; `onNativeWindowRedrawNeeded` waits for redraw completion. The Kotlin controller must not duplicate those callbacks.
- Direct touch/key/text event objects are callback-scoped. `android_input_buffer` belongs to the optional glue path and is not used by this direct-callback design.
- Audio remains media/music-focused. Existing Oboe exact-format and Medium sample-rate conversion behavior is preserved; AGDK game low-latency defaults are not applied without a separate audio-product decision.
- PiP pauses the Activity while content remains visible; rendering must not stop on raw `onPause`. Existing `StageRunEffect` semantics remain authoritative (`kb://android/develop/ui/views/picture-in-picture`).
- Multi-window can leave a visible Activity paused and delivers configuration changes for resize/orientation. The current manifest already handles the required size/orientation changes; native surface sizing must follow them (`kb://android/develop/adaptive-apps/guides/support-multi-window-mode`, `kb://android/guide/topics/resources/runtime-changes`).
- Android 16 ignores orientation/aspect/resizability restrictions on large screens; the rebuild must not rely on a fixed full-screen window (`kb://android/develop/adaptive-apps/guides/support-multi-window-mode`).
- A `Presentation` has its own display-specific Context and is canceled when its display is removed. Content must be reclaimed or recreated explicitly, and display metrics must come from the Presentation context (`https://developer.android.com/reference/android/app/Presentation`).
- Process death does not guarantee `onDestroy`; retained Stage state is in-memory only, while durable player/settings state remains in existing persistence (`kb://android/guide/components/activities/process-lifecycle`).
- Predictive back must use the AndroidX/Compose back dispatcher, not `KEYCODE_BACK` interception. GameActivity’s native back path must not preempt Compose navigation (`kb://android/guide/navigation/custom-back/predictive-back-gesture`, `kb://android/develop/ui/compose/system/predictive-back`).
- Shader optimization should minimize ALU/texture/branch work, keep uniform buffers small, use adequate precision, and move fragment-independent work out of fragment shaders; unnecessary passes and duplicate fetches are primary targets (`kb://android/agi/frame-trace/shader-performance`, `kb://android/games/optimize/optimization-tips`).

## GameActivity Host Contract

- `GeodeGameActivity` extends `GameActivity`; the `MainActivity` name is retired.
- GameActivity’s built-in `InputEnabledSurfaceView` is the single main visual surface.
- Compose is added as a sibling above that surface, not via replacing the activity content with `setContent`.
- The Compose overlay is transparent wherever the visual is visible; the Activity window/native surface supplies the black background. The existing opaque AppShell root background must not cover the SurfaceView.
- `libgeode.so` is loaded deterministically and idempotently before GameActivity native initialization.
- The direct native GameActivity host owns `ANativeWindow` create/resize/redraw/destroy and binds the retained Stage to it. `StageSurfaceController` owns only View/layout/display/sensor/performance concerns and never duplicates native window callbacks.
- The same surface view can be reparented into a `Presentation` for second-display output and reclaimed afterward.
- PiP, split-screen, fold, rotation, keyboard, and multi-window continue through existing configuration handling.
- `StageRunEffect` remains the draw-visibility policy; raw Activity pause/resume must not freeze PiP, second-display, or visible multi-window rendering.
- The direct callback host is a full AGDK integration: no `android_main`, `native_app_glue`, whole-window `takeSurface`, duplicate renderer, duplicate StageThread, or duplicate `JNI_OnLoad`.
- Back, touch, TV remote, IME, and Compose gestures have one explicit owner per context. Compose retains shell/UI gestures and semantics; direct native input callbacks return `false` unless a future native visual-input route is explicitly designed.
- Hilt injection and retained stage creation happen after GameActivity’s native initialization; the GameActivity native handle is not the retained stage handle.
- Native window destruction and redraw callbacks use bounded synchronous StageThread acknowledgements before returning, as required by the GameActivity callback contract.
- `GeodeGameActivity.onDestroy` releases controller/weak Stage ownership before `super.onDestroy`, because GameActivity's superclass path forces surface destruction and native termination.

## Pacing and Performance Contract

- Live visualizer targets fixed 30 FPS.
- Swappy is configured once per process and once per surface with a 30 FPS swap interval; auto swap interval and auto pipeline mode are disabled. It is the only simultaneous pacing authority; the documented Choreographer fallback is used only when Swappy is unavailable.
- `AChoreographer_registerRefreshRateCallback` tracks actual refresh changes; no per-frame refresh chasing.
- Fallback uses seamless-only frame-rate hints and the existing one-shot Choreographer vsync path.
- One frame budget remains derived in `core/stage/SwapPace.hpp`.
- ADPF work hints report CPU/GPU frame work and never change FPS or quality.
- Game Mode config opts out of FPS override and downscaling; the app does not advertise adaptive Game Mode quality/FPS behavior.
- Performance Tuner uses manual ticks, one process-wide controller, lifecycle reporting, and loading groups. It does not add per-frame JNI or cache game assets.
- Loading, shader compile/link/cache, render CPU/GPU, swap wait, and present interval telemetry are separated and measurable.

## Testing Strategy

### Current baseline and migration risks

- The instrumented suite contains 33 Kotlin files, 23 test classes, 10 helpers, and 65 `@Test` methods.
- Nine test classes launch `MainActivity` through `createAndroidComposeRule<MainActivity>()`; tour/store rules also use concrete `ActivityScenarioRule<MainActivity>` aliases.
- Current coverage is strong for Compose semantics, navigation, dialogs, controls, playback service behavior, and offscreen EGL smoke tests.
- Current coverage does not prove native surface creation, first-frame presentation, surface destruction, GameActivity input delivery, real Activity recreation with surface reattachment, runtime rotation, multi-window, second-display reparenting, full export output, or Cast lifecycle.
- PiP coverage is observational rather than assertive.
- `UiTour`, store-shot, reel, and thumbnail suites include artifact-generation/permissive-audit modes; these must not be treated as hard regression gates without assertions.
- The current PR workflow runs JVM tests but no connected instrumented suite, so a dedicated emulator/device CI lane is required.
- Wallpaper/dream has no runtime implementation or test surface at `origin/main` and remains out of scope.

### Host and surface

- Cold start links GameActivity and `libgeode.so`, installs the direct callback table, and creates exactly one renderer surface and one StageThread.
- Compose controls/sheets render above the visual and receive input.
- Rotation, resize, fold, keyboard, PiP, background/foreground, and Activity recreation preserve stage state.
- Second-display connect/disconnect reparents the same surface without a second renderer.
- `onNativeWindowDestroyed` waits for EGL detachment; `onNativeWindowRedrawNeeded` waits for redraw completion.
- No native surface callback is dropped or applied after destruction.

### Rendering and shaders

- Shader compile/link gate covers every source and include.
- CPU uniform payload tests cover palette/look/blur/quality resolution.
- Golden or measured checks verify inactive blur/post-FX/LUT paths do no extra passes.
- Pixel-output tests verify the uniform refactor preserves current visuals.
- Export/live parity tests ensure export uses the same resolved look semantics.

### Whole-app E2E

- Migrate all `MainActivity`/Compose rule assumptions to `GeodeGameActivity` and its direct native callback host.
- Cover first run, player/library, visuals/gestures, customize/presets, studio/export, settings, deep links, audio intents, voice search, PiP, rotation/multi-window, second display, Cast, recording, playback backgrounding, widgets/tiles, and process recreation where feasible.
- Prefer semantic/user-visible assertions; avoid Activity implementation details except in host-contract tests.
- CI runs the migrated instrumented suite on an emulator; physical-device validation remains user-controlled.

### Release

- JNI parity gate and packaged-symbol checks for `GameActivity_onCreate`, `Java_com_google_androidgamesdk_GameActivity_initializeNativeCode`, and one `JNI_OnLoad` per ABI.
- Minified sideload/release smoke coverage for GameActivity lifecycle, surface, input, and every native callback.
- R8 Configuration Analyzer report and narrow keep rules only.
- 16 KB native page alignment and no unintended extra shared library.

## Boundaries

### In scope

- GameActivity dependency, Prefab/CMake linkage, AppCompat theme, manifest metadata.
- Real GameActivity surface adoption and Compose sibling overlay.
- Direct GameActivity callback host, StageSurfaceController adaptation, and second-display reparenting.
- Fixed-30 Swappy/Choreographer/ADPF/Game Mode contract.
- Process-wide Performance Tuner and detailed loading/shader/render telemetry.
- UI/renderer/GLSL separation and redundant pixel-work removal.
- Whole-app E2E/instrumented suite redesign and CI wiring.
- Focused host, uniform, shader, lifecycle, and release tests.

### Out of scope

- Merging the PR without explicit user request.
- Local Gradle builds or full local test suites.
- Physical-device actions unless the user connects/authorizes the device.
- Automatic quality/FPS adaptation or thermal-driven scaling.
- A second native game loop, second renderer surface, or competing pacing authority.
- New wallpaper/dream runtime support; neither exists at `origin/main`.
- Performance Tuner upload validation without a Play-signed build, API key, package/SHA-1 restrictions, and Play Console access.
- Broad package-wide R8 keeps.

## Acceptance Criteria

1. First screen is a GameActivity with exactly one native visual surface and a Compose sibling overlay.
2. Existing `StageThread`, EGL, GLES, Swappy, recording, Cast, export, PiP, multi-window, and second-display behavior is preserved.
3. Live rendering is fixed 30 FPS with one pacing authority and one-frame budget.
4. UI-derived values are resolved outside GLSL; shaders read typed precomputed uniforms.
5. Redundant per-pixel LUT/palette/invariant computations and inactive-feature work are removed.
6. Performance Tuner and telemetry are process-wide, manual, and free of per-frame JNI/game-asset caching.
7. The entire journey/E2E suite is migrated and CI-gated.
8. JNI/R8/16 KB/release checks pass in CI.
9. No PR is merged without explicit user approval.
