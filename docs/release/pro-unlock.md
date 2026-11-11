# Pro unlock: Play Console setup and device test

Pro is one non-consumable one-time product, bought through Play Billing Library 9.1.0 (supported for new releases until 31 August 2028). The app has no server: it checks each purchase's signature on the device, acknowledges it from the device, and keeps the signed purchase so Pro works offline.

## Before the first upload

1. **Decide the product id.** The code asks for `muzviz_pro` (`BillingRepository.PRO_PRODUCT_ID` in `app/src/main/java/dev/geode/pro/BillingRepository.kt`). Buyers never see the id, only the product's name. Play never lets an id be renamed or used again, so change the constant now if you want another one (for example `beatrealms_pro`), and create the product under the same id.
2. **Decide the application id.** It is `com.muzviz.studiopro` and is permanent once the app is published. The product and the licence key belong to it.
3. **Set the licence key.** Play Console › Monetize with Play › Monetization setup › Licensing shows a Base64 public key. Save it as the repository *variable* `PLAY_LICENSE_KEY` (it is public, not a secret). `release.yml` refuses to sign a bundle without it. A build without the key refuses every purchase, so the buyer is refunded after three days.

## In Play Console

1. Create the app, then upload a signed bundle from `release.yml` to **Internal testing**. Products can only be created once a bundle with the `BILLING` permission is uploaded.
2. Monetize with Play › Products › **One-time products** › Create: id `muzviz_pro`, a name and description, one purchase option of type *Buy*, the price, all countries. **Activate** it. Until it is active the paywall shows "Pro isn't available in your country yet" with a Retry button.
3. Setup › **License testing**: add the testers' Google accounts. Their purchases cost nothing and offer test cards.
4. Internal testing › Testers: add the same accounts, and open the opt-in link on each test device.

## Test on a device

Use the release build under its real application id, installed from the internal testing track. A debug build cannot do this: its id ends in `.debug`, which Play does not know, and it bills against the fake with a debug panel.

| Test | Steps | Expect |
|---|---|---|
| Buy | Open a Pro style, tap the price, pay with "Test card, always approves" | Paywall turns to the owned screen, the style stays. In Play Console › Orders the order is acknowledged; an unacknowledged test order is refunded after 3 minutes |
| Declined | "Test card, always declines" | Error on the paywall, still free |
| Cancel | Close Play's sheet | "Purchase cancelled. Nothing was charged." |
| Pending | "Slow test card, approves after a few minutes" | "Payment pending", still free. Leave and reopen the app after a few minutes: Pro |
| Pending declined | "Slow test card, declines after a few minutes" | Stays free, the pending note goes on the next open |
| Process death | Buy, then force-stop the app and reopen it in aeroplane mode | Still Pro; the wallpaper and the screensaver animate |
| Restore | Clear the app's data or install on a second device with the same account, open the paywall, tap Restore purchases | "Pro restored" (it also arrives by itself when the app opens) |
| Already owned | Second device, same account, tap the price before the app has refreshed | Pro, no second charge |
| Refund | Play Console › Orders › the order › Refund, with **Remove entitlement** ticked. Open the app, then open it again more than 24 hours later (or move the device clock a day forward and reopen) | Still Pro on the first open, free on the second; a Pro style on screen gives way to the free first style. Buying again works |
| Trial | Start the trial from the paywall, set the device clock back 30 minutes | The trial ends within a minute and does not come back |

Clear Play Store's cache if a refund or a new product does not show up; Play answers the app from that cache.

## What the app does and does not defend

- Pro is granted only for a purchase in state PURCHASED whose signature verifies against the licence key. The cached copy is checked the same way on every read, so editing the prefs file gives nothing.
- A refund takes Pro away when Play has listed the account's purchases without it twice, at least 24 hours apart (`CachedEntitlement.REVOKE_GRACE_MS`). One empty list is ignored because Play's cache is empty after its data is cleared. A device that never reaches Play again keeps Pro.
- The app only asks Play while its window is open. A wallpaper-only user keeps a refunded Pro until the app is next opened twice.
- The trial is one hour of wall clock, once. Moving the clock back more than two minutes ends it. Clearing the app's data, or reinstalling before Android's backup has run, gives a new trial; without a server that cannot be closed.
- A rooted device or a patched APK can skip any of this, as with every client-only check.
