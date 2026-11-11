# Data safety form: answers and evidence

Play Console › Policy and programs › App content › Data safety. The rules quoted here are from
"Provide information for Google Play's Data safety section"
([answer/10787469](https://support.google.com/googleplay/android-developer/answer/10787469), read
2026-11-11) and Google's Cast SDK disclosure
([developers.google.com/cast/docs/android_sender/data_disclosure](https://developers.google.com/cast/docs/android_sender/data_disclosure)).
File and line numbers are `origin/main` at b8ac569b5; paths under `app/src/main/java/dev/geode/`
are written from `dev/geode/`.

## What Play counts

- **Collected** means sent off the device, by the app or by an SDK inside it. Data that is
  only processed on the device is not collected.
- **Shared** means passed to a third party. A transfer the user starts and expects (a share
  sheet, a web search they tap) is not sharing, and neither is a transfer to a service provider.
- Purchases handled by Google Play Billing are Google's, not the app's, as long as the app
  itself does not send purchase data anywhere.

## The one judgement call (owner decides)

The app's own code sends nothing off the device. The **Google Cast SDK** (linked through
`media3-cast`, `app/build.gradle.kts:266`; started in `dev/geode/cast/CastController.kt:128-133`
so the Cast button and the system output switcher can find a TV) does: Google says it logs
discovery and session events with device and app information, encrypted, without an identifier
that traces to a person, and that neither the user nor the developer can turn it off or delete
it. Under Play's definition an SDK's transmission is the app's collection, and Play checks SDK
behaviour against the form.

**Recommended: declare it** (the answers below). It is the answer that cannot be called a
mismatch later. The other reading, "no data collected" because Google's logging is anonymous
and Google is the party receiving it, is defensible but leaves the form disagreeing with what
Google's own SDK page says the SDK does.

## Answers (recommended)

| Question | Answer |
|---|---|
| Does your app collect or share any of the required user data types? | **Yes** |
| Is all of the user data collected by your app encrypted in transit? | **Yes** (Cast SDK logging is; the app sends nothing else) |
| Do you provide a way for users to request that their data is deleted? | **No** (Google's Cast logging is anonymous and cannot be deleted by anyone; the app has no accounts and keeps nothing off the device) |
| Account creation | The app has **no account creation** |

Data types: tick one, leave every other type unticked.

| Data type | Collected | Shared | Processed ephemerally | Required or optional | Purposes |
|---|---|---|---|---|---|
| App activity › **App interactions** | Yes | No | No | **Required** (users cannot turn it off; it starts with the app) | **Analytics** |

If Play's form asks about **Device or other IDs**: no. Google states the Cast logs carry no
identifier that traces to a user or device, and the app reads none (no `AD_ID` permission; CI
fails if it appears, `tools/check-manifest.sh`).

If the owner takes the other reading, the whole form is: collects or shares data **No**.

## Why nothing else is declared

| Data | What the app does with it | Evidence | Declared |
|---|---|---|---|
| Music files and tags | Read from the device to list and play; analysed in memory | `dev/geode/data/DeviceTrackQuery.kt:23`, `READ_MEDIA_AUDIO` asked in `dev/geode/ui/LibraryScreen.kt:159` | No: on device |
| Microphone audio | Only with Live input on; samples go into the in-memory ring, never a file or socket; paused in the background | `dev/geode/audio/MicCapture.kt:78`, `dev/geode/ui/CaptureController.kt:71-96` | No: on device, not stored |
| Other apps' audio | Only with Visualize other apps (system consent, ongoing notification) or the wallpaper's Hear other apps | `dev/geode/audio/PlaybackCaptureService.kt:128`, `dev/geode/wallpaper/OutputMixCapture.kt:73` | No: on device, not stored |
| Listening history, playlists, settings, presets | Kept in the app's private storage | `dev/geode/data/` stores | No: on device |
| Crash details | Written to `filesDir/crash-latest.txt` for the user to read; no crash SDK, nothing uploaded | `dev/geode/GeodeApp.kt:26-36` | No: on device |
| Purchase record | Read from the Play Store app, signature checked on the device, acknowledged back to Play; cached in app prefs. No developer server | `dev/geode/billing/PlayBillingRepository.kt:204-231`, `dev/geode/billing/EntitlementStore.kt:116-134` | No: Play Billing's own data; the app sends it nowhere else |
| Rendered videos, presets, templates | Leave the device only through the user's Share action | `dev/geode/ui/RenderBar.kt:232`, `dev/geode/ui/PresetActions.kt:382-392`, `dev/geode/ui/TemplatesSheet.kt:243`, `dev/geode/ui/LoopRenderSheet.kt:171` | No: user-initiated |
| Track title and artist | Handed to the user's browser when they tap "Search the web" for lyrics | `dev/geode/ui/LyricsSettings.kt:91-92` | No: user-initiated |
| The visual and the music while casting | Streamed from the phone to the TV on the user's own Wi-Fi; the only sockets in the app | `dev/geode/cast/LiveHttpServer.kt:41-55`, `dev/geode/cast/CastStream.kt:249` | No: user-initiated, to the user's own device |
| Device backup | Android's own backup of app data to the user's Google account, limited by `res/xml/data_extraction_rules.xml` | `AndroidManifest.xml:103-107` | No: the system's transfer, not the app's or an SDK's |

There is no analytics, advertising or crash-reporting SDK (CI fails the release build if one is
added, AGENTS.md › Conventions), and no network code outside `cast/`.

## Target audience and this form

Owner decision, 2026-11-11: the target audience is 13 and over, so the Families policy's data
rules (no SDK that is not approved for child-directed services, no identifiers from children)
do not apply, and the Cast SDK starting at launch is only the judgement call above, not a
blocker. [families.md](families.md) keeps what a younger audience would change.

## Privacy policy: does it agree?

`docs/privacy-policy.html` (and `.md`, and the copy in `app/src/main/assets/privacy_policy.md`)
describes every row above, including the Cast SDK's logging and the fact that it cannot be
turned off. Mismatches to settle before the form is submitted:

1. **The headline.** The policy opens with "collects nothing ... Two exceptions". With the
   recommended form answer (App interactions collected), Play's reviewer and a reader see
   "collects nothing" beside a form that says data is collected. Reword the opening to say the
   app itself collects nothing and Google's Cast component logs anonymous usage, as the Cast
   section already explains.
2. **The name.** On `main` the policy says "MuzViz Studio Pro". The rename branch
   (`origin/ccr-0a1f1dcf-87b4sp-beatrealms`, PR #1002) changes it to BeatRealms; the policy
   Play links to must carry the name on the store page.
3. **Deletion.** The form answers "no way to request deletion". The policy should say in one
   line that there is nothing held off the device to delete, and that clearing the app's
   storage or uninstalling removes what is on it.
4. **The URL.** Play needs a public, non-PDF URL. `pages.yml` publishes
   `docs/privacy-policy.html` to GitHub Pages on pushes to `main`; the owner confirms the live
   URL and that it shows the renamed text.

Nothing in the policy claims less than the code does. One thing in the app claims more: the
first-run safety notice (`res/values/strings.xml`, `safety_prompt`) says nothing on screen can
flash more than three times a second and that exported video is held to the same limit, while
AGENTS.md says the output flash limiter and its readback are gone and the limits are on the
parameters only. The store listing is worded to the weaker, true claim; the in-app notice is
the owner's to reconcile.
