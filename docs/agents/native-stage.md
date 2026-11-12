# Native stage (`core/stage`): render thread, EGL, vsync

One fact per line. The design of the native host every live surface draws through: the app's view, the live wallpaper, the dream and, later, Cast. Not yet wired to Kotlin: the wiring change adds the JNI and deletes the Kotlin drivers.

## Why

- `core/` has one renderer but no loop, no thread, no EGL context and no vsync source of its own.
- So each Kotlin driver repeats the frame protocol: `VisualizerView` + `VisualizerRenderer` + `FramePacer` on a `GLSurfaceView`, the wallpaper's own `GLSurfaceView` and `FramePacer`, the dream (through `VisualizerView`), and Cast's `cast-render` thread with its own EGL.
- The run rule exists three times in Kotlin: `VisualizerView.syncPacer` and the two near-identical `syncRunState` machines in `VisualizerWallpaperService` and `VisualizerDreamService`.
- The stage replaces all of them with one native implementation that a later change binds to `geode_viz`.

## Thread model

- Two kinds of thread, as today: one process-wide vsync thread, and one stage thread per surface.
- The vsync thread is named `GeodeFramePacer` and runs at nice -4 (`THREAD_PRIORITY_DISPLAY`), the name and priority of the Kotlin `PacerThread` it replaces.
- It is started on first use and never quit, like the Kotlin one: an idle looper costs nothing, and a thread torn down on every pause would race the next start.
- Its object is allocated once and never freed, so the thread never runs on an object a static destructor took down at exit.
- It owns an `ALooper` and the `AChoreographer` of that looper, and serves every stage's pacer, so a preview wallpaper beside the app costs no second clock thread.
- It never draws, so a slow frame on one surface never delays another surface's vsync and never stops the pacer from hearing every vsync.
- That is why the clock is not merged into the render thread: `AChoreographer` delivers one callback per looper turn, so a frame longer than a vsync would make the pacer miss vsyncs and change its period estimate and divide (`core/viz/frame/Pacer`), which the owner fixed as unchanged.
- Each stage thread is named `GeodeStage` and runs at nice 0, the default priority of `GLSurfaceView`'s `GLThread`, which sets none.
- One stage thread per surface, not one render thread for all: the app view and a live wallpaper (or its preview) can run at once, each `geode_viz` has its own GL objects in its own context, and one thread would switch contexts every frame and let one surface's slow frame stall the other.
- A stage thread owns an `ALooper` too: it sleeps in `ALooper_pollOnce` and is woken by `ALooper_wake` when a command arrives.
- `ALooper_pollAll` is not used: the NDK hides it because it may ignore wakes; every `ALooper_pollOnce` return is treated as a wake.

## What the shell hands over

- A window: an `ANativeWindow` (from `ANativeWindow_fromSurface` in the later JNI) with its size; the stage takes its own reference with `ANativeWindow_acquire`.
- Size changes, as `surfaceChanged` reports them.
- Visibility, two ways by kind: the view's four observations (attached, window visible, shown, alpha above 0), or the wallpaper's and dream's `GEODE_SURFACE_*` flags.
- The host's pause and resume (the view's `onPause`/`onResume`), a redraw request (`surfaceRedrawNeeded`), and the surface's own fps cap.
- Every call is any-thread and returns at once, except the window hand-over, which waits for the stage (see Teardown).

## What the shell reads back

- One state word (`geode_stage_state`): window, context, surface, drawable, vsync, paused, context refused, surface bad, released, and the run mode in bits 12-13.
- The rate the pacer draws at now (`geode_stage_paced_fps`).
- The frame events the client returned since the last read (`geode_stage_take_events`), OR-ed together.
- Native never calls up into Kotlin: no `JavaVM`, no global references; the shell polls.

## Run rule

