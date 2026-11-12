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

- Settings (`AppSettingsTab`) is a list, then pages.
- It is a list of fifteen tabs (`SettingsGroupKey`: icon, name, one-line summary).
- The tabs: Playback, Sound & equalizer, Output & devices, Library, Visual playback, Sound reaction, Photosensitivity & motion, Displays & casting, Wallpaper & screen saver, Gestures & controls, Look, Performance, Export & recording, Preferences, Backup, help & about.
- Visual playback has the same Auto and style-timer rows as Visuals' quick settings, on the same stores.
- Preferences is the home of small choices with no other tab: the watermark's placement (tiled, a corner or an end card) and Pro's switch to add it (`PreferencesSettings`).
- `SettingsGroupKey.fromName` restores the ten old group names.
- Each tab is a page with ← the title and ‹ › to the next tab.
- Basics are always open on top; `SettingsSectionKey` sections drop open below, several at once.
- Settings pages are at most 600 dp wide.
- `SettingsComponents` holds rows, Basics and section cards.
- Each tab's Basics and section list `SettingsRowKey`s; each row is placed once (`SettingsMapTest`) and drawn by key.
- The rows stay in their `*Settings.kt`: `BehaviourPrefs`' in `BehaviorSettings`, `PlayerPrefs`' in `PlaybackSettings`.

### Customize

