# Pro (one-time unlock)

One fact per line. Add a gate as a new bullet.

## Entitlement

- Product id `beatrealms3d_pro` (`BillingRepository.PRO_PRODUCT_ID`).
- Every gate asks a `FeatureGate` from `GetFeatureGateUseCase`.
- Pro beats the 1-hour trial beats free.
- What free keeps is `pro/FreeTier.kt` alone.
- Free gets the loose shader styles, the first style of each style family and the neon opener, Neon Chrome Sea.
- Neon Chrome Sea is also every install's first style (`FreeTier.FIRST_STYLE_ID`).
- The other eleven styles per family are Pro (`FreeTier.familyProIds`); the catalogue names each family's opener.
- Note & key reactions are free (Settings › Behaviour, on by default); their Customize › Advanced card is Pro like every card.

## Gates

- `PlayerViewModel.startExport`/`startLoopRender`/`startBatch`: 4K, 60 fps, the 1-minute cap, the full-frame watermark.
- Loop and batch renders need Pro.
- `PlayerSession.selectScene`/`applyPreset` via `PlayerSessionProvider` gate styles; auto-visuals skips locked ones silently.
- Record is gated as `ProFeature.STUDIO`, through `PlayerSessionProvider`.
- Studio › Render is open to free and trial users within `FreeTier`.
- The wallpaper button is gated.
- `CastController` gates when a Cast session starts (`ProFeature.CAST`); a locked session is ended.
- A trial that runs out mid-cast ends the cast after the song.
- A connected display is gated as Cast (`ProFeature.CAST`).
- `ProSnapshot` gates the wallpaper and dream services.
- The live auto-director is gated by `AutoDirectorPolicy`: Pro and trial get the whole song, free the first minute of each.
- An export's director is native (`director::forExport`), from the switch, the entitlement and the length the export forwards: Pro and trial throughout, free only when the export fits the minute.

## Paywall and debug billing

- `PaywallNavigator.request` is state; `PaywallHost` (a `Dialog`, drawn from `AppRoot`) shows it.
- Debug builds bill against `FakeBillingRepository` (`billing/BillingBuild`: `BuildConfig.USE_FAKE_BILLING` and a debuggable build).
- Debug builds show a debug panel on the paywall.
