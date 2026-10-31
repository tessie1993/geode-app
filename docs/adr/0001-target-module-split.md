---
status: proposed
---

# Target module split and Navigation 3 migration

`:app` holds ~46k lines in eleven packages. One `PlayerSession` object (built by `di/PlayerSessionProvider`) owns ~20 controllers and every ViewModel reads through it. Navigation is an enum plus a `when` over Compose state. We split `:app` into the layout below, following [`ANDROID_ARCHITECTURE_GUIDE.md`](../ANDROID_ARCHITECTURE_GUIDE.md) and [`ANDROID_ARCHITECTURE_REFERENCE.md`](../ANDROID_ARCHITECTURE_REFERENCE.md). Pure-Kotlin modules come first, because they are what keeps KMP cheap later. All navigation keys live in one `:core:navigation` module instead of an `api`/`impl` pair per feature: five top-level destinations and three cross-feature jumps don't pay for ten extra modules. Revisit that choice if a feature ever needs to hide its keys.

## Target layout

| Module | Kind | Takes from today's tree |
|---|---|---|
| `:core:model` | JVM, no `android.*` | `editor/*` (Timeline, Keyframes, Markers, AutoCut, Subtitles, AnimatableParams, EditorHistory, ClipTransition); the pure `data/*` types (`Preset`, `VideoTemplate`, `TemplateFormat`, `PerformanceTake`, `EditorProjectJson`, `PlaylistFormats`); every repository **interface** (`PresetRepository`, `SessionRepository`, `TakeRepository`, `PlayerPrefsRepository`, `FavouritesRepository`, `UserDataRepository`); `DeviceTrack`, `LyricLine`, `PresetLink` (today in `ui/`) |
| `:core:domain` | JVM | Use cases pulled out of controllers: scene choice from audio features (`AutoVisualsController`, `analysis/SceneSuggester`); template render planning (`VideoTemplate.kt` "Plans a single render"); auto-cut. Named `VerbNounUseCase` |
| `:core:data` | Android lib | `data/*Store`, `TemplateRepository`, `TextureStore`, `MilkPackImporter`, `NativeTags`, `GeodePrefsFiles`, `AtomicWrite`; the MediaStore and SAF parts of `MusicLibraryController`, `DeviceTrackQuery`, `TrackLibrary`; `ThemeStore` and `VizStateStore` persistence; Hilt `@Binds` for every repository |
| `:core:playback` | Android lib (data) | `playback/*`, `audio/*` (capture, AudioBus, FX), `QueueController`, `SleepTimer`; `PlaybackService` stays here, and `:app` declares it in the manifest |
| `:core:export` | Android lib (data) | `export/*`, `ExportController`, `TakeController`, `TrackAnalysisController` |
| `:core:designsystem` | Android lib | `ui/theme/*` (split `ThemePackCatalog` into per-pack files or a JSON asset), `Crystal*`, `Glass`, `StoneSurface`, `ColorDerive`, `PredictiveDismiss`, the composable half of `AppTheme.kt` |
| `:core:navigation` | JVM | `@Serializable` `NavKey`s for every destination, plus a `Navigator` interface |
| `:feature:player` | Android lib | `PlayerScreen`, `PlayerPanels`, `Lyrics`, `PlaybackSettings`, `PlayerViewModel` |
| `:feature:library` | Android lib | `LibraryScreen`, `LibraryBrowse`, `TrackLibrary` UI, `FolderTree`, `SmartPlaylistEditor`, `DuplicatesTab`, `TrackInfoEditor`, `LibraryViewModel` |
| `:feature:visuals` | Android lib | `VisualsHub` (split), `VisualizerScreen`, `CustomizeTabs` (one file per tab), `PaletteMaker`, `BackgroundSheet`, `TemplatesSheet`, `EnginePlumbing`, `VisualsViewModel` |
| `:feature:studio` | Android lib | `StudioScreen` (split), `StudioRender` (`StudioHub`), `ui/studio/*`, `StudioPreview`, `LoopRenderSheet`, `ExportHost`, `RenderBar`, `StudioViewModel` |
| `:feature:settings` | Android lib | `*Settings.kt`, `AppSettingsTab`, `SettingsViewModel` |
| `:feature:onboarding` | Android lib | `FirstRun`, `BootIntro`, `TutorialOverlay`, `TutorialStep`, `SafetyConsent` |
| `:app` | Application | `MainActivity`, `GeodeApp`, top-level `NavDisplay`, nav bar and rail (`AppShell` reduced to the shell), the `Navigator` implementation, DI wiring, `wallpaper/*`, `widget/*`, the manifest |

