# GameActivity Direct Native Host and Surface Design

## Decision

BeatRealms 3D uses a `GeodeGameActivity : GameActivity` subclass. A bare GameActivity cannot be the startup Activity; Android's migration guide requires an app-specific subclass.

The selected AGDK native path is GameActivity's **direct C callback API**:

- `GameActivity_onCreate(GameActivity*, void*, size_t)` is the app entry point.
- The app fills the existing `GameActivityCallbacks` table.
- GameActivity calls lifecycle, window, focus, touch, and key callbacks on the app main thread.
- The existing `StageThread` remains the one native game/render loop.
- Swappy remains the one frame-pacing authority.

`native_app_glue` and `android_main` are optional AGDK conveniences. They are **not required** for this design and are deliberately not linked. Adding them would introduce another native event thread/looper without improving this app's existing retained StageThread architecture.

## Documentation facts that govern this design

- GameActivity is an `AppCompatActivity` with a built-in `SurfaceView` (`kb://android/games/agdk/game-activity/index`).
- Apps must derive a startup Activity from GameActivity (`kb://android/games/agdk/game-activity/migrate-native-activity`).
- GameActivity 4.4.2 exposes `protected InputEnabledSurfaceView mSurfaceView` and `protected int contentViewId`. The content root is `findViewById<FrameLayout>(contentViewId)`; there is no verified `contentRoot` field.
- GameActivity 4.4.2's default `onSetUpWindow()` sets `PixelFormat.RGBA_8888` and `SOFT_INPUT_ADJUST_RESIZE`. It does not default to RGB_565.
- `GameActivityCallbacks` runs on the app main thread. `GameActivity::env` is valid only on that thread; `GameActivity::vm` is process-wide.
- The callback-table pointer is framework-owned; the app changes its function fields, not the pointer.
- Saved-state memory passed to `GameActivity_onCreate` is released after the callback returns; copy anything retained.
- `onNativeWindowDestroyed` must stop all drawing and synchronize with any render thread before returning. The window must not be touched afterward.
- `onNativeWindowRedrawNeeded` should not return until the window has been redrawn in its current state.
- The direct callback API and `native_app_glue` are alternative integration layers; choosing direct callbacks does not mean avoiding AGDK.

References:

- `https://developer.android.com/reference/games/game-activity/struct/game-activity`
- `https://developer.android.com/reference/games/game-activity/struct/game-activity-callbacks`
- `https://developer.android.com/reference/games/game-activity/structandroid/input-buffer` — glue-only input buffering; not used by the selected direct path.
- `https://developer.android.com/reference/games/game-activity/group/game-activity`
- `kb://android/games/agdk/game-activity/index`
- `kb://android/games/agdk/game-activity/get-started`
- `kb://android/games/agdk/game-activity/migrate-native-activity`

## Ownership tree

```text
GeodeGameActivity : GameActivity
├─ GameActivity content FrameLayout
│  ├─ index 0: GameActivity InputEnabledSurfaceView
│  └─ index 1: ComposeView
│              └─ AppRoot(stageSurfaceController)
├─ native GameActivityHost (one per Activity generation)
│  ├─ GameActivityCallbacks
│  ├─ pending/current ANativeWindow + generation
│  ├─ retained live Stage binding
│  └─ process-wide SwappyHost attach/detach
├─ StageSurfaceController (non-View)
│  ├─ layout/alpha/display/buffer sizing
│  ├─ tilt/blur/performance/keep-screen-on
│  └─ weak retained Stage attachment
├─ retained Stage
│  └─ StageThread
│     ├─ the one native game/render loop
│     ├─ EglWindow
│     ├─ SwappyHost
│     └─ Renderer
└─ optional Presentation
   └─ the SAME InputEnabledSurfaceView moved here
```

There is never a second live renderer surface, second StageThread, or second pacing authority.

## Native GameActivity host contract

