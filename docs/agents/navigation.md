# Navigation today

One fact per line, grouped by screen. Add a fact as a new bullet under its screen.

## Top level

- `GeodeDestination` enum in `ui/GeodeAppState.kt`: PLAYER, LIBRARY, VISUALS, STUDIO, SETTINGS.
- `GeodeAppState.dest` and `studioSection` are saved by name through `rememberSaveable`.
- A cold start opens on Settings › Look's "Open on" (`startDestination` in `ui/GeodeAppState.kt`).
- "Where you left off" is recorded by `rememberGeodeAppState`.
- Studio opens on the last tab used (`data/StudioPrefsStore`).
- While Visuals shows, `KeepScreenOnWhileShown` holds `FLAG_KEEP_SCREEN_ON` if Behaviour asks.
- `AppRoot` / `AppShell` in `ui/AppShell.kt` switch with `when (appState.dest)` and build the bar and rail from `GeodeDestination.entries`.
- The top level has no NavHost and no back stack; only the Visuals panel has one.
- Sheets and dialogs are local Compose state.
- All eight ViewModels live in the Activity's store.
- `PlayerSessionProvider`'s KDoc depends on that, and `PlayerViewModel.onCleared` shuts the session down.
- Read ADR 0001 § Consequences before you add per-destination stores.

## Shell chrome

- Every screen fills the window.
- The phone shell floats the mini player and the tab bar over the bottom as one matte glass slab, no gap or seam between them (`ui/ShellChrome.kt`).
- `DockSlab` draws the one glass and shadow; `FloatingMiniPlayer` and `FloatingTabBar` draw none.
- The mini player is a pill when the player is away (`MiniPlayerHandle`).
- The rail shell's mini player is a card of its own.
- `ShellChromeState` fades the mini player on every screen after `GuiPrefs.miniPlayerFadeSeconds` (6 s by default).
- A touch in the dock restarts the fade; a tap pins the mini player, swipe up opens Player, swipe down hides it.
- Every start of the activity wakes the mini player, and the countdown waits under the boot intro and first-run pages.
- On a phone's Visuals the mini player is docked under the options (`DockedMiniPlayer`: title, previous, play/pause, next, no art; `LocalDockedMiniPlayer`).
- There the options' glass runs on behind the tab bar, so sheet, player and tabs are one slab.
- The slab is kept while the options are up (`ShellChromeState.docked`, which holds the tab bar with no track loaded too).
- The slab fades together when the options are hidden or a drop-in covers them (`optionsHidden`).
- The dock's reserve stays the tab bar's place, so the options never slide.
- The accent follows the visual's palette.

## Studio

