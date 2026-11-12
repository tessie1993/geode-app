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
- [`docs/agents/entry-points.md`](docs/agents/entry-points.md): the manifest's activities, services, receivers and the intents the app takes.
- [`docs/agents/data-flow.md`](docs/agents/data-flow.md): sound in, render, export, persistence, by thread.
- [`docs/agents/navigation.md`](docs/agents/navigation.md): screens, back order, gestures, the tour.
- [`docs/agents/pro.md`](docs/agents/pro.md): the unlock, the free tier, every gate.
- [`docs/agents/build-ci.md`](docs/agents/build-ci.md): CI workflows, ktlint, versions, release, agent tooling.
- [`docs/agents/tests.md`](docs/agents/tests.md): every test file and where tests go.
- [`docs/agents/large-files.md`](docs/agents/large-files.md): Kotlin files to split, with line counts.

## Owner's standing rules

- No bias on BPM, ever: tempo never steers or weights a visual choice (details under Conventions; `tools/no-bpm-bias-gate.sh` fails the PR).
- No colour narrowing: the palette, saturation, brightness, temperature and contrast are the style's and the user's (`core/viz/Director.hpp`, `core/viz/NoteResponse.hpp`).
- No hardcoded reactions: what moves on a kick, a note or a drop is the reaction matrix alone, which the user can see and edit.
- Smallest fix possible.
- Always ask why: before deleting a caller or a file, understand what it did, how its absence breaks things and why.
- If the build breaks because a file was cut, do not put the file back; edit the test (or caller) so it no longer asks for it, after understanding what it asked for and why.
- No building, compiling, linting or testing in an agent container: CI tests; prechecks are done by reading.
- Clean room for ideas from other projects: take no code, names or strings from them, and the repo names no such project.
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
- That glue is Activity and Service lifecycle, Media3 session and player wiring, permissions, the MediaStore and MediaCodec calls, billing IPC, and the widget, tile, wallpaper and dream shells.
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
  - `core/output`: export and Record planning and timing (`FrameClock`, `PreRoll`, `Recorder`, `RecordPlan`)
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

- `GeodeApp` (`@HiltAndroidApp`) installs the crash handler; its lazy `GeodeContainer` sweeps leftover render scratch and pending MediaStore rows.
- `ui.MainActivity` is the only `@AndroidEntryPoint`: the phone and Android TV launcher, PiP of Visuals, and every intent the app takes (presets, templates, `.milk` files, audio files, play-from-search).
- Services: `playback.PlaybackService` (Media3 library service, Android Auto), `audio.PlaybackCaptureService`, `audio.GeodeNotificationListener`, `export.ExportService`, `cast.CastService`, `tile.PlaybackTileService`, `wallpaper.VisualizerWallpaperService`, `wallpaper.VisualizerDreamService`.
- Also declared: `ui.wallpaper.WallpaperOptionsActivity`, `tile.TileLongPressActivity`, `widget.NowPlayingWidgetReceiver`, Media3's `MediaTransferReceiver`, `FileProvider` `${applicationId}.presets`.
- Every entry point, intent and service type: [entry-points.md](docs/agents/entry-points.md).

## Data flow, navigation, Pro

- Sound: source → one native ring (`SampleRing`) → each renderer's own `core/sound` engine on its GL thread. No analysis thread. See [data-flow.md](docs/agents/data-flow.md).
- Navigation: `GeodeDestination` enum and `when` at the top level; only the Visuals panel runs on Navigation 3. See [navigation.md](docs/agents/navigation.md).
- Pro: one-time unlock; every gate asks a `FeatureGate` from `GetFeatureGateUseCase`; the free tier is `pro/FreeTier.kt` alone. See [pro.md](docs/agents/pro.md).

## DI

