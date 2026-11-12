# NDK refactor research: moving the audio-to-visual path out of Kotlin

Read-only research for the port in `engine-deep-scan.md`. Nothing in the repository was touched, nothing was built or installed.

## How to read the source marks

- **[H]** read in the NDK r28 header shipped in this container (`/root/Android/Sdk/ndk/28.0.13004108/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/...`). This is the primary source for API levels (`__INTRODUCED_IN`) and for the doc comments quoted.
- **[R]** page read this session (URL given).
- **[KB]** page read through `android docs fetch kb://...` (the official Android knowledge base; same text as developer.android.com).
- **[S]** search snippet only, page not opened.
- **[M]** from memory, not checked this session. Verify before relying on it.
- **[I]** my inference from the sources, not a statement in them.

minSdk is 33, so every API below at level ≤ 33 can be called without an availability guard. Only three things found sit above 33: `AThermal_getThermalHeadroomThresholds` (35), `ANativeWindow_getBuffersDefaultDataSpace` (34), `ASurfaceTransaction_setBufferWithRelease` (36) [H].

## Findings that change the port plan

1. **The render thread has to be an `ALooper` thread.** `AChoreographer_getInstance()` "must be called on an ALooper thread" and returns the instance "for the current thread" [H, R]. A bare `std::thread` with a condition variable cannot receive vsync. Design the native render thread as: `ALooper_prepare` → `AChoreographer_getInstance` → loop on `ALooper_pollOnce`. The sensor queue attaches to the same looper (step 4.3 costs almost nothing once 4.1 exists). Steps 4.1 (pacer) and 4.2 (render thread) are one design, not two.
2. **`ALooper_pollAll` is gone for new code.** The r28 header hides it: it "will not reliably respond to ALooper_wake"; use `ALooper_pollOnce` in a loop and "treat all return values as if they also indicate ALOOPER_POLL_WAKE" [H]. Most older samples and answers use `pollAll`; do not copy them.
3. **`ANativeWindow_setFrameRate` is a hint, never a throttle.** "Usage of this API won't introduce frame rate throttling, or affect other aspects of the application's frame production pipeline" [H]. It only lets the system pick a display mode, which then changes Choreographer callback timing. It has no effect on a window consumed by a codec [H]. So the native `Pacer` stays the only thing that decides fps; this call can move across unchanged from `Surface.setFrameRate`.
4. **Do not add Swappy (Frame Pacing library).** Information only, per the owner's "leave fps alone": Swappy wraps `eglSwapBuffers`, picks a swap interval, in auto mode changes it from measured frame times, and calls `ANativeWindow_setFrameRate` itself through `SwappyGL_setWindow` [KB, R]. That is a second pacer deciding fps beside `core/viz/frame/Pacer` and the thermal rule (ADR 0008). Details in section 1.3.
5. **Playback-audio capture, the `Visualizer` output-mix capture and Media3's `AudioProcessor` have no NDK form.** `AudioPlaybackCaptureConfiguration` goes into a Java `AudioRecord` builder [KB]; there is no AAudio equivalent [M]. These stay Kotlin as hand-over, like `PcmTap`. Only the plain mic (`MicCapture`) can become an AAudio/Oboe input stream.
6. **JNI lookup is by name today (294 `Java_...` functions, no `RegisterNatives`, no `JNI_OnLoad`).** With name lookup a wrong signature is found only "the first time the method is actually invoked"; with `RegisterNatives` from `JNI_OnLoad` "an incorrectly named function will cause an error during library load" [R]. For a port that rewrites hundreds of signatures, switching to a registration table makes every mismatch fail at `System.loadLibrary`, in any instrumented test, instead of in the one screen that calls it. Section 4 gives a no-device check that gets most of this at compile time.
7. **A native thread cannot `FindClass` app classes.** From a thread attached with `AttachCurrentThread`, `FindClass` uses the system class loader and "attempts to find app-specific classes will fail" [R]. Every hand-back from the native render thread to Kotlin (still frame as `Bitmap`, director moment, milk-loaded, error) needs its `jclass`/`jmethodID` cached as global refs in `JNI_OnLoad`, or better, no upcall at all: native queues the event and Kotlin polls or is woken on main. The JNI guide prefers that shape: "Avoid asynchronous communication between code written in a managed programming language and code written in C++ when possible" and "Minimize the number of threads that need to touch or be touched by JNI" [R].
8. **`surfaceDestroyed` must block until native has let go.** "If you have a rendering thread that directly accesses the surface, you must ensure that thread is no longer touching the Surface before returning from this function" [R]. GLSurfaceView did this for the app. The scan's "200 ms teardown latch" becomes a synchronous native call (`geode_stage_surface_destroyed` returns only after `eglMakeCurrent(none)` + `eglDestroySurface` + `ANativeWindow_release`).
9. **The scan's count is of origin/main.** The checkout this session saw (detached HEAD) has 294 `external fun`; origin/main has 370. On the checkout, the 294 Kotlin names and the 294 `Java_dev_geode_engine_bridge_GeodeNative_*` names match one to one (checked with grep, names only, not parameter types).

## 1. Per move: how to do it right

### 1.1 GLSurfaceView + Kotlin Renderer → native render thread owning EGL on an `ANativeWindow`

APIs: `ANativeWindow_fromSurface(env, surface)` (acquires a reference; pair with `ANativeWindow_release`) [H `android/native_window_jni.h`]; `eglGetDisplay`, `eglInitialize`, `eglChooseConfig`, `eglCreateContext` (ES 3), `eglCreateWindowSurface(display, config, window, …)` ("EGLNativeWindowType is a synonym for ANativeWindow") [R arch-egl-opengl]; `eglMakeCurrent`, `eglSwapBuffers`, `eglDestroySurface`. All available at minSdk. `EGL` and `GLESv3` are already linked by `core/CMakeLists.txt:153`.