`app/src/main/cpp/geode_game_activity.cpp` becomes a real direct-callback host, not an empty entry-point stub. A per-Activity host is stored in `GameActivity::instance` and owns:

- The active retained live Stage handle, if one has been created.
- The current `ANativeWindow*` identity and generation.
- Pending window/dimensions received before the retained Stage exists.
- Swappy attach/detach for the Activity.
- Callback-table installation and teardown.

`GameActivity_onCreate`:

1. Allocate the host and assign `activity->instance`.
2. Fill the framework-owned `activity->callbacks` function fields.
3. Attach the process-wide SwappyHost with `activity->env` and `activity->javaGameActivity`.
4. Replay any pending state when the retained Stage binds.

The retained Stage is still created from the application Context by Compose. Native host creation and Stage creation are independent; pending window state bridges their ordering.

### Callback routing

| Callback | Required action |
|---|---|
| `onNativeWindowCreated` | Record the window/generation and dimensions; acquire it through `StageThread::setWindow` when a Stage is bound, otherwise retain it as pending. |
| `onNativeWindowResized` | Send the new dimensions to the bound Stage. |
| `onNativeWindowRedrawNeeded` | Request a redraw and bounded-wait until StageThread acknowledges completion before returning. |
| `onNativeWindowDestroyed` | Verify window identity/generation, detach the EGL window, and bounded-wait until StageThread acknowledges detachment before returning. Never touch the window afterward. |
| `onDestroy` | Ensure the window is detached, unbind the retained Stage, detach Swappy, and delete the host. |
| `onStart` / `onResume` / `onPause` / `onStop` | Record Activity lifecycle if needed, but do not bypass `StageRunEffect`; PiP, second-display, and multi-window policy remains explicit. |
| `onTouchEvent` / `onKeyDown` / `onKeyUp` | Return `false`; Compose owns shell input. Any future native visual-input route must be explicit and must not duplicate Compose gestures. |
| `onTextInputEvent` | No-op; Compose owns IME. |
| `onTrimMemory` | Forward to the existing Stage memory-trim path. |
| `onWindowFocusChanged` | Do not change render policy directly; Kotlin returns focus to Compose when appropriate. |
| `onConfigurationChanged` | Let the Activity/controller update layout, display, and buffer sizing; do not create another renderer. |
| `onWindowInsetsChanged` | Compose remains authoritative for insets and IME layout. |

### Required synchronous native contracts

The existing asynchronous window command is not sufficient for `onNativeWindowDestroyed`. Add narrow bounded-wait operations that reuse StageThread's sequence acknowledgement:

- `setWindowAndWait(window, width, height)` — used for destruction and any callback that must guarantee EGL detachment.
- `redrawAndWait()` — used by `onNativeWindowRedrawNeeded`.
- C API wrappers for the direct host, such as `geode_stage_set_window_sync` and `geode_stage_redraw_sync`.

A timeout is logged and handled as a failed handover; the main thread must not wait indefinitely. These operations change synchronization only, not frame-rate policy.

### JNI safety

- Use `GameActivity::env` only on the app main thread inside direct callbacks.
- If a background thread needs JNI, cache the process-wide `JavaVM*`, attach with `AttachCurrentThread`, and detach only if that call attached the thread.
- SwappyHost retains the Activity through a global reference and deletes it on detach; no raw local `jobject` is cached across callbacks.
- Check `ExceptionCheck()` after JNI calls and release local references before returning.
- Copy any saved state needed beyond `GameActivity_onCreate`; the framework releases the supplied buffer afterward.
- No C++ exception crosses a `JNIEXPORT` or GameActivity callback boundary.

## StageSurfaceController contract

`StageSurfaceController` is a non-View owner/adaptor. `StageView.kt` may retain a compatibility alias:

```kotlin
typealias StageView = StageSurfaceController
```

It implements `StageSurfaceOwner` and exposes:

