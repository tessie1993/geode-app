# BeatRealms 3D (internal name Geode): Android music player, visualizer and video suite

Native Android music player, real-time GPU music visualizer and a small video
suite. Kotlin and Jetpack Compose on top; a C++20 engine built by CMake and
the NDK underneath (the sound engine, the GLES 3.x renderer, the DSP chain
and tag access). Everything runs on-device: the app holds no
network permission, and nothing it hears or renders leaves the phone.

The version is set in `gradle.properties` (`geode.versionBase`,
`geode.versionCodeBase`); minSdk 26, targetSdk 36, compileSdk 37,
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
  bit-perfect USB output toggle. Android
  Auto browsing (tracks, albums, artists, playlists, favourites, recently
  played) and a home-screen now-playing widget.
- **Visual scenes** — particle, simulation and fragment-shader scenes with an
  in-app GLSL editor; the GPU fluid family (nine Fluid looks over one solver,
  plus Curl Flow);
  Cymatics; MilkDrop via projectM 4 built in-tree from a git submodule. Every
  scene is rendered by the native core; the Customize panel exposes every
  parameter with per-param locks, a randomizer, LFO and ADSR modulation, a
  palette maker, presets (JSON + `.milk`) and photosensitivity clamps.
- **Studio** — a multi-lane timeline (visual, media, text, overlay, audio
  lanes) with trim, split, ripple, snapping, markers, tap-in, keyframe curves for scene and clip parameters, GL
  Transitions between clips, speed ramps, `.cube` LUTs and per-channel gamma,
  captions from lyrics or SRT (import and export). The visualizer export
  steps its own sound engine through the track at 1/fps
  (`render/offscreen/TrackAudio`), so it reacts exactly like the screen; the cut export runs on Media3
  Transformer. Both encode H.264 or HEVC, with H.264 as the fallback when a
  device has no HEVC encoder.
- **Live wallpaper** — the visualizer as a home-screen wallpaper, hearing the
  same audio as the app.
- **Other apps' audio** — Android 10+ playback capture feeds the same PCM ring
  buffer the player's tap and the microphone share, so every scene and the
  wallpaper work unchanged on foreign audio.

## Build

```bash
git submodule update --init --recursive   # projectM, taglib
./gradlew assembleDebug                   # debug APK, native core included
./gradlew installDebug                    # install on a connected device
./gradlew ktlintCheck detekt              # style and static analysis
```

Requires JDK 17+, the Android SDK with `platforms;android-37`, NDK
`28.0.13004108` and CMake 3.22.1; `local.properties` must point at the SDK.
`tools/setup-android-sdk.sh` installs those packages on a machine without
them. The workflows under `.github/workflows` build the debug APK, ship a
signed APK and cut a Play Store release; they check out the submodules.

## Architecture

Gradle modules, package `dev.geode`:

| Module | What it holds |
|---|---|
| `:engine:audio-core` | `GeodeNative`, the JNI binding to `libgeode.so`; the handle on the native PCM ring (`SampleRing`, `SoundCursor`; the ring is `core/audio/ingest`), the sound frame layout and the presentation clock |
| `:engine:scenes` | Scene ids and parameters, the GL-thread adapter over the native renderer, offscreen rendering for export (`TrackAudio` feeds it the track), the style catalogue as Kotlin reads it |
| `:app` | Compose UI, playback service and media session, library and playlist stores, the editor model, the export pipelines, the wallpaper and the widget |

The native core lives outside the modules and is built by the root
`CMakeLists.txt` into one `libgeode.so` (plus `libprojectM-4.so`, LGPL,
dynamically linked):

| Directory | What it holds |
|---|---|
| `core/api` | `geode_api.h`, the `extern "C"` ABI — the only thing JNI calls |
| `core/sound` | The sound engine: FFT bands (its own radix-2 `Fft`) and a per-sample filter bank, drums, onsets, melody, stereo field, stepped once per rendered frame into a `GeodeSoundFrame` |
| `core/viz` | GL capability probing, program cache, the frame graph (scene → trails → composite), the style catalogue (`Catalogue`), the auto-director (`Director`), the reaction matrix (`Reactions`), the per-frame motion (`MotionField`), the camera (`CameraRig`), transitions, safety clamps, every scene family |
| `core/audio/dsp` | Biquad equalizer, gain, crossfeed, lookahead limiter; the export resampler (`PcmResampler`) and the BS.1770 meter (`Loudness`) |
| `core/library` | TagLib tag reading and writing over a file descriptor |
| `app/src/main/cpp` | The JNI files: no logic, only marshalling into `core/api` |
| `third_party/` | Git submodules: projectm, taglib |

Shaders ship as assets under `app/src/main/assets/shaders/` and are loaded by
the native core. Inside `:app`, dependency flow is one-way: `ui` depends on
`playback`, `export`, `editor` and `data`, never the reverse.

## Tests

JVM unit tests live in `app/src/test` and `engine/*/src/test`, and CI runs
every module's; instrumented tests live in `app/src/androidTest`. Checks that cannot run
headless (GL behaviour, capture, wallpaper) are listed in
[docs/DEVICE_CHECKS.md](docs/DEVICE_CHECKS.md), a partial reconstruction.
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
- `ENABLE_PLAYLIST` in the root `CMakeLists.txt` is now `OFF`: nothing calls
  `libprojectM-4-playlist.so`, so it is no longer packaged.
- `compileSdk = 37` while the workflows install `platforms;android-36`; both
  are left as they are.

## Documentation

- [CHANGELOG.md](CHANGELOG.md) — version history, newest first.
- [docs/AUDIO_CHAIN.md](docs/AUDIO_CHAIN.md) — where playback audio goes and
  which stages the visuals see.
- [docs/PARAM_MATRIX.md](docs/PARAM_MATRIX.md) — param × scene-family matrix.
- [docs/DEVICE_CHECKS.md](docs/DEVICE_CHECKS.md) — on-device checklist.
- `tools/build-projectm.md` — how projectM is built from the submodule.
- `THIRD_PARTY_NOTICES` — licences and attributions; the in-app notices asset
  mirrors this file.
