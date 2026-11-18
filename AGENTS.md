# AGENTS.md: BeatRealms 3D (internal name Geode)

This file describes the repository. It holds no rules.
The owner's rules are in `CLAUDE.md`.
Every line here was true when it was written and may have gone stale: check the code before you rely on one, and correct the line when it is wrong.

## What it is

- Android music player, reactive visualizer and video studio.
- Kotlin and Compose with Hilt, plus a C++20 native core built as `libgeode.so`.
- The product name is BeatRealms 3D; the unlock is BeatRealms Pro.
- applicationId `com.beatrealms3d.app`; the debug build adds `.debug`.
- Kotlin packages are `dev.geode.*`.
- minSdk 33, targetSdk 36, compileSdk 37.

## Gradle modules (`settings.gradle.kts`)

- `:app`: the UI, services, stores, export and cast shells, and the JNI glue in `app/src/main/cpp`.
- `:engine:audio-core`: holds `GeodeNative.kt`, the JNI declarations.
- `:engine:scenes`: holds no main source.
- The Media3 PCM tap is `app/src/main/java/dev/geode/engine/audioandroid/PcmTap.kt`, in `:app`.

## Native core (`core/`)

- Directories: `api`, `audio`, `cast`, `library`, `output`, `scene`, `signal`, `sound`, `stage`, `studio`, `util`, `viz`, `tests`.
- `core/api/geode_api.h` is the C interface the JNI files call.
- `core/sound`: the audio analysis.
- `core/audio/ingest`: the PCM ring every source writes and every sound engine reads.
- `core/audio/dsp`: playback and export DSP.
- `core/signal`: the scene parameter resolver (`Resolver`).
- `core/viz`: the renderer, scenes, parameters, reactions, macros, safety.
- `core/output`: export and Record planning and timing.
- `core/stage`: the surface threads (`core/stage/android/StageThread.cpp`, `VsyncThread.cpp`).
- `core/tests`: host tests, built by `.github/workflows/native-host-tests.yml`.
- Shaders are in `app/src/main/assets/shaders`.
- The four base macro knobs are Shape, Motion, Colour and Depth (`core/viz/MacroBank.cpp`).
- Today the analysis and the parameter resolver run on the drawing thread (`core/viz/RendererFrame.cpp`).

## App packages (`app/src/main/java/dev/geode/`)

- `audio`, `billing`, `cast`, `data`, `di`, `editor`, `engine`, `export`, `model`, `playback`, `pro`, `render`, `stage`, `tile`, `ui`, `util`, `viz`, `wallpaper`, `widget`.

## Entry points (`app/src/main/AndroidManifest.xml`)

- Application: `GeodeApp`.
- Activities: `ui.MainActivity`, `ui.wallpaper.WallpaperOptionsActivity`, `tile.TileLongPressActivity`.
- Services: `playback.PlaybackService`, `audio.PlaybackCaptureService`, `audio.GeodeNotificationListener`, `export.ExportService`, `cast.CastService`, `tile.PlaybackTileService`, `wallpaper.VisualizerWallpaperService`, `wallpaper.VisualizerDreamService`.
- Receivers and providers: `widget.NowPlayingWidgetReceiver`, Media3's `MediaTransferReceiver`, a `FileProvider`.
- `ui/MainActivity.kt` is the only `@AndroidEntryPoint`; there are eight `@HiltViewModel` classes.

## CI (`.github/workflows/`)

- `build-lint.yml`, `native-host-tests.yml`, `arch-map.yml`, `shader-check.yml`, `ship-apk.yml`, `release.yml`, `render-thumbs.yml`, `store-assets.yml`, `ui-tour.yml`, `pages.yml`.
- `.github/actions/android-build-setup` is the setup every Gradle job uses: JDK 21, Gradle, SDK 36, NDK, CMake, optional ccache.
- `android.yml` runs only when started by hand: the emulator tests and the sound engine across frame rates on the test tracks.
- `arch-map.yml` runs the gates in `tools/`: `layer-gate.sh`, `engine-kotlin-gate.sh`, `jni-parity-gate.sh`, `no-bpm-bias-gate.sh`, `network-gate.sh`, `audio-kotlin-gate.sh`, `actions-pin-gate.sh`.
- `build-lint.yml` and `native-host-tests.yml` skip their heavy job on a pull request whose changed paths are all documents (`tools/ci-docs-only.sh`); `arch-map.yml` never skips.
- `tools/ktlint-changed.sh` runs CI's ktlint check on changed Kotlin without Gradle.
- A `ship-apk.yml` run on a commit that is no longer `main`'s tip when its tests finish publishes nothing; the tip's run ships.
- `ship-apk.yml` builds and publishes the debug APK; the `sideload` build type (release-optimised, not debuggable, debug key, id suffix `.debug`) exists in `app/build.gradle.kts` but is not published.

