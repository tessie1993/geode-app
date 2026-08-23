# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Geode — a native Android music player and real-time GPU music visualizer. Kotlin + Jetpack
Compose UI, OpenGL ES 3.0 rendering, a thin C/JNI layer for libprojectM (MilkDrop). Fully
on-device: no network permission, nothing captured or rendered leaves the phone.

Three JDK settings that are easy to conflate, and only the last reaches a device: the Gradle
daemon runs on JDK 21 (`gradle/gradle-daemon-jvm.properties`, so it ignores `JAVA_HOME`), the
compile toolchain is JDK 21 (`geode.kotlin-common`), and the bytecode target is 17. Do not "fix"
a mismatch between them — the split is deliberate. `compileSdk`/`targetSdk` 36, `minSdk` 26,
application id `dev.geode`. `local.properties`
must point at an Android SDK; on a machine without one, `tools/setup-android-sdk.sh` installs the
packages and writes `local.properties` (it needs `dl.google.com` and `maven.google.com` reachable).

## Commands

```bash
./gradlew assembleDebug             # debug APK (installs alongside release — .debug suffix)
./gradlew installDebug
./gradlew :app:testDebugUnitTest    # app unit tests
./gradlew test                      # unit tests in every module
./gradlew :app:lintDebug            # Android lint (abortOnError = true, HardcodedText is fatal)
./gradlew ktlintCheck               # official Kotlin style; ktlintFormat to fix
./gradlew :app:detekt               # static analysis, per-module baselines
./gradlew checkAll                  # `check` in every module — the full local gate
./gradlew :app:connectedDebugAndroidTest   # instrumented, needs a device/emulator (API 29+)
```

Run a single test: `./gradlew :app:testDebugUnitTest --tests "dev.geode.ui.PresetLinkTest"`.

CI (`.github/workflows/android.yml`) runs, in order: jniLibs checksum verify → `:app:detekt` →
`assembleDebug` → `testDebugUnitTest` → `lintDebug` → `ktlintCheck` → `checkThirdPartyNotices`,
plus a separate emulator job. `ShaderSyntaxTest` needs `glslang-tools` installed to actually
compile shaders; without it that check silently skips.

## Module graph

Six modules; the dependency direction is one-way and enforced by the build files.

```
:app  ──▶ :engine:runtime ──▶ :engine:scenes ──▶ :engine:gl
                          └─▶ :engine:audio-android ──▶ :engine:audio-core (pure JVM)
```

- `:engine:audio-core` — JVM-only DSP: FFT/spectrum, log bands, onset/beat/tempo/key,
  `SampleRing`/`RingReader` (the lock-free PCM ring), feature ring. No Android types; this is the
  module where analysis logic can be unit-tested headlessly.
- `:engine:audio-android` — the media3 PCM tap (`PcmTap`) and the sink clock.
- `:engine:gl` — GL ES capability probing, tier/format policy, compute support. No scenes.
- `:engine:scenes` — the renderer and everything visual: `render/scene` (fragment-shader and
  particle scenes), `render/fluid` (Fluid/CurlFlow/Water), `render/compute`, composite/FX,
  LFO/ADSR, `VisualSafety`, plus `analysis/` (offline analyzer, timeline, cache) and the GLSL in
  `src/main/res/raw/*.glsl`.
- `:engine:runtime` — the composition root: `EngineComposition` owns `EngineLifetime`s
  (`PROCESS`, `PLAYBACK_SESSION`, `VISUAL_SESSION`, `GL_CONTEXT`, `OUTPUT`, `EXPORT`) and closes
  them in reverse order.
- `:app` — package `dev.geode`: `audio/` (ExoPlayer wiring, ring buffer, mic and other-app
  playback capture), `playback/`, `export/` + `editor/` + `publish/` (Export Studio),
  `data/` (JSON-on-disk stores), `di/` (Hilt), `ui/` (Compose), `wallpaper/`, `billing/`.

Build conventions live in `build-logic` (an included build): `geode.kotlin-common` (ktlint,
detekt, JVM 17, provenance), `geode.android-library`, `geode.jvm-library`, `geode.provenance`.
Add a module by applying one of these, not by copying an existing `build.gradle.kts` block.

## Data flow

