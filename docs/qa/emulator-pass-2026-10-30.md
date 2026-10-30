# Emulator QA pass, 2026-10-30

Build: `claude/fix-merge-detekt` CI debug APK 303 (dfd7c09 = geode-finish after #91, #94, #97, #98, plus the detekt fix).
Device: emulator-5554, API 37, x86_64, GLES compute-es31. Tools: `android install`, `android layout`, `android input tap`, `android screen capture`, logcat.

| # | Area | Bug | Evidence | Status |
|---|---|---|---|---|
| 1 | Playback | The crash the user reported (`ForegroundServiceDidNotStartInTimeException`) came from a build before #97 (`polish-restyle`, `PlaybackService.kt:231`). | Line numbers match `polish-restyle`/`polish-onboarding`. The #97 build ran 30 s in the background with no crash. | Fixed in #97 |
| 2 | Build | `geode-finish` fails `:app:detekt`: `exportToDestination` has 21 parameters after #91 and #94. | CI run 41023373340 | Fixed in #99 |
| 3 | Visuals, Silk | Every Silk style is black. `SimGlsl` emits `const float SIM_STATE_SCALE = 1;` (int to float), so the compute and fragment steps both fail and log "Silk unavailable on this GPU". | logcat `SilkScene`, `GeodeRenderer` | Fixed in #99 (unverified) |
| 4 | Visuals, Fluid | Every fluid scene is unavailable. `FluidSim` builds `fluid_display_frag.glsl` without `LOOK` defined, so `#if LOOK == 1` fails to compile. This has been in the code since release 85491c2. | logcat `FluidSim: base shader rejected` | Fixed in #99 (unverified) |
| 5 | Playback | After a track ends with Repeat off, Play does nothing: ExoPlayer ignores `play()` in `STATE_ENDED`. Both play paths now go through `Util.handlePlayButtonAction` (`PlayerSession.togglePlayPause`, `PlaybackFades.togglePlayPauseFaded`). Previous works. | `android layout`: Seek 1:15 of 1:15 stays on Play | Fixed in #99 (unverified) |
| 7 | Accessibility | The live view's Collapse button: the clickable node has no label, and the "Collapse" label sits on a child that isn't clickable. An accessibility click (TalkBack, `android input tap content-desc=Collapse`) does nothing; only a coordinate tap works. | `android layout` | Open |
| 8 | Visuals, Life | All 10 Life styles render with no crash. Mitosis (mean luma 2.5) and Helicium Reef (9.7) are nearly black three seconds after they're applied; a slow start or a bug, not yet checked. | `styles/Life-*.png` | Check |
| 6 | Styles UI | On Free, a Pro style row shows no lock. Tapping it goes straight to the paywall. (The active style is marked with a dot.) | screenshots 03, 04 | Open |
| 9 | Visuals, Fluid | 9 of 11 Fluid styles are black (same cause as #4). The error then says "this GPU can't render half-float buffers" (`FluidScene.cpp:22`), which is wrong: when the shader fails, `FluidSim` releases itself and `FluidScene` reports any unavailable state as a half-float problem. Curlflow and Water render. | `results.txt` | Cause fixed in #99; message Open |
| 10 | Customize | Customize has no preview. Every change means opening View live and then Collapse, which lands on the Player tab (#11), and then going back to Visuals and Customize. The edits themselves apply: Domain warp, Ripple and Kaleidoscope all visibly change Living Ink. This is probably what "customization not improved" means. | `c_cmp2.png` | Open (UX) |
| 11 | Navigation | Opening the live view from Visuals and pressing Collapse returns to the Player tab, not Visuals. | `cust2.png` | Open |
| 12 | Customize | A slider at 0 has its thumb inside the system back-gesture zone, so dragging it from the left edge triggers Back (and leaves Visuals). Needs `Modifier.systemGestureExclusion()` or an inset. | slider drag from x=70 | Open |
| 13 | Accessibility | Customize checkboxes (Kaleidoscope, Mirror) have no label: the text is a separate node, so an accessibility click on "Kaleidoscope" does nothing. Same pattern as #7. | `android layout`: CheckBox with no text or desc | Open |
| 14 | Player | Repeat cycles off, all, one, but all and one show the same icon. Its content description is just "Repeat" with no state. | `rep2.png`; `PlayerSettingsController.kt:97` | Open |
| 15 | Live view | The player controls cover the bottom 30% and don't hide on their own (still up after 11 s). A tap hides them. The status bar stays visible (not immersive). | `live_idle_s.png` | Open (minor) |
| 16 | Logging | `NotificationProvider` logs "Failed to load bitmap: no embedded artwork" with a stack on every notification update for tracks with no art. | logcat | Open (minor) |
| 17 | Crashes | No crash reproduced: 91 styles in 8 categories, plus 20 fast style switches across scene types. The user's reported style crashes were probably on the older `polish-restyle` build, or on the phone's GPU. | `results.txt` | Needs phone |
