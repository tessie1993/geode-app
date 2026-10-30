# Architecture finish plan (2026-10-30)

Compares the code with [`ANDROID_ARCHITECTURE_GUIDE.md`](ANDROID_ARCHITECTURE_GUIDE.md),
[`ANDROID_ARCHITECTURE_REFERENCE.md`](ANDROID_ARCHITECTURE_REFERENCE.md) and
[ADR 0001](adr/0001-target-module-split.md), and sorts the gaps into what must land before the
Play release and what can wait. Vocabulary (module, interface, seam, adapter, depth, leverage,
locality) is the deep-module vocabulary of the `codebase-design` skill.

The test applied to every item: **does it change what a Play user sees, or the risk of a crash, ANR
or lost data?** If not, it waits, however much it offends the guide. A module split shipped the week
before release buys no user anything and moves every file other sessions are editing.

## Gaps, by guide rule

| # | Gap (from AGENTS.md "breaks the guide") | Deep-module reading | Play risk |
|---|---|---|---|
| G1 | Reverse dependencies into `ui`: `data/SmartPlaylistStore`, `playback/LibraryTree` → `ui.DeviceTrack`/`DeviceTrackQuery`/`TrackLibrary`/`LibraryTrack`; `data/TemplateFormat` → `ui.PresetLink`; `editor/Subtitles` → `ui.LyricLine`; `wallpaper/*` → `ui.ThemeStore`; `widget/NowPlayingWidget`, `audio/PlaybackCaptureService`, `export/ExportService` → `ui.MainActivity` (the last two by fully qualified name, missed by the AGENTS.md list) | The pure types sit on the wrong side of the seam, so no lower module can be lifted out without dragging `ui` with it. `MainActivity` is a concrete class used where only "the app's launch intent" is meant: one adapter behind no seam | None directly. It blocks every later step (`:core:model` needs it) |
| G2 | Data layer in `ui`: `*Controller`s, `VizStateStore`, `AutoVisualsPrefsStore`, `DeviceTrackQuery`, `UserDataRepository`; composables call `contentResolver` (`StudioScreen`, `TimelineLanes`, `TrackInfoEditor`, `DuplicatesTab`, `FolderSettings`, `LoopRenderSheet`) | Shallow: storage detail leaks into screens. Each composable that opens a stream is one more place that can block Main | **Medium**: `contentResolver` on Main is an ANR source. Audit those six call sites for main-thread I/O |
| G3 | God object: every ViewModel reads `PlayerSession` (~20 hand-built controllers), beside the `GeodeContainer` service locator | `PlayerSession` is deep for callers (one handle to everything) but has no locality: its interface is the union of 20 controllers, and nothing can be tested without building all of them | Low today. It is what makes Nav3 unsafe (ADR 0001 § Consequences) |
| G4 | One-shot events: `PlayerViewModel.vizApply`, `morphFade` as `SharedFlow` | Events can drop while the UI is stopped (guide: model them as state) | Low: drops a visual morph, not data |
| G5 | Navigation: enum + `when`, no back stack | Fine for five tabs. System back already returns to Player (`AppShell` `BackHandler`); sheets use `PredictiveBackHandler` | **None** for release. Nav3 is about deep links, adaptive panes and testability |
| G6 | Mixed module: `:engine:scenes` is an Android library holding mostly pure Kotlin; `editor/*` and `data/*` use `org.json` | Blocks pure JVM tests of file formats | Low. File-format tests (golden files) are the part with user value: shared presets and templates must keep loading |
| G7 | Quality: 2 use cases, few ViewModel/repository tests, hand-written baseline profile, no `reportFullyDrawn`, JankStats, Macrobenchmark, GWP-ASan | n/a | **Medium**: startup and stability are Play vitals. Owned by the play-readiness work, listed here so it isn't lost |

## Finish plan

### Before the Play release

1. **G1: fix the reverse dependencies** (ADR 0001 step 1). Mechanical, no behaviour change, touches
   none of the files the QA-fixes branch is editing except one line in `export/ExportService`.
   Move `DeviceTrack`, `LibraryTrack`, `LyricLine`, `PresetLink` down; move `TrackLibrary` and
   `DeviceTrackQuery` (both storage) into `data`; give the wallpaper and dream a reduced-motion read
   that doesn't need `ui`; replace `MainActivity` references with the package's launch intent.
   Afterwards, nothing outside `ui`, `di` and `GeodeApp` imports `dev.geode.ui`; a CI check keeps it so.
2. **G2, ANR half only:** move the six composables' `contentResolver` calls behind the owning
   ViewModel or repository on `Dispatchers.IO`. Do it after the QA-fixes branch merges
   (`StudioScreen` and friends are in flux).
3. **G6, golden files only:** freeze today's preset, template and project JSON as test fixtures, so
   any later serialization change is caught. No migration to `kotlinx.serialization` yet.
4. **G7:** `reportFullyDrawn` once the player is usable, a generated baseline profile, GWP-ASan in
   debug. Coordinate with play-readiness; don't duplicate.

### After the release (in ADR 0001 order)

5. `:core:model` + `:engine:scene-model`, then `kotlinx.serialization` behind the golden files (G6).
6. `:core:designsystem` (after the black-glass restyle has settled).
7. Dissolve `PlayerSession` and `GeodeContainer` into Hilt-bound repositories and use cases (G3),
   one controller per PR, smallest first. Turn `vizApply`/`morphFade` into state on the way (G4).
8. Navigation 3 (G5), only after 7: per-entry `ViewModelStore`s would otherwise tear down the shared
   session when Player is popped. Per-tab back stacks, `geode://` deep links as matchers, the paywall
   as a dialog entry.
9. Feature modules, smallest first.

## Decisions taken with the user (2026-10-30)

- First step: G1, the reverse-dependency fix. Navigation 3 waits until after the release.
- Pure shared types go to a new `dev.geode.model` package (future `:core:model`).
- `ThemeStore` moves to `data` with `GuiPrefs` and its enums, not a one-key reader.
- The widget and services open the app through the package launch intent.
- CI enforces the layer rule (`tools/layer-gate.sh`).

Recorded in [ADR 0002](adr/0002-release-before-restructure.md).
