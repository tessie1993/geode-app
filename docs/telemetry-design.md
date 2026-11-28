# Performance Telemetry Design

## Goal

Add process-wide Android Performance Tuner and detailed native loading/shader/render telemetry without adding per-frame JNI, competing frame pacing, automatic quality/FPS adaptation, or game-asset caching.

## Privacy and network boundary

Performance Tuner uploads frame/loading telemetry to Google Play through the Android Performance Parameters API. This is a product-visible change from the current “no analytics” wording and the current Cast/browser-only network gate.

Required before release:

- Update the privacy policy and store description to disclose performance telemetry and its purpose.
- Narrow the CI network allowlist to the Android Performance Parameters endpoint; do not add a generic HTTP stack.
- Supply an API key restricted to the release package name, SHA-1 certificate, and Android Performance Parameters API.
- Validate with a Play-signed build and Play Console access.
- Keep upload disabled/unavailable in debug/sideload builds unless explicitly configured for validation.

References:

- `kb://android/games/sdk/performance-tuner/custom-engine/index`
- `kb://android/games/sdk/performance-tuner/custom-engine/add-functions`
- `kb://android/games/sdk/performance-tuner/custom-engine/enable-api`
- `kb://android/games/sdk/performance-tuner/custom-engine/validate-and-package`

## Process-wide controller

One native controller lives for the process because the process hosts:

- `GeodeGameActivity`/live stage.
- `Presentation` second display using the same stage.
- PiP.
- Cast and browser live output.
- Record and still capture.
- Export/offscreen renderers.
- Playback/Cast/Export services.

The controller is initialized once from `Application.onCreate` after `libgeode.so` is loaded, or lazily by the first native entry point with a valid JNI environment/context. It is destroyed only at process shutdown.

The controller must not:

- Own the Stage or EGL context.
- Call into Kotlin per frame.
- Cache shader/program/game assets.
- Change FPS, resolution, march scale, or post-FX.
- Use Swappy tracer injection and manual ticks simultaneously.

## Manual frame timing

Initialize once with `TuningFork_init` during native startup. If `TFSettings.swappy_tracer_fn` is set, Tuning Fork records frame timing automatically and `swappy_version` must match the linked Swappy version. This repo already injects its own Swappy tracer, so the selected design keeps explicit `TuningFork_frameDeltaTimeNanos` / `TuningFork_frameTick` calls and does **not** set `swappy_tracer_fn` at the same time.

Use `TuningFork_frameDeltaTimeNanos` or `TuningFork_frameTick` from the stage/render thread. No JNI is involved after initialization.

Instrumentation keys:

- `FRAME_CPU` — stage-thread frame work.
- `FRAME_GPU` — prior-frame GPU fence time when available.
- `SWAP_WAIT` — Swappy pre/post wait duration.
- `PRESENT_INTERVAL` — actual presentation interval.
- `EXPORT_FRAME` — offscreen export frame time.
- `CAST_FRAME` — Cast render/encode frame time where useful.

The existing `StageThread::frameWork` and `SwappyHost` tracer already expose CPU/GPU/swap timing. Feed those native values into the controller instead of adding a second measurement loop.

## Lifecycle

Report `TUNINGFORK_STATE_ONCREATE`, `TUNINGFORK_STATE_ONSTART`, `TUNINGFORK_STATE_ONSTOP`, and `TUNINGFORK_STATE_ONDESTROY` from the process/Application or direct GameActivity bridge. Call `TuningFork_setFidelityParameters` only after a user quality change; telemetry never changes quality by itself.

Important:

- GameActivity lifecycle callbacks are main-thread callbacks.
- The retained Stage is not the GameActivity native handle.
- PiP/second-display rendering can continue while the Activity is paused; do not treat `onPause` as “rendering stopped.”
- Flush on background/foreground boundaries using `TuningFork_flush`.

## Loading groups

Only one loading group can be active. Use a process-wide coordinator with explicit scopes:

1. Application/native startup.
2. Live Stage creation and first surface.
3. Scene/style preparation and shader compile/link/cache.
4. Transition warmup.
5. Export renderer setup.
6. Cast fallback renderer setup.

Loading events:

- Asset/source load.
- Shader compile.
- Program link.
- Binary cache load/store.
- Scene/FBO/texture setup.
- First rendered frame.

Use `TuningFork_LoadingTimeMetadata::SHADER_COMPILATION`, `APK`, `DEVICE_STORAGE`, `FIRST_TOUCH_TO_FIRST_FRAME`, and start/stop handles. Do not start overlapping groups; serialize or nest logically in one coordinator.

## Native instrumentation points

| Path | Hook | Data |
|---|---|---|
| `StageThread::start/run` | Stage create/start/stop | Stage kind, thread start, surface ready |
| `StageThread::createContext/createSurface` | EGL setup | Context/surface result, dimensions |
| `StageThread::swap` | Swap | CPU, GPU, wait, interval |
| `ShaderSource::load/resolveIncludes` | Source load | Source name, include count, duration |
| `program::compile` | Shader compile | Stage type, duration, success |
| `program::build` | Link/cache | Cache hit/miss, link duration, success |
| `ProgramBinaryCache::load/store` | Binary cache | Hit/miss, bytes, duration |
| `ProgramStager::kick/finish/step` | Staged compile | Parallel compile, queue/in-flight, duration |
| `ScenePrep` | Scene preparation | Attempts, progress, total duration, outcome |
| `Renderer::render` | Render frame | CPU/GPU, pass count |
| `UiBlurPass` | UI blur | Active/inactive, region, pass duration |
| Export/Cast hosts | Offscreen/encode frame | Frame duration, encoder backpressure |

## Settings and assets

Add under `app/src/main/assets/tuningfork/`:

- `dev_tuningfork.proto` with `Annotation` and `FidelityParams`.
- `tuningfork_settings.txt` plus generated `tuningfork_settings.bin`.
- At least one `dev_tuningfork_fidelityparams_i.txt` plus generated binary.

Suggested annotations:

- Surface kind: live, export, Cast, still.
- Scene family or style id index.
- Quality tier.
- PiP/second-display state.
- Cold/warm start.

Fidelity parameters:

- Render resolution.
- March scale.
- Post-FX enabled.
- Live FPS fixed at 30.
- Export FPS where relevant.

The validation tool or `protoc` must generate binaries in CI/release preparation. Do not commit a real API key. Use a checked-in template and a protected CI property/secret for release settings generation.

## Failure behavior

Telemetry failure must never affect rendering, playback, export, or Cast:

- Initialization failure disables uploads and records a log counter.
- Tick/loading errors are ignored after a bounded failure count.
- No unbounded retry or blocking network call on the stage thread.
- Upload persistence uses Tuning Fork’s telemetry cache only; never store game assets there.
- Missing Play credentials leaves local collection/validation possible but release upload unverified.

## Validation

- Native unit tests for controller state, key mapping, loading-group scopes, and failure handling.
- Asset/proto validation in CI.
- Debug logcat/Tuning Fork Monitor validation.
- Release/sideload minified smoke.
- Play-signed internal-track validation with API key and Console access.
- Confirm no per-frame JNI calls in traces.
- Confirm fixed 30 FPS and no automatic quality changes under thermal/load stress.