```text
val surfaceView: SurfaceView
val activityRoot: FrameLayout
val stage: Stage                    // non-null after synchronous attach
var keepScreenOn: Boolean
var gyroWanted: Boolean
val display: Display?
val width: Int
val height: Int

fun attach(stage: Stage)
fun detach(stage: Stage)
fun release()
fun layoutHost(rect: Rect, alpha: Float)
fun moveToPresentation(presentation: Presentation)
fun restoreToActivity()
fun onConfigurationChanged(config: Configuration)
fun applyPerformance(settings: PerformanceSettings)
fun setUiBlurRegion(...)
```

The controller below is the target contract. The current `StageSurfaceController.kt` still contains pre-cutover `SurfaceHolder.Callback2` scaffolding; Slice 2 removes that path rather than treating it as final architecture.

The controller:

- Does **not** register a competing native `SurfaceHolder.Callback2` path.
- Does **not** call `stageSetWindow`, `stageSetSize`, or `stageRedraw`; the direct GameActivity host owns those callbacks.
- May use the holder for buffer sizing (`setFixedSize` / `setSizeFromLayout`).
- Owns View layout, alpha, display observation, buffer dimensions, sensors, blur, performance, and weak Stage attachment.
- Clears its weak Stage ownership on release without closing the retained Stage.

## Compose host

`GeodeGameActivity`:

1. Loads `libgeode.so` deterministically and idempotently before native initialization.
2. Installs the splash screen before `super.onCreate`.
3. Lets GameActivity create its surface, direct native host, and Swappy attachment.
4. Constructs `StageSurfaceController` around GameActivity's real surface/content root.
5. Creates a `ComposeView`, assigns an ID, uses `DisposeOnDetachedFromWindowOrReleasedFromPool`, and adds it at index 1 with `MATCH_PARENT`.
6. Calls `AppRoot(stageSurfaceController)`; it never calls Activity `setContent`.
7. Keeps GameActivity 4.4.2's verified default `onSetUpWindow()` behavior (RGBA_8888 and `SOFT_INPUT_ADJUST_RESIZE`) unless the app has a documented reason to override it; there is no RGB_565 problem to work around.
8. Removes any duplicate Kotlin `pacingAttach` / `pacingDetach` ownership once the native direct host owns Swappy attach/detach.

`AppRoot` / `AppContent` synchronously attach the retained Stage before any composable reads `controller.stage`.

The Compose root must be transparent wherever the visual is visible. The old opaque AppShell root background must not cover the SurfaceView. Individual non-visual screens may remain opaque.

`VisualizerCanvasHost` is layout-only: it computes the Visuals bounds/alpha and calls `controller.layoutHost(rect, alpha)`. It does not use `AndroidView`, remove parents, or place the surface itself.

## Presentation and external displays

`SecondScreenCanvas` moves the same `surfaceView` through `moveToPresentation` and restores it through `restoreToActivity`.

Moving a SurfaceView destroys the old window surface and creates a new one. The direct GameActivity host must follow those normal callbacks, preserve generation ordering, and hand the new window to the same retained StageThread.

Restore the surface to Activity index 0 on every exit path:

- Composition disposal.
- Presentation dismissal or Back.
- Display removal/change.
- `Presentation.show()` failure.
- Activity stop.
- PiP or PiP-transition precedence.
- Activity destruction.

No path creates a second Stage, StageThread, EGL context, or renderer.

## Input and back

- GameActivity's real surface and native listeners remain installed.
- Compose is the top sibling and owns shell gestures, D-pad/TV remote, IME, semantics, focus, and predictive back.
- Direct native touch/key callbacks return `false`; they do not preempt Compose.
- Direct-callback event objects are valid only for the duration of the callback. Copy anything retained; never keep a pointer to `GameActivityMotionEvent`, `GameActivityKeyEvent`, or `GameTextInputState`.
- `android_input_buffer` is part of the optional `native_app_glue` path. It is not used by this direct-callback design and must not be introduced as a second input queue.
- Do not intercept `KEYCODE_BACK` or override deprecated `onBackPressed`.
- Presentation Back dismisses the Presentation and restores the surface.
- `onWindowFocusChanged` calls `super`, then returns focus to Compose when appropriate.