- Hilt: `GeodeApp`, `MainActivity` and eight `@HiltViewModel`s, resolved through `geodeViewModel()` in `ui/GeodeViewModelFactory.kt`.
- `DataModule` provides `GeodePrefsFiles`, `@GeneralPrefs SharedPreferences` and `AppStateRepository` (the tour's), all taken from `GeodeContainer`, plus `@IoDispatcher`.
- `BackupRepository` is constructor-injected.
- `PlayerSessionProvider` (`@Singleton`) builds one `PlayerSession` by hand.
- That session builds its repositories and controllers itself, and the ViewModels get repositories from it: `session.playbackRepository`, `session.visualizerRepository`; the settings repositories are the container's (`SettingsViewModel` reads them through `session.container`).
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
- No bias on BPM, ever (owner's rule; `tools/no-bpm-bias-gate.sh` fails the PR): the tempo comes from the music alone; nothing prefers, weights or centres on a tempo.
- A tempo not yet heard is unknown (`bpm` 0, no beats, clocks run in seconds), never a stand-in number. Do not copy a beat tracker's prior or a library's start tempo from the literature.
- No 120 as a tempo anywhere, tests included: a test's stimulus plays other tempos (77, 93, 141, 174).
- Android TV is the same APK: no `uses-feature` may be required (touchscreen, faketouch, microphone, leanback are `required="false"`), checked by `TelevisionManifestTest` and `tools/check-manifest.sh`; how a TV differs is in [navigation.md](docs/agents/navigation.md).
- Cast (to a TV or a computer) is the app's only internet use (`INTERNET`, `ACCESS_NETWORK_STATE`); the one admitted networking library is libdatachannel with Mbed TLS, for Show on a computer (owner 2026-11-12).
- CI fails if INTERNET appears without `CastService`, if a socket or HTTP API is used outside `cast/` or a socket or WebRTC header outside `core/cast/` (`tools/network-gate.sh`), if a networking, analytics or ads SDK joins the release build or a networking submodule joins `third_party/`, or if AD_ID appears.
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
- The same script holds the prefs files to one owner (#1009): `data/GeodePrefsFiles` names every file, `GeodeContainer` builds it once, everything else reads `context.geodeContainer.prefsFiles`; only `data/` and the container call `getSharedPreferences` or build a `GeodePrefsFiles`.
- **Engine Kotlin only shrinks (enforced).** `tools/engine-kotlin-gate.sh` fails a PR whose net Kotlin lines grow under `engine/*/src/main/`, `export/` or `audio/dsp/`, or in the Spec 3.0 areas (#461).
- The Spec 3.0 areas: Record (`ui/RecordController`, `record/`; the deleted `ui/TakeController` and `data/PerformanceTake` stay listed only until #1035 lands) and the timeline (`editor/{Timeline,Keyframes,AnimatableParams}`), the Scene model (`scene/`), glTF and pack import (`gltf/`, `*Gltf*`, `*Glb*`, `*Pack*Import*`) and the render job (`ui/ExportController`); the script header lists them.
- A UI edit in a watched file lands if it removes as much as it adds; only `GeodeNative.kt` and `NativeViz.kt`, the JNI declarations, are exempt. Write the logic in `core/` and call it through `geode_api.h`.
- **Data layer lives in `ui`.** Stores and MediaStore access in the `*Controller`s sit there (`VizStateStore`, `AutoVisualsPrefsStore` and `BuiltInPresets` now live in `data`).
- `MyVideosTab` (thumbnails), `TrackInfoEditor`, `DuplicatesTab`, `FolderSettings` and `LoopRenderSheet` go through `data/MediaStoreSource` (`context.geodeContainer.mediaStore`).
- **One-shot events.** `PlayerViewModel` exposes `vizApply` and `morphFade` as `SharedFlow`.
- **Engine logic in Kotlin (ADR 0003).** Engine internals belong in `core/`; GLOSSARY › Code has the words.
- Native already runs the touch field, the pinch and twist (`core/viz/TouchTransform`, #654), the smear (`core/viz/TouchSmear`), Customize's randomizer, reset and category map, the flash clamp, the cymatics plate, the export resampler and the BS.1770 loudness meter (their Kotlin copies are gone).
- Native also runs the export's track audio and AIFF reads (`core/audio/ingest/TrackFeed`, `AiffReader`; `TrackAudio` only drives MediaCodec), the loop render's drift stops and the export loudness advice (`core/output`), the built-in palettes' hues and the artwork palette (`core/viz/ArtPalette`).
- The playout clock is native (`core/sound/interp/PlayoutClock`; the dead Kotlin clock is gone) and wired: the sink feeds it, the live feed reads it.
- The PCM ring is native (`core/audio/ingest`, read on the GL threads until `core/sound/hop` exists).
- Still Kotlin: the template JSON codec (`data/TemplateFormat`), the scene and style catalogue, and in export the render ETA (`RenderEta`), the range clamp (`ExportRange`) and the Media3 AIFF extractor's header parse (`audio/AiffExtractor`); the ADR's table gives each one's target and blocker.
- **Mixed module.** `:engine:scenes` is an Android library, but most of its files are pure Kotlin.
- `editor/*` and much of `data/*` are pure too, except for `org.json` (`TemplateFormat`, `EditorProjectJson`). There is no `kotlinx.serialization` yet.
- **Navigation.** Enum and `when` at the top level; only the Visuals panel runs on Nav3 so far.
- **Quality items not set up:** two use cases so far (`pro/GetFeatureGateUseCase`, `render/scene/BuildStyleReactionsUseCase`), almost no ViewModel or repository tests, a hand-written (not generated) baseline profile (`app/src/main/baseline-prof.txt`), no `reportFullyDrawn`, JankStats, Macrobenchmark or GWP-ASan.

## Gates

- `.github/workflows/arch-map.yml` runs `tools/layer-gate.sh` (the layer rule above), `tools/engine-kotlin-gate.sh`, `tools/no-bpm-bias-gate.sh` (the tempo rule under Conventions), `tools/network-gate.sh` (the internet rule under Conventions) and `tools/actions-pin-gate.sh` on every PR; see [build-ci.md](docs/agents/build-ci.md).
- There is no AGENTS.md check; keep this map current by hand.

## Keeping this map current

- **One fact per line.** Every bullet here and in `docs/agents/` holds one fact. Add a fact as a new line in its group; never grow a line into a paragraph or a table cell.
- **Why:** when one line holds many facts, every PR edits it and every PR conflicts with every other. Separate lines let two PRs add different facts without touching the same line.
- **Where detail goes:** AGENTS.md stays a short map under 200 lines. Put detail in the matching `docs/agents/*.md` file; add a new file there (and a link above) for a new area.
- **When you refactor,** update the module, package, entry point, thread and large-file lines in the same PR. Then update the "breaks the guide" list.
- When a split step from ADR 0001 lands, rewrite the Modules section and `docs/agents/modules.md` to match, and record any change of plan as a new ADR.
- Verify every path you name.
