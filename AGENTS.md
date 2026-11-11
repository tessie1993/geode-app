# AGENTS.md: BeatRealms 3D (internal name Geode)

Android music player, reactive visualizer and video studio. Kotlin and Compose with Hilt, plus a C++20 native core (`libgeode.so`).

- The product name is BeatRealms 3D ("BeatRealms" in running text, "BeatRealms Pro" for the unlock).
- applicationId `com.beatrealms3d.app` (debug adds `.debug`); product id `beatrealms3d_pro`.
- The old name MuzViz Studio Pro survives only in names the owner sets by hand or old data needs: the `MUZVIZ_UPLOAD_*` secrets, `muzviz.*` Gradle properties, the backup format id, the old `Movies/MuzViz` folder.
- Kotlin packages stay `dev.geode.*`.
- minSdk 33 (Android 13+), targetSdk 36, compileSdk 37.

This file is a map. Detail lives in `docs/agents/`, one fact per line:

- [`docs/agents/modules.md`](docs/agents/modules.md): Gradle modules and what each holds.
- [`docs/agents/native-core.md`](docs/agents/native-core.md): `core/`, the per-frame chain, safety, shaders, endless zoom.
- [`docs/agents/app-packages.md`](docs/agents/app-packages.md): every `:app` package.
- [`docs/agents/data-flow.md`](docs/agents/data-flow.md): sound in, render, export, persistence, by thread.
- [`docs/agents/navigation.md`](docs/agents/navigation.md): screens, back order, gestures, the tour.
- [`docs/agents/pro.md`](docs/agents/pro.md): the unlock, the free tier, every gate.
- [`docs/agents/build-ci.md`](docs/agents/build-ci.md): CI workflows, ktlint, versions, release, agent tooling.
- [`docs/agents/tests.md`](docs/agents/tests.md): every test file and where tests go.
- [`docs/agents/large-files.md`](docs/agents/large-files.md): Kotlin files to split, with line counts.

## Owner's standing rules

- No bias on BPM, ever: tempo never steers or weights a visual choice.
- No colour narrowing: the palette, saturation, brightness, temperature and contrast are the style's and the user's (`core/viz/Director.hpp`, `core/viz/NoteResponse.hpp`).
- No hardcoded reactions: what moves on a kick, a note or a drop is the reaction matrix alone, which the user can see and edit.
- Smallest fix possible.
- Always ask why: before deleting a caller or a file, understand what it did, how its absence breaks things and why.
- If the build breaks because a file was cut, do not put the file back; edit the test (or caller) so it no longer asks for it.
- Dead code may be deleted once verified unreferenced.
- Macros: 4 base knobs (`core/viz/MacroBank`: Glow, Heat, Bend, Flow), global, editable and saveable (`MacroStore`).
- Record is Pro (`ProFeature.STUDIO`); screenshots are free.
- No flash limiter on output: safety limits params only (`core/viz/VisualSafety`).
- Goal: Play Store ready.

## Owner's hard rule: Kotlin is the shell, C++ is the app (read first)

- **Kotlin only for the main-thread shell**: Compose screens, ViewModels, Android services and platform APIs, stores, and the JNI declarations in `GeodeNative`.
- Kotlin draws what native says and hands values across. It decides nothing about a parameter, a sound or a picture.
- **Everything inside is C++ (`core/`), never Kotlin**: all logic off the main thread, all parameter code (rolls, resets, which control is where, modulation, LFOs, envelopes, reactions), the audio engine and DSP, shaders and the renderer, every engine internal.
- That includes "small" helpers and tables: if it computes or decides, it is C++, with a host test in `core/tests/`.
- **Why:** one implementation for live, wallpaper, dream and export (no Kotlin copy to drift), testable on the host without a device, and the GL and audio threads never wait on the JVM.
- ADR `0003` is the plan. Any Kotlin engine code still left is debt to port, never a pattern to copy.
- If you are about to write engine logic in Kotlin, stop and put it in `core/` instead.

## Read before you change structure