## PiP and multi-window

- Preserve `STARTED && (RESUMED || PIP || SECOND_SCREEN || MULTI_WINDOW)` draw policy.
- A visible paused Activity in split-screen/free-form mode keeps drawing through `MULTI_WINDOW`.
- Hidden or alpha-zero surfaces still stop through existing `STAGE_VIEW_*` policy.
- API 35+ PiP transition state hides chrome/sheets at animation start.
- Keep surface alpha 1 through PiP transition and clear native UI blur.
- PiP source rect/aspect follow the real surface bounds.
- Disable PiP auto-enter while a Presentation is active; prevent new Presentations during PiP/transition.

Direct GameActivity lifecycle callbacks record state but do not override this Compose/native draw policy.

## Lifecycle and teardown

- The retained Stage and StageThread survive Activity recreation.
- The native GameActivityHost and StageSurfaceController are per-Activity-generation.
- Window destruction synchronously detaches EGL before the callback returns.
- `GeodeGameActivity.onDestroy` releases its controller/weak Stage ownership before `super.onDestroy`, because GameActivity's superclass path forces surface destruction and native termination.
- Activity destruction releases only the host/controller; it does not close the retained Stage.
- Recreation binds the new window to the same StageThread.
- No native surface callback is applied after its host generation is destroyed.
- Process death does not guarantee `onDestroy`; durable state remains in existing stores.

## CI and symbol contract

- Link `game-activity::game-activity_static`.
- Keep the `Java_com_google_androidgamesdk_GameActivity_initializeNativeCode` anchor and require that symbol in the packaging gate.
- Export exactly one app `GameActivity_onCreate` and one `JNI_OnLoad`.
- Keep `android_native_app_glue.c.o` unextracted; reject accidental `android_main` linkage for this direct-only contract.
- Verify the direct callback host survives release/R8/minification.
- Verify 16 KB alignment and the native-library allowlist.

## Verification

- Exactly one GameActivity `InputEnabledSurfaceView`; Compose is sibling index 1.
- Exactly one StageThread game/render loop; no glue thread, `android_main`, or second Swappy instance.
- Direct callback table is installed and survives release.
- Pending window before Stage bind replays once.
- `onNativeWindowDestroyed` waits for StageThread EGL detachment.
- `onNativeWindowRedrawNeeded` waits for redraw completion.
- Stale Activity/window generations cannot clear a newer surface.
- Activity recreation retains Stage identity and replaces only host/controller.
- Presentation move/restore produces one renderer and one StageThread.
- PiP keeps drawing while paused and stops on stop.
- Visible multi-window keeps drawing; hidden/alpha-zero does not.
- Compose touch, D-pad, IME, semantics, and predictive Back work without duplicate native input.
- Second-display refresh/buffer/tilt state follows `surfaceView.display`.

## Additional references

- `kb://android/games/develop/gameloops`
- `kb://android/games/agdk/configure-graphics`
- `kb://android/games/sdk/frame-pacing/index`
- `kb://android/games/sdk/frame-pacing/opengl/index`
- `kb://android/games/sdk/frame-pacing/opengl/add-functions`
- `kb://android/develop/ui/compose/migrate/interoperability-apis/compose-in-views`
- `kb://android/develop/ui/views/picture-in-picture`
- `kb://android/develop/adaptive-apps/guides/support-multi-window-mode`
- `kb://android/guide/topics/resources/runtime-changes`
- `kb://android/guide/components/activities/activity-lifecycle`
- `kb://android/guide/components/activities/state-changes`
- `https://developer.android.com/reference/android/view/SurfaceView`
- `https://developer.android.com/reference/android/app/Presentation`
