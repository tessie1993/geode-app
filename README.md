# Geode — Android Music Visualizer

Native Android music player and real-time GPU music visualizer, written in
Kotlin (Jetpack Compose UI, OpenGL ES 3.0 rendering, a thin C/JNI layer for
libprojectM). Everything runs on-device: the app holds no network permission,
and nothing it hears or renders leaves the phone.

Currently v1.7.0 (versionCode 31); minSdk 26, targetSdk 36. The full version
history is in [CHANGELOG.md](CHANGELOG.md).

## Features

- **Player** — MediaStore library plus SAF folder roots and imports, editable
  queue, favourites, playlists, multi-term search; loudness-curve seek bar,
  timed lyrics (`.lrc`), A-B repeat, fades, sleep timer, equalizer, playback
  speed/pitch, skip silence; listening time is measured per track and per day,
  and Home's shelves are built from that real history.
- **Visual scenes** — particle scenes; fragment-shader scenes with an in-app
  GLSL editor; the GPU fluid family (Fluid / Curl Flow / Water); Cymatics
  (the standing-wave field of what is playing); MilkDrop via libprojectM 4
  (arm64-v8a — rebuild notes in `tools/build-projectm.md`). The Customize
  panel exposes every parameter with per-param locks, one randomizer, LFO and
  ADSR modulation, a palette maker, presets (JSON + `.milk`), and
  photosensitivity clamps (Settings > Visual safety).
- **Export Studio** — deterministic offline export: scenes re-rendered
  frame-exact from the analysis timeline into H.264 with the original audio
  muxed alongside, then trim / grade / speed / reframe / caption editing on
  Media3's Transformer (a trim-only edit is a lossless container rewrite).
  Exports apply the same FX, modulation and safety clamps as the live view.
- **Live wallpaper** — the visualizer as a home-screen wallpaper
  (`wallpaper/VisualizerWallpaperService`), with an idle drive so it keeps
  moving without audio.
- **Other apps' audio** — Android 10+ playback capture feeds the same PCM
  ring buffer the player's tap and the microphone already share, so the FFT,
  every scene, the exporter and the wallpaper work unchanged on foreign
  audio. Runs as a foreground service with an ongoing notification; nothing
  captured is written to disk or sent anywhere. Apps that opt out of capture
  (e.g. Spotify) are diagnosed explicitly and the microphone is offered
  instead.

## Build & test

```bash
./gradlew assembleDebug           # build the debug APK
./gradlew :app:testDebugUnitTest  # headless JUnit suite
./gradlew lintDebug               # Android lint
./gradlew ktlintCheck             # style (CI-enforced, official Kotlin style)
./gradlew installDebug            # install on a connected device
```

Requires JDK 17+ and Android SDK 36; `local.properties` must point at your
SDK.

On a machine with no SDK — a fresh container, a cloud session —
`tools/setup-android-sdk.sh` installs the packages `compileSdk` asks for and
writes `local.properties` for you. Every package comes from `dl.google.com`
(which `maven.google.com` redirects to), so a restricted network must allow
both hosts; without them Gradle cannot resolve the Android plugin either, and
the script says so rather than failing mid-download.

## Architecture

One Gradle module (`:app`), package `dev.musicviz`. Dependency flow is
one-way: `ui` depends on the engine packages, never the reverse. Source lives
under `app/src/main/java/dev/musicviz/`:

| Package | What it holds |
|---|---|
| `audio/` | ExoPlayer wiring and the PCM tap (`TeeAudioProcessor` → lock-free mono ring buffer), AIFF reading, mic input, other-apps playback capture |
| `analysis/` | FFT → log-spaced bands, beat/BPM/key/section extraction, offline analyzer and the binary analysis cache |
| `render/` | GL ES 3.0 renderer, the scene implementations (`render/scene`, `render/fluid`), composite/FX pipeline, LFO/ADSR engines, visual-safety clamps; shaders in `res/raw/*.glsl` |
| `playback/` | Playback service, queue operations, sleep timer |
| `export/` | Deterministic video export and the Export Studio editor |
| `data/` | JSON-on-disk stores: presets, palettes, favourites, history, prefs |
| `ui/` | Compose app shell, player, library, customize surfaces, settings |
| `wallpaper/` | Live-wallpaper service |

## Tests

The unit suite lives in `app/src/test/java/dev/musicviz/` (`*Test.kt`,
headless JUnit plus some Robolectric). Beyond ordinary unit tests, several
suites are *surface gates* that read main source files as text — for example
`CustomizeSurfaceTest` regenerates `docs/PARAM_MATRIX.md` from the sources
and fails until the committed copy matches. Renaming an identifier or moving
a file can fail such a gate: before editing a main source file, grep
`app/src/test` for its filename and for the identifiers you are changing, and
update the gating test in the same change.

Checks that cannot run headless (GL behavior, capture, wallpaper) are listed
in [docs/DEVICE_CHECKS.md](docs/DEVICE_CHECKS.md) — note it is a partial
reconstruction; see the note at its top.

## Documentation

- [CHANGELOG.md](CHANGELOG.md) — full version history, newest first.
- [docs/PARAM_MATRIX.md](docs/PARAM_MATRIX.md) — param × scene-family matrix,
  generated by the test suite (do not edit by hand).
- [docs/VISUAL_STYLE_RESEARCH.md](docs/VISUAL_STYLE_RESEARCH.md) — design
  rationale, open-source references and the style catalogue for the composite
  visual families.
- [docs/quality/QUALITY_BAR.md](docs/quality/QUALITY_BAR.md) — the quality
  bar this project is held to.
- [docs/DEVICE_CHECKS.md](docs/DEVICE_CHECKS.md) — numbered on-device
  checklist (reconstructed).
- [docs/wireframe.html](docs/wireframe.html) — full-app wireframe.
- `THIRD_PARTY_NOTICES` — licenses and attributions (libprojectM is LGPL-2.1
  and dynamically linked; the in-app notices asset mirrors this file).
