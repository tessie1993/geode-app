# MuzViz Pro: the in-app product

## The product to create (Monetize with Play > Products > One-time products)

| Field | Value |
|---|---|
| Product ID | **`muzviz_pro`**. Must match `BillingRepository.PRO_PRODUCT_ID`. Can't be changed or reused after creation |
| Name | MuzViz Pro |
| Description | Every visual style, full-length 4K and 60 fps exports without the watermark, the Studio editor, live wallpaper and screensaver. One payment, yours to keep. |
| Purchase option | One "Buy" option, **not** consumable-by-the-app (the app never consumes it; it is a permanent unlock) |
| Price | **USD 9.99** as the base. Let Play convert the other countries, then round the ones you care about |
| Availability | All countries where the app is distributed |
| Status | **Active** (a draft product returns `ITEM_UNAVAILABLE`, shown in the app as "Pro isn't available in your country yet") |

Tax: Play handles VAT/GST. In the EU, UK and similar markets the price users see includes
tax, so pick a base price you are happy with *after* Play's share (15% on the first
USD 1M per year, if you enrol in the reduced service fee programme: Monetize with Play >
Monetization setup).

## What the app does with it

- Library: Play Billing Library **9.1.0** (`com.android.billingclient:billing`), the
  latest stable at the time of writing, with `enablePendingPurchases(enableOneTimeProducts)`
  and `enableAutoServiceReconnection()`.
- Price shown on the paywall comes from `ProductDetails` (`formattedPrice`), never hard-coded.
- **Verification.** The app has no backend and no network permission, so it verifies the
  purchase signature on the device against the app's licence public key, then
  **acknowledges** the purchase (an unacknowledged purchase is refunded automatically
  after 3 days; acknowledgement retries on every app foreground).
  - Key: Play Console > Monetize with Play > **Monetization setup > Licensing**, the
    "Base64-encoded RSA public key". It is public, not a secret. Put it in a GitHub
    **repository variable** named `PLAY_LICENSE_KEY` (or the Gradle property
    `muzviz.playLicenseKey` for local release builds). Without it, signatures are not
    checked (the app logs this) and purchases still work, but a tagged release (`v*`)
    refuses to build without it.
- **Pending purchases** (cash, bank transfer): the paywall says so; Pro unlocks when Play
  reports the payment complete, even across app restarts.
- **Restore**: the paywall's "Restore purchase" re-reads purchases from Play. A purchase on
  another device with the same Google account restores the same way, and is also picked
  up automatically on every foreground.
- **Offline**: the entitlement is cached on the device. If the Play Store can't be reached,
  Pro stays unlocked for 30 days from the last confirmation. A refund or revoked order
  removes Pro the next time Play answers.
- **Trial**: one hour of every Pro style, tool and export setting, once per install, on the
  device clock. Exports stay at the free limits (1 minute, full-frame watermark), and Studio
  exports and loop renders ask to unlock. This is not a Play free trial (Play offers trials only on subscriptions),
  so there is nothing to configure in the console.

## Testing before release

1. Play Console > Settings > **License testing**: add your own Google account (and any
   testers). License testers are never charged and get test cards ("Test card, always
   approves", "...always declines", "slow test card" for pending).
2. Upload a **signed** release AAB to the **Internal testing** track. Billing only works
   for the real `com.muzviz.studiopro` id signed with the upload key, installed from
   Play. Debug builds (`.debug` id) use the fake billing instead.
3. Install from the internal-testing opt-in link, open the paywall, buy with a test card.
   Then check: the Pro unlock, the watermark is gone, the export runs the full track, Studio
   opens and exports, and Restore on a second device.
4. Refund it from Order management, reopen the app, and check that Pro goes away.

Debug builds: the paywall shows a red **Debug billing (fake)** panel to own/unown Pro and
pick the next purchase outcome (succeed, pending, cancel, network error, unavailable).
`./gradlew :app:assembleDebug -Pmuzviz.fakeBilling=false` builds a debug APK on the real
client instead. That only helps once the id and signature match a console app.

## Tuning the free tier

Everything a free user keeps is in `app/src/main/java/dev/geode/pro/FreeTier.kt`:
`freeStyleIds` (styles that stay free: the whole fragment-shader family), `freeFeatures`,
the 1080p / 30 fps / 1-minute export ceiling, the trial length, and whether trial exports
keep those limits (`TRIAL_EXPORTS_LIMITED`).

What a non-Pro export gets: at most 1080p, 30 fps and **1 minute** (the rest of the track is
cut), with the wordmark **tiled diagonally across the whole frame**. Loop renders and Studio
exports need Pro.