- Studio (`StudioHub` in `ui/StudioRender.kt`) owns every render, in two tabs (`StudioSection`): Render and My videos.
- There is no Edit tab or timeline (#227); a tab name `EDIT` saved before then restores to Render (`StudioSection.fromName`).
- Render is one stack: Source, Look, Format, Length and Save, over `RenderBar`.
- Source (`RenderScopePicker`): the song playing, play queue, a playlist, chosen tracks.
- Batches are Pro and run one track after another in `ExportController.startBatch`.
- Look: current or a preset.
- Format: a destination's defaults from Settings › Export, then each option; one ♛ per group.
- Length: whole, part, or Loop (`LoopRenderSheet` through `ExportHost`).
- `RenderBar` shows the summary and Render, then Open, Share and Show in folder.
- A free user's Pro choice offers the paywall or a free version.
- A single render first asks whether to add it to My videos, which keeps it as a project (`StudioViewModel.keepNextRenderAsProject`, answer kept in `StudioPrefsStore`).
- My videos (`ui/MyVideosTab.kt`) is one grid of renders (`StudioProject.renders`, with a frame as thumbnail); recordings are renders too.
- My videos has Undo on delete: the delete waits in `StudioViewModel` through `ui/PendingDeletes`, so leaving the tab mid-snackbar still deletes.
- Back retraces the tabs taken (a `NavHistory` of `StudioSection`s in `StudioHub`), then leaves Studio; it never stops a render.
- Settings › Export and the Visuals export icon only open Render (`GeodeAppState.openStudioRender`).
- Settings › Export › Record holds the recording's settings.
- Recordings are listed in Studio › My videos.

## Visuals: layout

- Visuals is `VisualsHub`, the root.
- `ui/VisualsStage.kt` is the visual, its gestures and stage icons.
- `ui/VisualsSheet.kt` is the options; `ui/MilkDropPage.kt` and `ui/TexturesPage.kt` are pages.
- Visuals is the visual at full window size under translucent options (`VisualsOptions`, its glass `GuiPrefs.barOpacity`).
- In portrait the options are a snap sheet (`SheetHeight`: hidden, peek, half, tall); a list at its top hands the drag to the sheet.
- In landscape (wider than tall, from the medium width class) the options are a right-hand panel, full height, 30–50 % wide, resized or hidden by its leading edge.
- One `VisualsNavState` drives both.
- The stage icons fade with the mini player through their alpha, staying composed.
- Stage icons: Settings, Studio and Fullscreen top-right; the Cast slot and a connected display left of them; ‹ › at the sides; Record bottom-right.
- A stage icon is drawn and takes touches only where all of it lies in the part of the stage the sheet or panel leaves in view, below the top row for the arrows and Record (`stageIconRoom`, `StageIconRoomTest`).
- MilkDrop opens on its overview cards: your presets open, the loaded preset's textures, Textures ›.

## Visuals: gestures

- Every gesture is read by one detector in `ui/VisualGestures.kt`.
- Swipes and the swirl count only from outside `WindowInsets.systemGestures`.
- Each gesture is also a TalkBack custom action.
- Each gesture runs the action Settings › Gestures & controls' map gives it (`BehaviourPrefs.actionFor`).
- Default: a tap anywhere wakes the chrome.
- Default: double tap hides or brings back the sheet.
- Default: sideways swipe is next/previous style (‹ › step the same).
- Default: swipe up is the mini player.
- Default: swipe down is `MacrosDrawer` (the macro bank's knobs; its edit icon opens Customize).
- Default: a hold through a red ring (~0.8 s) records a video.
- Default: two circles with one finger randomize everything: a style the user may keep, a preset made for it, every Customize category rolled, kept controls held (native `core/viz/RandomizeAll`, `geode_randomize_all_*`).
- The two-circle roll is Pro.
- Hidden and free (`ui/EasterEggs.kt`): a firm shake saves the visual alone to Pictures.
- Hidden and free: four taps on the heard beat roll Customize › Music.
- The shake and the tap-along are decided natively by `core/viz/Shake` and `core/viz/TapAlong`; `TapAlong` only reads the beat.
- Default: two fingers are touch play (the visualizer's touch field).

## Visuals: pages and back

- The sheet's pages (`VisualsPage`: Styles, Customize, Presets, MilkDrop; Textures opens from MilkDrop) are the app's first Navigation 3 back stack (`NavDisplay`, saved by name, no per-entry ViewModel decorator).
- Every Visuals navigation decision (the route, the sheet height, back order) is `VisualsNavState` in `ui/VisualsNavigation.kt`.
- There is one `VisualsKey` per page and sub-page on a `NavHistory` (`ui/NavHistory.kt`, the true-history pattern Studio reuses).
- On a `NavHistory` a tab goes forward and a key already on the route is returned to.
- Advanced's ‹ › arrows step sideways without a back entry.
- A tab raises the sheet to half.
- Customize's open category is a `VisualsKey.CustomizeCard` on the same history (‹ › step sideways, no back entry), so back closes it first and returns to the list where it was scrolled.
- Back order: a hidden sheet comes back, then an open Customize category closes, then the route in reverse to Styles, then leaves.

## Visuals: Styles and Presets

- Styles (`StylesTab` in `ui/StylesPage.kt`) is one family dropdown over a plain list of that family's styles (no stills, no search, no family inside a family).
- The families are one flat level in the native catalogue's order (`Catalogue.picker`, `picker` rows of `core/viz/Catalogue`): Silk, Life, Mycelium, Morphing, Shaders for every shader style outside a family, each shader family, MilkDrop, Fluid, Cymatics.
- The stage swipes styles in the same order.
- Your styles (on screen, favourites, recent, kept by `data/StyleShelfStore`) is the dropdown's first entry, with Random the icon beside it.
- Styles opens on the family on screen and keeps the last pick.
- A tap loads the style; a Pro one plays while the paywall offers it (`PlayerSession.leaveScenePreview` puts the chosen style back).
- Presets is `PresetsTab` in `ui/PresetsPage.kt`; its sheet, dialogs and share are in `ui/PresetActions.kt`.
- Presets is a text list with no previews (`PresetRow`: name, style, Built-in or Mine, the applied row lit with a check; one column on a phone).
- The list sits under chips: All, For this style, Mine, Built-in, each folder (`model/PresetShelf`).
- A tap applies a preset through the gate.
- A long press or TalkBack action opens the sheet: playlist, share, duplicate, rename, move, replace, delete.
- "Save look" is the one save path.

## Full screen, PiP, connected display

- There is no separate live view: full screen is Visuals with the sheet hidden (Fullscreen, the double tap).
- Player's full-screen button opens Visuals.
- A PiP window (auto-entered from Visuals while playing; `VisualizerPipCoordinator` in `MainActivity.kt`) shows the visual alone.
- PiP is never auto-entered on a TV.
- In PiP `VisualsHub(pip = true)` hides the sheet and `rememberShellChrome(held)` the chrome.
- A connected display (HDMI, USB-C, a wireless display; `ui/ConnectedDisplays.kt`) shows the visual alone in a `Presentation` (`SecondScreenCanvas`).
- The presentation is retried when Android drops it, and both screens are kept on.
- Which display, and whether, are native (`core/viz/StageRun`, `geode_stage_pick_display`/`geode_stage_presents`).
- The rule picks a presentation display that is not the phone's or the app's own, the user's pick, Pro as Cast (`ProFeature.CAST`).
- Its switch is a stage icon left of Settings while one is connected, a row in Visuals › Settings, and Settings › Connected display.
- The stage's view draws while somebody can see it, not only while the Activity is resumed.
- `ui/StageRun` packs lifecycle, PiP and second screen for the native rule `core/viz/StageRun` (`geode_stage_run`).
- A PiP window and a second screen keep moving under a paused Activity; a stopped one draws nothing.

## Lyrics

- Lyrics are Settings › Library & lyrics (`LyricsSettings`): web search in the browser, or an .lrc loaded per track by `LyricsLoader.attach`.

## The new-user tour

- The tour (`ui/tour`, drawn by `TourHost` from `AppRoot`) points at the real controls, never a carousel.
- It has four chapters (`model/Tour`).
- First steps: the Library tab, play a song, the Visuals tab; once, on a new install.
- Then Visuals, Customize and Studio, each the first time one of its controls is on screen.
- A screen marks a control with `Modifier.tourTarget(TourSpot)`.
- A step whose control is not laid out is passed over, never waited for.
- The shell's chrome stays awake while a pointer is up.
- Skip is on every step: in First steps it turns the whole tour off (one line says where to replay it); elsewhere it ticks that chapter.
- A chapter left half way resumes at its step.
- The one-time Pro offer follows the first Visuals chapter.
- Gesture tips (`LocalTourNudge`) are said once each: ‹ › (swipe), Fullscreen or the sheet's × (double tap), Record (hold), a tap on the mini player (swipe up), Quick knobs (swipe down), the dice (two circles).
- `VisualsHub` drops a tip whose gesture Settings › Gestures & controls has moved.
- Settings › Backup, help & about › Tutorials (`TutorialSettings`) replays any chapter, taking the user to its screen, or resets it all.

## Android TV

- On a TV the shell is the rail inside the overscan margins.
- On a TV focus glows in the accent.
- On a TV the stage takes the remote: D-pad steps styles while full screen, centre toggles the options (`StageKeys` gives each gesture a remote key, run through the same map).
- On a TV a key keeps the chrome awake.
- Phones are unchanged.
