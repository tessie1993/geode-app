# UI polish audit, 2026-10-30

Release polish for edge-to-edge, adaptive layouts (phones, foldables, tablets, ChromeOS), Compose
performance and accessibility. Read-only: this document changes no code. The fixes the user picks
land in the same PR.

**Method.**
- Code read against the Android `edge-to-edge`, `adaptive` and `jetpack-compose-performance`
  skills, and WCAG 2.2 AA (`design-skills:accessibility-audit`). Contrast ratios are computed with
  the WCAG formula.
- Emulator runs on build 316 (`8588501`, `claude/geode-finish`) on a `medium_tablet` AVD
  (2560×1600, 320 dpi), resized with `wm size` / `wm density` to these windows:
  - tablet landscape, 1280×800 dp
  - phone landscape, 914×411 dp
  - phone portrait, 411×914 dp, at font scale 2.0
  - foldable unfolded, 673×841 dp
- Tools: `android install`, `android layout`, `android screen capture`, `adb input`.

**Ownership.** Some findings touch code that other sessions hold. These are listed but are not fixed
here:
- *customize-macros* owns `CustomizeTabs.kt`, `CustomizePanel` in `VisualsHub.kt`, and how the
  `CrystalControls` sliders behave.
- *studio-export* owns the export dialog in `SettingsDialog.kt`, `LoopRenderSheet`, and moving export
  into Studio.
- *qa-fixes* has uncommitted edits to `AppShell.kt`, `VisualsHub.kt`, `VisualizerScreen.kt`,
  `PlayerScreen.kt`, `CustomizeTabs.kt`, `SettingsDialog.kt`, `CrystalControls.kt` and
  `TutorialOverlay.kt`. Fixes in those files wait until `claude/qa-fixes` merges.

Severity: **H** blocks a good release, **M** is worth fixing now, **L** is a nice to have.

Screenshots: [`ui-polish/`](ui-polish/).

---

## 1. Edge-to-edge

`MainActivity` calls `enableEdgeToEdge` with `SystemBarStyle.dark(TRANSPARENT)` for both bars. That
gives light icons and no contrast scrim on the navigation bar, which is right for an app that is dark
in every mode. The paywall is a full-screen `Dialog` with `decorFitsSystemWindows = false` and handles
its insets itself. Onboarding uses `safeDrawingPadding`. The rest:

