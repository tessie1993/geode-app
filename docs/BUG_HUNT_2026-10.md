# Bug hunt and architecture review, October 2026

Branch `claude/arch-bug-hunt-3a10cc`, cut from `claude/geode-finish` at `76b4fb09` (2026-10-31).

## How this was done

Five read-only passes went over the app, one per area: playback and capture, export, UI state and
stores, Pro/billing/wallpaper/backup/manifest, and the GL render path with the JNI boundary. Each
pass traced its findings from caller to root cause. The top findings were then re-read in the code
before anything was fixed. Nothing here was run on a device; CI is the build. Areas that other open
PRs were rewriting at the time (sound engine wiring, Customize macros, the nav overhaul, the shader
vortex, MilkDrop preset choice) got less attention.

Ranking is by what a Play user would hit: crash or data loss first, then broken features, then
battery and jank.

## Fixed on this branch

| # | Severity | Where | Failure | Fix |
|---|---|---|---|---|
| 1 | High | `app/src/main/cpp/geode_viz_jni.cpp` `vizTakeMilkPresetLoaded` | The JNI asked for the length first, and the take emptied the slot, so Kotlin always got `""`. The shown MilkDrop preset was lost on Visuals reattach, on relaunch (`milk_path=""`), in the wallpaper and dream, and in every MilkDrop export. | One take into a PATH_MAX buffer. **Lands in PR #178** (MilkDrop session), not on this branch. |
| 2 | High | `data/TemplateFormat.kt` `decode`, `data/PresetStore.kt` `fromJson` | A `geode://template` or preset link carrying `1e39` or `"NaN"` decoded to a non-finite float that `JSONObject.put` later refused. The template link crashed `MainActivity.onCreate`, and relaunching from Recents crashed again. The preset link crashed after confirming the import. | `FiniteJson` reads non-finite numbers as the fallback; `readTemplate` failures return `Malformed`. Test: `SharedLinkNumbersTest`. |
| 3 | High | `billing/EntitlementStore.kt` | Cached Pro expired 30 days after the last in-app Play query. Wallpaper and screensaver buyers who never open the app lost Pro: the wallpaper froze and the dream exited. The next app open also switched their locked style to a free one before Play answered. | The cache vouches until a successful query no longer lists the purchase. Revocation is unchanged. `docs/play/BILLING.md` updated. |
| 4 | High | `export/VideoExporter.kt` | Long 4K/1080p renders passed MediaMuxer's 4 GB limit, finished as "Saved", and stopped playing at 4 GB. | `bitRateUnderMuxerLimit` lowers the bit rate to fit. Test: `MuxerLimitTest`. |
| 5 | Med | `audio/dsp/NativeDspProcessor.kt` | 5.1 FLAC/WAV/AAC: `onConfigure` threw for more than 2 channels, the AudioTrack failed and the track was skipped. | The processor steps aside (`NOT_SET`) for formats it can't handle. |
| 6 | Med | `playback/PlaybackEngine.kt` | A session the service starts alone (a headset's play button after process death, Android Auto) ran on Media3 defaults: no pause on unplug (music went to the speaker), no ReplayGain, shuffle and repeat lost. | `PlaybackSession` applies the player-level prefs at construction through `applyPlayerPrefs`, the same function Settings uses. |
| 7 | Med | `export/ExportService.kt` | The foreground notification was rebuilt for every rendered frame: two binder calls on Main, about 30 a second, during every render. | Rounded to whole percent and `distinctUntilChanged`. |
| 8 | Med | `ui/ExportController.kt` | Studio and project exports ran without `ExportService` and outside `ExportRun`'s guard. They died once the app was backgrounded, and a visualizer render could start beside one. | `runStudioExport` takes the guard and the service. |
| 9 | Med | `ui/RenderScopePicker.kt` | Batch renders write straight to MediaStore, which needs a storage permission below Android 10, so every batch failed on API 26–28. | Batch scopes are hidden below Q. |
| 10 | Med | `ui/DuplicatesTab.kt` | Header keys were `title\|artist`, which two groups can share (different lengths), so the tab crashed with "Key was already used". | Keyed by the group's first URI. |
| 11 | Med | `ui/LibraryScreen.kt` | The grant was read once; after "Open settings" and a grant there, the Library stayed empty until a restart. | Re-read on resume. |
| 12 | Med | `ui/CaptureController.kt` | The mic-reactive restore read `GuiPrefs` before the store had loaded them, so it never fired on a cold start. (The same race hit beat sensitivity, which went away with the old analysis path in #180.) | Waits for `loadedGuiPrefs()`. |
| 13 | Low+ | `GeodeContainer.kt` | The orphaned `IS_PENDING` sweep queried without opting into pending rows, so it matched nothing. | `QUERY_ARG_MATCH_PENDING` on R+, `setIncludePending` on Q. |
| 14 | Low+ | `engine/scenes/.../VisualizerView.kt` | Detaching reports GONE while `windowVisibility` still reads VISIBLE, so "Tilt phone to look" kept the rotation sensor on after leaving Visuals. | `gyroLook.setActive(false)` on detach. |
| 15 | High (opt-in) | `playback/NativePlayer.kt` and `core/audio/player` | The native engine (a Settings toggle, off by default): "Play next" and queue edits played the wrong track (pre-roll keyed by index); no audio focus or becoming-noisy; the queue stalled when gapless was off; a use-after-free on release. | **Removed at the user's request.** ExoPlayer only. The crossfade and gapless settings went with it; bit-perfect USB (Android 14+) now applies to ExoPlayer. Not checked on a USB DAC. |

## Still open

| Severity | Where | Failure | Suggested fix |
|---|---|---|---|
| Med | `export/VideoExporter.kt`, `export/LoopRender.kt` `negotiate` | No size fallback: on an encoder that tops out at 1080p, 4K or 21:9 FHD fails with a raw `CodecException`, although the UI string promises an automatic fallback. | Add 1080p and 720p attempts, or clamp to `VideoCapabilities.isSizeSupported`. |
| Med-Low | `billing/PlayBillingRepository.kt` `handle` | `onPurchasesUpdated(OK)` with an empty or Pro-less list leaves the status `InProgress`, so Buy, Restore and Dismiss stay dead until a restart. | Fall back to `queryOwned()`, or let `clearPurchaseStatus` reset `InProgress`. |
| Low-Med | `wallpaper/VisualizerWallpaperService.kt` | No `preserveEGLContextOnPause`: every screen-on rebuilds every program, FBO and scene, projectM included. Left alone because PR #178 edits this file. | One line in the `WallpaperGlSurfaceView.apply {}` block. |
| Low | `core/viz/Renderer.cpp` `onSurfaceCreated` | After a context loss, `TrailPass`/`Transition` `create()` delete the old context's ids after `overlays_.recreate()` has allocated new ones, and can hit live objects. Worst on the wallpaper (see the row above). | `forget()` old ids instead of `glDelete*` on a new context. |
| Low | `VisualizerView.kt`, `VisualizerWallpaperService.kt` | Teardown waits 200 ms for the GL thread; a slow frame drops the destroy and leaks the whole native renderer. | Wait longer, or destroy on the next `onSurfaceCreated`. |
| Low | `OffscreenSceneRenderer.prepare` | `check(viz.setScene(...))` throws before `native = viz`, leaking a renderer for every export of an unknown style id. | Assign `native` right after create. |
| Low | `ui/ExportController.kt` `cancelExport` | Cancel only sets a flag; the analysis phase runs on for a minute or more on a long mix. | Also `exportJob?.cancel()`. |
| Low | `export/StudioExporter.publish`, `VideoExporter.kt:214`, `LoopExtend.write`, `LoopRender.renderStops` | Some failure paths (IOException on copy, `openFileDescriptor` throwing, a non-listed RuntimeException) skip deleting the pending row or the scratch reel. The sweep (now working) catches them at the next launch. | Catch-all cleanup. |
| Low | `playback/ReplayGain.kt` | A track change applies 0 dB until the tag read finishes, so a −8 dB track starts loud. | Keep the previous gain while pending. |
| Low | `playback/PlaybackService.ensureRunning` | `startForegroundService` without Media3 promoting the service: play-then-pause in a fresh process can throw `ForegroundServiceDidNotStartInTimeException`. | Connect a `MediaController` instead. |
| Low | `widget/WidgetPublisher.kt` | After a reboot or process death the widget still shows "playing". | Clear the saved state in `GeodeApp.onCreate` when no session runs. |
| Low | `data/backup/BackupRepository.kt` | Between a restore and the Restart tap, a running session can overwrite restored files. | Pause and restart right after the restore. |
| Low | `MusicLibraryController.refreshNumericTitles` | Runs before the library has loaded, so it never repairs anything. | Run it at the end of `refresh()`. |
| Low | `ui/studio/TimelineLanes.kt` SRT import | Reads the picked file on Main (cloud provider → ANR risk). | Read on IO. |
| Low | `ui/StudioScreen.kt` | The picked clip URI gets no persistable grant, so it is unreadable after process death. | `takePersistableUriPermission`. |
| Low | `UserDataRepository.setGuiPrefs` | Each slider frame writes on the parallel IO pool; writes can land out of order. | Serialise the writes. |
| Low | `billing/PurchaseVerifier.kt`, `release.yml` | An unparseable `PLAY_LICENSE_KEY` silently accepts every signature. | Fail the release build when the key doesn't parse. |
| Note | `res/xml/*backup*` | With no cache expiry, an entitlement restored by Auto Backup onto another account holds until the first successful Play query (`docs/play/RELEASE_READINESS.md` N3). | Exclude `geode-pro` from backup if that matters more than the offline case. |

## Architecture

The guide's debt list in `AGENTS.md` still holds. Nothing found here needs a structural change before
release. What the bugs say about the structure:

- **Async state read synchronously.** Fixes 3, 6 and 12 share one shape: a value loaded on IO (or
  owned by a surface that may not exist) read as if it were already there. The `PlayerSession`
  constructor is where this bites. When the god object is split (finish plan step 8), give each
  controller a `loaded` gate rather than a `StateFlow` that starts at defaults.
- **Two-call native getters.** Fix 1 was a consuming getter called through the length-then-copy
  pattern. `vizCustomShader` and `vizSceneIds` use the same pattern safely because they don't
  consume. Any new "take" in `geode_api.h` should hand back the value in one call.
- **Untrusted text through `org.json`.** Fix 2. Links and imported files are a trust boundary;
  `FiniteJson` covers presets, and `TemplateFormat` now treats any read failure as malformed. The
  golden-file tests in the finish plan (step 3) are the place to grow this.
- **Services outside the UI graph.** The wallpaper, dream, widget and a headless playback session
  each re-derive state the UI owns (fixes 3 and 6, the open widget item). `ProSnapshot` and
  `applyPlayerPrefs` are the seams to keep: one function both sides call.