PCM tap (`TeeAudioProcessor` → `PcmTapSink` → `PcmRingBuffer`) → analysis → `AudioBus`
(a process-wide `object` holding the latest `AudioFeatures`, stale after 1.5 s, with a consumer
count that drives capture on/off) → every consumer: live scenes, the wallpaper service, the
offline exporter. Mic input and Android 10+ playback capture feed the *same* ring buffer, so a
scene never knows which source it is reacting to. `docs/AUDIO_CHAIN.md` documents the media3
processor order and why the chain is owned rather than configured — read it before touching
`audio/dsp/MvzAudioProcessorChain.kt`.

Export is deterministic: scenes are re-rendered frame-exact from the analysis timeline, so the
same track must produce the same frames. Anything wall-clock- or randomness-driven in a scene
breaks export reproducibility.

## Gates that are easy to trip

- **Provenance** (`checkEngineProvenance`, on `check` in every module). A source file adapted
  from an external project must carry both `// SPDX-License-Identifier: …` and
  `// Origin: <url>@<commit>`, the URL must be a registered source in
  `docs/visualizer-v2/provenance.json` at that pinned commit and in an adoptable tier
  (ADAPT/RETAIN), and no STUDY/EXCLUDE repository may be named anywhere in shipped source.
  `docs/visualizer-v2/SOURCE_ARCHIVE.md` is the prose; where the two disagree, the registry wins.
- **Third-party notices.** `THIRD_PARTY_NOTICES` at the root and
  `app/src/main/assets/third_party_notices.txt` must be byte-identical; adding a dependency means
  editing the root file then `./gradlew :app:syncThirdPartyNotices`.
- **Native libs.** `app/src/main/jniLibs/<abi>/*.so` are committed blobs built out-of-band by
  `.github/workflows/native-libs.yml` (libprojectM v4 + the JNI bridge from `tools/milkdrop_jni.c`).
  CI verifies each ABI's `SHA256SUMS`, and `checkNativePageAlignment` fails a release build whose
  `.so` is not 16 KB page aligned. Never hand-edit or hand-replace these; rerun that workflow.
- **Detekt baselines.** Each module has its own `detekt-baseline.xml` covering what existed when
  it came under the tool. Thresholds in `config/detekt/detekt.yml` sit just above the worst
  measured value — lower a threshold in the same commit that shrinks its worst offender rather
  than raising it. A baseline entry is keyed on the full signature string, so adding a parameter
  to a baselined function un-baselines it and the old finding returns as "new".
- **`checkAll` is broader than CI.** CI runs `:app:detekt` and `:app:lintDebug` — `:app` only —
  so engine-module detekt and lint findings can accumulate behind a green CI. Run `checkAll`
  before believing the engine is clean.
- **Surface-gate tests.** Some tests read main source files as *text* (e.g. regenerating
  `docs/PARAM_MATRIX.md` from the sources and failing until the committed copy matches). Renaming
  an identifier or moving a file can fail one. Before editing a main source file, grep the test
  directories for its filename and for the identifiers you are changing.
- **`docs/PARAM_MATRIX.md` is generated.** Do not edit it by hand.

## Conventions

- ktlint with `android = true`, official Kotlin style; `@Composable` functions are exempt from
  function-naming rules (`.editorconfig`).
- Hilt for injection, but a manual `GeodeContainer` (built in `GeodeApp`) still owns
  process-scoped state; `di/DataModule` bridges the container into the Hilt graph.
- Comments in this codebase explain *why* a non-obvious choice was made, often citing the bug or
  CI run that forced it. Match that when you touch such code — and read those comments before
  "simplifying" the thing they defend.
- The engine modules and `:app` both use `dev.geode.*` package names but are separate Gradle
  modules; `dev.geode.render.*` and `dev.geode.analysis.*` live in `:engine:scenes`, not `:app`.

## Note on the README

`README.md` predates the module split: it describes a single `:app` module with package
`dev.musicviz` and a large unit-test suite under `app/src/test/java/dev/musicviz/`. The actual
tree is the six-module layout above under `dev.geode`, and only a handful of test files survived
the flattening (`app/src/test`, `app/src/androidTest`, `engine/audio-core/src/test`). Its feature
descriptions, build commands and doc index are still accurate; its architecture table and test
paths are not. Several docs (`docs/DEVICE_CHECKS.md`, `docs/AUDIO_CHAIN.md`) likewise cite
`dev.musicviz` paths — translate them to `dev.geode` and the owning module.