| # | Sev | Where | Finding | Fix |
|---|---|---|---|---|
| E1 | M | `AndroidManifest.xml` `.ui.MainActivity`; no `imePadding` / `fitInside(Ime)` anywhere | No `windowSoftInputMode="adjustResize"`, and nothing in the UI reacts to the IME. There are 29 text fields in 11 files. The ones low on screen can end up behind the keyboard: `TemplatesSheet`, `TrackInfoEditor`, `SmartPlaylistEditor`, `PaletteMaker`, the Studio `EditorToolbar`. | Add `adjustResize`, plus `Modifier.fitInside(WindowInsetsRulers.Ime.current)` (or `imePadding()` before `verticalScroll`) on the containers of those sheets and editors. |
| E2 | M | `AppShell.kt:401` (rail shell), `VisualizerScreen.kt:359` (top bar), `:207` (transport) | Only `statusBarsPadding` and `navigationBarsPadding` are applied. In landscape, a display cutout sits on the left or right, and the rail and the live view's Collapse/PiP buttons can land under it. | Use `WindowInsets.safeDrawing` (or add `displayCutout`) on the rail Row and on the live-view chrome. |
| E3 | L | `AppShell.kt:388-389` | `Box(Modifier.padding(pad))` without `consumeWindowInsets(pad)`. Inner screens that add `statusBarsPadding` get it twice. Lists are padded instead of using `contentPadding`, so they stop at the bar instead of scrolling behind it. | `padding(pad).consumeWindowInsets(pad)`. Pass the bottom inset to each list's `contentPadding` where the bar is translucent (`barOpacity` < 1). |
| E4 | L | `VisualizerScreen.kt` | The live view is fullscreen but keeps the status and navigation bars (QA log #15). | Hide the system bars with `WindowInsetsControllerCompat` (`BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE`) while the chrome is hidden, and show them with it. |
| E5 | M | `CrystalSlider` (owned by customize-macros) | A slider dragged from the left edge triggers system Back (QA log #12). No `systemGestureExclusion` anywhere. | `Modifier.systemGestureExclusion()` inside `CrystalSlider`. This covers 32 call sites. |

## 2. Adaptive

`AppShell` swaps the bar for a `NavigationRail` once the width class is not COMPACT, so at 600 dp and
up. Library becomes a two-pane screen (Library | Player) at the same breakpoint. No other screen
reacts to the window size: no height class, no maximum content width, no columns. There are no
`@Preview`s or screenshot tests. Nav3 and multi-pane scenes are the architecture session's plan
(ADR 0001), so they are out of scope here.

| # | Sev | Where | Finding | Evidence | Fix |
|---|---|---|---|---|---|
| D1 | **H** | `PlayerScreen.kt:182` | The hero art is `fillMaxWidth().aspectRatio(1f)`. On a 1280 dp tablet it is ~1180 dp tall, so the title, transport and quick actions are two screens down. The same happens in the right pane of the two-pane Library. | `tablet-player.jpg`, `tablet-library-visuals-settings.jpg` | Cap the art (`widthIn(max = 480.dp)` plus `heightIn(max = …)`). At EXPANDED width, or COMPACT height, lay the hero and the transport side by side. |
| D2 | **H** | `AppShell.kt:244-262` | Two-pane Library starts at MEDIUM width. That includes phone landscape (914×411) and an unfolded foldable (673 dp). Both panes get cramped: the Player pane shows only artwork, and the sort buttons break mid-word. | `phone-land-library.jpg`, `foldable-library.jpg` | Two panes only at EXPANDED width and not COMPACT height; rail plus a single pane at MEDIUM. |
| D3 | M | `AppShell.kt:419` | In two-pane Library the mini player still shows above the Library pane, so play/pause appears twice. | `foldable-library.jpg` | Hide the mini player while the Player pane is visible. |
| D4 | M | Visuals presets list, Settings tabs, Library list | Rows and sliders stretch to 1180 dp on a tablet, and the preset Apply arrow sits 1100 dp away from its name. | `tablet-library-visuals-settings.jpg` | A shared `Modifier.readableWidth()` (`widthIn(max = 720.dp)`, centred) for forms and Settings. `LazyVerticalGrid(GridCells.Adaptive(320.dp))` for the presets and styles lists. |
| D5 | M | Every screen header (`GlowTitle` and its overline) | At COMPACT height (phone landscape, 411 dp) the header plus the Settings Pro card take about half the screen, and they don't collapse on scroll. | `p1` (Settings, phone landscape) | Collapse the header on scroll (`enterAlwaysScrollBehavior`), or drop the overline at COMPACT height. |
| D6 | M | `VisualizerScreen.kt:202-336` | In phone landscape the transport panel covers about 55% of the visual and doesn't auto-hide (QA log #15). | `phone-land-live.jpg` | At COMPACT height use a one-row transport (seek bar with the buttons inline), and auto-hide the chrome after ~4 s of no input when `!reducedMotion`. |
| D7 | M | `AppShell.kt:394-423` | The rail shell ignores `gui.playerPosition` and `gui.barOpacity`, which only the bar shell reads. The rail's items aren't centred and it has no header slot for the search FAB. | code | Pass `gui` through; `NavigationRail(header = …)`; `Arrangement.Center` on tall windows. |
| D8 | L | `AppShell.kt:244` | `currentWindowAdaptiveInfo().windowSizeClass.windowWidthSizeClass` is the deprecated accessor. | code | `windowSizeClass.isWidthAtLeastBreakpoint(WIDTH_DP_EXPANDED_LOWER_BOUND)`. |
| D9 | L | TutorialOverlay, SafetyConsent | The tutorial card runs the full width of a tablet (2500 px). | `t07` | `widthIn(max = 560.dp)`, centred. |
| D10 | L | `VisualsHub.kt:122` | The Visuals overline says "MuzViz". Every other screen says "MUZVIZ STUDIO PRO", and this one is a hard-coded string. | screenshots | Use the shared overline string. |
| D11 | L | — | No `@Preview`s at all, so there is no way to check form factors without an emulator. | — | A `@FormFactorPreviews` annotation (phone, foldable, tablet, desktop) on the five top-level screens. Screenshot tests later. |

ChromeOS and desktop windowing: no `screenOrientation` lock and `configChanges` covers resize, which
is good. `resizeableActivity` defaults to true. Keyboard and mouse: the rail and the buttons are
focusable. There is no hover state and no keyboard shortcut for play/pause (space) or seek (←/→).
Space/←/→ on the Player would be worth adding (L).

## 3. Compose performance

Two things are already right. The 60 Hz `AudioFeatures` flow never goes through composition
(`VisualizerEngineBindings` writes the renderer from a `LaunchedEffect`). Animated values in
`dismissTransform` and `BootIntro` are read in `graphicsLayer` or the draw phase. Strong skipping is on
(Kotlin 2.4 compose plugin, no stability config needed). The hot spots:

| # | Sev | Where | Problem | Rate | Fix |
|---|---|---|---|---|---|
| P1 | **H** | `AppShell.kt:197-271`, `:305` | The shell and the current screen stay composed, laid out and drawn underneath the fullscreen visualizer. On Player, `LiveSpectrum` keeps its 20 Hz loop running, invisible, beside the GL thread. | 20 Hz plus ticks, the whole time the live view is up | Don't place the shell while `expanded && !onSecondScreen`; at least `LiveSpectrum(live = false)`. |
| P2 | **H** | `PlayerSession.kt:557-576`, `:1368-1383` | `refresh()` builds a new `PlayerUiState` (new `positionMs`) every 500 ms. That recomposes `AppRoot`'s theme content (rebuilding `navEntries`, `MiniPlayer`), all of `VisualizerScreen`, and `PlayerScreen`, `PlayerHero` and `TransportCard`. | 2 Hz whole screens, during playback | Position goes in its own `StateFlow<Long>`. `WaveformSeekBar` and the mini player's progress take `() -> Long`; the time labels are their own leaf composable. |
| P3 | **H** | `CustomizeTabs.kt:222-251`, `:862-875` (owned by customize-macros) | Each slider's `onChange` captures `p`, so every edit makes every lambda new: 15-40 slider rows recompose per drag event, and each runs `"%.2f".format`. | 60-120 Hz while dragging | `onEdit: (SceneParams.() -> SceneParams) -> Unit`, plus `remember(value)` for the label. |
| P4 | **H** | `LibraryScreen.kt:586-597` | Dragging a playlist row reads `dropIndex` in composition and recomposes a `Column` of every row. Each row runs `tracks.firstOrNull { it.uri == uri }` over the whole library. | Per drag event, O(rows × library) | `associateBy` map, `derivedStateOf` for the index, rows as lazy `items`. |
| P5 | **H** | `pro/PaywallScreen.kt:674-716` | The hero animation is read in composition, so it recomposes every frame and allocates 3 radial gradients and 56 `Brush.linearGradient`s per frame on the onboarding conversion screen. | 60 Hz while the paywall is open | Read `.value` in the Canvas lambda; `drawWithCache` for the brushes. |
| P6 | M | `PlayerScreen.kt:468-535` | `LiveSpectrum`: `val bars by produceState` recomposes at 20 Hz, with a new `Brush` per draw and `copyOf()` per tick, and it keeps running while the app is stopped. | 20 Hz | Read `bars.value` inside the draw; `drawWithCache`; gate it on `STARTED`. |
| P7 | M | `EnginePlumbing.kt:85-97`, `:47-61` | `LaunchedEffect(viz.params)` relaunches on every param edit (pinch is 120 Hz), one frame late. The features collector keeps running on Main while the Activity is stopped. | per edit, and 60 Hz in the background | One `distinctUntilChanged` collector; `repeatOnLifecycle(STARTED)`. |
| P8 | M | `LibraryScreen.kt:197-199` | Every `TrackRow` calls `geodeViewModel()` and collects `trackOverrides` itself. | per row during a fling | Collect once in the list and pass `overrides[uri]`. |
| P9 | M | `ExportController.kt:226-229`, `:343/368` (owned by studio-export) | Export progress is published per encoded frame and read at the top of `ExportHost` and `StudioScreen`. | frame rate during a render | Publish only on whole-percent changes; read it in a leaf. |
| P10 | M | `QueueController.kt:245-272` | The queue is rebuilt and compared every 500 ms on Main. | 2 Hz, O(N) | Rebuild it from `Player.Listener` timeline, transition and shuffle callbacks. |
| P11 | M | `theme/StonePress.kt:30-40` | The press scale is read in composition, so `CrystalButton` recomposes for each animation frame. | 10-20 per press, about 130 buttons | `graphicsLayer { scaleX = s.value … }`. |
| P12 | M | `studio/LaneRows.kt:86-148`, `studio/Playhead.kt:77-88` | Scrubbing recomposes every lane and rebuilds `snapContext` per lane. | per drag event | `playheadMs: () -> Long`; build the snap context in the gesture handlers. |
| P13 | M | `app/src/main/baseline-prof.txt` | The hand-written profile is missing `VisualizerScreenKt`, `EnginePlumbingKt`, `PlayerPanelsKt`, `Crystal*`, `GlassKt`, `ui/theme/**`, `LibraryScreenKt`, `VisualsHubKt` and the `ui/*Controller`s. | startup and first live view | Add the rules now; generate the profile with Macrobenchmark later. |
| P14 | L | `LibraryScreen.kt:164-166`, `:360`; `VisualsHub.kt:210-211`, `:347` | `groupBy`, sort and dedupe run on every recomposition, including each search keystroke. | per keystroke | `remember(inputs)`. |
| P15 | L | `VisualsHub.kt:611`, `StudioScreen.kt:228`, `PlayerPanels.kt:189`, `LibraryScreen.kt:414` | Lazy items have no `key`, or no `contentType` on mixed lists. | scroll / reorder | Add keys and content types. |
| P16 | L | `studio/KeyframeLane.kt:100`, `MarkerLane.kt:108`, `CurveEditor.kt:125` | A new `Path` is allocated per item per draw. | per draw | Reuse one `Path` with `rewind()` / `drawWithCache`. |
| P17 | L | `VisualizerScreen.kt:118-129` | `onGloballyPositioned` allocates a `Rect` per frame during predictive back. | per frame of the gesture | `onLayoutRectChanged`. |
| P18 | L | `TrackAnalysisController.kt:122` | Analysis progress goes through `vizState` about 100 times per track. | 100 per track | Give it its own flow. |
| P19 | L | `PlayerSession.kt:372-385` | The renderer's `progress` comes from the 500 ms position, so progress-driven visuals step twice a second. | 2 Hz steps | Read `player.currentPosition` or interpolate. |

## 4. Accessibility (WCAG 2.2 AA)

A correction to the QA log (#7): `FilledTonalIconButton` and `IconButton` merge their icon's
`contentDescription` into the clickable node, so TalkBack reads "Collapse, button". uiautomator shows
the unmerged tree, which is why the label appeared on a child. The real defect is `CrystalPlayButton`
and `CrystalButton`: a `Box.clickable` with no `role`.

Already right: `WaveformSeekBar` (range info, `setProgress`, description), playlist reorder
alternatives, decorative images set to `null`, live regions for the playback notice and the paywall
message, onboarding headings, reduced motion (`LocalReducedMotion` honours the animator scale), and the
core tokens: OnSurface 16.9:1, OnSurfaceVariant 8.9:1, Primary 12.0:1 on black.

| # | Sev | Where | Finding | WCAG | Fix |
|---|---|---|---|---|---|
| A1 | **H** | ~53 Switch/Checkbox rows: `CustomizeTabs.kt:884` `CheckRow` (customize-macros), `TutorialOverlay.kt:100`, Settings (`AutoVisualsSettings`, `BehaviorSettings`, `LookSettings`, `PlaybackSettings:283`, `AudioSettings`, `EqualizerSettings`, `ExportSettings`, `ExternalAudioSettings`, `HelpSettings`), `StudioScreen:662`, `TrackInfoEditor:229`, `studio/CurveEditor:171`, `LoopRenderSheet:221` (studio-export) | The control and its label are siblings, so TalkBack says "Off, switch" with no name (QA log #13). | 1.3.1, 4.1.2 | Add `LabeledSwitch` / `LabeledCheckbox` to `CrystalControls`: `toggleable(role = …)` on the row, `onCheckedChange = null` on the control. `BackupSettings.kt:140` already does it. |
| A2 | **H** | `CrystalSlider` (`CrystalControls.kt:150`), `LabeledSlider` (~119 calls) (customize-macros) | Sliders have no name and announce "26 percent" instead of the value. | 1.3.1, 4.1.2 | Add a `label` param; `semantics { contentDescription = label; stateDescription = valueText }`. |
| A3 | **H** | `PlayerScreen.kt:341/368`, `VisualizerScreen.kt:241/266`, `AppShell.kt:467` | Shuffle and Repeat state is shown only by tint, repeat-all and repeat-one share one icon (QA log #14), and the mini player always says "Play/pause". | 1.4.1, 4.1.2 | `StoneIcon.REPEAT_ONE`; `stateDescription` off/all/one; `IconToggleButton` for shuffle; play or pause label by state. |
| A4 | **H** | `CrystalButton` (`CrystalControls.kt:82`, ~128 sites), `CrystalPlayButton` (`:129`) | `clickable` has no `role`, and PlayButton's label is only on the child icon. | 4.1.2 | `role = Role.Button`; `semantics { contentDescription }` on the PlayButton box. |
| A5 | **H** | `pro/PaywallScreen.kt:316`, `:734` | White 16 sp text on the CTA gradient 8B5CF6→EC4899 measures 4.23:1 and 3.53:1. | 1.4.3 | 7C3AED→BE185D (5.7:1, 6.0:1). |
| A6 | **H** | `VisualizerScreen.kt:87`, `:357`, `:206` | `chromeAlpha` floors at 0.25. E6E6EB text over a 25% panel on a bright frame measures 1.38:1, and the top bar has no scrim. | 1.4.3 | Floor the panel at ~0.75, or add a `glassScrim` behind the top bar. |
| A7 | **H** | `TutorialOverlay` (`AppShell.kt:334`), `SafetyConsent` / `FirstRunGate` (`:317-330`) | They draw in the app's window, so TalkBack can swipe into the shell underneath (the emulator layout tree lists the rail and the Player behind the consent screen). The tutorial's `clickable {}` is a no-op "double-tap to activate" node. | 2.4.3, 4.1.2 | Host them in a `Dialog`, or mark the shell `hideFromAccessibility()` while they're up; replace `clickable {}` with `pointerInput { detectTapGestures {} }`. |
| A8 | M | `VisualizerScreen.kt:103` | Tap to show or hide the chrome is a bare `detectTapGestures`. Once the chrome is hidden, TalkBack has nothing to focus to bring it back. | 2.1.1 | `semantics { onClick(label = "Show controls") { … } }`. |
| A9 | M | `PlayerScreen.kt:686` `QuickActionShell`, `VisualizerScreen.kt:462` `PanelChip`, `StudioScreen.kt:839` `StudioChip`, `PlayerPanels.kt:309` | Toggle and tab state is shown only by colour. | 1.4.1, 4.1.2 | `toggleable(role = Switch)`, `selectable(role = Tab)`, `semantics { selected }`. |
| A10 | M | `GlowTitle` (`Crystal.kt:114`), `SettingsGroup` (`AppSettingsTab.kt:93`), `SectionHeader` / `CardSubHeader` (CustomizeTabs, customize-macros), `StudioSection` (`StudioScreen.kt:785`) | Screen and section titles are not headings (~70 headers). | 1.3.1 | `semantics { heading() }` in the 4 shared components. |
| A11 | M | `SettingsDialog.kt:129`, `studio/TimelineLanes.kt:543` (studio-export) | Export done or failed is not announced. | 4.1.3 | `liveRegion = Polite` on the status text. |
| A12 | M | `studio/ClipStrip.kt:80`, `KeyframeLane.kt:59`, `MarkerLane.kt:61`, `Playhead.kt:43`, `CurveEditor.kt:102` | The timeline canvases are pointer-only. | 2.1.1, 2.5.7 | Per-lane `contentDescription` and `customActions` (select, split, nudge); `setProgress` on the playhead. |
| A13 | M | `theme/GeodeTokens.kt` `GeodeColors.Outline` 3A3B42 | 1.88:1 on black, and the unchecked Switch thumb is 1.35:1 against its track. | 1.4.11 | Raise it to ~74767F (4.6:1). |
| A14 | M | `PlayerPanels.kt:215` (past lyrics, 0.35 alpha, 2.63:1), `CustomizeTabs.kt:154` (LockChip 0.45, 3.75:1) | Low-alpha text fails contrast. | 1.4.3 | Alpha ≥ 0.55, or `onSurfaceVariant`. |
| A15 | M | `theme/GeodeTheme.kt:57-61` | A user's font colour is checked for contrast, but `onSurfaceVariant = font@0.78` is not, so a 4.69:1 grey becomes 3.18:1. | 1.4.3 | Check the blended colour, or pick the alpha so it stays ≥ 4.5:1. |
| A16 | M | `CustomizeTabs.kt:148-161` LockChip (customize-macros) | Hard-coded strings and no toggle role. | 4.1.2 | `toggleable(role = Switch)`, plus string resources. |
| A17 | M | `StudioPreview.kt:101` | Unlabelled while playing: the label sits on the PlayArrow icon, which only shows when paused. | 4.1.2 | `clickable(role = Button, onClickLabel)` plus `stateDescription`. |
| A18 | M | Library sort `CrystalSegmented` (`LibraryScreen.kt:155`), nav bar labels, `PlayerScreen.kt:678/694` QuickAction `width(84.dp)`, `LookSettings.kt:175`, `PlayerScreen.kt:291` | At 200% font the sort buttons break mid-word ("Arti st", "Albu m", "Len gth"), and the nav labels and "Shuffle all" truncate. At 673 dp the sort buttons break mid-word even at 100%. | 1.4.4, 1.4.10 | Make the sort row scrollable chips, or a dropdown when narrow. `widthIn(min = 84.dp)` with `maxLines = 2`. Nav bar: `alwaysShowLabel = false` at large font scale. |
| A19 | L | `CrystalButton(compact = true)` (minHeight 36 dp, ~70 uses), `PanelChip`, `StudioChip` (~28 dp), Library "Back" link (`LibraryScreen.kt:340`, ~24 dp) | Below the 48 dp Android target (these pass the WCAG 2.5.8 24 dp minimum). | 2.5.8 | `minimumInteractiveComponentSize()` on the clickable. |
| A20 | L | `VisualsHub.kt:296-353`, `:1231-1335` | ~16 literal content descriptions ("Apply", "Delete" with no preset name) and ~87 literal `Text("…")`. | 2.4.6, l10n | `stringResource(R.string.x, presetName)`. |
| A21 | L | `LibraryScreen.kt:647/651`, `PlayerPanels.kt:325`, `AppShell.kt:448`, `PlayerScreen.kt:176/454` | "Up"/"Down" with no track name; a literal "★" (TalkBack reads "black star"); expand rows have no `onClickLabel`. | 2.4.6 | Name the track; `onClickLabel = "Open player"`. |

## 5. Suggested order

1. **Shared-component a11y** (A1 for non-Customize rows, A3, A4, A10, A13, A14, A19). Few files,
   a large share of the issues.
2. **Adaptive layout** (D1, D2, D3, D4, D9, D10, E2). Biggest visible win on tablets and foldables;
   mostly `AppShell` and `PlayerScreen` (blocked on qa-fixes).
3. **Performance** (P1, P2, P5, P6, P7, P8, P11, P13). P1 and P2 are the playback-time wins.
4. **Edge-to-edge and IME** (E1, E3, E4).
5. **Owned elsewhere:** hand A2, E5, P3 and A16 to customize-macros; P9 and A11 to studio-export.