- Customize is `CustomizePanel` over `CustomizeBrowser`.
- It is a scrollable row of eleven category tabs (`CustomizeCategory`): Quick knobs, Look, Shape, Motion, Dive (the endless zoom), Camera, Music (with the reaction, envelope and wave editors), Effects (with transitions), Liquid, Engine (Fluid's, Cymatics' or MilkDrop's own, only on those), Expert; a tab makes no back entry.
- Each tab is a list of collapsible groups (the native sections: free Basics, then Pro ones), all collapsed until opened.
- An opened group stays open across leaving and restarts (`data/CustomizeGroupsStore`, with the tab); Collapse all sits beside the tabs.
- Look has one palette list (`PaletteDropdown`, built-ins then the user's own); the blend's second slot steps through the built-ins.
- Look's Your colours (`PaletteMaker`, `ColourPicker`): up to five colour stops, picked Photoshop-style or with an eyedropper on a still of the visual (`geode_viz_still_request`) or the artwork.
- The stops are scene params `paletteStop0..4` that loop natively (see native-core.md); the user's palettes are saved in `files/palettes/`.
- Which control is where, what is free, which styles show what, and each category's Randomize and Reset are native (`core/viz/CustomizeMap`).
- A roll moves only what the panel offers on the style, and keeps or opens Dive's and Liquid's own switch.
- `shownOn`'s `CATEGORY:roll` lines say where the dice has something to move.
- ⋮ › Randomize every group rolls every category; Quick knobs' dice rolls the knobs' turns.
- Without Pro the lock `#PRO` holds all but the Basics, for the dice and the two-circle gesture alike.
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
- `StageKeys` gives each gesture a TV remote key, run through the same map.
- `MacrosDrawer` is the knobs a swipe down pulls.
- `MacroEditor` (Quick knobs › Edit knobs, Pro) copies, reorders and deletes knobs and edits their links (range and curve each).
- `MacroEditor` draws only what native's `geode_macro_bank_editor` text offers.
- New macro under the knobs (in Customize and in the drawer) makes an empty draft, filled by Add a parameter or Random (`geode_macro_bank_roll`) and kept only by Save macro as.
- A long press on a knob opens its editor (`KnobEditorDialog`).
- A long press on a Customize slider offers "Add to knob" (`KnobLinks`, from `geode_macro_bank_controls`, which also marks the sliders a knob moves).
- `VisualsSettingsPage` (`VisualsQuickSettings.kt`) is Visuals › Settings, the page the ⚙ at the top of the visual opens (no gesture).
- Visuals › Settings holds Auto and its reach, the style timer and its sources, the visual playlist, sheet glass and mini player fade, writing the same stores as Settings.
- Studio (`StudioHub`: Render and My videos (`MyVideosTab`)) owns export, loop render and export defaults.
- Studio has no editor: the owner dropped video editing (#227).
- Record (`RecordController`, Pro) is a live video of the visual and what it hears, until the user stops or Settings › Export › Record's maximum length (`ui/RecordSettings`).
- The stage's GL thread copies each frame its clock keeps into an H.264 surface (`core/viz/frame/RecordTap`).
- Every block the stage's sound engine took goes into `core/output/Recorder`, which times the file: frame slots and PTS, A/V placement, the stop.
- Record's capture fps drops first on a hot phone.
- Record's encoders and MP4 run native on their own thread (`core/output/RecordSession` on `core/output/EncodeSink`); `RecordController` opens the fd, starts, stops and polls.
- `data/RecordFiles` writes into the place asked once (`RecordSaveSheet`), or a scratch file before that.
- `EditorController` only keeps the project list My videos reads (recordings and kept renders).
- Library (`LibraryScreen`) draws `LibraryViewModel.uiState`.
- The header's magnifier turns the header into the search field (← or back closes and clears, × clears).
- A debounced query (150 ms) replaces the tabs with one result list: Artists, Albums, then Tracks, best first.
- The order, match and ranking are native (`core/library/Search`).
- `LibraryBrowse.browse` only turns the rows a `LibraryIndex` answers into tracks and groups, on `Dispatchers.Default`.
- Duplicates are found on `Dispatchers.Default` once per library change.
- A granted library MediaStore answered empty shows `LibraryEmpty` (Rescan, Settings › Library, listen to another app or the mic).
- Settings › Audio and the player card say when a running capture hears only silence (nothing plays, or the playing app blocks capture).

## `ui/tour`

- Open first: `TourViewModel`, `TourHost`, `TourTargets`, `TourOverlay`.
- `TourViewModel` is the one state holder: a `StateFlow<TourUiState>`, events as state handed back once.
- Tour progress is kept in `AppState.tourProgress`.
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
- `CastHost` (on every screen) feeds `cast/CastMirror` the stage's live look.
- While the stage is not drawing, Auto steps on the cast renderer's director moments instead (`CastMirror.directorMoment`, same native rule).

## `ui/theme`

- Open first: `GeodeTokens`, `SelectedGlow`, `GeodeTheme` (`geodeColorScheme`, `geodeTypography`), `VisualAccent`, `GeodeMotionScheme`, `Wordmark`, `FloatingGlass`, `GlassBlur`, `StoneIcon`, `StonePress`, `UiSounds`.
- The one "black glass" theme.
- Design tokens: colour, matte glass, shadow, spacing and edge inset, radius, motion.
- Floating glass is `floatingGlass`.
- Its blur is `glassBlur` over a `glassBackdrop`: the recorded content under the glass, blurred; never the GL surface, which no view can sample.
- `selectedGlow` is the soft accent glow on the selected thing: one shadow pass, fades through the motion scheme, nothing drawn when off.
- The M3 colour scheme takes its accent from the visual (`VisualAccent`: its hue, readable on black, else #9CC7FF).
- `GeneralSettings.accentFollowsVisual` off keeps #9CC7FF.
- The compact type roles live here.
- `Wordmark` draws the BeatRealms 3D wordmark from `res/drawable/splash_wordmark.xml`, which is the system splash's branding image too.
- `tools/wordmark.py` outlines the wordmark from Pacifico and Orbitron.
- M3 Expressive motion follows the system's "Remove animations" live (`GeodeMotionScheme`, read through `LocalGeodeMotion` until Material opens `MotionScheme`).
- Icons are Material Symbols Rounded (`StoneIcon`, outlined at rest and filled when selected; paths generated by `tools/material_symbols.py`).
- Press feedback, haptics and interface sounds live here.
- Haptics (`StoneHaptics`) are off with Settings › Look's Vibrate on touch.
- The TV seam is `Television`: `isTelevision` by `FEATURE_LEANBACK`, `LocalTelevision` provided by `AppRoot`, the accent focus ripple and the overscan margins a TV gets.
- No screen code; ready to move into `:core:designsystem`.

## `playback`

- Open first: `PlaybackEngine`, `PlaybackService`, `BitPerfectOutput`.
- `PlaybackEngine` is the process-wide holder of the one ExoPlayer, the ring and the source's sample rate.
- `PlaybackEngine`'s `PlaybackSession` applies the saved player prefs itself, so a session the service starts alone behaves like an in-app one.
- That includes the native-output switch: on, and not bit-perfect, it lets `audio/TapRenderersFactory` hand the sink the Oboe output.
- `BitPerfectOutput` is USB DAC bit-perfect via `setPreferredMixerAttributes`, API 34+.
- `PlaybackService` is a MediaLibraryService.
- Media browsers (Bluetooth, Assistant; Android Auto is not declared in 1.0) browse `LibraryTree` and searches it natively (`geode_library_search`) through `onSearch`/`onGetSearchResult`.
- Android Auto browses `LibraryTree` and searches it natively (`geode_library_search`) through `onSearch`/`onGetSearchResult`.
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
- `AiffExtractor` plays AIFF through Media3 and only moves bytes; the header, the times and the seek offsets are native (`core/audio/ingest/AiffHeaderScan`, `geode_aiff_*`).
- `AudioFxPresets` names the equalizer curves native lists (`core/audio/dsp/EqPresets`, `geode_dsp_eq_preset_*`).
- `PlayoutSink` wraps that sink and reports its buffers, position, play, pause, flush and speed to the native playout clock.

## `engine/audioandroid`

- `PcmTap` is the Media3 PCM tap: it hands the chain's direct buffer and its format to the native ring and reads no sample.
- `PcmTap.owns` is set by `CaptureController` through `PlayerSession`; while another source writes the ring the tap writes nothing.

## `render`

- The engine module's package, for the shell's own pieces of it.
- `fluid/FluidShaderTemplate` reads the fluid splat shader asset that Expert › Shader code starts from.

## `audio/dsp`

- Open first: `MvzAudioProcessorChain`.
- The Media3 processor chain order and native DSP.

## `data`

- Open first: `PresetStore`, `TemplateRepository`, `VideoTemplate`, `GeodePrefsFiles`, `GeneralSettings`.
- Stores (SharedPreferences, JSON files) and their repositories.
- The live state's stores: `VizStateStore`, `AutoVisualsPrefsStore`.
- The bundled looks: `BuiltInPresets`.
- `MediaStoreSource`: the screens' consent requests and picked-folder grants; main-safe on IO.
- `MediaStoreSource` also owns the controllers' picked-document I/O: the background image's grant and readability, preset, template and .milk reads, a .milk folder walk, the preset mirror folder's copies and deletes, a queued file's display name. No `*Controller` in `ui/` calls the `ContentResolver` (#896 step 1).
- Template and preset formats.
- What was `GuiPrefs`, split by subject: `GeneralSettings`, `SoundInputSettings`, `VisualsSettings`, `LibrarySettings`, and `AppState` (onboarding and the tour, not a preference); each a codec over `geode-prefs` under its old keys.
- Track favourites: `FavouritesRepository` over `FavouritesStore`, file `geode-favourites`, keyed by the track's URI.
- Player's heart, Library's ⋮ menu and Playlists › Favourites, the notification's heart and Android Auto all read and write the favourites.
- The track list is `TrackLibrary`.
- The music library over it is `LibraryRepository`, implemented by `MusicLibraryRepository`: device tracks, folders, playlists, tags; what `LibraryViewModel` takes.
- A track's lyrics: `Lyrics`, `LyricsLoader` (an attached .lrc, a sidecar or the tags).
- `DocumentUris` is the one place a picked document's grant is kept and its display name asked (`persistGrant`, `displayNameOf`).
- `DeviceTrackQuery` is the MediaStore query; it leaves out tracks under the user's minimum.
- The minimum is `MinTrackLength`, 30 s by default, kept by `LibraryPrefsStore` in `geode-library` and set in Settings › Library.
- Library shows how many tracks the minimum hid.
- Settings › Performance is `PerformanceStore`: live, wallpaper and dream only, never export, never restored from Backup.
- The app-wide choices and canvas gesture map are `BehaviourPrefs`, kept by `BehaviourCodec` behind the container's one `behaviour` repository, with rows on seven Settings tabs; the defaults are the owner's.
- `BehaviourPrefs` has rows only for the choices that are read: start screen (`ui/GeodeAppState.startDestination`), keep-screen-on in Visuals, headphones, queue end, shuffle/repeat, notification buttons, gestures, the dream's.
- Reduced motion is `SystemMotion`: Android's "Remove animations" alone, read by the UI, renderer, paywall, exports, cast and wallpaper.
- Customize's tab and open groups are `CustomizeGroupsStore` on a `SettingsRepository`: state, never in a backup.
- Quick knobs' macro bank is `MacroStore`: the native bank text, one key, pushed to native once its read at process start is back, and on every edit.
- Each macro edit is saved as made; a new knob is a draft until saved.
- A knob off screen is kept in the bank's library for Add macro to put back.
- `MacroEditorTable` reads the knob editor's rows.
- The dials' size is `GeneralSettings.knobSize`.
- `AutoDirectorStore` keeps the auto-director's switch and reach (Visuals quick settings), play seed and Customize's keep set, pushed to native the same way.
- Note & key reactions' switch is `NoteResponseStore` (`ui/NoteResponseSettings`).
- Studio's project and template folders are `EditorProjectStore`: `files/projects/<id>/`; the old replayed takes are deleted at start.
- Where recordings go, asked once, is `RecordSaveStore`.
- What recordings are encoded at and how long one may run is `RecordPrefsStore` (Settings › Export › Record): resolution and frame rate, which follow the Custom defaults until picked, and a maximum length.
- The offered lengths and the size estimate are native (`core/output/RecordPlan`, `geode_record_*`).
- `LookStore` is the saved look snapshot for the wallpaper, the dream and Cast (the session's keys, plus `pushProcessWide` for the hand mask).
- Settings › Export's per-destination defaults are `ExportPrefsStore` and `ExportDestination`.
- `data/prefs/PrefKey` is the prefs schema (#1009): every owner of a SharedPreferences key lists it once as `PREF_KEYS` (file, name, type, `BackupClass`).
- `BackupClass` is portable, device, state or cache, as Settings › Backup treats it today.
- `PREF_KEYS` is read by nothing at run time yet; `PrefsSchemaTest` holds it to the stores and `BackupFormat`.
- `data/prefs/SettingsRepository` is the one settings repository shape: `PrefsSettingsRepository` over a `PrefsCodec`, a `StateFlow`, `loaded()`, `update(transform)` under one lock, IO inside; `current()` for the few synchronous readers on main.
- So far on it: Playback's (`PlayerPrefsRepository`).
- So far on it: Settings › Preferences' watermark (`WatermarkPrefsRepository`, `WatermarkPrefsCodec`; general prefs, portable).
- On it too: the container's `behaviour`, `general`, `soundInput`, `visuals`, `library` and `appState`.
- The four process-wide stores (`macros`, `autoDirector`, `noteResponse`, `styleShelf`) are the container's too, each over a repository, none built by a `get(context)`.

## `model`

- Open first: `DeviceTrack`, `LibraryTrack`, `LyricLine`, `QueueTrack` (with `QueueUiState`), `VizPlaylistEntry`, `MilkFile`, `PresetLink`, `PresetShelf`, `StyleShelf`, `Tour`, `TabLabels`, `ScreenRotation`, `ControlRange`, `WatermarkChoice`.
- Pure value types shared across layers, staged for `:core:model` (ADR 0001 step 2). No `android.*`.
- `PresetShelf` holds the Presets grid filters and naming.
- `render/scene/StyleCatalogue` keeps the native catalogue's answers (ids, picker families, retired ids, param scopes as `ParamScope`); it decides nothing.
- `render/scene/StyleLabels` maps a style or family id to its string resource (`style_<id>`, `style_family_<name>`).
- `Tour` holds the tour's chapters, steps, rules and saved progress.
- `TabLabels` is Settings › Look's tab bar labels choice.
- `ScreenRotation` is Settings › Look's and Visuals › Settings' screen rotation: follow the phone, lock portrait or landscape.
- `ControlRange` is every Customize slider's and scene keyframe track's range: the params schema's, with its few UI caps.
- `WatermarkChoice` is a render's watermark, pinned at its start through `RenderLimits.withMark`.
- `OpenedAudio` is the id an audio file opened from another app plays under.

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

## `export`

- Open first: `VideoExporter`, `LoopRender`, `ExportRun`, `ExportService`, `WatermarkOverlay`, `RenderBatch` (batch queue and render scopes, pure).
- Visualizer export: MediaCodec + offscreen GL.
- `ExportScene` opens the export's renderer and the track's descriptor and hands the look over; the native export job (`core/output/ExportFrame`) runs the prepare order, the pre-roll, the track's decode and each frame.
- Loop render and chapters.
- Loudness: `LoudnessMeter` decodes; the BS.1770 maths is native.
- `PcmResampler` takes hi-res sources down to an AAC-supported rate; it is a handle over native maths, so `close()` it.
- The time left is native (`core/output/RenderEta`, `geode_render_eta_*`); `ExportRun.describeEta` words it.
- A segment's clamp is native (`core/output/ExportRange`, `geode_export_range`); `ExportRange` carries the result.
- The watermark: `WatermarkOverlay` rasterises the tile.
- Whether there is a watermark, where (tiled, a corner, an end card), its size, its opacity and the end card's timing are native (`exportmath::watermark`).
- An end card is a corner on a loop, a recording or a layered Studio segment.
- The watermark's compositing and the loop seam are native Passes in `core/viz/frame/ExportPasses`.
- The loop seam's schedule and GPU budget are `core/viz/frame/LoopSeam`, driven by the native loop job.
- `LoopRender` hands the native loop job (`loopJob*`) its values, polls its progress every 50 ms, passes a cancel on and words its outcome.
- `MuxerBudget` caps the bit rate so a long render stays under MediaMuxer's 4 GB.

## `viz`

- Open first: `BackgroundImage`, `ArtPalette`.
- The background image layer.
- The artwork palette's shell (`ArtPalette`): a cover's pixels to native `core/viz/ArtPalette`, `geode_art_palette_extract`.
- Customize › Look's "Take the colours from the artwork" and the widget's glow read it.

## `wallpaper`

- Open first: `VisualizerWallpaperService`, `WallpaperRunState`, `SoundSource`, `EdgeGlow`, `VisualizerDreamService`.
- The live wallpaper and the dream; each builds its own `VisualizerView`.
- The wallpaper's run rule is native (`core/viz/SurfaceRun`, `geode_surface_*`); `WallpaperConditions` only packs what the shell sees.
- The wallpaper stops when hidden or covered by the shade or recents.
- It holds a still frame without Pro or once a trial ends.
- It holds a still frame under reduced motion: the in-app switch or the system's Remove animations (`SystemAnimations`, a `ContentObserver` on `ANIMATOR_DURATION_SCALE`).
- It holds a still frame in battery saver or after 6 s of silence; the silence rule is native in `core/sound/Silence`.
- The still frame's 2 s settle is native too (`geode_surface_settle_ms`).
- Why it holds still, the rule's first reason, is named on the options page (`geode_surface_reason`).
- The phone's refusal of the output mix is one such reason, kept as `mix_refused`.
- Its fps is `WallpaperSettings.fps`, the dream's `BehaviourPrefs.dreamFps`, default 30, snapped by `geode_surface_fps_choice`; not Performance's cap.
- Its sound is `SoundSource`: the app's ring while fed, else any app's audio through `OutputMixCapture`, a `Visualizer` on the output mix.
- The edge glow look is the AOD-like look, since ambient mode is a system API.
- The edge glow is native (`core/viz/frame/EdgeGlowPass`); `EdgeGlow` is only its options, which `WallpaperRenderer` hands over through `geode_viz_set_edge_glow`.
- The wallpaper's options are `WallpaperPrefs`, own file `geode-wallpaper`, backed up with Settings but `mix_refused`.
- The dream runs by the same rule and sound, except that silence shows no visual (dark, `GEODE_SURFACE_DARK_WHEN_SILENT`) where the wallpaper keeps a still frame.
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

- Open first: `CastController` (`state`, `consumed`, `stop()`), `ComputerLink`, `CastService`, `CastStream`, `CastMirror`.
- Cast to TV and Show on a computer (Pro, the only internet use): Media3 Cast to Google's Default Media Receiver.
- The stream is the stage's own frames, not a second render: the stage's renderer (`NativeViz.castSource`) copies its composite into GPU buffers both contexts share (`core/viz/frame/CastTee`, rules in `CastTeeRule`, `geode_cast_tee_*`).
- The cast thread blits the newest of them into an H.264+AAC encoder (`CastStream`, `CastEncoder`).
- While a cast streams the stage keeps drawing on every tab, even before Visuals was laid out, in its whole rect with no bars (`CastMirror.streaming`, `ui/Crystal`'s `VisualizerCanvasHost`, the rule native: `geode_stage_host`).
- The tee fills the 16:9 stream with no bars or stretch: a portrait stage turned a quarter turn counter-clockwise (its top on the receiver's left), both centre-cropped (`tee::place`).
- A new stream waits up to 3 s for the stage's first copy (`geode_cast_tee_await_stage`).
- Only while the stage draws nothing (the app in the background) does a fallback `VisualizerRenderer` draw the saved look and the stage's last style and .milk (`CastMirror`).
- The encoder is paced by the native `core/viz/frame/FixedRateClock`.
- `LiveSegments` cuts the fragmented MP4 into live HLS.
- Segment timing, the playlist and each fragment's rewrite into a self-contained segment (tfdt and moof-relative offsets) are native in `core/viz/frame/HlsSegments`.
- The AAC feed's counts are native in `CastAudio` (`core/viz/frame/CastAudio`).
- `LiveHttpServer` serves the stream on the LAN under a random token; it handles only the socket and bytes.
- Who may connect (a TV cast the Cast device alone, a shared link the Wi-Fi's subnet), every route, the constant-time token check and each response head (no-store, nosniff, no-referrer, the page's CSP) are native in `core/viz/frame/LinkShare`.
- It serves from the Wi-Fi or Ethernet address, never a VPN's or cellular's.
- Show on a computer (`ComputerLink`) serves the same picture, with or without a TV, to a browser on the Wi-Fi.
- Its doors (`ComputerLinkDoor`) are Visuals › Settings' row, the stage's Share to PC icon beside Cast and the Player's tile.
- Its sheet is `ui/cast/ComputerLinkSheet`: the whole link only as a QR code (`core/util/QrCode`), and for a computer, which has no scanner, the short link with no token as text, Copy, Share and the code its page asks for.
- The typed path is native in `core/viz/frame/LinkPair` (`geode_link_pair_*`): a random 6-character code from the OS (Crockford base32, 30 bits), single-session, replaced after 5 minutes, a few tries per code with a gap and a lock-out, compared in constant time; only a right code gets the token's path.
- Its sheet is `ui/cast/ComputerLinkSheet`: the link as text, a QR code from `core/util/QrCode`, Copy and Share.
- The link it shows has the host as one number (`geode_link_share_url`, the WHATWG IPv4 parser; hides the dotted address from the eye only) and a two-word label.
- The page (`core/viz/frame/LinkPage`, plain HTTP) tries WebRTC first: it POSTs a receive-only offer to `rtc` and plays the answer from `core/cast/WebRtcLink` with a 0.5 s `jitterBufferTarget`.
- `core/cast/WebRtcLink` is libdatachannel + Mbed TLS, the one native networking code; the media is DTLS-SRTP encrypted, the page and signalling are not.
- Its rules are in `core/viz/frame/WebRtcRule`: the encoder's own H.264 via `CastEncoder`'s video tap, Opus from `OpusFeed` while a browser is on, host candidates on the bound address only, no STUN or TURN, mDNS names dropped.
- Without WebRTC the page reads one long-lived `stream.mp4` into MSE with half a second in hand, or plays the HLS natively.
- Show on a computer's state is native (`geode_link_step`); it stops on Stop, a network change or Pro's end.
- A TV that errors, goes idle or never fetches ends the TV's part (`geode_link_receiver_health`).
- Sound plays on the TV or computer only (phone muted); nothing else in the app changes while casting.
- `CastOptionsProvider` builds the controller when Cast starts, so the system output switcher (`MediaTransferReceiver`) works too.
- `CastController.state` is process-wide.
- `ui/cast/CastButton` spins while the stream starts.
- `CastHost` (from `AppRoot`) shows a one-shot state once: the paywall for a locked pick, a failure's reason.

## `tile`

- Open first: `PlaybackTileService`.
- Quick Settings tile: play/pause through a `MediaController` on `PlaybackService`'s session while the shade shows it.
- It opens the app when nothing is loaded and on a long press.
