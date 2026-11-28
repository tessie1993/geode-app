# BeatRealms 3D Functionality Map

## Product shape

BeatRealms 3D is an offline, no-account music player, real-time GPU music visualizer, and video studio. It uses a single Activity/Compose shell over one retained native `Stage`, with process-wide playback, export, and Cast services.

The GameActivity rebuild must preserve product behavior and process-wide ownership, not merely replace the Activity superclass.

## Capability map

| Capability | Current implementation | Host/surface/lifecycle dependency |
|---|---|---|
| First run, safety, setup, tutorial | Splash waits for settings; safety acknowledgement and setup gate the shell; tutorial progress persists and can replay. | Compose root, persisted preferences, saved navigation, lifecycle-aware overlays. |
| Player, library, search, queue | ExoPlayer/`PlaybackSession`, PCM tap/rings, DSP, history, resumption, sleep timer, crossfade, ReplayGain, native search, playlists/favourites/recent. | Process-wide player; MediaStore; audio focus/wake lock; service hold counts. |
| Microphone and other-app audio | `CaptureController`, microphone/Oboe and playback-capture paths, consent and permission handling. | Runtime permissions, foreground capture service, shared analysis ring. |
| Live visual host | `rememberLiveStage` retains one application-context `Stage`; one surface is hosted below all screens; `StageRunEffect` controls drawing. | Surface create/change/destroy, EGL/Swappy, buffer sizing, refresh/display changes, lifecycle. |
| Styles, customize, presets, shaders | Style catalogue, categorized controls, presets/folders/import/templates, custom shader overrides, Auto director. | Native look snapshots, live Stage, shader compile/cache, entitlement. |
| Background underlays | Persistable URI grant, debounced decode/blur/dim, re-decode on surface-size change, RGBA push to live/export. | SAF/MediaStore grants, actual render pixels, GL upload. |
| Gestures, tilt, remote | Compose gesture map plus native touch field; game-rotation sensor on a dedicated thread; TalkBack actions; TV D-pad focus. | Compose input overlay, native recognizer, sensor lifecycle, display rotation, focus. |
| Record and stills | Stage frame tee + audio, entitlement/watermark, save flow; shake-to-screenshot and native still readback. | Valid live surface/size, MediaCodec, storage, sensors, Pro gate. |
| Studio and export | Single/batch/loop renders, 720p–4K, 24–60 FPS, H.264/HEVC, aspect templates, MediaStore/SAF output, My Videos. | Foreground service, native GL/MediaCodec workers, wake lock, process-wide cancellation; independent of Activity lifetime. |
| Settings and performance | Playback/EQ/output/library/visual/sound/safety/displays/gestures/look/performance/export/preferences/help. Live FPS is being fixed to 30; quality remains user-controlled. | Preferences, DisplayManager, window/configuration changes, native Perf state, surface buffer. |
| Google Cast | Default Media Receiver, Pro gate, CastService, playlist transfer, receiver health, playback/wake/Wi-Fi locks. | MediaRouter/Cast SDK, foreground service, LAN, entitlement, session lifecycle. |
| Browser/computer link | Local WebRTC stream, QR/share/pair URL, independent TV/link targets. | LAN/WebRTC server, CastService, pair state, entitlement. |
| Second display | Same surface View moves into a `Presentation`; retry/failure/removal handling. | DisplayManager, Presentation context/window, explicit restore on every exit path. |
| PiP | Source rect/aspect, auto-enter eligibility, play/pause/next remote actions, chrome suppression. | Activity PiP params/callbacks, continued rendering while paused, playback state. |
| Playback service, notification, Android Auto, TV | MediaLibraryService/MediaLibrarySession, trusted controller checks, media-button receiver, Leanback launcher, TV focus/overscan. | Exported service/manifest, Media3 notification, TV input focus. |
| Widget and Quick Settings tile | Glance widget metadata/artwork/progress/actions; tile controls playback or launches app. | SessionToken/MediaController, service pending intents, lock-screen state. |
| Deep links, opened audio, voice search | Preset/template confirmation, content URI validation/grant checks, one-shot intent consumption, voice query. | `onCreate` vs `onNewIntent`, singleTask, URI grants, navigation. |
| Pro, billing, paywall | Free/Trial/Pro entitlement, signed purchase cache, one-hour trial, feature gates, purchase/restore/pending flows. | Foreground Activity for billing UI, persistent entitlement/paywall state. |
| Themes, motion, accessibility | Always-dark Crystal/Material theme, visual accent, reduced motion, font scale, TalkBack semantics, contrast, TV focus. | Compose CompositionLocals, system settings, edge-to-edge, semantics. |
| Wallpaper/dream | No current `WallpaperService` or `DreamService`; only retired preference cleanup and stale store copy. | Not implemented; requires separate feature/surface design. |

## Non-negotiable preservation scenarios

1. Cold first run and tutorial.
2. Rotation, fold, split-screen, free-form resize, and rapid resize.
3. Background/foreground and Activity recreation without player/export/Cast restart.
4. PiP enter/exit with continued visual/audio and hidden chrome.
5. Second-display connect/remove/failure with the same surface restored.
6. Service-only playback and media controls.
7. Export continuing through Activity recreation and cancellation.
8. Cast TV disconnect while browser link remains active.
9. Opened-audio, shared-link, and voice-search intents without duplicate playback/import.
10. Font scale, TalkBack, reduced motion, contrast, and TV D-pad behavior.

## Ownership rules

- `PlaybackEngine`, `PlayerSession`, `Stage`, `ExportRun`, and Cast targets remain process-wide.
- `GeodeGameActivity : GameActivity` is the required Activity subclass; direct GameActivity native callbacks own `ANativeWindow` lifecycle.
- StageThread remains the one native game/render loop, with Swappy as its only pacer. `native_app_glue`/`android_main` are optional and are not part of this design.
- `StageSurfaceController` owns View layout, alpha, display, buffer sizing, sensors, blur, and performance; it does not duplicate native window callbacks.
- Compose owns shell UI, navigation, dialogs, semantics, focus, IME, and shell input.
- Audio remains a media/music output path. The existing Oboe stream uses Media/Music usage, Shared/None mode, exact-format matching, and Medium sample-rate conversion; AGDK's game-oriented LowLatency/Exclusive/Game defaults are not applied blindly to this music product.
- The same native surface moves into `Presentation`; no second live renderer is created.
- Surface-dependent work waits for actual surface readiness and nonzero dimensions.
- One-shot intents are consumed exactly once.
- No wallpaper/dream behavior is claimed or migrated because it is absent at `origin/main`.