`:engine:*` and the native core stay as they are. The pure files inside `:engine:scenes` (`SceneParams`, `SceneIds`, `ParamKeys`, `CustomizeTab`, `VisualStyleCatalog`, `AudioFeatures`, `FeatureTimeline`) are what `:core:model` needs. Move them into a JVM `:engine:scene-model` that `:engine:scenes` re-exports with `api`, so `:core:model` never depends on an Android library.

Dependency rule: `feature → core:{domain,data,playback,export,designsystem,navigation,model}`, `core:data|playback|export → core:model + engine`, `core:domain → core:model`. Features never depend on features. `:app` depends on everything.

## Order (each step is a separate PR, and `AGENTS.md` changes with it)

1. **Fix the reverse dependencies.** `data/SmartPlaylistStore` and `playback/LibraryTree` import `ui.DeviceTrack`. `data/TemplateFormat` imports `ui.PresetLink`. `editor/Subtitles` imports `ui.LyricLine`. `wallpaper/*` import `ui.ThemeStore`. `widget/NowPlayingWidget` imports `ui.MainActivity`. Move each type down. The widget gets a launch-intent provider instead.
2. **`:core:model` and `:engine:scene-model`.** Replace `org.json` (part of the Android platform, so not pure) with `kotlinx.serialization` in `TemplateFormat`, `EditorProjectJson`, `PerformanceTake` and `PresetStore`'s JSON. Shared presets and templates are a file format: add golden-file tests first, so old files still load.
3. **`:core:designsystem`.** Mechanical; it holds no state.
4. **Dissolve `PlayerSession` and `GeodeContainer`.** Each controller becomes a repository, or a use case when it holds no state. It is bound in Hilt and is `@Singleton` only when it holds shared mutable state. ViewModels inject repositories, not `PlayerSessionProvider`. `PlayerSession.shutdown` currently does four things: flush store writes, release the `PlaybackEngine` UI hold, stop the foreground service and cancel the poll scope. Each one moves to the repository that owns it.
5. **Navigation 3 in `:app`,** while screens still live in `:app`. `GeodeDestination` becomes `NavKey`s in `:core:navigation`. Bottom-bar tabs get one back stack each. `geode://preset` and `geode://template` deep links become matchers. Add navigation regression tests.
6. **Extract features,** smallest first: settings, onboarding, library, player, studio, visuals.

## Consequences

- **Do step 4 before step 5.** `PlayerSessionProvider`'s KDoc relies on all five ViewModels sharing the Activity's store and being cleared together, and `PlayerViewModel.onCleared` tears the shared session down. Nav3's per-entry `ViewModelStore` breaks that: popping the Player entry would kill the session the other screens are still reading.
- `ExportRun.scope` and `PlaybackEngine` are process-scoped on purpose: renders and playback outlive the Activity. They become `@Singleton` holders in `:core:export` and `:core:playback`. They do not become entry-scoped ViewModels.
- The injected `Navigator` must hold a saveable back stack (`rememberNavBackStack`, or a `NavigationState` per tab). The official modular-Hilt recipe keeps its back stack in an in-memory list, which loses the user's place when the process dies.
- `wallpaper/*` and the dream build their own `VisualizerView` and read `AudioBus` directly. They stay outside the Nav3 graph.
