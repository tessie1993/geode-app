# Geode: app review and native Kotlin repair plan

Date: 2026-09-25. Status: **planning only; no app implementation changes**.

## 1. Product requirements

- A high-quality local music player with a responsive, music-driven visualizer.
- Native Kotlin Android UI. No WebView, WebGL, JavaScript bridge, browser engine, CDN, or downloaded UI code in the shipping UI.
- Preserve the supplied Opaline design system: dimensional forms, visible shoulders and sidewalls, coherent lighting, local contact deformation, and stable readable content. A collection of flat gradient cards is not an equivalent conversion.
- Fully offline playback, library browsing, presets, visual assets, and rendering. Optional Android system pickers may expose third-party providers; core functionality must work with local files in airplane mode.
- Retain the existing C++ audio/analysis/visualizer engine where it is appropriate. Native OpenGL ES is not WebGL. The UI port should be Kotlin-owned, with native GPU shader programs where necessary; this does not mean rewriting the audio engine in Kotlin.
- Plan and agree the implementation stages before changing application code.

## 2. Sources, scope, and confidence

The application review is anchored to local commit `46d1ce75b018e515051e1ddd72f932c17cb30bac`. The new design-system import is merged PR [#83](https://github.com/tessie1993/geode-app/pull/83), head `adf28629565a7803d2adb2d23b3a9964f0c4487a`, under `ui-system/Opaline-3D-Library/`. This task's checkout predates the import; its exact files were inspected through the existing Git ref, without switching or merging branches.

PR #83 adds 1,694 files and 763,370 text lines. Of those added text lines, 667,226 are under `vendor/`. Do not interpret the total as 763,370 lines of authored UI that must be translated into Kotlin. The PR description reports 1,693 original archive files and 191,020,071 bytes, plus the repository integration file. The design library is source/reference material, not an Android runtime.

Read reference contracts: `docs/START-HERE.md`, `docs/COMPONENT-ANATOMY.md`, `docs/ASSEMBLY-API.md`, `docs/IMPLEMENTATION-STATUS.md`, `catalogue/design-tokens.json`, `catalogue/instantiate-composition.js`, and `src/motion.js`. Also inspected the current Android Opaline adapter, Compose components/routes, visualizer adapter/JNI owner, playback integration, native mixer/output/player, manifest and test workflow.

Graphify was used first for architecture, symbol location, renderer bindings, native-player structure, source-library motion symbols, and test links. Its first graph matched the local application commit. During review its promoted index advanced to `5a364df`, which includes the imported library. Broad queries then returned many vendored Three.js symbols; file-scoped queries and exact source reads were used to avoid mistaking dependency code for application architecture. The application findings below were checked against the local files. Graphify reported no linked tests for `VisualizerRenderer`; this is a graph result, not proof that no indirect test exists.

This is a focused deep static review of the highest-risk paths, not an assertion that every source file or binary asset was audited. No Android build, instrumentation, audio capture, GPU profiling, or visual comparison was completed. A read-only device listing failed because adb could not initialize its default Android directory in this environment. The local third-party submodule directories are unpopulated and this checkout has no `local.properties`. These are validation limitations, not evidence that the application build fails. Current remote CI results were not assessed. Open PR #84 should be compared before implementing overlapping fixes.

## 3. Findings that drive the repair order

Priorities: P1 = release-blocking reliability/product requirement; P2 = material correctness/usability defect. “Static” means the mechanism is present in reviewed code; exact device symptoms still need reproduction.

### F1 — P1: current UI renderer conflicts with the required platform

`app/src/main/java/dev/geode/ui/opaline/OpalineScene.kt:260` constructs a WebView, enables JavaScript, loads the bundled HTML scene, and sends JSON/evaluateJavascript updates. `OpalineApp.kt:156` installs it behind the app. Bundling it locally already permits offline operation, but does not make it Kotlin-native. Removing a network dependency alone cannot satisfy the requirement.

**Repair:** replace the bridge with a Kotlin scene owner and native drawing backend, keeping Compose responsible for routes, text, actions, focus, accessibility and editable inputs. Remove browser assets from the APK after native parity passes. Preserve the original `ui-system` source as reference with attribution; do not ship its development viewer or entire vendor tree.

### F2 — P1: renderer destruction loses persistent visual configuration

`engine/scenes/.../VisualizerView.kt:64` releases scenes on detach. `VisualizerRenderer.kt:111` implements that release as destruction of the whole native handle. The same Kotlin view/renderer is remembered by `OpalineApp`, and later attached again. `onSurfaceCreated` recreates the handle and invalidates parameter/LFO/ADSR caches, but does not replay the selected MilkDrop preset or custom shader. Those arrive through the long-lived `EnginePlumbing.kt:117` effect and one-shot `vizApply` events. Overlay/background effects are keyed by their values, not by a new renderer generation.

**Trigger:** select a custom shader or MilkDrop preset, open immersive, leave it and reopen without changing the selection. The new native renderer can lose configuration while the UI continues to display the selected state. Underlay/overlay replay must also be checked, especially when the recreated surface has the same size.

**Repair:** immutable complete `RenderSessionState` plus explicit surface generation; replay assets, shaders, presets, layers, modulation, overlay and underlay on every recreation. Separate GL-resource loss from terminal session disposal. Regression: repeat open/close, background, rotation, PiP and display transfer and assert the selected output survives.

### F3 — P1 risk: JNI handle lifetime is not protected for all callers

`engine/scenes/.../bridge/NativeViz.kt:53` synchronizes destruction; most methods read the volatile handle and enter JNI without the same lifetime guard. Several configuration methods are called from Main while GL teardown can destroy the native object. A volatile pointer value provides visibility, not an ownership lease. Native `stateLock_` protects renderer state only while the renderer still exists.

**Repair:** one serialized owner for create/use/destroy, or a correctly scoped handle lease that covers each full JNI call. Close admission before teardown, drain admitted work, then destroy. Do not add blocking work to audio callbacks. Stress-test updates racing detach under native memory diagnostics. This is a static race risk, not an observed crash stack.

### F4 — P1: native queue identity follows mutable indices

`app/src/main/java/dev/geode/playback/NativePlayer.kt:52` maps load tokens to integer indices. `queueNext():351` treats an unchanged index as an unchanged queued track. Queue removal/reordering can change the item at that index; the preloaded native deck and UI queue can disagree. `syncFromEngine():292` then resolves the transition using the old position.

**Example:** A is playing, B is preloaded at index 1; remove B so C moves to index 1. The early return can retain B's native deck while the queue shows C.

**Repair:** token-to-stable-entry-UID mapping, generation-stamped requests, and identity-based preloading. Re-resolve the index from the UID when applying native events. Test add/remove/move/shuffle/repeat while the next track is already buffered.

### F5 — P1: disabling gapless also disables ordinary automatic advance

`NativePlayer.kt:343` selects no next item when gapless is false and crossfade is zero. `syncFromEngine` has no end-of-track advancement path; `getState` exposes `ENGINE_ENDED` directly. The reviewed app paths do not provide an alternative advancing listener.

**Repair:** separate transition style from playlist progression. At end-of-stream, ordinary playback must load the next eligible item and obey repeat/shuffle even without pre-roll. Test two short tracks with both enhancements off, including repeat-one and repeat-all.

### F6 — P1: native engine lacks the ExoPlayer audio-focus integration

`PlaybackEngine.kt:94` enables automatic audio-focus handling for ExoPlayer; the alternative `NativePlayer` at line 105 does not use that implementation. No explicit `requestAudioFocus`/focus-loss handling was found in the reviewed app source. Setting Oboe usage/content type does not implement Android focus policy.

**Repair:** Kotlin focus coordinator for the native backend, including granted/denied/delayed focus, transient loss, ducking, permanent loss, and safe resumption. Audit route-disconnect/noisy handling for both backends; no explicit noisy-event handler was found. Test incoming calls, another player, assistant interruption, wired/Bluetooth disconnect, and return to playback.

### F7 — P2: foreground visualizer has no touch routing

`OpalineCreativeScreens.kt:198` mounts the renderer but attaches no gesture-to-renderer adapter. `VisualizerView` also has no touch override. Calls to `submitTouchPoints` exist in the wallpaper path, but none were found in the foreground UI; `queueTouchStroke` has no foreground caller.

**Repair:** native pointer adapter with surface-local coordinates, distinct pointer IDs, pressure/velocity where available, cancellation and detach cleanup. Preserve transport controls' ownership. Test single/multi-touch and cancellations; compare wallpaper and foreground behavior.

### F8 — P2: Visuals workspace lacks its specified live preview

`CreativeVisualsHome`, `OpalineCreativeScreens.kt:265`, displays an icon/current scene and gallery. It does not mount a live preview. Customize/Palette/Modulation similarly provide forms without persistent preview. This prevents immediate visual feedback while tuning the product's core feature.

**Repair:** an explicitly owned preview viewport beside/before the inspector, with one state source shared with fullscreen. On phones keep preview and selected controls visible; on larger windows use two panes. Do not move one View between multiple parents without an ownership protocol.

### F9 — P2: custom control visibility depends on the removed backend

`ui/opaline/creative/CreativeControls.kt:145` makes slider thumb and both tracks transparent; switch colors are also transparent at lines 166–171. The fallback in `opalinePart` draws a rounded body, not a value-bearing thumb or switch state.

**Repair:** native visible representations of value, selection, disabled and focused state must always exist. A rendering fallback must preserve control meaning. Add screenshot and semantics tests for min/mid/max, selected/unselected, focused and disabled states, including reduced motion and backend failure.

### F10 — P2: immersive controls lack safe-inset layout

`OpalineCreativeScreens.kt:233,243` places close and transport rows using fixed 16/24 dp padding without system-bar/display-cutout inset handling. Edge-to-edge is enabled in the Activity.

**Repair:** let artwork/visualization extend edge-to-edge; inset interactive chrome with safe drawing bounds. Check portrait, landscape, gesture/three-button navigation, display cutouts, PiP and 200% system text.

### F11 — P2: crossfade consumes incoming samples before their audible interval

`core/audio/player/Mixer.cpp:99–105` enters crossfade when a callback crosses the fade boundary, then pulls the entire incoming callback. Samples preceding the fade start have incoming gain clamped to zero but have already been consumed. The incoming beginning can be shortened by the prefix of one render block.

**Repair:** split at the exact fade boundary; consume incoming frames only for the overlapping span. Define underflow behavior explicitly so failed pre-roll does not silently fade toward silence. Test with impulse/ramp inputs, varying callback sizes and fade boundaries inside a callback.

### F12 — P1 validation gap: current checks do not establish product quality

The Android workflow compiles `androidTest` but does not execute it. The navigation smoke test covers section/search/back transitions, not rendering, audio continuity or lifecycle restoration. JS scene tests exercise the current browser adapter and cannot certify a native replacement. The reviewed CMake configuration/source inventory did not establish a runnable native audio regression suite in this task.

**Repair:** add executed device tests and focused native audio/renderer tests as part of each implementation stage. Build success, catalog count, screenshots of the reference viewer and Graphify coverage are different evidence; none alone establishes working Android behavior.

## 4. Native conversion architecture

Recommended implementation: Kotlin/Compose owns all UI, layout, state and accessibility; a Kotlin-owned native OpenGL ES 3 rendering layer draws dimensional Opaline shells beneath stable Compose content. Use the existing GLES device baseline. This satisfies the no-browser requirement without pretending that Compose gradients implement 3D transmission. Shader programs remain GPU shader source, loaded locally; they are not JavaScript.

Keep the UI material scene and music visualizer separate in ownership and state. They can share low-level graphics utilities and a frame-budget coordinator after those contracts are proven. Do not merge their mutable scene worlds or let decorative UI physics block audio processing.

Proposed logical boundaries (module extraction only where it earns its cost):

| Boundary | Responsibilities |
|---|---|
| Kotlin design model | Typed element/composition IDs, tokens, themes, material roles, content anchors, component state and motion policy |
| Compose components | Accessible buttons, sliders, switches, text fields, list rows, dock/rail, sheets, focus, IME and native navigation |
| Kotlin scene owner | Measured layout snapshots, coordinate transforms, clipping, component identity, lifecycle, asset residency and render commands |
| Native GLES UI backend | Meshes, camera, lighting, shadows, material passes and bounded local deformation, invoked from Kotlin |
| Visualizer session | Complete replayable visual state, foreground/external-display/PiP ownership and input adapter |
| Playback service | MediaSession, transport/queue identity, focus, foreground lifetime and backend policy |
| Existing C++ engine | Decoder/mixer/DSP, analysis and music scene rendering; deterministic tested interfaces |

### Source-to-Kotlin port map

| Supplied library | Native destination and fidelity requirement |
|---|---|
| `catalogue/design-tokens.json` | Typed Kotlin tokens; retain Tidal/Opal/Moss/Obsidian/Aurora/Amber and material roles; define metres-to-layout mapping explicitly, not literal metres-to-dp conversion |
| `catalogue/compositions.json` and `instantiate-composition.js` | Kotlin recipe parser/model with transforms, owner-local content anchors and semantic bindings; validate duplicate IDs/unknown owners |
| `assets/models/*.glb`, model manifest, `src/geometry.js` | Reuse selected authored mesh data through a validated build-time conversion or bounded runtime loader; preserve child-part identities, normals, bounds and material assignments |
| `src/materials.js`, `src/shaders/`, optics contracts | Port material equations and uniforms to native shaders; retain distinct gel/water/film/pearl/mineral behavior; GLB alone does not contain custom shaders |
| `src/motion.js` | Kotlin contact state and fixed-step spring/deformation rules; preserve value, pointer ownership, release and cancel semantics |
| `src/physics/` | Port only necessary, profiled numeric modules; use reusable primitive arrays; preserve tests/invariants; do not run every catalog solver for every list row |
| `src/transitions.js` / transition contracts | Native route/scene choreography with stable text and reduced-motion settling |
| `behaviorBindings` | Explicit Compose actions connected to the existing Navigator, repositories and player; never execute arbitrary imported code |
| `vendor/three`, HTML viewer and tooling | Reference/development only; no mechanical Kotlin translation of the browser/Three.js engine |

Library facts: 247 catalog entries, 157 authored physical element entries, 76 compositions, six themes. Its own status document distinguishes geometry, preview deformation, numerical solvers, and unresolved physical effects. Carry those distinctions into a port coverage matrix. Do not promise every scientific simulation or custom optical effect merely because a GLB exists.

## 5. Implementation stages and exit gates

### Stage A — establish a reproducible baseline and capture failures

1. Pin the application commit plus PR #83 import; compare PR #84 for overlapping changes.
2. Record the currently failing journeys with a local music fixture library: start/play/pause/seek, queue mutation, presets, open/close visualizer, background/restore and external display.
3. Run build, lint, Kotlin unit checks and existing scene tests on the exact baseline. Record actual failures without blanket baseline expansion.
4. Build the source-design inventory and coverage matrix: used by app, native port status, semantic status, visual proof, runtime cost.

**Exit:** reproducible failures and exact source/version references, plus a small agreed visual reference set. No mass UI deletion.

### Stage B — stabilize music and visualizer ownership

Fix F2–F7 and F11 through small isolated changes, starting with renderer lifecycle, queue identity and focus. Retain current user data and settings. Review Kotlin thread confinement, callback dispatch, cancellation and background I/O; audit C++ RAII, FD ownership, callback allocation/locking, command acknowledgments and shutdown paths. Treat `@Volatile` as visibility, not lifecycle safety.

**Exit:** regression tests for the specific defects; repeated lifecycle loops preserve shader/preset/overlays; queue edits play the intended item; focus interruptions behave correctly; crossfade sample boundaries are verified.

### Stage C — native design-system vertical slice

Implement one diagnostic Compose screen using the supplied A22 play puck, B07 seek assembly, C02 track row, C20 media ring and D03 dock, all locally packaged. Preserve actual geometry, shared camera/light rig and distinct material roles. Keep text and actions stable and native. Add accessible slider value control and keyboard activation alongside pointer deformation. Native rendering failure must reveal a fully usable Compose fallback.

Start with raster material approximations whose limitations are documented. Full CPU photon caustics and high-resolution volumetric simulation are not a prerequisite for a reliable music UI. Prototype any such effect separately under measured GPU/CPU budgets; do not run the reference photon tracer on Main.

**Exit:** on-device side-by-side reference comparisons, correct native semantics, no WebView in the native slice, no network access, no hidden continuous rendering, acceptable frame/memory results. This stage is the fidelity decision point before migrating the entire app.

### Stage D — migrate the full app, starting with listening

1. Listen: artwork/media ring, metadata, seek, transport, favorite, queue, lyrics and sleep timer.
2. Library: permissions/SAF import, fast virtualized tracks/albums/artists/folders/playlists, search, empty/error/loading states.
3. Visuals: live preview plus inspector, scene gallery, presets, layers, background, palette/modulation and shader error reporting; fullscreen with touch and safe controls.
4. Settings: every setting demonstrably changes behavior, persistent across process recreation; theme/text/motion/accessibility controls included.
5. Studio/export: preserve projects and persistence; integrate preview/timeline/inspector after playback and visualizer are stable. Cancellation/output failures must be clear and must not corrupt saved projects.

**Exit:** route and feature parity checklist, including accessibility paths and error states; no unreachable controls; phone, landscape and expanded-window layouts reviewed. Route by route replacement rather than another full UI teardown.

### Stage E — remove the browser integration and enforce offline packaging

Remove `OpalineBridge`, WebView creation, JS evaluation/probing and shipped HTML/Three.js runtime assets. Keep the original library in `ui-system` as source reference, outside runtime asset source sets. Replace adapter-specific CI checks with native port checks while retaining useful source-library invariant tests as reference tests. Inspect merged release manifest, dependency graph and APK contents for browser runtime and network permissions; add an explicit regression gate for the offline requirement.

**Exit:** a clean install with networking disabled supports import of local fixtures, browsing, playback, customization, preview and export. Neither a browser engine nor an online resource is required for core journeys.

### Stage F — release qualification

Execute the matrix below, fix regressions, and collect trace/screenshot/audio evidence for the exact release candidate. Update README and design status documents: the current README says the app has no UI although this checkout has Opaline screens, and integration notes still describe the browser adapter as the intended direction.

**Exit:** all release gates pass, known limits are documented, and the release can be reproduced from its commit. No claim of flagship quality based only on an assembled APK.

## 6. Validation and performance criteria

| Area | Required evidence |
|---|---|
| Android/Kotlin | Compile/lint/detekt/format; unit tests of state/cancellation; executed navigation, restoration and semantics tests; no disk/decode work in UI event handlers |
| UI fidelity | Device captures of selected reference compositions in idle/press/drag/release, all themes, and fallback; readable content remains stable while shells deform |
| Accessibility/layout | TalkBack, keyboard/D-pad, minimum 48 dp action targets, visible focus, large font, RTL, IME, safe insets and reduced motion |
| Renderer | Repeated attach/detach/background/context-loss tests; shader/preset replay; no stale handle use; touch cancellation; PiP/display migration |
| Audio | Deterministic samples for gapless/crossfade/seek/DSP; queue edits during preload; focus and route changes; malformed/truncated and mixed-rate media; screen-off playback |
| Native diagnostics | Focused host tests where feasible, Android native sanitizer builds where supported, ownership assertions and callback timing/underrun counters |
| Offline | Merged APK/manifest inspection and airplane-mode end-to-end test using exclusively local files |
| Export | Cancellation, storage errors, audio/video duration and synchronization, context teardown, valid output playback |

Initial targets to validate on named devices, not performance claims: 60 Hz UI interaction within a 16.7 ms frame budget on the chosen reference device; explicit 30/60 Hz visualizer quality tiers; no intentional audio callback blocking/allocation; no audio underruns in the defined reference playback test; no sustained memory growth across 100 lifecycle cycles; no decorative rendering when not visible. Record thermal status, device/GPU, resolution and scene complexity with every result. Use percentile frame times and traces rather than averages alone.

Test across the supported API floor (26), a pre-AGSL device, API 33+, and the current target platform; include at least one Adreno and one Mali device, compact/landscape/expanded windows, Bluetooth and wired routes. Emulator tests supplement hardware evidence; they cannot certify audio latency or mobile GPU behavior.

## 7. Standards used to shape the plan

- [Android Compose accessibility defaults](https://developer.android.com/develop/ui/compose/accessibility/api-defaults): semantic actions, control meaning and touch-target sizing.
- [AndroidView lifecycle/reuse contract](https://developer.android.com/reference/kotlin/androidx/compose/ui/viewinterop/AndroidView.composable): explicit view ownership and disposal, rather than assuming remembered views automatically replay renderer state.
- [Android audio latency guidance](https://source.android.com/docs/core/audio/latency/app): short callback work without unbounded blocking or priority inversion.
- Opaline PR #83's anatomy and assembly documents: dimensional geometry, separate stable content, explicit host semantics, local contact ownership, and honest separation of mesh assets from shader/solver behavior.

## Recommended first implementation scope

After plan approval: baseline/reproductions, renderer-state and playback regression fixes, then the five-component native Kotlin vertical slice. Do not start by translating 763,370 lines, stripping the whole UI, or replacing the reference design with generic flat controls. Expand the port only after the slice proves fidelity, usability and device performance.