Rules:
- "Only one EGLSurface can be associated with a surface at a time" (a BufferQueue takes one producer); destroying the EGLSurface disconnects it. "An EGLSurface must be current on only one thread at a time." The context lives in thread-local storage [R https://source.android.com/docs/core/graphics/arch-egl-opengl].
- "only one thread can ever draw into a Surface" [R SurfaceHolder.Callback].
- Two state machines, Activity and Surface. Start order is `onCreate`, `onResume`, `surfaceCreated`, `surfaceChanged`; on back `onPause` then `surfaceDestroyed`. Screen off gives `onPause` only, the surface stays. "You can't initialize in the surfaceCreate() callback because it won't fire again" if the surface was not recreated [R https://source.android.com/docs/core/graphics/arch-sv-glsv]. So: the EGL **context** and all GL objects belong to the render thread's lifetime (kept across surface loss, which is what `setPreserveEGLContextOnPause` gave); only the EGL **surface** follows `surfaceCreated/Destroyed`.
- What GLSurfaceView did that native must now do: create the thread and context, pass messages between threads, clean up on pause [R same page].
- `surfaceChanged` "is always called at least once, after surfaceCreated" [R]: take the size there, not from the view.

Traps:
- Context loss: after `eglSwapBuffers` fails with `EGL_CONTEXT_LOST`, or `EGL_BAD_SURFACE`/`EGL_BAD_NATIVE_WINDOW` when the window died, all GL objects of that context are gone and scenes must be rebuilt [M, EGL 1.4 spec §2.7 "Power Management"; https://registry.khronos.org/EGL/]. The existing `ScenePrep`/`ProgramStager` rebuild-across-frames path is the right recovery.
- Hosts that are not a `SurfaceView`: the wallpaper (`WallpaperService.Engine` hands a `SurfaceHolder` with the same callbacks), the dream, PiP and the `Presentation` on a second display each give their own `Surface`. One native "stage host" keyed by a surface handle serves all [I].
- Compose: host a plain `SurfaceView` in `AndroidView`, or Compose's `AndroidExternalSurface` [M, not checked]. The View stays Kotlin; it only forwards `Surface`, size and destroy.
- `SurfaceHolder.Callback2.surfaceRedrawNeeded` asks for a frame synchronously on rotation/resize [M]; forward it or the first frame after a resize may be stretched.
- Main-thread calls that wait on the render thread (`setUiBlurRegion`, still frame): post a command and return; never block main on GL except in `surfaceDestroyed`.
- Samples: `android/ndk-samples` has `gles3jni`, `hello-gl2`, `native-activity`, `teapots`, `endless-tunnel` (Apache-2.0) [R https://github.com/android/ndk-samples, directory list only]. Note that `gles3jni` and `hello-gl2` still use GLSurfaceView; `native-activity`/`teapots` own EGL natively but through `native_app_glue`, which this app should not adopt (section 2.3).

### 1.2 Kotlin `FramePacer` thread → `AChoreographer`

Header `android/choreographer.h`, reference https://developer.android.com/ndk/reference/group/choreographer [H, R].

| API | Level | Notes |
|---|---|---|
| `AChoreographer_getInstance()` | 24 | "Get the AChoreographer instance for the current thread. This must be called on an ALooper thread." |
| `AChoreographer_postFrameCallback64` | 29 | one-shot; gives `frameTimeNanos` (CLOCK_MONOTONIC) only |
| `AChoreographer_postVsyncCallback` | 33 | one-shot: "to render continuously the callback should itself call AChoreographer_postVsyncCallback". Payload `AChoreographerFrameCallbackData` "does not outlive the callback" |
| `AChoreographerFrameCallbackData_getFrameTimeNanos` | 33 | "should **not** be used to advance animation clocks" |
| `…_getFrameTimelinesLength`, `…_getPreferredFrameTimelineIndex` | 33 | several possible timelines; the preferred one is "the default by which the platform scheduled the app" |
| `…_getFrameTimelineExpectedPresentationTimeNanos(data, i)` | 33 | "This time should be used to advance any animation clocks." |
| `…_getFrameTimelineDeadlineNanos(data, i)` | 33 | when the frame must be ready to make that timeline |
| `…_getFrameTimelineVsyncId(data, i)` | 33 | token for `ASurfaceTransaction_setFrameTimeline` (33); not usable with plain `eglSwapBuffers` [I] |
| `AChoreographer_registerRefreshRateCallback` / `unregister…` | 30 | "thread-safe. Any thread is allowed to register"; passes the new vsync period in ns; from API 31 consistent with `Display.getRefreshRate` |

Vsync callback vs refresh-rate callback: the vsync callback is the per-frame tick (one-shot, re-posted each frame, carries timelines); the refresh-rate callback fires only when the display mode changes and carries the new period. The pacer needs both: tick from the first, "native rate" from the second. Neither sets a rate.

Threading: callbacks arrive on the thread whose looper owns that choreographer instance, i.e. the thread that called `getInstance` [I from "instance for the current thread"; the Java twin is `Choreographer.getInstance()` per-thread]. "Input and sensor events should not be handled in the Choregrapher callback" [R]. Without Choreographer "you may render too quickly for the display, increasing latency" [R].

What `postVsyncCallback` does (information for "leave fps alone"): it paces nothing by itself. It tells the app when to start a frame and offers timelines; an app that picks none "would be presented at the earliest possible timeline" [R]. The existing rule (native `pacerOnVsync` decides whether this tick draws) keeps working: call it from the vsync callback with the expected presentation time, re-post, done.

`ANativeWindow_setFrameRate(window, fps, compatibility)` (30); `ANativeWindow_setFrameRateWithChangeStrategy(…, strategy)` (31); `ANativeWindow_clearFrameRate` (31, inline) [H `android/native_window.h`]:
- Hint only, no throttling (quoted in finding 3). `frameRate` 0 = accept the system's choice. The value "does not need to be a valid refresh rate for this device".
- Non-video content uses `ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT`; `FIXED_SOURCE` "is intended only for video apps" [H, R https://developer.android.com/media/optimize/performance/frame-rate].
- The 30 form equals the 31 form with `ONLY_IF_SEAMLESS` [H].
- "avoid calling setFrameRate() every frame or multiple times per second"; pass the app's preferred rate and keep it, whatever the display does; "there is no guarantee that your app will get the frame rate you request" [R same page].
- Clear (0) "only when the surface remains visible without being used"; not needed on destroy or when the app is hidden [R].
- A recreated surface is a new `ANativeWindow`: set the hint again after each `surfaceCreated` [I].
- When the display runs at a multiple of the app rate, the docs recommend presentation timestamps (`EGL_ANDROID_presentation_time`) to avoid early presents [R].

Traps: one pacer thread serves three hosts today (scan §7.2). A choreographer instance is per thread, so either one native looper thread ticks all sinks, or each render thread has its own instance; pick one and test start/stop races on the host with a fake tick source. Stop = stop re-posting; there is no cancel call for a posted callback [H: none declared], so the callback must check a "stopped" flag and the `data` pointer must outlive one more tick.

### 1.3 Frame Pacing library (Swappy, AGDK), information only

Sources: https://developer.android.com/games/sdk/frame-pacing [R], `kb://android/games/sdk/frame-pacing/opengl/add-functions` [KB].
- What it is: part of AGDK; "helps OpenGL and Vulkan games achieve smooth rendering and correct frame pacing".
- What it does: uses Choreographer ("NDK Choreographer if it is available") for ticks; uses presentation timestamps (`EGL_ANDROID_presentation_time`) so frames are not shown early, and sync fences (`EGL_KHR_fence_sync`) to wait instead of stuffing the buffer queue.
- It replaces every `eglSwapBuffers` with `SwappyGL_swap(display, surface)`; `SwappyGL_init(env, jactivity)` needs a JNIEnv and an Activity object.
- It sets fps: `SwappyGL_setSwapIntervalNS`; auto mode "adjusts the swap duration and pipeline mode based on the average duration of previous frames"; it "avoids [extra updates] by setting the refresh rate to the value supported by the device that's closest to the target frame rate", via `ANativeWindow_setFrameRate` through `SwappyGL_setWindow`.
- Google's own "recommended mode" turns auto swap interval and auto pipeline **off** and sets a fixed interval.
- Verdict: built for a game with one Activity and one window. It needs an Activity (the wallpaper and dream have none), it decides fps, and it duplicates the native `Pacer` + thermal rule. Not for this app. The two techniques inside it (presentation timestamps, fence waits) can be used directly if judder at 90/120 Hz ever needs them.

### 1.4 `SensorManager` gyro → `ASensorManager`

Header `android/sensor.h` [H]; guide: none specific in the KB beyond the samples (`sensor-graph`, `teapots` in ndk-samples) [R list].
- `ASensorManager_getInstanceForPackage(const char* packageName)` (26). `ASensorManager_getInstance()` is deprecated since 26. The package name has to be handed from Kotlin once (debug builds add `.debug`).
- `ASensorManager_getDefaultSensor(manager, ASENSOR_TYPE_GAME_ROTATION_VECTOR)` (type 15) [H].
- `ASensorManager_createEventQueue(manager, looper, ident, callback, data)`: "Creates a new sensor event queue and associate it with a looper." `ident` ≥ 0 for polling, or `ALOOPER_POLL_CALLBACK` with a non-NULL callback [H].
- `ASensorEventQueue_registerSensor(queue, sensor, samplingPeriodUs, maxBatchReportLatencyUs)`: "For sensor streaming, set [latency] to 0" [H]; `ASensorEventQueue_disableSensor`; `ASensorEventQueue_getEvents(queue, events, count)`; `ASensorManager_destroyEventQueue`.
- Threading: events are delivered on the looper the queue was created with. Put it on the render looper and drain in the looper callback, not inside the choreographer callback ("Input and sensors should be handled separately via callbacks registered with AInputQueue and ASensorManager" [R choreographer page]).
- Traps: no NDK API for display rotation (scan already says so): Kotlin hands it over on configuration change. Disable the sensor whenever the surface is not shown (the `gyroListens` rule) or it drains battery. Rates above 200 Hz need the `HIGH_SAMPLING_RATE_SENSORS` permission on API 31+ [M]. Background sensor access stops for idle apps on API 28+ [M], which matches the visibility rule.

### 1.5 Export decode: `MediaExtractor`/`MediaCodec` → `AMediaExtractor`/`AMediaCodec`

Headers `media/NdkMediaExtractor.h`, `media/NdkMediaCodec.h`, `media/NdkMediaFormat.h` [H]. Link `mediandk` (not linked today, scan §8).
- `AMediaExtractor_new`, `AMediaExtractor_setDataSourceFd(ex, fd, offset, length)` (21), `getTrackCount`, `getTrackFormat`, `selectTrack`, `readSampleData`, `getSampleTime`, `getSampleFlags`, `advance`, `seekTo` (all 21); `getSampleSize`, `getFileFormat` (28).
- `AMediaCodec_createDecoderByType` / `createCodecByName`, `configure`, `start`, `dequeueInputBuffer`, `getInputBuffer`, `queueInputBuffer`, `dequeueOutputBuffer`, `getOutputBuffer`, `getOutputFormat`, `releaseOutputBuffer`, `flush`, `stop`, `delete` (all 21).
- Async mode: `AMediaCodec_setAsyncNotifyCallback` (28). "All callbacks are fired on one NDK internal thread. AMediaCodec_setAsyncNotifyCallback should not be called on the callback thread. No heavy duty task should be performed on callback thread." After `flush` in async mode the client must call `start` again [H]. For an offline export the synchronous dequeue loop on the export thread is simpler and has no extra thread; use it [I].
- Content URIs: there is no NDK ContentResolver. Kotlin opens the `AssetFileDescriptor`, passes fd + offset + length (what `AiffPcm` already does). `setDataSourceFd` does not document taking ownership; keep the Kotlin descriptor open until the extractor is deleted, or `dup` it natively [I; the Java `setDataSource(FileDescriptor)` says the caller closes it, M].
- Samples: `native-codec` (AMediaExtractor + AMediaCodec decode to an `ANativeWindow`) and `native-media` in ndk-samples [R list].
- Traps: output format arrives as `AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED` and PCM encoding may be 16-bit or float depending on the key `AMEDIAFORMAT_KEY_PCM_ENCODING` [M]; `AMediaFormat` getters return false when a key is missing, so read rate and channel count from the *output* format, not the track format. The NDK wraps the same codecs as Java, but the scan's golden test (decoded PCM hashes, Java path against NDK path, per container) is still the right gate.

### 1.6 Encode for export, Cast and Record: `MediaCodec` input Surface + `MediaMuxer` → `AMediaCodec` + `AMediaMuxer`

[H] unless marked.
- `AMediaCodec_createEncoderByType`, `configure(codec, format, NULL, NULL, AMEDIACODEC_CONFIGURE_FLAG_ENCODE)`.
- `AMediaCodec_createInputSurface(codec, ANativeWindow** surface)` (26): "can only be called after the codec has been configured … and before AMediaCodec_start() has been called. The application is responsible for releasing the surface by calling ANativeWindow_release()".
- `AMediaCodec_createPersistentInputSurface` + `AMediaCodec_setInputSurface` (26): a surface reusable across codec instances, one codec at a time. Useful for the encoder fallback ladder (`encoder_ladder_test`): the EGL surface survives a codec swap [I].
- `AMediaCodec_signalEndOfInputStream` (26): only valid for an encoder fed by an input surface, in executing state.
- `AMediaCodec_setParameters` (26) for bit-rate changes mid-stream; "Some of these parameter changes may silently fail to apply."
- EGL side: the encoder's `ANativeWindow` becomes an EGL window surface in the render thread's context (shared or same context); config needs `EGL_RECORDABLE_ANDROID` and each frame's PTS is set with `eglPresentationTimeANDROID` before `eglSwapBuffers` [M; extensions `EGL_ANDROID_recordable`, `EGL_ANDROID_presentation_time`, https://registry.khronos.org/EGL/extensions/ANDROID/; the Frame Pacing and frame-rate pages name the second one, R]. `ANativeWindow_setFrameRate` on that window does nothing [H].
- `AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4)` (21), `AMediaMuxer_addTrack(muxer, format)` (returns the track index), `AMediaMuxer_start` ("after addTrack and before writeSampleData"), `AMediaMuxer_writeSampleData(muxer, track, data, &info)` (samples of a track "in chronological order"), `AMediaMuxer_stop`, `AMediaMuxer_delete` (21); `AMediaMuxer_append` (31; fd "must be opened with read and write permission. Does not take ownership of this fd").
- Kotlin keeps: the MediaStore insert with `IS_PENDING`, opening the fd (`"rw"`, MP4 needs a seekable fd [M]), publishing the row, the foreground service. Native gets an fd.
- Traps: `addTrack` needs the format from the encoder's `OUTPUT_FORMAT_CHANGED` (it carries csd-0/csd-1), not the format you configured [M]. Codec-config buffers (`AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG`) must not be written as samples [M]. The muxer is not documented as thread-safe: one writer thread, or a lock, when video and audio drain on different threads [I]. Cast's fragmented-MP4 path (`LiveSegments`) depends on how the Java muxer is configured today; check that the NDK muxer offers the same container options before porting Cast (the NDK `OutputFormat` enum is small) [I, not verified].
- Media3 `Transformer` (`StudioExporter`) has no native form. It is platform glue like ExoPlayer.

### 1.7 Thermal → `AThermal`

Header `android/thermal.h` [H]; guide `kb://android/games/optimize/adpf/thermal` [KB].
- `AThermal_acquireManager` / `AThermal_releaseManager` (30), `AThermal_getCurrentThermalStatus` (30), `AThermal_registerThermalStatusListener(manager, callback, data)` / `unregister…` (30; returns `EINVAL` if already added, `EPERM`, `EPIPE`), `AThermal_getThermalHeadroom(manager, forecastSeconds)` (31), `AThermal_getThermalHeadroomThresholds` (35).
- Headroom: 1.0 = throttling at `ATHERMAL_STATUS_SEVERE`; "Returns NaN if the device does not support this functionality or if this function is called significantly faster than once per second" [H]. The guide is stricter: "You shouldn't call it more than once every 10 seconds" and "Avoid calling from multiple threads" [KB]. The first calls return current headroom while the system gathers samples [H].
- Callback thread: not documented in the header or guide. Treat the callback as arriving on an arbitrary binder thread: only store an atomic and wake the looper [I].
- Trap for today's code: `ThermalGovernor.sample()` runs on the GL thread; if it is per frame it is already over the rate limit. In native, sample on a timer (every 10 s) from one thread and handle NaN as "unknown", never as cool.

### 1.8 Media3 `AudioProcessor` PCM tap → as thin as possible

- `TeeAudioProcessor` (`androidx.media3.exoplayer.audio`, `@UnstableApi`) takes an `AudioBufferSink` with `flush(sampleRateHz, channelCount, encoding)` and a per-buffer callback [S https://developer.android.com/reference/androidx/media3/exoplayer/audio/TeeAudioProcessor]. `AudioProcessor` is a Java interface (`configure`, `queueInput(ByteBuffer)`, `getOutput`, `flush`, `reset`) [S https://developer.android.com/reference/androidx/media3/common/audio/AudioProcessor]. The thread these run on was not confirmed by a source this session; ExoPlayer drives them from its playback thread [M].
- There is no native ExoPlayer; the decode, queue and sink stay Java. So `PcmTap` (55 lines, reads no sample) is already the floor. Keep it at: one JNI call per buffer with the **direct** `ByteBuffer` (`GetDirectBufferAddress`, no copy; "If there's no clear winner, use a direct byte buffer" [R JNI tips]) and one per flush.
- Possible trim: mark the per-buffer native as `@FastNative` and register it with `RegisterNatives` ("strongly recommended to explicitly register"). It must stay short and lock-free: while it runs "garbage collection cannot suspend the thread" [R]. The lock-free ring write qualifies. `@CriticalNative` is out: it cannot take a `ByteBuffer` object [R].
- `NativeDspProcessor`, `PlayoutSink`, `OboeAudioOutput` are the same class of thing: Media3 Java interfaces, one native call per method. They stay; only maths inside them moves.
- Capture: mic → AAudio/Oboe input stream is possible (`AAudio` guide covers input streams; the data callback is a high-priority thread and "should not perform a read or write on the stream that invoked it") [KB `kb://android/ndk/guides/audio/aaudio/aaudio`]. Playback capture (`AudioPlaybackCaptureConfiguration` → `AudioRecord.Builder.setAudioPlaybackCaptureConfig`, needs MediaProjection) is Java only [KB `kb://android/media/platform/av-capture`]; `android.media.audiofx.Visualizer` is Java only [M]. Their pumps stay Kotlin, writing direct buffers into the native ring.

### 1.9 Kotlin value classes mirroring native state → native-owned state with a small typed ABI

No Android page prescribes this; the JNI guide's four rules are the brief [R https://developer.android.com/ndk/guides/jni-tips]: minimise marshalling ("minimizes the amount of data you need to marshall and the frequency"), avoid async across the boundary, few threads touching JNI, and "Keep your interface code in a low number of easily identified C++ and Java source locations … Consider using a JNI auto-generation library as appropriate."
- Shape that follows: native owns the look; Kotlin holds an opaque `Long` handle ("use a long field rather than an int when storing a pointer" [R]) and edits by control id: `set(handle, controlId, float)`, `get`, `snapshotText(handle)`/`loadText` for presets and backup. Reads for the UI in batches (one direct `FloatBuffer` or one string per screen), not one JNI call per slider per frame.
- Handles freed by `Cleaner` (as `SampleRing` does) are fine, but give every handle an explicit `close()` too; a native render thread must never be torn down from the GC's thread [I].
- Bitmap hand-back: there is no NDK call that creates a `Bitmap`, but `AndroidBitmap_lockPixels(env, jbitmap, &addr)` / `unlockPixels` let native write into a Kotlin-allocated one [H `android/bitmap.h`]. So `NativeViz.deliverStill` can shrink to "Kotlin allocates, native fills".
- C++ exceptions "must not be thrown across the JNI transition boundary" [R]: every `*_jni.cpp` entry stays `noexcept` in effect.
- On an attached native thread local references are never freed until detach; delete them by hand. Attached threads "must call DetachCurrentThread() before they exit" [R].

### 1.10 Hand-kept Kotlin copies of native tables → generated or queried

- The repository already has the pattern the scan calls "allowed": native emits a versioned text table (`geode_catalogue_text`, `geode_params_schema_text`, `geode_mod_catalog_text`), a host test pins it against a checked-in copy, Kotlin parses it once. That already satisfies the JNI guide (one call, one string, no per-item marshalling). Extend it to `ReactSource`, `SceneIds`, `ParamKeys`, enum labels (scan step 2). No external tool is needed.
- If compile-time Kotlin constants are wanted (for `when` exhaustiveness on ids), generate a Kotlin file from the same text in a Gradle task or a checked-in file with a CI "regenerate and diff" gate, the way `params_schema_test --write` works. Generated constants still count as Kotlin lines for `tools/engine-kotlin-gate.sh`, so prefer the query [I].
- The 132 `const val` mirrors of `GEODE_*` in `GeodeNative` are the same problem: generate them from `geode_api.h` or check them with a script (section 4).

## 2. Tools

### 2.1 JNI binding generators and checkers

| Tool | What it is | Status and licence | Worth adding here? |
|---|---|---|---|
| `RegisterNatives` from `JNI_OnLoad` (no tool) | Explicit table name + descriptor + function pointer. Official recommendation: "Most apps should call RegisterNatives() from JNI_OnLoad()"; errors at library load; only `JNI_OnLoad` exported; Android Studio can check it [R jni-tips] | Platform | **Yes.** Biggest safety gain for the least machinery. Table can be generated from `javap -s` output (section 4) |
| Version script | Linker file that exports only `JNI_OnLoad`; "more robust form of -fvisibility=hidden" [KB `kb://android/ndk/guides/symbol-visibility`]. NDK CMake already passes `-Wl,--no-undefined-version`, so a script naming a missing symbol fails the link [H `build/cmake/flags.cmake:81`] | Platform | Yes, after RegisterNatives. Smaller `.so`, no accidental exports |
| CheckJNI | Runtime mode that swaps the JNI function tables for checking ones: wrong-thread `JNIEnv`, bad refs, pending exceptions, bad MUTF-8, wrong return types. "If you're using the emulator, CheckJNI is on by default"; on a device `adb shell setprop debug.checkjni 1`; on for `android:debuggable` apps [R jni-tips] | Platform | Already free: the emulator job (`android.yml`, journeys) runs with it on. It is the check for the new native-thread upcalls. It does **not** find a missing or mis-typed native method until it is called |
| `-Xcheck:jni` | The HotSpot/JVM flag of the same idea [M] | JVM only | Not applicable on ART; only relevant if a host JVM ever loaded a host build of the library |
| `javac -h` / `javah` | Generates C headers from Java `native` methods. The Android guide still mentions `javah` [R] | `javah` removed from the JDK; `javac -h` reads Java source only, not Kotlin [M] | No (Kotlin). Use `javap -s` on the compiled `GeodeNative.class` instead: it prints each method's JNI descriptor |
| jni-bind (google/jni-bind) | Header-only C++ metaprogramming layer: "compile time validation of method calls and field accesses" for **C++ calling Java/Kotlin**. C++20 (C++17 under clang). Single header `jni_bind_release.h` can be copied in; Bazel optional [R https://github.com/google/jni-bind] | Apache-2.0; release 1.5.1 referenced; 468 commits; last-commit date not visible on the page | No for now. It solves upcalls (native → Kotlin), which this port should be removing, and its page shows nothing for declaring natives called from Kotlin. Reconsider only if upcalls grow past a handful |
| fbjni (facebookincubator/fbjni) | C++ wrapper classes for JNI refs, exceptions, hybrid objects; React Native's default [S] | Apache-2.0 [S, secondary source]; maintained as part of React Native [S] | No. Heavy, object-model oriented, brings a Java-side library; the ABI here is flat C functions |
| Djinni (Dropbox original; Snapchat fork; cross-language-cpp fork) | IDL → generated C++/Java/ObjC interfaces and marshalling [S] | Dropbox original is unmaintained [M]; Snapchat fork showed activity in March 2026 [S]; Apache-2.0 [M] | No. It wants to own the interface definition and generates Java, not Kotlin `external fun`; migrating 294–370 functions to an IDL is a second refactor |
| SWIG | Generates JNI C and Java proxy classes from C/C++ headers [S] | Active: 4.3.1 on 2025-04-15, a 4.4.x line exists [S]; GPL-3 tool, generated code is unrestricted [M] | No. Generated Java proxies + intermediary class are more Kotlin-side surface, the opposite of the goal |
| JNI Zero (Chromium `third_party/jni_zero`) | Python generator: Java annotations → C++ stubs and a registration table; "zero-overhead" [S https://chromium.googlesource.com/chromium/src/+/main/third_party/jni_zero/README.md] | BSD-3 [M]; lives in Chromium's tree, no standalone release found [S] | No. Tied to GN and Java annotation processing |
| JavaCPP (bytedeco) | Java-annotation-driven binding generator [M] | Not checked | No, same reason as SWIG |
| A repo script (recommended) | ~100 lines: `javap -s -p` on `GeodeNative.class` (or a Kotlin-source regex) → expected C prototypes or a `JNINativeMethod` table; compare with `*_jni.cpp` | Own code | **Yes.** See section 4. Cheaper than any dependency and exact for this codebase's one-object, flat-function shape |

### 2.2 clang-tidy and compiler checks for NDK code

`clang-tidy` ships in the NDK (`toolchains/llvm/prebuilt/linux-x86_64/bin/clang-tidy`) [seen locally] and there is no `.clang-tidy` in the repository yet.
- `android-cloexec-*` (open, fopen, dup, pipe, pipe2, socket, accept4, epoll-create, memfd-create, inotify-init…) and `android-comparison-in-temp-failure-retry` are the Android module [S https://clang.llvm.org/extra/clang-tidy/checks/list.html]. Relevant because the port passes fds across JNI (extractor, muxer, AIFF) and adds an eventfd/pipe for looper wake-ups. Perfetto's `.clang-tidy` (android-cloexec-* + bugprone-* + the retry check) is a real Android-adjacent example [S].
- Generic groups that pay off on a new render thread: `bugprone-*` (use-after-move, dangling handle, unchecked optional), `concurrency-mt-unsafe`, `performance-*`, `clang-analyzer-*`, `cppcoreguidelines-pro-type-reinterpret-cast` on the JNI files [M, names from the same list].
- Clang Thread Safety Analysis (`-Wthread-safety` with `GUARDED_BY`/`REQUIRES` annotations) is the static check for "this state is only touched on the render thread / under this mutex" [M https://clang.llvm.org/docs/ThreadSafetyAnalysis.html]. Worth adopting for the surface state machine and the command queue.
- `-Wunguarded-availability`: the NDK CMake toolchain adds `-Werror=unguarded-availability` when `ANDROID_WEAK_API_DEFS` is on [H `build/cmake/flags.cmake:45-48`]. At minSdk 33 it only matters for the three API 34+ calls listed at the top; without weak defs, calling an API above minSdk is already a compile error (the declaration is hidden) [M].
- Linker: NDK CMake passes `-Wl,--no-undefined`, `-Wl,--fatal-warnings`, `-Wl,--no-undefined-version` by default [H `build/cmake/flags.cmake:81-123`]. Forgetting `mediandk` therefore fails the CI link, not the device.

### 2.3 Native-first app structure: GameActivity / NativeActivity, and when not to use them

- `NativeActivity`: an Activity "implemented purely in native code"; it owns the window and input queue and is a `SurfaceHolder.Callback2` itself [R SurfaceHolder.Callback subclasses list]. No View hierarchy, so no Compose [M].
- `GameActivity` (AGDK, Jetpack): "a direct descendant of NativeActivity"; passes lifecycle, input and text to C/C++ through `native_app_glue`, "which runs mostly in its own native thread … and executes tasks with its looper"; it "renders into a SurfaceView, making it much easier for games to interact with other UI components" and derives from `AppCompatActivity` [KB `kb://android/games/agdk/game-activity/index`].
- Why neither fits here [I]: both make native the owner of the Activity lifecycle and input, for a full-screen game. This app's Activity is a Compose shell (five destinations, sheets, a paywall), and the visual also runs with no Activity at all (wallpaper, dream, a `Presentation`, an offscreen export, Cast). `native_app_glue` assumes one Activity and one window. What is worth taking from GameActivity is only its thread model, which matches finding 1: a native thread with an `ALooper` that receives surface and lifecycle commands from the Java side.
- The fitting structure: Kotlin keeps Activity/Service/View; a `SurfaceView` (or the wallpaper/dream `SurfaceHolder`) forwards `surfaceCreated/Changed/Destroyed`; native owns thread, EGL, pacer, sensors. This is the "SurfaceView + own render thread" pattern the platform docs describe [R arch-sv-glsv].

## 3. Agent skills

### 3.1 `android skills find <keyword>` (the CLI's verb is `find`, not `search`) [ran locally]

| Keyword | Result |
|---|---|
| ndk, jni, opengl | "No skills found" |
| native | `android-profiler` only |
| performance | `android-profiler`, `jetpack-compose-performance`, `r8-analyzer` |
| media | `adaptive`, `media3-cast-integration`, `android-for-cars-parked-apps` |
| graphics | `jetpack-compose-performance` |
| audio | XR glasses skills, cars; nothing on Oboe/AAudio |

Google's `android/skills` (Apache-2.0, 96 commits) has top-level areas build-system, camera, cars, device-ai, devtools, identity, jetpack-compose, media (media3-cast-integration), navigation, performance, play, profilers, security, system, testing, tv, wear, xr; no NDK, JNI, C++, GLES or native-media skill is visible [R https://github.com/android/skills, top level only]. Web search found no published NDK/JNI skill from a first-party source; community "android-native" skills found are Kotlin/Compose ("native" as opposed to cross-platform) [S].

### 3.2 What the repository already has that applies

| Skill | Source | Covers | Use in this port |
|---|---|---|---|
| `geode-native-core` | repo (tracked) | The JNI boundary rule (three places per call: `geode_api.h`, `*_jni.cpp`, `GeodeNative`), GL-thread rules, host tests, clangd check, `tools/hostlink` | Primary. **Needs an update** once the render thread is native: it still says "Rendering runs on the GLSurfaceView GL thread" and has nothing on looper, EGL ownership, RegisterNatives or NDK media |
| `geode-sound-engine` | repo | Playback chain, tap, rings, core/sound, export DSP | For 1.5, 1.8 and the capture pumps |
| `geode-shaders` | repo | GLSL styles, `glslcheck` | Unaffected by the port; context loss recovery touches shader rebuild only |
| `android-profiler` | android/skills | Perfetto traces: jank, frame drops, thread scheduling, native heap | After the move, to compare frame pacing and render-thread scheduling before/after on a device. Needs a device |
| `anr-audit-and-fix` | android/skills | ANR risks in code, ANR dumps | Useful once: main-thread calls that wait on the render thread, and the blocking `surfaceDestroyed` |
| `kotlin-concurrency-and-flow` | chrisbanes/skills | Raw Thread/Executor, scopes | For the Kotlin that remains (capture pumps, polling native events on main) |
| `media3-cast-integration` | android/skills | Media3 Cast setup | Only the Kotlin shell of Cast |
| `r8-analyzer` | android/skills | Keep rules | When `SceneParams` reflection codec goes, its keep rule (`proguard-rules.pro:58`) goes; with RegisterNatives the `GeodeNative` keep rule still has to keep member names |
| `android-cli` | android/skills | `android docs search/fetch` | Used for this research; the KB has the NDK guides |
| `systems-programming:memory-safety-patterns` (plugin) | installed plugin | RAII, ownership in C++ | Generic; for EGL/ANativeWindow/AMedia* RAII wrappers |

### 3.3 Worth installing?

Nothing found is worth installing. No official or credible third-party skill covers NDK render threads, AChoreographer, NDK media or JNI registration. The better spend is a short addition to `geode-native-core` (or a sibling `geode-ndk-platform` skill) holding sections 1 and 4 of this file as rules: looper thread, surface state machine, API-level table, the JNI check script. That is a repository change and is left to the owner.

## 4. Verification without a device build (what CI can run)

Agents may not build; CI can. In rising cost:

1. **Name check, no build (seconds, could run in `arch-map.yml`).** Every `external fun X` in `GeodeNative.kt` has exactly one `Java_dev_geode_engine_bridge_GeodeNative_X` in `app/src/main/cpp/*_jni.cpp` and the reverse (JNI mangling: `_` in a name becomes `_1`). A grep version of this ran clean on the checkout (294/294). It catches a missing or orphaned function, not a wrong parameter list.
2. **Signature check by generated header (needs the Kotlin compile CI already does).** After `:engine:audio-core:compileKotlin`, run `javap -s -p` on `GeodeNative.class`; it prints each native method with its JNI descriptor. A script turns each into a C prototype (`(JLjava/nio/ByteBuffer;I)V` → `void Java_…(JNIEnv*, jobject, jlong, jobject, jint)`) in a generated header that every `*_jni.cpp` includes. Functions with C linkage cannot be overloaded, so a definition whose parameters differ from the generated declaration is a **compile error** in the NDK build [I, standard C++ rule]. This is `javah` rebuilt for Kotlin and needs no new dependency (`javap` is in the JDK). Types that collapse (`jobject` for every reference type, `jlong` for handles) are the limit of what it sees.
3. **Same data, as a registration table.** Generate `JNINativeMethod[]` from the same `javap` output and call `RegisterNatives` in `JNI_OnLoad`. Then the runtime also verifies name and descriptor against the class at load: a mismatch fails `System.loadLibrary` on the first instrumented test (emulator job), not on first use [R jni-tips]. With a version script exporting only `JNI_OnLoad`, `--no-undefined-version` fails the link if the script and the code disagree [H, KB].
4. **Link-time, already in CI.** `:app:assembleDebug` links `libgeode.so` with `-Wl,--no-undefined -Wl,--fatal-warnings` [H]: a call to `AMediaCodec_*` without `mediandk`, or to any undefined function, fails the PR. Nothing to add except the libraries in `target_link_libraries` (`mediandk`; `android` is already there and carries `AChoreographer`, `ASensorManager`, `ALooper`, `ANativeWindow`, `AThermal` [M: all in `libandroid.so`]).
5. **Post-build symbol audit (seconds, after assembleDebug).** `llvm-nm -D --defined-only libgeode.so` must contain every expected `Java_…` (or only `JNI_OnLoad` after step 3); `llvm-readelf --needed-libs` must list only NDK-public libraries; `llvm-nm -D --undefined-only` must show no symbol outside them. Tools are in the NDK `bin/` [seen locally]. `android analyze` on the APK may give the same listing [not tried].
6. **Host syntax check of the JNI files (no NDK).** `clang++ -std=c++20 -fsyntax-only -I$JAVA_HOME/include -I$JAVA_HOME/include/linux -Icore app/src/main/cpp/*_jni.cpp` with the generated header: the JDK's `jni.h` has the same types. `tools/hostlink` today covers `core/viz`, `core/sound`, `core/util` and says itself that it does not cover `api/` or the JNI files; this closes that gap. New NDK headers (`choreographer.h`, `sensor.h`, `NdkMediaCodec.h`, `thermal.h`, `native_window.h`) need stubs in `tools/clangd-stubs/android` and hostlink's stub list, or keep every NDK call behind one small platform file per subsystem (as `OboeOutput.cpp` is "the one file that includes Oboe") so the rest stays host-compilable and host-testable.
7. **Constants.** A script comparing the 132 `const val` in `GeodeNative` with the `GEODE_*` values in `core/api/geode_api.h` (regex on both) belongs in the same gate.
8. **Design for host tests.** Put the surface state machine, the pacer tick logic, the encoder drain loop and the sensor-to-look maths behind interfaces with a fake `Window`, `TickSource`, `Codec`; test create/change/destroy/re-create in every order on the host (scan §7.1 asks for exactly this). The platform files then hold only calls.
9. **Runtime, emulator job.** CheckJNI is on by default on the emulator [R]: it aborts on a `JNIEnv` used from the wrong thread, a local ref leaked across threads, a pending exception, a wrong return type. It is the only check here for native-thread upcalls. `GL` on the emulator is not the device GPU, so EGL lifecycle (PiP, rotation, second display, wallpaper preview, lock screen) stays a device checklist.
10. **clang-tidy in CI.** A `.clang-tidy` with `android-cloexec-*`, `bugprone-*`, `concurrency-*`, `clang-analyzer-*` over `core/` and `app/src/main/cpp`, driven by the `compile_commands.json` the CMake build emits; start as warnings on changed files.

## Sources opened this session

- https://developer.android.com/ndk/reference/group/choreographer [R]
- https://developer.android.com/ndk/guides/jni-tips [R]
- https://developer.android.com/games/sdk/frame-pacing [R]
- https://developer.android.com/media/optimize/performance/frame-rate [R]
- https://developer.android.com/reference/android/view/SurfaceHolder.Callback [R]
- https://source.android.com/docs/core/graphics/arch-sv-glsv [R]
- https://source.android.com/docs/core/graphics/arch-egl-opengl [R]
- https://github.com/android/ndk-samples , https://github.com/android/skills , https://github.com/google/jni-bind [R, landing pages]
- `kb://android/ndk/guides/symbol-visibility`, `kb://android/games/agdk/game-activity/index`, `kb://android/games/optimize/adpf/thermal`, `kb://android/games/develop/gameloops`, `kb://android/games/sdk/frame-pacing/opengl/add-functions`, `kb://android/games/sdk/frame-pacing/opengl/update-build-settings`, `kb://android/media/platform/av-capture`, `kb://android/ndk/guides/audio/aaudio/aaudio` [KB; the first four read in part, the rest by keyword lines]
- NDK r28 headers: `android/choreographer.h`, `native_window.h`, `native_window_jni.h`, `sensor.h`, `looper.h`, `thermal.h`, `bitmap.h`, `surface_control.h`, `media/NdkMediaCodec.h`, `NdkMediaExtractor.h`, `NdkMediaMuxer.h`, `NdkMediaFormat.h`; NDK `build/cmake/flags.cmake` [H]
- Search snippets only [S]: NDK thermal and media reference pages, Media3 `TeeAudioProcessor`/`AudioProcessor` reference, clang-tidy check list, fbjni, Djinni, SWIG news, JNI Zero README.

## Not verified

- Which thread Media3 calls `AudioProcessor`/`AudioBufferSink` on, and which thread `AThermal` status callbacks arrive on.
- The EGL extension texts (`EGL_ANDROID_recordable`, `EGL_ANDROID_presentation_time`) and the context-loss error codes: from memory.
- Whether `AMediaMuxer` can produce the fragmented MP4 Cast's HLS path needs.
- Compose `AndroidExternalSurface` as the host view.
- Maintenance dates and licences of fbjni, Djinni, SWIG, JNI Zero: snippets or memory; the GitHub API was not reachable for other repositories in this session.
- Nothing here was compiled or run on a device.
