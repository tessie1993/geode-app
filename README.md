# BeatRealms 3D (internal name Geode): Android music player, visualizer and video suite

Native Android music player, real-time GPU music visualizer and a small video
suite. Kotlin and Jetpack Compose on top; a C++20 engine built by CMake and
the NDK underneath (the sound engine, the GLES 3.x renderer, the DSP chain
and tag access). Everything runs on-device: the app's only internet use is
Cast (a TV, or a browser on the same Wi-Fi), and nothing it hears or renders
is sent to a server.

The version is set in `gradle.properties` (`geode.versionBase`,
`geode.versionCodeBase`); minSdk 33, targetSdk 36, compileSdk 37,
ABIs arm64-v8a and x86_64. The full version history is in
[CHANGELOG.md](CHANGELOG.md).

## Features

- **Player** — MediaStore library plus SAF folder roots and imports, editable
  queue, favourites, playlists and smart playlists (rules over title, artist,
  album, folder, length, age, play count and favourite), a duplicate finder,
  multi-term search; timed lyrics (`.lrc`), A-B repeat, fades, sleep timer, a
  ten-band equalizer, ReplayGain, playback speed/pitch, skip silence, resume
  position for tracks over twenty minutes; tag editing that can write back
  into the file. Gapless joins (ExoPlayer) and, on Android 14+, a
  bit-perfect USB output toggle. Media
  browsing for Bluetooth and Assistant (tracks, albums, artists, playlists,
  favourites, recently played; Android Auto is not declared) and a
  home-screen now-playing widget.
- **Visual scenes** — particle, simulation and fragment-shader scenes with an
  in-app editor for the fluid scenes' force and dye shaders; the GPU fluid
  family (Fluid looks over one solver, plus Curl Flow);
  Cymatics. Every
  scene is rendered by the native core; the Customize panel exposes every
  parameter with per-param locks, a randomizer, LFO and ADSR modulation, a
  palette maker, presets (JSON) and photosensitivity clamps.
- **Studio** — renders the visualizer over a song, a queue, a playlist or
  chosen tracks (one render, a loop render or a batch) and keeps the results in
  My videos; Record captures the live visual. There is no timeline editor. The
  export steps its own sound engine through the track at 1/fps
  (`core/output/ExportFrame`), so it reacts exactly like the screen. It encodes
  H.264 or HEVC, with H.264 as the fallback when a device has no HEVC encoder.
- **Live wallpaper** — the visualizer as a home-screen wallpaper, hearing the
  same audio as the app.
- **Other apps' audio** — Android 10+ playback capture feeds the same PCM ring
  buffer the player's tap and the microphone share, so every scene and the
  wallpaper work unchanged on foreign audio.

## Build

```bash
git submodule update --init --recursive   # TagLib and the other third_party libraries
./gradlew assembleDebug                   # debug APK, native core included
./gradlew installDebug                    # install on a connected device
./gradlew ktlintCheck detekt              # style and static analysis
```

Requires JDK 17+, the Android SDK with `platforms;android-37`, NDK
`28.0.13004108` and CMake 3.22.1; `local.properties` must point at the SDK.
`tools/setup-android-sdk.sh` installs those packages on a machine without
them. The workflows under `.github/workflows` build and lint, run the host
tests, publish the debug APK and, on a manual run, build the signed Play AAB;
they check out the submodules.

## Architecture

Gradle modules, package `dev.geode`:

| Module | What it holds |
|---|---|
| `:app` | Compose UI, playback service and media session, library and playlist stores, the editor model, the export pipelines, the wallpaper and the widget |

The native core lives outside the modules and is built by the root
`CMakeLists.txt` into one `libgeode.so`:

| Directory | What it holds |
|---|---|
| `core/api` | `geode_api.h`, the `extern "C"` ABI — the only thing JNI calls |
| `core/sound` | The sound engine: FFT bands (its own radix-2 `Fft`) and a per-sample filter bank, drums, onsets, melody, stereo field, analysed on a thread of its own per PCM ring (inline for an export), published as hops a renderer reads into a `GeodeSoundFrame` |
| `core/viz` | GL capability probing, program cache, the frame graph (scene → trails → composite), the style catalogue (`Catalogue`), the auto-director (`Director`), the reaction matrix (`Reactions`), the per-frame motion (`MotionField`), the camera (`CameraRig`), transitions, safety clamps, every scene family |
| `core/audio/dsp` | Biquad equalizer, gain, crossfeed, lookahead limiter; the export resampler (`PcmResampler`) and the BS.1770 meter (`Loudness`) |
| `core/library` | TagLib tag reading and writing over a file descriptor; the library's pure rules (tracks, playlists, search, history) |
| `core/stage`, `core/output`, `core/player`, `core/shell`, `core/backup`, `core/cast`, `core/studio` | The surface threads, export and Record planning, the queue and transport (not wired to the app yet), shell rules, backup rules, Cast, project documents |
| `app/src/main/cpp` | The JNI files: no logic, only marshalling into `core/api` |
| `third_party/` | Git submodules: taglib, cgltf, meshoptimizer, basis_universal, kissfft, libdatachannel, mbedtls |

Shaders ship as assets under `app/src/main/assets/shaders/` and are loaded by
the native core. Inside `:app`, dependency flow is one-way: `ui` depends on
`playback`, `export`, `editor` and `data`, never the reverse.

## Tests

JVM unit tests live in `app/src/test`, and CI runs
them; the native host tests are in `core/tests`; instrumented tests live in `app/src/androidTest`. Checks that cannot run
headless (GL behaviour, capture, wallpaper) need a device.
`tools/glslcheck` compiles and renders every fragment style and every fluid
display variant headlessly (`node check.mjs --all`), and
`tools/hostlink/hostlink.sh` compiles and links the native renderer on the
host with no undefined symbols allowed; both are lower bounds on
portability, not device checks.

## Maintainer notes

- Tag writing covers files the app holds a write grant for. For a
  `content://media/...` track the app did not create, on API 30+ the editor
  now launches `MediaStore.createWriteRequest` for the user's consent and
  retries the write once if it is granted; below API 30, for a non-MediaStore
  URI, or if consent is denied, it reports the refused write instead.
- `compileSdk = 37` while the workflows install `platforms;android-36`; both
  are left as they are.

## Documentation

- [CHANGELOG.md](CHANGELOG.md) — version history, newest first.
- `THIRD_PARTY_NOTICES` — licences and attributions; the in-app notices asset
  mirrors this file.