## Other documents

- `GLOSSARY.md`: the domain words.
- `docs/superpowers/specs/2026-11-17-pipeline-refactor-design.md`: the proposed pipeline refactor; its section 7 lists documents that disagree with the code.
- `docs/agents/`: older per-area notes. Several are known to be out of date (`modules.md`, `native-core.md`, `data-flow.md`, `large-files.md`, `engine-kotlin-audit.md`, `kotlin-logic-map.md`, `engine-deep-scan.md`); treat them as history, not as a map.

## Edit log

One line per change, newest last: date, what changed, where.
A line records what was done; it is not a rule.

- 2026-11-17: Added the pipeline refactor design (`docs/superpowers/specs/2026-11-17-pipeline-refactor-design.md`).
- 2026-11-17: Rewrote `AGENTS.md` as facts plus this log and moved the owner's rules to `CLAUDE.md`; removed every decision-record reference, the dead document links and the mentions of another project from comments and docs.
- 2026-11-17: `ship-apk.yml` ships an optimised APK from a new `sideload` build type instead of the debug build (`app/build.gradle.kts`, #1214).
- 2026-11-17: The `sideload` build uses fake billing so Pro can be unlocked in it; the guard is the flag plus a build type other than `release` (`billing/BillingBuild.kt`).
- 2026-11-17: `SoundEngine` keeps the last 128 analysis hops by input-rate sample position; `frameAt(samplePos)` reads one (`core/sound/SoundEngine.*`, `Rhythm::Clock`, `core/tests/sound_history_test.cpp`, #1212).
- 2026-11-17: CI speed: host tests compile each source once per flag set, in parallel, through `tools/hostcc/g++`, and check out only the four submodules they use (`native-host-tests.yml`); the native build in `ship-apk.yml` and `build-lint.yml` runs through ccache, kept by `actions/cache`.
- 2026-11-17: `ship-apk.yml` publishes the debug APK again (as before #1214); the optimised `sideload` build closed at launch and stays in `app/build.gradle.kts` unpublished until it boots.
- 2026-11-18: CI duplicates: `android.yml` keeps only the emulator tests and the real-music frame-rate check (its build, lint and unit-test job repeated `build-lint.yml`; its last job published a pre-release); the release build's banned-SDK check moved to `build-lint.yml`; `ship-apk.yml` lost its `workflow_call` trigger.
- 2026-11-18: `ship-apk.yml` skips the build and the release when a newer commit is on `main` by the time its tests pass (the step "Is this still main's newest commit"); `tools/release-number.sh` prints why the last tag push was refused.
- 2026-11-18: Pull requests that change only documents skip the build and the host tests through a `Changed paths` job (`tools/ci-docs-only.sh`, `build-lint.yml`, `native-host-tests.yml`, self-test in `arch-map.yml`); a pull request's `pages.yml` run cancels the one it replaces.
- 2026-11-18: Workflow comments, headers and the pre-release notes match what the workflows do: the debug APK's own application id in the release notes, no mention of jobs `android.yml` no longer has (`.github/workflows/*.yml`).
- 2026-11-18: One Android build setup for every Gradle job (`.github/actions/android-build-setup`, used by seven workflows); `build-lint.yml` and `ship-apk.yml` share one ccache (`ccache-debug-`); `tools/actions-pin-gate.sh` and Dependabot cover `.github/actions`; `ship-apk.yml` and `render-thumbs.yml` give write permission to the one job that writes.
- 2026-11-18: Basis Universal 073eb6a: `geode_basisu` compiles `encoder/3rdparty/basisu_wuffs.cpp` (its new Wuffs image loader); the host test's object count is 33 (`core/CMakeLists.txt`, `.github/workflows/native-host-tests.yml`).
