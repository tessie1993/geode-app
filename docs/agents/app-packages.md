# `:app` packages (`app/src/main/java/dev/geode/`)

One fact per line. Each package has an "Open first" line and then its facts. Add a fact as a new bullet.

## (root)

- Open first: `GeodeApp`, `GeodeContainer`, `RingLog`, `AppLaunch`.
- `Application`, the crash file, and the `GeodeContainer` service locator.
- The app's launch intent is `appLaunchIntent`, for the widget and notifications.

## `di`

- Open first: `DataModule`, `BillingModule`, `PlayerSessionProvider`.
- Hilt modules. Very thin: most objects are built by hand.

## `pro`

- Open first: `FreeTier` (edit to tune the free tier), `FeatureGate`, `GetFeatureGateUseCase`, `BillingRepository`, `PaywallNavigator`, `AutoDirectorPolicy`.
- Pure Kotlin (no `android.*`).
- Holds the Pro entitlement rules, the free tier, the billing and trial interfaces, the one gate use case, the paywall request, and the auto-director's taste rule.

## `billing`

- Open first: `PlayBillingRepository`, `FakeBillingRepository`, `ProSnapshot`.
- Play Billing (PBL 9) behind `BillingRepository`.
- The fake for debug and tests, the entitlement cache, the trial store, the purchase signature check.

## `ui`

- Open first: `MainActivity`, `AppShell`, `ShellChrome`, `GeodeAppState`, `PlayerSession`, `StudioHub`, `ExportHost`, `*ViewModel`.
- Every screen, the eight ViewModels, settings screens, theme glue.
- `PlayerSession` builds the 15 `*Controller`s.

### Settings

- Settings (`AppSettingsTab`) is built in Customize's pattern.
- It is a list of ten groups (`SettingsGroupKey`: icon, name, one-line summary).
- There is no Auto group; Visuals' quick settings own Auto.
- Each group is a page with ← the title and ‹ › to the next group.
- Basics are always open on top; `SettingsSectionKey` sections drop open below, several at once.
- Settings pages are at most 600 dp wide.
- `SettingsComponents` holds rows, Basics and section cards.
- The rows of each group stay in its `*Settings.kt`; Behaviour's are in `BehaviorSettings`.

### Customize

- Customize is `CustomizePanel` over `CustomizeBrowser`.
- It is a list of nine categories (`CustomizeCategory`): Quick knobs, Look, Shape, Motion, Camera, Music (with the reaction, envelope and wave editors), Effects (with transitions), Liquid, Expert.
- Each category opens on its free Basics, with Pro sections that drop open below (several at once, kept by name).
- ‹ › step to the neighbour category.
- Which control is where, what is free, which styles show what, and each category's Randomize and Reset are native (`core/viz/CustomizeMap`).
- The waves' own Randomize, which Music's roll and randomize-everything run for the waves already on, is `core/viz/WaveRandomizer` (`geode_waves_randomize`).
- A kept category is native too: its ⋮ › Keep when randomizing stores `#CATEGORY` in the lock set, which `expandLocks` turns into its controls, after MilkDrop's locked mash-up bins.
- ⋮ › Take from a preset is native (`customize::borrow`, `geode_customize_borrow`): one category of a saved look laid over this one.
- `CustomizeMapTable` splits the map's rows.
- `CustomizeControls` draws each control by name.
- Widgets are in `CustomizeTabs`, editors in `CustomizeAdvanced`, knobs in `CustomizeMacros`, Music's moments in `CustomizeEvents`, list maths in `MusicMoments`.
- Expert › Shader code (`ShaderCodePage`) shows on Fluid only, with the custom force and dye editors.
- Free users get every Basics (`pro/FreeTier`).
- A Pro control is gated through `ProLock`: it can be tried, then springs back.

### Visuals, Studio, Library

