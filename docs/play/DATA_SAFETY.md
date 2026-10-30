# Data safety form (Policy > App content > Data safety)

Derived from the code and the **merged release manifest**, not from intent. The facts
that drive every answer:

- The merged manifest has **no `INTERNET`** and no `ACCESS_NETWORK_STATE` (both are
  removed with `tools:node="remove"`, and `release.yml` fails the build if either INTERNET
  or AD_ID appears). The app can't transmit anything itself.
- The only third-party SDK that talks to anything off the device is the **Google Play
  Billing Library**, and it talks to the Play Store app over IPC.
- No analytics, crash reporting, ads or account SDKs (checked in `app/build.gradle.kts`
  and `gradle/libs.versions.toml`).
- Audio (microphone, playback capture), music files and tags, picked images, the
  now-playing labels and the crash file are all processed **on the device only**.

Play defines *collected* as "transmitted off the user's device". On-device processing is
not collection. That's why everything except purchase history below is "not collected".

An automated audit of the codebase (the `play-policy-insights` skill) found every data
type local-only except purchase history, which it flagged IMPORTANT for review. Its notes
are summarised at the end.

## Page 1: Data collection and security

| Question | Answer |
|---|---|
| Does your app collect or share any of the required user data types? | **Yes**. Only because of purchase history, see below |
| Is all of the user data collected by your app encrypted in transit? | **Yes**. The only transfer is the Play Store app's own TLS connection to Google |
| Do you provide a way for users to request that their data is deleted? | **No**. There's no account and nothing held off-device by you. (Google Play order history is Google's; users manage it in their Google account.) If the form requires a deletion path, answer Yes and give the in-app route: *Settings → Apps → MuzViz Studio Pro → Storage → Clear data* |

## Page 2: Data types

Declare **only**:

| Category › type | Collected | Shared | Processed ephemerally | Required or optional | Purposes |
|---|---|---|---|---|---|
| **Financial info › Purchase history** | Yes | No | No | **Optional** (only if the user buys Pro) | **App functionality** (unlocking the purchased feature) |

Everything else: **not collected, not shared.** In particular:

| Type | Why it's not declared |
|---|---|
| Audio › Voice or sound recordings, Music files, Other audio | Mic and playback capture go into an in-memory ring buffer for the visuals; never written or sent |
| Photos and videos | Background images and textures are picked with the system picker and used locally. Exported videos are shared only when the user picks a target app in the share sheet: user-initiated transfer, exempt |
| Files and docs | Presets, templates and playlists stay in app storage or a folder the user picks |
| App activity, App info and performance (crash logs, diagnostics) | The crash file stays in app storage; the user can only copy it out manually |
| Device or other IDs | None read; no AD_ID permission |
| Personal info, Location, Contacts, Messages, Health, Calendar, Web browsing | Not accessed |
| Financial info › User payment info | Google Play collects payment details in its own sheet; the app never sees them |

"Shared" is No for purchase history: Google Play is processing the purchase as the
platform's payment system, not receiving data from the app for its own use.

## Purchase history: why it's declared

**Decision (2026-10-30): declare it.** The app asks the Play Store app for the user's purchases, receives a purchase record
(product, time, order id, token), and sends the token back to Play to **acknowledge**
the purchase. That acknowledgement leaves the device, through the Play Store app.

- **Declared (the choice above).** It's the conservative reading of "a library
  in your app transmits data off the device", and the Android docs' Data safety guidance
  lists "Uses Google Play's Billing Library" under Financial info. Over-declaring
  purchase history has no downside for a paid unlock: the listing shows it as
  "Financial info: Purchase history", with "App functionality" as the purpose.
- The alternative, not declaring it, treats Google's billing processing as Google's own
  collection. It was considered and not taken: a "No data collected" label that a reviewer
  could dispute isn't worth the cleaner look.

Keep this consistent with `docs/privacy-policy.md` ("Buying MuzViz Pro").

## Security practices

- Encrypted in transit: **Yes** (only Play's own connection).
- Independent security review: **No**.
- Committed to Families policy: **No** (the target audience is 13+).

## Audit notes (play-policy-insights, 2026-10-30)

| Data type | Finding |
|---|---|
| Music, audio, other audio | Local only (Media3, AudioRecord, playback capture → in-process PcmSink) |
| Photos | Local only (system document picker → background and textures) |
| Videos | Manual sharing through the share sheet: exempt |
| Files and docs, crash logs, device ID, user account | Local only or not accessed |
| Performance diagnostics | False positive: the debug-only fake billing panel (`USE_FAKE_BILLING=false` in release) |
| **Purchase history** | **IMPORTANT: declared, as discussed above** |
| Permissions | Four foreground service types, each typed and with a notification. The FGS declaration is needed in the console: see [CONSOLE_CHECKLIST.md](CONSOLE_CHECKLIST.md) §7 |
| User accounts | None: no login, so no account-deletion requirement and no demo credentials needed for review |

Re-run the audit (and update this file) if a networking, analytics, crash-reporting or
ads SDK is ever added, or INTERNET comes back.
