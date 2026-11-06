# Play Billing: purchase lifecycle, backend or no backend

Research for the Pro unlock (`muzviz_pro`, one one-time product, Play Billing Library 9.1.0).
Question: does Geode need a backend to run the purchase lifecycle properly, or is the serverless
client enough? Everything below is from Google's own pages, fetched 2026-11-06. A claim with no
link, or marked **unconfirmed**, could not be verified from a primary source and must not be
relied on.

## What the app does today (read from the code)

- `PlayBillingRepository` (`app/src/main/java/dev/geode/billing/`): `queryPurchasesAsync(INAPP)` on
  every `refresh()`, `acknowledgePurchase` for any PURCHASED, unacknowledged, signature-valid
  purchase, `enablePendingPurchases(enableOneTimeProducts)`, `enableAutoServiceReconnection()`.
- `PurchaseVerifier`: `SHA1withRSA` over `originalJson` with the Play Console licensing key
  (`PLAY_LICENSE_KEY`). No key, no check.
- `CachedEntitlement.after`: Pro is revoked only by a successful full query that no longer lists
  the product as PURCHASED; errors never revoke.
- `PaywallHost` refreshes on every foreground. No WorkManager, no JobScheduler in the repo.
- INTERNET exists only for Cast; `android.yml` and `release.yml` fail the build otherwise.
- `docs/play/BILLING.md` says a pending purchase that clears while the app stays closed for 3 days
  is refunded. The docs say it more precisely, see 2.

## 1. What Play's servers do, what a backend adds