- `VisualGestures` reads the visual's gestures.
- `MacrosDrawer` is the knobs a swipe down pulls.
- `VisualsSettingsPage` (`VisualsQuickSettings.kt`) is Visuals › Settings, the page the ⚙ at the top of the visual opens (no gesture).
- Visuals › Settings holds Auto and its reach, the style timer and its sources, the visual playlist, sheet glass and mini player fade, writing the same stores as Settings.
- Studio (`StudioHub`: Render and My videos (`MyVideosTab`)) owns export, loop render and export defaults.
- Studio has no editor: the owner dropped video editing (#227).
- A stopped take (`TakeController`, Pro) renders to MP4 in the background through `ExportController.renderProject`.
- A take also stops by itself at Settings › Export › Record's maximum length (`ui/RecordSettings`).
- A take's first stop asks where to save (`RecordSaveSheet`).
- `EditorController` only keeps the project list My videos reads (takes and kept renders).
- Library (`LibraryScreen`) draws `LibraryViewModel.uiState`.
- Library's debounced query (150 ms) runs search, ranking and the album, artist and folder grouping on `Dispatchers.Default`.
- The grouping is `LibraryBrowse.browse` over a `LibraryIndex` folded once per library change.
- `SearchMatcher` does accent and case folding and the title › artist › album tiers.
- Duplicates are found on `Dispatchers.Default` once per library change.
- A granted library MediaStore answered empty shows `LibraryEmpty` (Rescan, Settings › Library, listen to another app or the mic).
- Settings › Audio and the player card say when a running capture hears only silence (nothing plays, or the playing app blocks capture).

## `ui/tour`

- Open first: `TourViewModel`, `TourHost`, `TourTargets`, `TourOverlay`.
- `TourViewModel` is the one state holder: a `StateFlow<TourUiState>`, events as state handed back once.
- Tour progress is kept in `GuiPrefs.tourProgress`.
- The tour's rules are pure, in `model/Tour`.
- `TourHost` draws the tour from `AppRoot` and reports the marked controls on screen, the screen and whether music plays.
- `Modifier.tourTarget` registers a control with the `TourTargets` registry (screen pixels).
- `TourPointer` and `TourHint` are the spotlight dialog and glass card.
- `TourWords` holds each step's words.
- `LocalTourNudge` is what a control used the long way round calls, so its gesture is said once.

## `ui/pro`

- Open first: `PaywallViewModel`, `PaywallScreen`, `ProComponents`.
- Self-contained paywall (own palette, no theme APIs) and its ViewModel.
- The Pro badge, the locked-destination gate, and Settings' "Go Pro" row.

## `ui/cast`

- Open first: `CastUi`.
- `CastButton` is Media3's route button for the Visuals stage; without Pro a tap opens the paywall.
- `CastButton` also feeds `cast/CastMirror` the stage's live look.
- While the stage is not drawing, Auto steps on the cast renderer's director moments instead (`CastMirror.directorMoment`, same native rule).

## `ui/theme`

- Open first: `GeodeTokens`, `SelectedGlow`, `GeodeTheme` (`geodeColorScheme`, `geodeTypography`), `VisualAccent`, `GeodeMotionScheme`, `Wordmark`, `FloatingGlass`, `GlassBlur`, `StoneIcon`, `StonePress`, `UiSounds`.
- The one "black glass" theme.
- Design tokens: colour, matte glass, shadow, spacing and edge inset, radius, motion.
- Floating glass is `floatingGlass`.
- Its blur is `glassBlur` over a `glassBackdrop`: the recorded content under the glass, blurred; never the GL surface, which no view can sample.
- `selectedGlow` is the soft accent glow on the selected thing: one shadow pass, fades through the motion scheme, nothing drawn when off.
- The M3 colour scheme takes its accent from the visual (`VisualAccent`: its hue, readable on black, else #9CC7FF).
- The compact type roles live here.
- `Wordmark` draws the BeatRealms 3D wordmark from `res/drawable/splash_wordmark.xml`, which is the system splash's branding image too.
- `tools/wordmark.py` outlines the wordmark from Pacifico and Orbitron.
- M3 Expressive motion follows the system's "Remove animations" live (`GeodeMotionScheme`, read through `LocalGeodeMotion` until Material opens `MotionScheme`).
- Icons are Material Symbols Rounded (`StoneIcon`, outlined at rest and filled when selected; paths generated by `tools/material_symbols.py`).
- Press feedback, haptics and interface sounds live here.
- No screen code; ready to move into `:core:designsystem`.

## `playback`

- Open first: `PlaybackEngine`, `PlaybackService`, `BitPerfectOutput`.
- `PlaybackEngine` is the process-wide holder of the one ExoPlayer, the ring and the source's sample rate.
- `PlaybackEngine`'s `PlaybackSession` applies the saved player prefs itself, so a session the service starts alone behaves like an in-app one.
- That includes the native-output switch: on, and not bit-perfect, it lets `audio/TapRenderersFactory` hand the sink the Oboe output.
- `BitPerfectOutput` is USB DAC bit-perfect via `setPreferredMixerAttributes`, API 34+.
- `PlaybackService` is a MediaLibraryService.
- Android Auto browses `LibraryTree` and searches it with `SearchMatcher` through `onSearch`/`onGetSearchResult`.
- Queue, sleep timer and ReplayGain live here.
- `AutoDirectorFeed` is the auto-director's play feed: a seed per track, the position for the free taste.
- The Behaviour options that run with the app closed live here.
- `TrackCrossfade` fades out and in on the one player; it only tells native the length and whether anything follows, the fade is a gain ramp.
- `HeadphoneAutoplay` lives here.
- `QueueEnd`: stop, repeat, or similar tracks from `LibraryTree.similarTo`.
- `NotificationButtons` adds shuffle, repeat and favourite as custom session commands.
- Android draws two notification buttons, so `NotificationButton.ordered` lists a chosen favourite first and Settings keeps the choice to two.
- `PlayRecorder` is a listener on the one player: it saves the resumption queue and position to `SessionStore` and records each play in the session's one `HistoryStore`, whoever holds the player.
- So playing on after the UI closes is still resumed and remembered; the UI only reads them.
- `VoiceSearch` reads Media3's play-from-search item (Android Auto and Assistant) for `LibraryTree.search`; the match is native.
- `PlaybackQueue` and `QueueOps` are the queue's pure rules: which list a tapped track plays from, the 1001-track window, play order.

## `audio`

- Open first: `AudioBus`, `AudioCapturePump`, `PlaybackCaptureService`, `TapRenderersFactory`, `OboeAudioOutput`, `PlayoutSink`.
- Capture sources: mic, other apps' audio.
- `AudioBus` holds the session's `SampleRing` for the wallpaper and dream.
- Platform FX and the media chain live here.
- `TapRenderersFactory`'s provider hands the sink `OboeAudioOutput` (Media3's `AudioOutput` over the native Oboe output, one native call per method) when Settings › Playback › Output and more has it on and the config is plain 16-bit or float PCM, mono or stereo, not offload, tunneling, bit-perfect or output-side speed.
- Otherwise, or when the stream will not open or fails, the sink uses the AudioTrack.
- `PlayoutSink` wraps that sink and reports its buffers, position, play, pause, flush and speed to the native playout clock.

## `audio/dsp`

- Open first: `MvzAudioProcessorChain`.
- The Media3 processor chain order and native DSP.

## `data`

- Open first: `PresetStore`, `TemplateRepository`, `VideoTemplate`, `GeodePrefsFiles`, `UserDataRepository`.
- Stores (SharedPreferences, JSON files) and their repositories.
- The live state's stores: `VizStateStore`, `AutoVisualsPrefsStore`.
- The bundled looks: `BuiltInPresets`.
- `MediaStoreSource`: the screens' consent requests and picked-folder grants; main-safe on IO.
- Template and preset formats.
- The GUI prefs: `ThemeStore`, `GuiPrefs`.
- Track favourites: `FavouritesRepository` over `FavouritesStore`, file `geode-favourites`, keyed by the track's URI.
- Player's heart, Library's ⋮ menu and Playlists › Favourites, the notification's heart and Android Auto all read and write the favourites.
- The track list is `TrackLibrary`.
- The music library over it is `LibraryRepository`, implemented by `MusicLibraryRepository`: device tracks, folders, playlists, tags; what `LibraryViewModel` takes.
- A track's lyrics: `Lyrics`, `LyricsLoader` (an attached .lrc, a sidecar or the tags).
- `DocumentUris` is the one place a picked document's grant is kept and its display name asked (`persistGrant`, `displayNameOf`).
- `DeviceTrackQuery` is the MediaStore query; it leaves out tracks under the user's minimum.
- The minimum is `MinTrackLength`, 30 s by default, kept by `LibraryPrefsStore` in `geode-library` and set in Settings › Library.
- Settings › Performance is `PerformanceStore`: live, wallpaper and dream only, never export, never restored from Backup.
- Settings › Behaviour's app-wide choices and canvas gesture map are `BehaviourStore` in `BehaviourPrefs`; the defaults are the owner's.
- Behaviour has rows only for the choices that are read: start screen (`ui/GeodeAppState.startDestination`), keep-screen-on in Visuals, headphones, queue end, shuffle/repeat, notification buttons, gestures, the dream's.
- Reduced motion is `SystemMotion`: Android's "Remove animations" alone, read by the UI, renderer, paywall, exports, cast and wallpaper.
- Quick knobs' macro bank is `MacroStore`: the native bank text, one key, pushed to native at app start.
- `AutoDirectorStore` keeps the auto-director's switch and reach (Visuals quick settings), play seed and Customize's keep set, pushed to native the same way.
- Note & key reactions' switch is `NoteResponseStore` (`ui/NoteResponseSettings`).
- Studio's project and template folders are `EditorProjectStore`: `files/projects/<id>/`.
- A project's takes are in `files/project-takes/<id>/` (out of cloud backup), as `TakeStore` saves them.
- Older takes are moved out of project folders natively by `core/library/StudioFiles`.
- Where takes render, asked once, is `RecordSaveStore`.
- What takes render at and how long one may run is `RecordPrefsStore` (Settings › Export › Record): resolution and frame rate, which follow the Custom defaults until picked, and a maximum length.
- The offered lengths, the stop, the saved length and the size estimate are native (`core/output/RecordPlan`, `geode_record_*`).
- `LookStore` is the saved look snapshot for the wallpaper, the dream and Cast (the session's keys, plus `pushProcessWide` for the hand mask).
- Settings › Export's per-destination defaults are `ExportPrefsStore` and `ExportDestination`.

## `model`

- Open first: `DeviceTrack`, `LibraryTrack`, `LyricLine`, `QueueTrack` (with `QueueUiState`), `VizPlaylistEntry`, `MilkFile`, `PresetLink`, `PresetShelf`, `StyleShelf`, `StyleLook`, `Tour`, `ControlRange`.
- Pure value types shared across layers, staged for `:core:model` (ADR 0001 step 2). No `android.*`.
- `PresetShelf` holds the Presets grid filters and naming.
- `StyleLook` holds the Styles look groups.
- `Tour` holds the tour's chapters, steps, rules and saved progress.
- `ControlRange` is every Customize slider's and scene keyframe track's range: the params schema's, with its few UI caps.

## `data/backup`

- Open first: `BackupFormat`, `BackupArchive`, `BackupRepository`.
- The manual backup zip (Settings › Backup).
- Format, allowlist, checks and migrations are pure Kotlin.
- Creative borrows the macro bank, LFO and reaction keys of `geode-prefs` (`BackupSection.borrowedKeys`, its own slice); the old knobs' keys are skipped.
- `BackupRepository` does the picker, filesDir and prefs I/O.

## `editor`

- Open first: `StudioProject`, `Timeline`, `Keyframes`, `Markers`.
- The Studio project model: pure Kotlin, no `android.*`.
- A project is a folder (`StudioProject`: edit, kind Standard or Loop, its own export settings, its renders).
- A template is a project minus audio (`ProjectTemplate`, fitted to a new track; batch = template × tracks).
- A take is a scene clip with a `takeId` on track time (ADR 0005).

## `export`

- Open first: `VideoExporter`, `StudioExporter`, `LoopRender`, `ExportRun`, `ExportService`, `WatermarkOverlay`, `RenderBatch` (batch queue and render scopes, pure).
- Visualizer export: MediaCodec + offscreen GL.
- Studio's layered renders: Media3 Transformer, `StudioExporter.exportComposition`.
- Loop render, chapters, captions.
- Loudness: `LoudnessMeter` decodes; the BS.1770 maths is native.
- `PcmResampler` takes hi-res sources down to an AAC-supported rate; it is a handle over native maths, so `close()` it.
- The non-Pro watermark: `WatermarkOverlay` rasterises the tile.
- The watermark's compositing, the loop seam and Studio's GL transitions (`GlTransitionEffect`) are native Passes in `core/viz/frame/ExportPasses`.
- The loop seam's schedule and GPU budget are `core/viz/frame/LoopSeam`, called through `geode_export_*`.
- `MuxerBudget` caps the bit rate so a long render stays under MediaMuxer's 4 GB.

## `viz`

- Open first: `BackgroundImage`.
- The background image layer.

## `wallpaper`

- Open first: `VisualizerWallpaperService`, `WallpaperRunState`, `SoundSource`, `EdgeGlow`, `VisualizerDreamService`.
- The live wallpaper and the dream; each builds its own `VisualizerView`.
- The wallpaper's run rule is native (`core/viz/SurfaceRun`, `geode_surface_*`); `WallpaperConditions` only packs what the shell sees.
- The wallpaper stops when hidden or covered by the shade or recents.
- It holds a still frame without Pro or once a trial ends.
- It holds a still frame under reduced motion: the in-app switch or the system's Remove animations (`SystemAnimations`, a `ContentObserver` on `ANIMATOR_DURATION_SCALE`).
- It holds a still frame in battery saver or after 6 s of silence; the silence rule is native in `core/sound/Silence`.
- The still frame's 2 s settle is native too (`geode_surface_settle_ms`).
- Its fps is `WallpaperSettings.fps`, the dream's `BehaviourPrefs.dreamFps`, default 30, snapped by `geode_surface_fps_choice`; not Performance's cap.
- Its sound is `SoundSource`: the app's ring while fed, else any app's audio through `OutputMixCapture`, a `Visualizer` on the output mix.
- The edge glow look is the AOD-like look, since ambient mode is a system API.
- The edge glow is native (`core/viz/frame/EdgeGlowPass`); `EdgeGlow` is only its options, which `WallpaperRenderer` hands over through `geode_viz_set_edge_glow`.
- The wallpaper's options are `WallpaperPrefs`, own file `geode-wallpaper`.
- The dream runs by the same rule and sound.
- The dream is covered when the proximity sensor reads near (`DreamCover`).
- The dream runs in the gentle flight (reduced camera motion) unless reduced motion stills it.
- The time, date and now playing drift over the dream (`DreamOverlay`); each is a toggle in `BehaviourPrefs`, beside `dreamScreenBright`.

## `widget`

- Open first: `NowPlayingWidget`, `WidgetPublisher`, `WidgetStyles`, `WidgetVisual`.
- Glance now-playing widget in black glass (`SizeMode.Exact`, four layouts from 2x1 to 4x3+).
- Progress moves on a slow tick while playing.
- The style on screen shows as a picture (`WidgetVisual`: its thumbnail, else a glow from the cover's palette).
- Style buttons go to the session through `WidgetStyles` (it collects `steps`, calls `publish`); with no session they open the app.
- Play/pause is Media3's button action, the tile's rule: nothing loaded opens the app.

## `cast`

- Open first: `CastController` (`state`, `consumed`, `stop()`), `CastService`, `CastStream`, `CastMirror`.
- Cast to TV (Pro, the only internet use): Media3 Cast to Google's Default Media Receiver.
- The phone renders its own `VisualizerRenderer` (the stage's look through `CastMirror`, previews included) into an H.264+AAC encoder (`CastStream`, `CastEncoder`).
- The encoder is paced by the native `core/viz/frame/FixedRateClock`.
- `LiveSegments` cuts the fragmented MP4 into live HLS.
- Segment timing, the playlist and each fragment's rewrite into a self-contained segment (tfdt and moof-relative offsets) are native in `core/viz/frame/HlsSegments`.
- The AAC feed's counts are native in `CastAudio` (`core/viz/frame/CastAudio`).
- `LiveHttpServer` serves the stream on the LAN under a random token.
- It serves from the Wi-Fi or Ethernet address, never a VPN's or cellular's.
- Sound plays on the TV only (phone muted); nothing else in the app changes while casting.
- `CastOptionsProvider` builds the controller when Cast starts, so the system output switcher (`MediaTransferReceiver`) works too.
- `CastController.state` is process-wide.
- `ui/cast/CastButton` spins while the stream starts.
- `CastHost` (from `AppRoot`) shows a one-shot state once: the paywall for a locked pick, a failure's reason.

## `tile`

- Open first: `PlaybackTileService`.
- Quick Settings tile: play/pause through a `MediaController` on `PlaybackService`'s session while the shade shows it.
- It opens the app when nothing is loaded and on a long press.
