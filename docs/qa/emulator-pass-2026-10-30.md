# Emulator QA pass, 2026-10-30

Build: `claude/fix-merge-detekt` CI debug APK 303 (dfd7c09 = geode-finish after #91, #94, #97, #98, plus the detekt fix).
Device: emulator-5554, API 37, x86_64, GLES compute-es31. Tools: `android install`, `android layout`, `android input tap`, `android screen capture`, logcat.

| # | Area | Bug | Evidence | Status |
|---|---|---|---|---|
| 1 | Playback | The crash the user reported (`ForegroundServiceDidNotStartInTimeException`) came from a build before #97 (`polish-restyle`, `PlaybackService.kt:231`). | Line numbers match `polish-restyle`/`polish-onboarding`. The #97 build ran 30 s in the background with no crash. | Fixed in #97 |
| 2 | Build | `geode-finish` fails `:app:detekt`: `exportToDestination` has 21 parameters after #91 and #94. | CI run 41023373340 | Fixed in #99 |
| 3 | Visuals, Silk | Every Silk style is black. `SimGlsl` emits `const float SIM_STATE_SCALE = 1;` (int to float), so the compute and fragment steps both fail and log "Silk unavailable on this GPU". | logcat `SilkScene`, `GeodeRenderer` | Fixed in #99 (unverified) |
| 4 | Visuals, Fluid | Every fluid scene is unavailable. `FluidSim` builds `fluid_display_frag.glsl` without `LOOK` defined, so `#if LOOK == 1` fails to compile. This has been in the code since release 85491c2. | logcat `FluidSim: base shader rejected` | Fixed in #99 (unverified) |
| 5 | Playback | After a track ends, with Repeat on or off, Play does nothing and Previous does nothing. The session stays `STOPPED` at 1:15/1:15, so the only way out is a restart. | `dumpsys media_session`; taps by selector | Open |
| 7 | Accessibility | The live view's Collapse button: the clickable node has no label, and the "Collapse" label sits on a child that isn't clickable. An accessibility click (TalkBack, `android input tap content-desc=Collapse`) does nothing; only a coordinate tap works. | `android layout` | Open |
| 8 | Visuals, Life | All 10 Life styles render with no crash. Mitosis (mean luma 2.5) and Helicium Reef (9.7) are nearly black three seconds after they're applied; a slow start or a bug, not yet checked. | `styles/Life-*.png` | Check |
| 6 | Styles UI | The Styles list doesn't mark which style is active, and applying a Pro style on Free opens the paywall with no lock shown on the row beforehand. | screenshots 03, 04 | Open |