- The wallpaper and the dream reuse the native rule as it is: `geode_stage_set_surface` runs `viz::surface::mode` (`core/viz/SurfaceRun.hpp`) and hands the mode to the stage.
- The view's rule is `stage::viewMode` (`core/stage/StageLifecycle.hpp`): moving while attached, its window visible, shown and not transparent, else stopped; the host's pause is a separate input.
- A surface stage pauses when the rule says Stopped, as both services call `onPause` there; a view stage keeps its surface while hidden, as `VisualizerView` did at alpha 0.
- Dark keeps the context and surface and draws nothing, as the dream did.
- Still paces for `viz::surface::settleMs` on the way in and then stops; a restated Still with no settle running draws one frame.
- Leaving a drawing state trims the spare scenes with the context current, before the surface goes (#854), and only then: Dark does not trim.

## Lifecycle (`core/stage/StageLifecycle`)

- Pure: no Android header, host-tested in `core/tests/stage_lifecycle_test.cpp`.
- Inputs: window set, window changed, window gone, run mode, tick (time), pause, resume, redraw, context lost, surface bad, frame drawn, surfaceless support, quit.
- Outputs, one at a time in order: create context, create surface, make current, make current surfaceless, client surfaceCreated, client surfaceChanged, start vsync, stop vsync, draw once, client trimSpares, client contextLost, release current, destroy surface, destroy context, client release, report state.
- The executor runs each action and reports its outcome before asking for the next, so a failed create never leaves a stale plan behind it.
- The lifecycle compares what is wanted with what exists after every event, so events in any order and repeated (window gone twice, changed before set, resumed while hidden, quit at any point) converge to the same state.
- It never asks for a client call that touches GL (surfaceCreated, surfaceChanged, trimSpares, a frame) without a current context.
- Every create has its destroy: after quit there is no context, no surface, no vsync, and the client was released exactly once.
- The context lives as long as the stage, across pause and surface loss, which is what `preserveEGLContextOnPause` gave the views.
- The surface follows the window and the pause: created when wanted, destroyed when the window goes, the host pauses, the window is replaced, or EGL calls it bad.
- surfaceCreated is told once per context; surfaceChanged after every new surface and every size change, as `GLSurfaceView` did.
- The order on a pause is stop vsync, trim spares, destroy surface: the same order as the services' stop, trim, `onPause`.
- A frame is drawn only on a vsync while the vsync runs, or by a draw-once, and only when the context is current and the client knows the size.

## Stage core (`core/stage/StageCore`)

- Pure: what a stage thread does with each command and on each turn of its loop, host-tested in `core/tests/stage_core_test.cpp`.
- It holds the `StageCommand` kinds, the command-to-lifecycle mapping, the window references, the hand-over sequence and its acknowledgement, the frame-due gate and the frame-rate restate.
- A vsync's frame is drawn only while the lifecycle's vsync runs and a frame can be drawn; a request in flight across a stop is dropped.
- A hand-over is acknowledged after the lifecycle's actions for it ran, so a window that went has no surface left on it.
- The platform is a `StagePort` (EGL, the window, the vsync thread, the thermal rung, the acknowledgement), which `core/stage/android/StageThread` implements and a model implements in the test.
- A `kStageService` command has the stage thread call the client's `service(ready)` once, between frames.

## Commands (`core/stage/CommandQueue`)

- A bounded ring of fixed-size commands under one mutex: no allocation after construction.
- A command may be pushed as last-wins: it overwrites a pending command of the same kind and target, so a burst of sizes or frame requests costs one slot.
- A last-wins push never merges across an ordered command (a window hand-over, quit), so the stage sees them in the order they were sent.
- A full queue refuses the push; every push retries for 100 ms, and a dropped one is logged.
- Quit is also an atomic flag the stage thread reads on every turn, so a full queue cannot lose it.
- A stage leaving the vsync thread goes on a list under a mutex, not through the queue, so the detach cannot be dropped and no callback re-posts for a stage that is gone.
- The consumer is woken through a callback, only when the queue turns non-empty; the platform layer passes `ALooper_wake`.

## Vsync and pacing (`core/stage/StagePacing`)

- `AChoreographer_postFrameCallback64` (API 29) on the vsync thread's own choreographer, re-posted from each callback.
- The pacer is fed the callback's `frameTimeNanos`, `CLOCK_MONOTONIC`: the same time base as the Java `Choreographer.FrameCallback` the Kotlin pacer used, so pacing stays exactly as it is.
- `AChoreographer_postVsyncCallback` (API 33) adds frame timelines and an expected presentation time; feeding that instead would change the dt the pacer sees, so any switch is a separate, later decision.
- `StagePacing` owns the nine-int64 `PacerState` natively instead of a Kotlin `LongArray`, and applies the process thermal step as `geode_pacer_on_vsync` does (`frame::pacerCapFps`).
- A callback re-posts first, before the pacer runs, so the cadence survives a slow frame and a skipped vsync still wakes the next one.
- There is no NDK call to remove a posted callback, so stop only clears the running flag and the pending callback lapses on its next vsync.
- A start while a callback is still pending posts nothing, so two starts never double the callback and halve every dt.
- A start resumes the pacer, so a resume never inherits the gap it was paused across; the period estimate survives.
- A drawn vsync hands the stage thread one last-wins frame request carrying the paced rate and the surface's cap; requests coalesce like `requestRender` did.
- The rate handed to the frame is the pacer's paced rate, or the cap until the first vsync after a cap change, as `FramePacer.fps()` was.

## Frame-rate hint

- `ANativeWindow_setFrameRate` (API 30) with `ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT`: the NDK documents it as the change strategy only-if-seamless, the twin of the two-argument `Surface.setFrameRate` the Kotlin used.
- The rate is the effective cap (`StagePacing::frameRateFor`): the surface's cap under the thermal step, 0 for Follow device; 0 is the platform's no preference.
- It is stated on every new window, every size change and every cap change, and again when a vsync paces to a new cap (the thermal step moved), as `FramePacer.applyTo` did.
- It is called on the stage thread, which holds the window; a hint from a vsync reaches it as a last-wins command.

## EGL

- `eglGetDisplay(EGL_DEFAULT_DISPLAY)` and `eglInitialize`, once per stage; the display is never terminated, since RecordTap, CastTee, the export's encoder and the other stages share it.
- The config is `GLSurfaceView`'s default for client version 3: `eglChooseConfig` with red, green, blue 8, alpha 0, depth 16, stencil 0 and `EGL_OPENGL_ES3_BIT_KHR`, then the first config whose red, green, blue and alpha match exactly and whose depth is at least 16 and stencil at least 0 (`SimpleEGLConfigChooser` and `ComponentSizeChooser` in AOSP).
- The context asks `EGL_CONTEXT_CLIENT_VERSION` 3 and shares nothing, as `DefaultContextFactory` does.
- The window surface has no attributes, as `DefaultWindowSurfaceFactory` does, and no swap interval is set (`GLSurfaceView` sets none).
- Every EGL call is checked and logged under `GeodeStage`, in the style of `RecordTap.cpp`.
- `EGL_CONTEXT_LOST` from `eglMakeCurrent` or `eglSwapBuffers` is a context loss; any other failure there (`EGL_BAD_SURFACE`, `EGL_BAD_NATIVE_WINDOW`, `EGL_BAD_MATCH`, `EGL_BAD_ALLOC`) marks the surface bad, as `GLSurfaceView` sets `mSurfaceIsBad`.
- Destroying a surface first releases the current context, so no destroyed surface stays current.
- At quit the client is released with the context current: on the window surface if one is usable, else surfaceless where `EGL_KHR_surfaceless_context` exists, else without a context and the client is told so.

## Context loss and a refused context

- On a lost context the context is released from the thread, the client is told with none current (`contextLost`: its GL names are gone, delete none), the surface and context are destroyed, and both are created again; the client then gets surfaceCreated and surfaceChanged and rebuilds, as after `GLSurfaceView`'s `lostEglContext`.
- Three losses in a row with no frame drawn between them count as a refused context, so a broken driver cannot loop.
- A refused context (no display, no ES 3 config, `eglCreateContext` returning `EGL_NO_CONTEXT`) is reported in the state word and retried only for a new window; `GLSurfaceView` threw and crashed the app there.
- A bad surface is destroyed and not retried until a new window arrives.

## Teardown and its time bound

- The window hand-over (`geode_stage_set_window`, a window or none) waits until the stage thread has applied it, at most 1000 ms after at most 100 ms for room in the queue: when it returns with none, the EGL surface on the old window is destroyed and released, which is what `surfaceDestroyed` must guarantee.
- A hand-over that returns on its bound is a known degradation: the surface may outlive the call for a moment, and the state word's surface bit tells when it is gone.
- Waiting is bounded so a wedged driver cannot turn a teardown into an ANR; past the bound the stage still holds its own window reference, so the late destroy is safe.
- `geode_stage_destroy` sends quit and waits at most 200 ms in all, the push included, for the thread to finish.
- A stage thread that ends on an exception still runs the lifecycle's quit (`StageCore::abandon`): the client is released and the EGL objects freed on that thread.
- Within the bound the thread is joined; past it the thread is detached and finishes on its own, freeing everything it owns.
- Either way the client is released on the stage thread with the context current where possible, never on the caller's thread and never contextless when a context exists, which the Kotlin release (run after `GLSurfaceView` had unbound its context) could not promise.
- The client is owned by the stage from create: release is its last call, after which the stage never touches it.

## C ABI (`core/api/geode_stage_api.h`)

- `geode_stage_create(kind, client, cap_fps)`, `geode_stage_set_window`, `_set_size`, `_set_view`, `_set_surface`, `_pause`, `_resume`, `_redraw`, `_set_cap`, `_state`, `_paced_fps`, `_take_events`, `geode_stage_destroy`.
- The client is a C struct of function pointers (`geode_stage_client`), so the wiring change binds it to `geode_viz` inside native code; Kotlin never sees it.
- No JNI function and no `external fun` exist for it yet: the parity gate wants both sides and there is no Kotlin caller.
- No exception leaves the ABI or the threads: each entry point and each thread body catches at its edge.

## Files

- `core/stage/StageLifecycle.{hpp,cpp}`, `core/stage/StageCore.{hpp,cpp}`, `core/stage/CommandQueue.hpp`, `core/stage/StagePacing.{hpp,cpp}`, `core/stage/StageClient.hpp`: pure, host-tested.
- `core/stage/android/EglWindow.{hpp,cpp}`, `core/stage/android/VsyncThread.{hpp,cpp}`, `core/stage/android/StageThread.{hpp,cpp}`: the platform layer, Android only, in no host-test line, and deciding nothing.
- Host tests: `stage_lifecycle_test.cpp`, `stage_core_test.cpp`, `command_queue_test.cpp`, `stage_pacing_test.cpp`.

## What cannot be host-tested

- Looper wake-ups, choreographer timing, EGL creation and loss, the frame-rate hint and the thread bounds need a device.

## Phone checklist (before the wiring change lands)

- Rotation and fold: one surfaceChanged, no black flash, the rate hint restated.
- Picture-in-picture: the stage draws while paused-but-started, at the PiP size.
- Second screen (Presentation): a second stage on the external window, both pacing independently.
- Wallpaper preview beside the home-screen wallpaper: two surface stages and the app view at once, one `GeodeFramePacer` thread.
- Lock screen: the lock look, Stopped when covered, Still with the settle burst and then no frames.
- 60, 90 and 120 Hz panels: the same divides and paced rates as the Kotlin pacer (60 cap on 90 Hz draws 45), and the panel mode follows the hint where it can switch seamlessly.
- Screen off and on: vsync stops, the context survives, the first frame after is not a 600 s step.
- Low-memory trim: `geode_memory_trim` still reaches the renderer while the stage is stopped, and spares trim on the way out.
- Context loss (force with a GPU reset or a driver that drops contexts on sleep): surfaces rebuild and keep drawing.