- **Target rules: the official Android architecture guidance** (<https://developer.android.com/topic/architecture>; `android docs search "<keywords>"` fetches a page).
- UI layer: one `StateFlow<UiState>` per ViewModel, one-shot events as state consumed once.
- Data layer: repositories and data sources that are main-safe (suspend, injected dispatcher) and own every `ContentResolver`, prefs and file call.
- Hilt owns the graph. Navigation 3 back stacks. Modules split leaf first (`:core:model`, `:core:designsystem`, `:core:data`).
- Where the code disagrees, the code is the debt; issues #888–#899 are the phased plan (7 splits `PlayerSession`, 8 moves data out of `ui/`, 9 is Navigation 3 and adaptive, 10 the module split).
- The ADRs are decisions; their files are no longer in the tree, `git log -- docs/adr` has them.
- ADR `0001`: the target module split and the Nav3 plan.
- ADR `0002`: the Play release before steps 2–6.
- ADR `0003`: every engine internal in C++, Kotlin only as the shell, with the port order.
- ADR `0005`: wave 1 rebuilds Studio, Visuals and Customize before the release (overrides 0002's order) and sets the Studio project model.
- ADR `0006` (proposed): the fosfora-shaped target: a hop chain on the sample clock, one feature and one param schema, a binding bus with base vs modulation, one frame graph for every surface, one typed command channel.
- ADR `0007`: drops video editing from Studio.
- ADR `0008`: frame pacing, heat and rest are one native rule (fps goes first on a hot phone).
- [`GLOSSARY.md`](GLOSSARY.md): domain words. Name things with them.
- The playback audio path: the tap is first, before any DSP, and the chain never outputs float. The write-up is `git show '8b4b5fa6^:docs/AUDIO_CHAIN.md'`; read it before touching that path.

## Where code lives (owner rule, no exceptions)

- **Kotlin runs on the main thread only:** Compose UI, ViewModels' UI state, and the thin Android glue that has to be Kotlin.
- That glue is Activity and Service lifecycle, Media3 session and player wiring, permissions, the MediaStore, MediaCodec and Transformer calls, billing IPC, and the widget, tile, wallpaper and dream shells.
- **Everything else is C++ in `core/`, behind `core/api/geode_api.h`, never Kotlin.** That means:
  - every worker, GL, audio and export thread
  - all logic and decisions
  - all audio code: DSP, the PCM ring, clocks, analysis
  - params: definitions, ranges, defaults, randomizing, clamping, modulation, routing
  - the catalogue, scheduling, export maths and timing
  - shaders, which `core/viz` drives
  - the engine's inner workings
- **Where:**
  - `core/sound`: hearing, analysis and the musical layer
  - `core/viz`: the renderer, scenes, params, Director, NoteResponse, Reactions, CameraRig, Catalogue and safety
  - `core/audio/dsp`: playback and export DSP
  - `core/audio/ingest`: the PCM ring every source writes and every sound engine reads (`PcmRing`, `Cursor`)
  - `core/studio`: the programme clock
  - `core/signal`: the scene params resolver (`Resolver`), the per-frame chain from requested to drawn params, and the engine settings it reads (`EngineSettings`: `SettingsLatch` hands it the process globals live, or an export's copy pinned once at prepare)
  - `app/src/main/cpp/*_jni.cpp`: only marshals values
- **Why:**
  - One engine serves live, export, wallpaper and dream, so they can't drift apart.
  - It is deterministic and testable on the host in `core/tests`.
  - There are no GC pauses or chatty JNI calls on real-time threads.
  - There is one source of truth instead of a Kotlin copy and a C++ copy.
  - See ADR 0003 and ADR 0006.
- **When a change touches logic that is still in Kotlin,** put the new logic in C++ and leave Kotlin as a thin caller. Never add logic to Kotlin. The leftovers are listed under "Where the code breaks the architecture guide".
- **Never build, compile or lint in an agent container,** and never run Gradle, the APK build or the emulator. Push and let GitHub CI test it.

## Modules

```
:app ──► :engine:scenes ──api──► :engine:audio-core (JVM, pure Kotlin)
   └───► :engine:audio-android ──api──┘
:app ──externalNativeBuild──► CMakeLists.txt ──► libgeode.so + libprojectM-4.so
```

- `:engine:audio-core`: `GeodeNative` (every JNI `external fun`), the PCM ring's handle, the sound frame layout, user sound tuning.
- `:engine:audio-android`: the Media3 PCM tap (`PcmTap`).
- `:engine:scenes`: the GL adapter, the look snapshot, scene ids and params, the shell's reads of the native schema, catalogue and modulation table.
- `:app`: everything else: UI, playback service, stores, editor model, export, wallpaper, widget.
- Detail: [modules.md](docs/agents/modules.md), [native-core.md](docs/agents/native-core.md), [app-packages.md](docs/agents/app-packages.md).

## Entry points (`app/src/main/AndroidManifest.xml`)

- `GeodeApp`: `@HiltAndroidApp`. It installs the crash handler, and its lazy `GeodeContainer` sweeps leftover render scratch and pending MediaStore rows.
- `ui.MainActivity`: the only `@AndroidEntryPoint`, the launcher, and PiP of Visuals; the only other activities are `ui.wallpaper.WallpaperOptionsActivity` and `tile.TileLongPressActivity`.
- `ui.MainActivity` handles `geode://preset`, `geode://template` and `MEDIA_PLAY_FROM_SEARCH`, then calls `AppRoot()` in `ui/AppShell.kt`.
- `playback.PlaybackService`: exported Media3 library service, also used by Android Auto. Media3's `MediaButtonReceiver` handles resumption.
- `audio.PlaybackCaptureService`: MediaProjection foreground service.
- `audio.GeodeNotificationListener` (in `NowPlayingBridge.kt`): lets the app name what another app is playing.
- `export.ExportService`: keeps the process in the foreground during a render. It does not own the render; `ExportRun.scope` does.
- `wallpaper.VisualizerWallpaperService` and `wallpaper.VisualizerDreamService`.
- `ui.wallpaper.WallpaperOptionsActivity`: the wallpaper picker's Settings button (`android:settingsActivity` in `res/xml/wallpaper.xml`), a plain `ComponentActivity` outside Hilt.
- `widget.NowPlayingWidgetReceiver` (in `NowPlayingWidget.kt`).
- `cast.CastService`: foreground service that keeps a cast streaming in the background; started by `CastController` when a Cast session starts.
- `cast.CastService` is typed `mediaPlayback`, plus `microphone` while Live input is on, taken while the app is visible so the mic keeps driving the TV in the background (`CaptureController.onBackground` keeps it then).
- Media3's `MediaTransferReceiver` (declared) lets the system output switcher start a cast.
- `tile.PlaybackTileService`: the Quick Settings tile (`BIND_QUICK_SETTINGS_TILE`); `tile.TileLongPressActivity` beside it answers the tile's long press by opening the app.
- `FileProvider` `${applicationId}.presets`, for sharing presets.

## Data flow, navigation, Pro

- Sound: source → one native ring (`SampleRing`) → each renderer's own `core/sound` engine on its GL thread. No analysis thread. See [data-flow.md](docs/agents/data-flow.md).
- Navigation: `GeodeDestination` enum and `when` at the top level; only the Visuals panel runs on Navigation 3. See [navigation.md](docs/agents/navigation.md).
- Pro: one-time unlock; every gate asks a `FeatureGate` from `GetFeatureGateUseCase`; the free tier is `pro/FreeTier.kt` alone. See [pro.md](docs/agents/pro.md).

## DI

- Hilt: `GeodeApp`, `MainActivity` and eight `@HiltViewModel`s, resolved through `geodeViewModel()` in `ui/GeodeViewModelFactory.kt`.
- `DataModule` provides `GeodePrefsFiles`, `@GeneralPrefs SharedPreferences` and `UserDataRepository`, all taken from `GeodeContainer`, plus `@IoDispatcher`.
- `BackupRepository` is constructor-injected.
- `PlayerSessionProvider` (`@Singleton`) builds one `PlayerSession` by hand.
- That session builds its repositories and controllers itself, and the ViewModels get repositories from it: `session.playbackRepository`, `session.visualizerRepository`, `session.userDataRepository`.
- Services, the wallpaper and the widget don't inject. They use `PlaybackEngine.acquireForService`, `AudioBus`, `context.geodeContainer` or construct stores directly.

## Build, test, CI

- **Don't build locally.** Push a branch and open a PR; `build-lint.yml`, `native-host-tests.yml` and `arch-map.yml` gate it.
- **Before you push Kotlin, run `tools/ktlint-changed.sh`**: CI's `ktlintCheck` without Gradle, in seconds.
- Every workflow, version and release step: [build-ci.md](docs/agents/build-ci.md). Every test: [tests.md](docs/agents/tests.md).

## Conventions

- detekt is baselined per module (`*/detekt-baseline.xml`, config `config/detekt/detekt.yml`); ktlint has no baseline. New code must pass both clean.
- KDoc explains *why*, at length. Match the density of the file you're in.
- Use `bestEffort(TAG, "what")` (`dev.geode.util`) for best-effort failures. Failures go to `RingLog`.
- Scratch files use the prefixes `geode_aac_`, `geode_loop_` and `studio-`; `GeodeContainer` sweeps them.
- Renders insert with `IS_PENDING` into `Movies/BeatRealms` as `beatrealms_*.mp4`.
- `Movies/MuzViz` from before the rename is still listed and swept, never moved.
- The tap stays first in the audio chain, and the chain never gets float output.
- Cast to TV is the app's only internet use (`INTERNET`, `ACCESS_NETWORK_STATE`).
- CI fails if INTERNET appears without `CastService`, if a socket or HTTP API is used outside `cast/`, if a networking, analytics or ads SDK joins the release build, or if AD_ID appears.
- Everything else is offline; Play Billing works over IPC to the Play Store app.
- Runtime permissions are asked for in context through `ui/PermissionRequest.kt` (`rememberPermissionRequest`: rationale first, retry after a refusal, app Settings once Android stops asking).
- First run is the safety notice alone (`SafetyConsent` on `OnboardingScaffold`; `FirstRunGate` passes straight through).
- Music access is asked the first time Library opens.

## Large files to split

- The Kotlin files to split, with line counts, are in [large-files.md](docs/agents/large-files.md).

## Where the code breaks the architecture guide

- **God object.** Every ViewModel reads `PlayerSession`, which builds 15 controllers and its repositories by hand.
- That's a service locator (`GeodeContainer`, `context.geodeContainer`) sitting beside Hilt.
- **Layer rule (enforced).** Nothing outside `ui/` and `di/` may name `dev.geode.ui`; `tools/layer-gate.sh` fails the PR otherwise. Move a type down instead: `model` if it is pure, `data` if it touches storage.
- **Engine Kotlin only shrinks (enforced).** `tools/engine-kotlin-gate.sh` fails a PR whose net Kotlin lines grow under `engine/*/src/main/`, `export/` or `audio/dsp/`, or in the Spec 3.0 areas (#461).
- The Spec 3.0 areas: the take's capture and choices (`ui/TakeController`, `data/PerformanceTake`, `editor/{Timeline,Keyframes,AnimatableParams}`, a future `take/` or `record/`), the Scene model (`scene/`), glTF and pack import (`gltf/`, `*Gltf*`, `*Glb*`, `*Pack*Import*`) and the render job (`ui/ExportController`); the script header lists them.
- A UI edit in a watched file lands if it removes as much as it adds; only `GeodeNative.kt` and `NativeViz.kt`, the JNI declarations, are exempt. Write the logic in `core/` and call it through `geode_api.h`.
- **Data layer lives in `ui`.** Stores and MediaStore access in the `*Controller`s sit there (`VizStateStore`, `AutoVisualsPrefsStore` and `BuiltInPresets` now live in `data`).
- `MyVideosTab` (thumbnails), `TrackInfoEditor`, `DuplicatesTab`, `FolderSettings` and `LoopRenderSheet` go through `data/MediaStoreSource` (`context.geodeContainer.mediaStore`).
- **One-shot events.** `PlayerViewModel` exposes `vizApply` and `morphFade` as `SharedFlow`.
- **Engine logic in Kotlin (ADR 0003).** Engine internals belong in `core/`; GLOSSARY › Code has the words.
- Native already runs the touch field, the pinch and twist (`core/viz/TouchTransform`, #654), Customize's randomizer, reset and category map, the flash clamp, the cymatics plate, the export resampler and the BS.1770 loudness meter (their Kotlin copies are gone).
- The playout clock is native (`core/sound/interp/PlayoutClock`; the dead Kotlin clock is gone) and wired: the sink feeds it, the live feed reads it.
- The PCM ring is native (`core/audio/ingest`, read on the GL threads until `core/sound/hop` exists).
- Still Kotlin: the scene and style catalogue; the ADR's table gives each one's target and blocker.
- **Mixed module.** `:engine:scenes` is an Android library, but most of its files are pure Kotlin.
- `editor/*` and much of `data/*` are pure too, except for `org.json` (`TemplateFormat`, `EditorProjectJson`, `PerformanceTake`). There is no `kotlinx.serialization` yet.
- **Navigation.** Enum and `when` at the top level; only the Visuals panel runs on Nav3 so far.
- **Quality items not set up:** two use cases so far (`pro/GetFeatureGateUseCase`, `render/scene/BuildStyleReactionsUseCase`), almost no ViewModel or repository tests, a hand-written (not generated) baseline profile (`app/src/main/baseline-prof.txt`), no `reportFullyDrawn`, JankStats, Macrobenchmark or GWP-ASan.

## Keeping this map current

- **One fact per line.** Every bullet here and in `docs/agents/` holds one fact. Add a fact as a new line in its group; never grow a line into a paragraph or a table cell.
- **Why:** when one line holds many facts, every PR edits it and every PR conflicts with every other. Separate lines let two PRs add different facts without touching the same line.
- **Where detail goes:** AGENTS.md stays a short map under 200 lines. Put detail in the matching `docs/agents/*.md` file; add a new file there (and a link above) for a new area.
- **Gates.** `.github/workflows/arch-map.yml` runs `tools/layer-gate.sh` (the layer rule above) and `tools/engine-kotlin-gate.sh` on every PR. There is no AGENTS.md check; keep this map current by hand.
- **When you refactor,** update the module, package, entry point, thread and large-file lines in the same PR. Then update the "breaks the guide" list.
- When a split step from ADR 0001 lands, rewrite the Modules section and `docs/agents/modules.md` to match, and record any change of plan as a new ADR.
- Verify every path you name.