- Play does the payment, owns the order, signs the purchase and keeps its state. The client
  library only talks to the Play Store app over IPC, so it needs no INTERNET permission of its own.
  ([integrate](https://developer.android.com/google/play/billing/integrate),
  [BillingClient](https://developer.android.com/reference/com/android/billingclient/api/BillingClient))
- Google's recommended "life of a purchase" is: launch the flow, **verify on your server**, grant,
  acknowledge. ([integrate](https://developer.android.com/google/play/billing/integrate))
- A backend adds: token verification with Google (`purchases.productsv2.getproductpurchasev2`),
  acknowledgement and refund/revoke that do not depend on the device, a record of every
  `purchaseToken` to catch reuse, RTDN push of purchase and void events, and the Voided Purchases
  pull. ([security](https://developer.android.com/google/play/billing/security),
  [backend](https://developer.android.com/google/play/billing/backend),
  [one-time lifecycle](https://developer.android.com/google/play/billing/lifecycle/one-time))
- The security page says to move sensitive logic to a backend "as much as your app design
  permits", and that client-side acknowledge/consume is possible but server-side is safer against
  poor connectivity and tampering.
  ([security](https://developer.android.com/google/play/billing/security))
- Serverless options and limits:
  - Client-only ack and `queryPurchasesAsync` are supported APIs
    ([BillingClient](https://developer.android.com/reference/com/android/billingclient/api/BillingClient)).
    `queryPurchasesAsync` itself carries the note that backend verification is recommended "if you
    have one".
  - On-device signature check: `Purchase.getSignature()` is signed with the developer's private
    key, RSASSA-PKCS1-v1_5
    ([Purchase](https://developer.android.com/reference/com/android/billingclient/api/Purchase)).
    That Google recommends the licensing-key check as a substitute for a backend is **not stated**
    on the pages read; the security page recommends the backend path. The hash (SHA-1) the app uses
    is **unconfirmed** from a primary page in this pass.
  - Limit of serverless: nothing can act on a purchase while the app is not running, and nothing
    can see a refund that leaves no trace on the device.

## 2. Acknowledgement

- Every purchase must be acknowledged within **three days** or it is refunded and the entitlement
  revoked. One-time products: `acknowledgePurchase` or `consumeAsync`.
  ([integrate](https://developer.android.com/google/play/billing/integrate),
  [BillingClient](https://developer.android.com/reference/com/android/billingclient/api/BillingClient))
- Grant only in PURCHASED. Do not acknowledge while PENDING. "The three-day acknowledgement window
  begins only when the purchase state transitions from PENDING to PURCHASED."
  ([integrate, pending](https://developer.android.com/google/play/billing/integrate#pending))
  So `BILLING.md` is right in effect but should say the 3 days run from clearing, not from the tap.
- `onPurchasesUpdated` fires for a cleared pending purchase only while the app runs with an
  active connection. If the app is closed, it will not be called; Google's answer is
  `queryPurchasesAsync` in `onResume()`.
  ([integrate](https://developer.android.com/google/play/billing/integrate#fetch))
- Server route: an RTDN `ONE_TIME_PRODUCT_PURCHASED` arrives when a pending purchase becomes
  PURCHASED, so a backend can acknowledge at once without the app.
  ([one-time lifecycle](https://developer.android.com/google/play/billing/lifecycle/one-time),
  [security](https://developer.android.com/google/play/billing/security))
  A server ack avoids the "user never opens the app in 3 days" case; the security page says exactly
  this.
- On-device background work: **Google documents none.** The integrate, lifecycle and BillingClient
  pages say to connect and query when the app launches or comes to the foreground, and never
  mention WorkManager, JobScheduler or a Service. The BillingClient page does not forbid it
  either, and says the methods marked `@AnyThread` may be called from any thread. Whether a
  `BillingClient` bound from a background job is supported is **unconfirmed**.
  ([integrate](https://developer.android.com/google/play/billing/integrate),
  [BillingClient](https://developer.android.com/reference/com/android/billingclient/api/BillingClient))
- What an unacknowledged-then-refunded purchase costs here: the user is refunded and the app never
  granted Pro (it grants on PURCHASED it has seen), so the loss is a lost sale and a confused
  user, not money taken without delivery. This is inference from the two rules above.

## 3. Refunds and voids: what the client sees

- `queryPurchasesAsync`: "Only active subscriptions and non-consumed one-time purchases are
  returned." A refunded and revoked purchase therefore drops out of the list on the next
  successful query; that is what `CachedEntitlement.after` already relies on. Exactly when after
  the refund the list changes is **unconfirmed**.
  ([BillingClient](https://developer.android.com/reference/com/android/billingclient/api/BillingClient))
- A refund without revoke leaves the item with the user; only revoke removes access. For one-time
  products the API is `orders.refund` with `revoke=true`.
  ([orders.refund](https://developers.google.com/android-publisher/api-ref/rest/v3/orders/refund),
  [security](https://developer.android.com/google/play/billing/security)) What the Console's own
  refund button does to the client for in-app items is **unconfirmed**: the help page read covers
  subscriptions only.
  ([help](https://support.google.com/googleplay/android-developer/answer/2741495))
- Voided Purchases API: pull, one-time and subscription orders, "only revoked orders", chargebacks
  included, **30-day** window, 6,000 queries/day and 30 per 30 s per package, needs the financial
  data permission. The client cannot call it. Also, voids with no associated clawback content are
  not exposed.
  ([voided-purchases](https://developers.google.com/android-publisher/voided-purchases),
  [security](https://developer.android.com/google/play/billing/security))
- Difference: the client sees "no longer listed" and cannot tell refund from chargeback or know
  why; the API gives `voidedSource`/`voidedReason` (their meanings were **not defined** on the page
  read). For a permanent unlock with no clawback of consumed content, the extra data buys nothing
  the app would act on.
- RTDN `VoidedPurchaseNotification` carries `purchaseToken`, `orderId`, `productType`,
  `refundType`; `PendingRefundReviewNotification` (chargeback, answer within 24 h through
  `orders.reviewrefund`) is a developer-only channel.
  ([rtdn-reference](https://developer.android.com/google/play/billing/rtdn-reference))

## 4. The backend option in full

**Developer API calls.** All scope `https://www.googleapis.com/auth/androidpublisher`.

| Call | Use | Source |
|---|---|---|
| `GET .../purchases/productsv2/tokens/{token}` | state, acknowledgement, obfuscated account id; the call the lifecycle page tells you to use after an RTDN | [getproductpurchasev2](https://developers.google.com/android-publisher/api-ref/rest/v3/purchases.productsv2/getproductpurchasev2), [one-time lifecycle](https://developer.android.com/google/play/billing/lifecycle/one-time) |
| `GET .../purchases/products/{productId}/tokens/{token}` | older form; docs name it in security page; no deprecation note on its page | [products.get](https://developers.google.com/android-publisher/api-ref/rest/v3/purchases.products/get) |
| `POST .../purchases/products/{productId}/tokens/{token}:acknowledge` | non-consumable ack, empty body | [acknowledge](https://developers.google.com/android-publisher/api-ref/rest/v3/purchases.products/acknowledge) |
| `purchases.products.consume` | consumables only; Pro is permanent, so not used | [security](https://developer.android.com/google/play/billing/security) |
| `GET .../purchases/voidedpurchases` | pull of refunds, 30 days | [voided-purchases](https://developers.google.com/android-publisher/voided-purchases) |
| `POST .../orders/{orderId}:refund?revoke=true` | reject a purchase; do this, do not let it time out | [orders.refund](https://developers.google.com/android-publisher/api-ref/rest/v3/orders/refund) |

Quota is 3,000 queries per minute per bucket by default, the one-time purchase bucket being
separate. ([quotas](https://developers.google.com/android-publisher/quotas))

**RTDN.** Pub/Sub, not a direct webhook. The message carries only a notice; you must call the API
for the state. `OneTimeProductNotification` has `notificationType` 1 `ONE_TIME_PRODUCT_PURCHASED`,
2 `ONE_TIME_PRODUCT_CANCELED`, plus `purchaseToken` and `sku`; it is sent only if you choose "Get
all notifications for subscriptions and one-time products". Voided purchase notices come with
either option. Dedupe on `messageId`.
([rtdn-reference](https://developer.android.com/google/play/billing/rtdn-reference),
[getting-ready](https://developer.android.com/google/play/billing/getting-ready#configure-rtdn))

**Service account and Console linking** (steps as the docs give them):
1. Create or pick a Cloud project and enable the Google Play Android Developer API.
2. Create a service account.
3. Play Console, Users and permissions, Invite new users, enter the service account email, grant
   "View financial data, orders, and cancellation survey responses" and "Manage orders and
   subscriptions".
4. The page says a Cloud-project link is no longer needed. How long the grant takes to take effect
   is **unconfirmed**.
([getting_started](https://developers.google.com/android-publisher/getting_started),
[getting-ready](https://developer.android.com/google/play/billing/getting-ready))

**RTDN setup:**
1. Create a Pub/Sub topic; give `google-play-developer-notifications@system.gserviceaccount.com`
   the Pub/Sub Publisher role on it.
2. Create a push (docs suggest push) subscription to your HTTPS endpoint.
3. Play Console, Monetize, Monetization setup, Real-time developer notifications: topic name
   `projects/{project_id}/topics/{topic}`, Send Test Message, choose "all notifications ... and
   one-time products", Save.
([getting-ready](https://developer.android.com/google/play/billing/getting-ready#configure-rtdn))
How a Cloud Run push endpoint authenticates Pub/Sub (OIDC token) was **not verified** here.

**Replay and fraud.** Keep every `purchaseToken` (globally unique, safe as a primary key; do not
key on `orderId`, promo-code purchases have none). Verify with `getproductpurchasev2`, grant only
PURCHASED, check `obfuscatedAccountId` against your account if you set one, refund and revoke
rejected purchases instead of letting them time out.
([security](https://developer.android.com/google/play/billing/security)) Geode has no accounts, so
there is no user id to bind `setObfuscatedAccountId` to; the page uses it to link Play accounts to
in-app accounts. A random per-install id would work mechanically; whether Google's fraud signals
gain from it is **unconfirmed**.

**Hosting and cost** (Google Cloud pricing pages, fetched 2026-11-06):
- Cloud Run, request-based billing: free per month 180,000 vCPU-seconds, 360,000 GiB-seconds,
  2 million requests (us-central1 basis); egress 1 GiB free in North America; free tier is by
  billing account. ([run/pricing](https://cloud.google.com/run/pricing)) Cloud Functions
  (2nd gen) is Cloud Run under the same page; Firebase's Blaze requirement and prices were
  **not confirmed** (page is script-rendered).
- Pub/Sub: first 10 GiB per month of Message Delivery Basic throughput is free, then $40/TiB; a
  1 KB minimum per request applies. ([pubsub/pricing](https://cloud.google.com/pubsub/pricing))
- A one-product indie app produces a handful of messages per sale. At any realistic volume this is
  inside the free tiers. A billing account must exist on the project; the page states the free tier
  is applied per billing account, and that a card is mandatory is **unconfirmed**.

## 5. Security: device check vs server, Play Integrity

- Google's order: server-side verification first. On-device signature checking is exactly what
  Geode has; it stops a forged purchase blob but not a patched APK (an attacker removes the
  check), which a server cannot fully fix either for a client-gated unlock.
  ([security](https://developer.android.com/google/play/billing/security)) This last clause is my
  judgement, not Google's.
- `Purchase.getSignature()` verification needs the licensing public key from Monetization setup.
  ([Purchase](https://developer.android.com/reference/com/android/billingclient/api/Purchase),
  [BILLING.md](../play/BILLING.md))
- Play Integrity API: verdicts on app, device and licensing (`accountDetails`, `appIntegrity`,
  `deviceIntegrity`), meant to be checked by "your backend server", 10,000 requests/day default,
  not "the sole anti-abuse mechanism". Without a backend it has no one to enforce it. Whether a
  verdict can be decoded on the device in a safe way is **unconfirmed** here.
  ([overview](https://developer.android.com/google/play/integrity/overview))
  A $10 non-consumable with no server content does not justify it.

## 6. Play policy

- **Payments policy.** App features must be sold through Play Billing; the in-app price must match
  Play's; no steering to other payments. A backend does not change this.
  ([policy](https://support.google.com/googleplay/android-developer/answer/9858738))
- **Restore purchases.** `queryPurchasesAsync` on connect and foreground is the documented restore
  path (multiple devices, outside-app purchases). A policy sentence requiring a "Restore" button
  was **not found** on the pages read.
  ([integrate](https://developer.android.com/google/play/billing/integrate#fetch))
- **Family Library.** Does not share in-app purchases: "You can't share in-app purchases and apps
  downloaded at no charge with your family members."
  ([Play help](https://support.google.com/googleplay/answer/7007852))
- **Promo codes.** Up to 500 per quarter per app across non-subscription promotions; they work on
  one-time products with an active buy option; such purchases carry no `orderId`, so never key on
  it. Redeemed outside the app, they arrive through `queryPurchasesAsync`. The app already keys on
  the token.
  ([promo help](https://support.google.com/googleplay/android-developer/answer/6321495),
  [security](https://developer.android.com/google/play/billing/security),
  [test](https://developer.android.com/google/play/billing/test))
- **License testing and tracks.** License testers get test instruments including "Slow test card,
  approves after a few minutes" (pending) and "approves then charges back"; a tester's unacknowledged
  purchase is refunded after **3 minutes**, not 3 days. Internal and draft tracks have spend
  limits; closed/open/production do not. Non-testers on test tracks are really charged. Play Billing
  Lab can simulate out-of-app purchases.
  ([test](https://developer.android.com/google/play/billing/test))
- **Data safety and privacy policy.** "Collect" means transmitting data off the device, SDK data
  included, and the form must stay accurate; the privacy policy is required for it. Card data a
  payment service collects need not be declared, but "purchase history" or data received from the
  payment service for fraud checks may need to be. Which Data safety category a purchase token
  falls into is **unconfirmed**.
  ([Data safety help](https://support.google.com/googleplay/android-developer/answer/10787469))
  In this repo the Cast release already moved the form off "no data"; `docs/play/DATA_SAFETY.md`
  and the INTERNET gate in `android.yml:323` and `release.yml:135` would both need to change if the
  app itself talked to a backend.

## Recommendation for Geode

### Two ways to add a server

The server's job splits in two, and only one half needs the app online:

- **Server-only ack (B1).** RTDN pushes the token to the server, the server verifies and
  acknowledges. The device sends nothing. No INTERNET, no Data safety change for the app.
- **App-to-server verification (B2).** The app posts the token. INTERNET, CI gates and Data safety
  all change. It buys nothing Geode needs: no account, no server content, no server-gated feature.

### Option A: stay serverless, harden the pending path

Steps in this repo (Kotlin glue only, per AGENTS.md; no logic that belongs in `core/`):
1. In `PlayBillingRepository.handle`, when a Pro purchase is PENDING, write a "pending since" flag
   next to the entitlement (`EntitlementStore`).
2. Schedule one background retry while the flag is set: a one-time `JobScheduler` job (platform API,
   minSdk 33, no new dependency) or WorkManager (new dependency, version not checked here), with
   backoff, that runs the existing `refresh()`. Cancel it when a full query shows no pending Pro
   purchase. Keep a cap, since how long Play keeps a pending purchase open was not confirmed.
3. Keep the current foreground refresh and `CachedEntitlement` rules unchanged.
4. Fix `BILLING.md`: say the 3 days start when the purchase becomes PURCHASED.
5. Test: license tester + "Slow test card, approves after a few minutes", close the app before it
   clears, wait 4 minutes; with the job the order stays, without it Play refunds at 3 minutes (the
   tester window). Also test the job's `BillingClient` connection with the app process dead; this is
   the part Google does not document.

Console / Cloud: nothing new. Licensing key and license testers as in `BILLING.md`.

Cost: no money. Cost is code and an undocumented-behaviour risk (step 2), plus one more job to
keep alive under Doze and battery saver; a missed run only degrades to today's behaviour.

Policy and privacy: none. No new permission with JobScheduler (a persisted job needs
`RECEIVE_BOOT_COMPLETED`; skip persistence, as the job is only a convenience). The network gate
stays true.

### Option B1: minimal ack-only backend

Steps:
1. Cloud project; enable the Google Play Android Developer API; Pub/Sub topic and push
   subscription.
2. One Cloud Run service (any language) with: parse Pub/Sub envelope, dedupe on `messageId`, on
   `ONE_TIME_PRODUCT_PURCHASED` call `getproductpurchasev2`, if PURCHASED and unacknowledged call
   `purchases.products.acknowledge`; on `ONE_TIME_PRODUCT_CANCELED` or a voided notice do nothing
   but log (the device learns from its next query). Store nothing.
3. Service account for the runtime, no key file; invite its email in Play Console with the two
   billing permissions above.
4. Play Console, Monetization setup: topic name, Send Test Message, "all notifications ... and
   one-time products".
5. No app change except `BILLING.md`; the foreground ack stays as the fallback and must tolerate
   `isAcknowledged == true` (it already filters on it).

Cost: within the free tiers above for any volume a one-product app reaches; billing account
required on the project. Real cost is operating a service and its credentials.

Policy and privacy: the app's Data safety and the INTERNET gate are unchanged, since the device
sends nothing. The privacy policy needs no change on the app's behaviour, but Google's token data
reaches the developer's own server; whether that is a new item for the policy is **unconfirmed**.

### Verdict: A

Reasons:
- The failure A and B1 address is narrow and its worst case is a refund, not a stolen sale or an
  undelivered paid unlock: the app grants only after it has seen PURCHASED, and an
  unacknowledged purchase is refunded.
- Refund handling already works without a server for a permanent unlock: the purchase leaves
  `queryPurchasesAsync` and `CachedEntitlement` revokes it.
- A backend's other strengths (token reuse, account binding, fraud checks, Integrity) need identity
  that Geode, being offline and account-free, does not have, so B2 would add network use and a
  moving CI gate for no extra protection.
- B1 is the only server option worth considering. Take it later, if pending-purchase refunds show
  up in Play Console's order reports at a rate that hurts; the pieces (topic, service account,
  Console linking) are cheap and do not touch the app.

Do A's steps 1, 2 and 4 now, test step 5 on the internal track, and treat step 2's job as a
best-effort extra, because Google documents only the foreground path.

## Unconfirmed in this pass

- Whether a `BillingClient` in a background job (JobScheduler, WorkManager, Service) is supported.
- Exact timing of a refund's effect on `queryPurchasesAsync`, and the Console-refund behaviour for
  in-app products without revoke.
- SHA-1 as the signature hash (the Purchase page names the RSASSA-PKCS1-v1_5 scheme only).
- Meanings of `voidedSource` and `voidedReason`.
- How long a pending cash purchase stays open before Play cancels it.
- Pub/Sub push authentication to Cloud Run; Firebase Functions/Blaze terms; whether a card is
  needed for a billing account; Service-account permission propagation time.
- Whether a "Restore purchases" button is a stated policy requirement.
- Which Data safety category a purchase token falls under; the on-device use of Play Integrity.
- Whether `setObfuscatedAccountId` with a random per-install id helps fraud signals.
