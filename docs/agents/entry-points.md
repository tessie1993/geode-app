# Entry points (`app/src/main/AndroidManifest.xml`)

One fact per line. Add an entry point or an intent the app takes as a new bullet.

## Application and activities

- `GeodeApp`: `@HiltAndroidApp`. It installs the crash handler, and its lazy `GeodeContainer` sweeps leftover render scratch and pending MediaStore rows.
- `ui.MainActivity`: the only `@AndroidEntryPoint`, the launcher, and PiP of Visuals; the only other activities are `ui.wallpaper.WallpaperOptionsActivity` and `tile.TileLongPressActivity`.
- `ui.MainActivity` is also the Android TV launcher (`LEANBACK_LAUNCHER` with `drawable/tv_banner`); PiP is never auto-entered on a TV.
- `ui.MainActivity` handles `geode://preset`, `geode://template`, a `.milk` another app opens and `MEDIA_PLAY_FROM_SEARCH`, then calls `AppRoot()` in `ui/AppShell.kt`.
- A shared preset, template or `.milk` is imported only after the user's yes in `SharedLinkConfirm`; a `.milk` never goes over an existing preset.
- `ui.MainActivity` sets `requestedOrientation` from `GeneralSettings.screenRotation` live (no recreation: `configChanges`); Android ignores it in multi-window and on screens 600 dp and wider.
- `ui.wallpaper.WallpaperOptionsActivity`: the wallpaper picker's Settings button (`android:settingsActivity` in `res/xml/wallpaper.xml`), a plain `ComponentActivity` outside Hilt.
- `tile.TileLongPressActivity` sits beside the tile and answers the tile's long press by opening the app.

## Opening an audio file

- "Open with" for an audio file (`ACTION_VIEW`, `content` only, `audio/*`, `application/ogg`, `application/x-flac`) goes to `ui/OpenedAudioIntent.receiveAudioFile`, ahead of the `.milk` handler in `MainActivity`; it reads only the uri and type.
- `data/OpenedAudioSource` refuses the app's own providers and an ungranted uri, off main.
- An opened file plays as a one-item queue (`QueueController.playOpened`) under a `model/OpenedAudio` id that `PlayRecorder` keeps out of history and resumption.
- An opened file is never copied and its grant is not persisted.

## Services

- `playback.PlaybackService`: exported Media3 library service (notification, Bluetooth, Assistant; no Android Auto declaration in 1.0). Media3's `MediaButtonReceiver` handles resumption.
- `playback.PlaybackService`: exported Media3 library service, also used by Android Auto. Media3's `MediaButtonReceiver` handles resumption.
- `audio.PlaybackCaptureService`: MediaProjection foreground service.
- `audio.GeodeNotificationListener` (in `NowPlayingBridge.kt`): lets the app name what another app is playing.
- `export.ExportService`: keeps the process in the foreground during a render. It does not own the render; `ExportRun.scope` does.
- `wallpaper.VisualizerWallpaperService` and `wallpaper.VisualizerDreamService`.
- `cast.CastService`: foreground service that keeps a cast streaming in the background; started by `CastController` when a Cast session starts.
- `cast.CastService` is typed `mediaPlayback`, plus `microphone` while Live input is on, taken while the app is visible so the mic keeps driving the TV in the background (`CaptureController.onBackground` keeps it then).
- `tile.PlaybackTileService`: the Quick Settings tile (`BIND_QUICK_SETTINGS_TILE`).

## Receivers and providers

- `widget.NowPlayingWidgetReceiver` (in `NowPlayingWidget.kt`).
- Media3's `MediaTransferReceiver` (declared) lets the system output switcher start a cast.
- `FileProvider` `${applicationId}.presets`, for sharing presets.
