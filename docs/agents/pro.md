# Pro (one-time unlock)

One fact per line. Add a gate as a new bullet.

## Entitlement

- Product id `beatrealms3d_pro` (`BillingRepository.PRO_PRODUCT_ID`).
- Every gate asks a `FeatureGate` from `GetFeatureGateUseCase`.
- Pro beats the 1-hour trial beats free.
- What free keeps is `pro/FreeTier.kt` alone.
- Free gets ten styles (`FreeTier.freeStyleIds`): Neon Chrome Sea and the openers of the first nine shader families. Every dedicated renderer is Pro.
- Neon Chrome Sea is also every install's first style (`FreeTier.FIRST_STYLE_ID`).
- The others in each family are Pro (`FreeTier.familyProIds`); the catalogue names each family's opener.
- Sharing the app keeps one more style, the locked one the paywall offers, once per install (`ShareBonusRepository`, `FeatureGate.bonusStyleId`); opening the share sheet earns it.
- Exporting a video is Pro (`ProFeature.EXPORT`, first in `FeatureGate.blockingExportFeature`); the trial exports one minute with the watermark.

## Gates

- `PlayerViewModel.startExport`/`startLoopRender`/`startBatch`: 4K, 60 fps, the 1-minute cap, the watermark, placed as Settings › Preferences says.
- Loop and batch renders need Pro.
- `PlayerSession.selectScene`/`applyPreset` via `PlayerSessionProvider` gate styles; auto-visuals skips locked ones silently.
- Record is gated as `ProFeature.STUDIO`, through `PlayerSessionProvider`.
- Studio › Render is open to free and trial users within `FreeTier`.
- The wallpaper button is gated.
- `CastController` gates when a Cast session starts (`ProFeature.CAST`); a locked session is ended.
- A trial that runs out mid-cast ends the cast after the song.
- A connected display is gated as Cast (`ProFeature.CAST`).
- `ProSnapshot` gates the wallpaper and dream services.
- Show on a computer is Pro like Cast; it stops at Pro's end.
- The two-circle Randomize everything is Pro; without Pro the lock `#PRO` holds all but the Basics, for the Customize dice and the gesture alike.
- Quick knobs › Edit knobs (`MacroEditor`) is Pro.
- The live auto-director is gated natively (`director::liveAmount`, fed by `AutoDirectorStore`): Pro and trial get the whole song, free the first minute of each; when it ends, Visuals shows a chip that opens the paywall (`AutoDirectorStore.freeTasteEnded`).
- A free user's screensaver shows a notice for ten seconds instead of the visual; a tap opens the app on the paywall (`MainActivity.openPaywallIntent`).
- An export's director is native (`director::forExport`), from the switch, the entitlement and the length the export forwards: Pro and trial throughout, free only when the export fits the minute.

## Paywall and debug billing

- Automatic paywalls are `PaywallPrompts`, free users only: once after `FreeTier.TASTE_VISUALS_MS` of Visuals, at the next pause or track change; and on a return after `FreeTier.RETURN_AWAY_MS` away, not in the first session nor from a screen the app opened (`MainActivity.startActivityForResult`).
- `PaywallNavigator.open` ranks triggers: a tap or Settings beats the trial's end, which beats the milestone, which beats a return.
- The tour makes no Pro offer.
- Every `ProFeature` has a row in the paywall's comparison (`COMPARE_ROWS`, `PaywallRowsTest`).

- `PaywallNavigator.request` is state; `PaywallHost` (a `Dialog`, drawn from `AppRoot`) shows it.
- Debug builds bill against `FakeBillingRepository` (`billing/BillingBuild`: `BuildConfig.USE_FAKE_BILLING` and a debuggable build).
- Debug and sideload builds show a tester-code field on the paywall (`TesterCode`, only its SHA-256 in the repo); until the code is entered every purchase fails as billing unavailable, after it Pro is granted and the debug panel opens.
