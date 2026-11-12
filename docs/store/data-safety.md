# Data safety form: answers and evidence

Play Console › Policy and programs › App content › Data safety. The rules quoted here are from
"Provide information for Google Play's Data safety section"
([answer/10787469](https://support.google.com/googleplay/android-developer/answer/10787469), read
2026-11-11) and Google's Cast SDK disclosure
([developers.google.com/cast/docs/android_sender/data_disclosure](https://developers.google.com/cast/docs/android_sender/data_disclosure)).
File and line numbers were taken at b8ac569b5; the rows were rechecked against `origin/main` at b1ac6a75a (Record, screenshots and the sensors are new since) and again at 8fd87553d (Show on a computer, #1048); paths under
`app/src/main/java/dev/geode/` are written from `dev/geode/`.

## What Play counts

- **Collected** means sent off the device, by the app or by an SDK inside it. Data that is
  only processed on the device is not collected.
- **Shared** means passed to a third party. A transfer the user starts and expects (a share
  sheet, a web search they tap) is not sharing, and neither is a transfer to a service provider.
- Purchases handled by Google Play Billing are Google's, not the app's, as long as the app
  itself does not send purchase data anywhere.
- **Data an SDK in the app sends counts as the app's collection**, whether it goes to the
  developer or to a third party (same page, "SDKs"). This is why "the app collects nothing" is
  not the whole answer here.

## The one judgement call (owner decides)

The app's own code sends nothing off the device. The **Google Cast SDK** (linked through
`media3-cast`, `app/build.gradle.kts:264`; started with the app, `dev/geode/ui/MainActivity.kt:397` and
`dev/geode/cast/CastOptionsProvider.kt:29`, building the `RemoteCastPlayer` in `dev/geode/cast/CastController.kt:92-96`,
so the Cast button and the system output switcher can find a TV) does: Google says it logs
discovery and session events with device and app information, encrypted, without an identifier
that traces to a person, and that neither the user nor the developer can turn it off or delete
it. Under Play's definition an SDK's transmission is the app's collection, and Play checks SDK
behaviour against the form.

**Recommended: declare it** (the answers below). Google's Cast page (read 2026-11-11) lists the
SDK's logs as automatic, encrypted in transit, not shared with third parties, and impossible for
user or developer to opt out of or delete; the answers below say the same. It is the answer that cannot be called a
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
| Music files and tags | Read from the device to list and play; analysed in memory | `dev/geode/data/DeviceTrackQuery.kt:36`, `READ_MEDIA_AUDIO` asked in `dev/geode/ui/LibraryScreen.kt:210` | No: on device |
| Microphone audio | Only with Live input on; samples go into the in-memory ring, never a socket; paused in the background unless a cast holds the microphone. They reach a file only if the user starts Record while Live input is on: the MP4 on the device holds what the visual heard | `dev/geode/audio/MicCapture.kt:50`, `dev/geode/ui/CaptureController.kt`, `dev/geode/ui/RecordController.kt:38-41` | No: on device |
| Other apps' audio | Only with Visualize other apps (system consent, ongoing notification) or the wallpaper's and screen saver's Hear other apps; in a file only through Record, as above | `dev/geode/audio/PlaybackCaptureService.kt`, `dev/geode/wallpaper/OutputMixCapture.kt:77` | No: on device |
| Listening history, playlists, settings, presets | Kept in the app's private storage | `dev/geode/data/` stores | No: on device |
| Crash details | Written to `filesDir/crash-latest.txt` for the user to read; no crash SDK, nothing uploaded | `dev/geode/GeodeApp.kt:26-36` | No: on device |
| Purchase record | Read from the Play Store app, signature checked on the device, acknowledged back to Play; cached in app prefs. No developer server | `dev/geode/billing/PlayBillingRepository.kt:204-231`, `dev/geode/billing/EntitlementStore.kt:116-134` | No: Play Billing's own data; the app sends it nowhere else |
| Rendered videos, recordings, screenshots, presets, templates | Written to the device (videos where the user chooses, screenshots to `Pictures/BeatRealms`, `dev/geode/data/StillFiles.kt`); leave it only through the user's Share action | `dev/geode/ui/RenderBar.kt:234`, `dev/geode/ui/PresetActions.kt:390-406`, `dev/geode/ui/TemplatesSheet.kt:243`, `dev/geode/ui/LoopRenderSheet.kt:169` | No: user-initiated |
| Track title and artist | Handed to the user's browser when they tap "Search the web" for lyrics | `dev/geode/ui/LyricsSettings.kt:91-92` | No: user-initiated |
| The visual and the music while casting or showing on a computer | Streamed from the phone to the TV, or to a browser on a computer, on the user's own Wi-Fi; only the player's music, never the microphone or another app's audio (`dev/geode/cast/CastStream.kt:195-197`); the app's one server socket, bound to the Wi-Fi address; the page the browser opens loads nothing from the internet | `core/cast/LiveServer.cpp` (`start`), `dev/geode/cast/CastStream.kt:296-321`, `dev/geode/cast/ComputerLink.kt` | No: user-initiated, to the user's own device |
| Motion sensors | Accelerometer (a shake on Visuals saves a screenshot), gyroscope (camera look), proximity (the screen saver's cover); read while that screen shows, decided natively, nothing kept | `dev/geode/ui/EasterEggs.kt`, `render/GyroLook.kt`, `dev/geode/wallpaper/DreamCover.kt` | No: on device, not stored |
| Connected display | The visual on an HDMI, USB-C or wireless display through Android's `Presentation`; no socket of the app's | `dev/geode/ui/ConnectedDisplays.kt` | No: no transfer by the app |
| Device backup | Android's own backup of app data to the user's Google account, limited by `res/xml/data_extraction_rules.xml` | `AndroidManifest.xml:103-107` | No: the system's transfer, not the app's or an SDK's |

There is no analytics, advertising or crash-reporting SDK (CI fails the release build if one is
added, AGENTS.md › Conventions), and no network code outside `cast/`.

## Target audience and this form

Owner decision, 2026-11-11: the target audience is 13 and over, so the Families policy's data
rules (no SDK that is not approved for child-directed services, no identifiers from children)
do not apply, and the Cast SDK starting at launch is only the judgement call above, not a
blocker. [families.md](families.md) keeps what a younger audience would change.

## Privacy policy: does it agree?

Yes, as of this branch. `docs/privacy-policy.html` (and `.md`, and the copy in
`app/src/main/assets/privacy_policy.md`, which `PrivacyPolicyTest` keeps identical) describes
every row above:

- the microphone and other-app rows say the sound is never transmitted and is stored only
  inside a video the user records (› Record and screenshots), which is what Record does;
- its opening says the app *itself* collects nothing and names Google's Cast logging, so it
  agrees with "App interactions collected";
- the Cast section says the logging starts with the app and cannot be turned off or deleted;
- "Deleting your data" says nothing is held off the device to delete on request, which matches
  "no way to request deletion";
- the name is BeatRealms 3D throughout;
- the contact line is the placeholder `CONTACT_EMAIL`: the owner puts the store listing's
  contact address there (all three copies, one commit) before the policy goes live.

Play needs a public, non-PDF URL. `pages.yml` publishes `docs/privacy-policy.html` to GitHub
Pages on pushes to `main` (the address is in the workflow's header); the owner opens it and checks it shows this text before pasting it.

The in-app safety notice (`res/values/strings.xml`, `safety_prompt`, with `help_topic_safety_body`
and `behavior_flash_guidance`) says what the code does: brightness, contrast and bloom are capped
and brightness modulation slowed (`core/viz/VisualSafety`), nothing measures the finished picture,
and a MilkDrop preset only gets the brightness cap (`core/viz/scenes/MilkdropScene` reads the
clamped `brightness`), so the flashes it draws itself are not limited. The listing's
photosensitivity warning says the same without naming strobe or flash effects (#994).
