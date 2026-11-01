# Play release readiness (2026-10-30)

A pass over what stands between `claude/geode-finish` and a production release. It
builds on [POLICY_AUDIT.md](POLICY_AUDIT.md) (play-policy-insights, compliant) and adds
the `android-permissions-security` and `android-intent-security` checks on the manifest.
R8 keep rules are covered by a separate analysis and are not repeated here.

Ranked: **blockers** stop a production release; **should fix** won't get the app
rejected but is a real defect; **nice-to-have** is polish.

## Blockers

| # | Finding | Where | Who |
|---|---|---|---|
| B1 | **Privacy policy is not hosted.** *(Decided 2026-10-30: the policy now also ships in the app and the store description points to it. Play's App content form still needs a public URL.)* *(2026-11-01: `.github/workflows/pages.yml` now publishes `docs/privacy-policy.html`, an index from `docs/site/` and, later, `docs/help/` as `/help/`, on every push to `claude/geode-finish` or `main`.)* What remains is yours: Settings › Pages › Source = **GitHub Actions** (it still builds from the stale branch `claude/repo-audit-fix-plan-rfw176-u12`), then check that `https://tessie1993.github.io/geode-app/privacy-policy.html` loads. To deploy from `main` too, add it under Settings › Environments › github-pages › Deployment branches. | Repo Pages settings | You (Pages source) |
| B2 | **No release secrets or variables in the repo.** `gh secret list` and `gh variable list` are empty. Without `MUZVIZ_UPLOAD_*` the tag build is unsigned; without the `PLAY_LICENSE_KEY` variable `release.yml` refuses a `v*` tag build, by design. | Repo settings, [CONSOLE_CHECKLIST.md](CONSOLE_CHECKLIST.md) | You |
| B3 | **Console declarations.** Foreground service declaration for all four types, each with a short video (mediaPlayback, mediaProjection, mediaProcessing, dataSync). Data safety, content rating, target audience and ads declaration ("no ads") are all prepared in this folder but must be entered. | Play Console > App content | You |
| B4 | **Personal developer accounts** need a closed test with 12 opted-in testers for 14 consecutive days before production access. | Play Console > Testing | You |
| B5 | **Android developer verification.** From 2026 certified devices only install apps whose package is registered to a verified developer, starting in Brazil, Indonesia, Singapore and Thailand and widening through 2027. Play apps are covered once the Play developer account is verified and `com.muzviz.studiopro` is registered in Play Console; the debug APKs `ship-apk.yml` puts on GitHub releases are sideloaded and need their own package (and the committed debug key) registered, or testers in those countries can't install them. | Play Console › Android developer verification | You |
| B6 | **Trader status (EU Digital Services Act).** A paid product (`muzviz_pro`) makes you a trader for the DSA; Play requires the declaration, and from November 2026 apps from accounts without one are removed from EU listings. Traders' address, phone and email are shown on the listing. | Play Console › Settings › Developer account › Account details | You |
| B7 | **Japan (if it is a sales country).** Under Japan's Mobile Software Competition Act, Play asks developers selling in Japan to accept the updated terms and authorise Google as seller of record in Japan by **2026-12-01**; without it, Japan is dropped from the app's countries. Skip if Japan is excluded. | Play Console › Settings › Payments profile / Policy and programs | You |

Code-side, nothing found would get the build rejected: targetSdk 36 meets the current
requirement, every `.so` is 16 KB aligned and CI checks it, INTERNET and AD_ID are kept
out of the merged manifest by a CI check, every foreground service has a type, every
`PendingIntent` is `FLAG_IMMUTABLE`, and PBL 9.1.0 is current (no upgrade needed).

## Should fix

| # | Finding | Where | Fix |
|---|---|---|---|
| S1 | **Any installed app can read the music library without permission.** `PlaybackService` is exported (it must be, for System UI, Bluetooth and Android Auto) and accepts every controller. `onGetLibraryRoot`/`onGetChildren`/`onGetItem` serve the whole library tree (titles, artists, albums, folders) to any caller, so an app with no `READ_MEDIA_AUDIO` grant can enumerate it. `onAddMediaItems`/`onSetMediaItems` also accept caller-supplied URIs and play them with this app's own access. | `playback/PlaybackService.kt` | Override `onConnect`: give the library commands only to trusted controllers (`ControllerInfo.isTrusted`: this app, System UI, apps with `MEDIA_CONTENT_CONTROL` or an enabled notification listener) and to Android Auto, Wear and the Assistant verified by package name **and** Google Play as the installer. Untrusted controllers keep transport only. Drop caller-supplied `localConfiguration` from untrusted controllers. |
| S2 | **Shared links import silently and can overwrite.** `geode://preset` and `geode://template` are `BROWSABLE`, so any web page or app can fire them. The import runs on arrival with a toast after the fact; a template link with an existing id **replaces** the user's template, and a template may carry a custom GLSL shader. Decoding is bounded (8 kB link, 4 MB inflate), so this is a consent problem, not a crash. | `ui/MainActivity.kt`, `ui/VisualsViewModel.kt` | Show a confirm dialog ("Import preset *X*?" / "Replace template *X*?") before writing anything. |
| S3 | **Android Auto opt-in is missing from the checklist.** The listing promises Android Auto, but Auto only shows apps opted in under *Release > Advanced settings > Form factors*, which triggers a separate Auto quality review. | [CONSOLE_CHECKLIST.md](CONSOLE_CHECKLIST.md) | Add the step, or drop Auto from the listing for the first release. |
| S4 | **Memory quality bar (February 2027).** Play adds an Android vitals technical-quality threshold for excessive memory use (low-memory kills and out-of-memory crashes in the foreground); apps over it get a store warning and lower visibility. The live visualizer (projectM textures, 4K export buffers, fluid sims) is the risk. Track it in Android vitals from the closed test on, and add a memory check to [DEVICE_CHECKS.md](../DEVICE_CHECKS.md) before February 2027. | Android vitals | Measure, then fix what it shows |

## Nice-to-have

| # | Finding | Where |
|---|---|---|
| N1 | Live input keeps its `AudioRecord` thread open while the app is in the background. On targetSdk 36 without a microphone FGS type the platform hands it silence, so nothing is heard, but the loop still runs. Pause on `ProcessLifecycleOwner` ON_STOP, resume on ON_START (already in POLICY_AUDIT.md). | `ui/CaptureController.kt` |
| N2 | `NowPlayingWidgetReceiver` is exported for `APPWIDGET_UPDATE`, as Glance requires. Any app can send it an explicit update; the worst case is a redraw. Leave it. | Manifest |
| N3 | The entitlement cache is in Auto Backup. Since 2026-11-01 a verified purchase stays owned offline with no expiry (Pro is removed only when Play answers "not owned"), and the signature is re-checked on every read, so a forged cache fails. A genuine cache restored to a device on a different Google account still reads "owned" until Play first answers; the foreground refresh corrects that within seconds when online, but a device kept offline stays Pro. Excluding the file would also drop the useful case (same account, offline). Accepted for now. | `billing/EntitlementStore.kt`, `res/xml/*backup*` |

## Fixed on this branch (`claude/play-readiness-50643f`)

- **B1, partly:** the About screen's privacy button opened a dead URL
  (`tessie1993.github.io/music-visualizer-2/…`). The policy now ships as an asset and opens
  in a dialog, offline; `PrivacyPolicyTest` keeps the asset equal to `docs/privacy-policy.md`.
  The store description points to it. The console URL is still yours to provide.
- **S1:** `PlaybackService.onConnect` gives untrusted controllers transport and the current
  item only; library browsing goes to trusted controllers, system apps, and Android Auto,
  Wear OS and the Google app when Google Play installed them.
- **S2:** a readable `geode://` link opens an Import / Replace dialog; nothing is written
  until the user confirms.
- **S3:** the Android Auto opt-in step is in [CONSOLE_CHECKLIST.md](CONSOLE_CHECKLIST.md) §7.
- **N1:** Live input releases the mic in `MainActivity.onStop` and resumes in `onStart`.

Device checks, 2026-10-30, emulator (API 37.1, small_phone), debug build of the PR merge
commit e9cca06:
- **Privacy dialog:** Settings › About › Privacy policy opens offline; headings, bullets
  and the table (as "What: why. Where." bullets) read cleanly. Pass.
- **Deep link:** `am start -a VIEW -c BROWSABLE -d geode://preset/…` shows "Import shared
  preset?" with the preset's name. Cancel leaves `files/presets/` untouched; Import writes
  `Link Test.json`. Pass. (The template Replace path wasn't exercised.)
- **Mic:** with Live input on, `dumpsys audio` shows an active, unsilenced recording;
  HOME logs `rec stop`; returning starts a new recording. Pass.
- **Session:** playing a track gives the foreground media notification with transport,
  system controllers connect, and hardware PLAY/PAUSE keys reach the session. Pass.
- **Not checked:** an untrusted third-party controller being refused the browse tree, and
  Android Auto (DHU) browsing. Both need an extra app on the device.

## Checked and fine

- **Exported components.** `MainActivity` (launcher, deep links, `MEDIA_PLAY_FROM_SEARCH`, which only searches the local library); `PlaybackService` (see S1); `MediaButtonReceiver` (Media3); wallpaper and dream services, protected by `BIND_WALLPAPER` and `BIND_DREAM_SERVICE`; the widget receiver. Everything else is `exported="false"`.
- **Notification listener.** Not exported, protected by `BIND_NOTIFICATION_LISTENER_SERVICE`, does nothing but enable `getActiveSessions`. The settings screen explains it before sending the user to system settings.
- **MediaProjection.** `PlaybackCaptureService` is not exported, typed `mediaProjection`, asks for consent through `createScreenCaptureIntent` each time, and shows a notification with a Stop action. The projection result is read only from its own start intent.
- **FileProvider.** Not exported, `grantUriPermissions` only, scoped to `files/presets/`.
- **Intents.** No nested-intent redirection, no sticky broadcasts, no dynamic receivers. `onNewIntent` calls `setIntent` and applies the same handlers as `onCreate`.
- **Billing.** PBL 9.1.0 with pending purchases, auto reconnection, on-device signature check, acknowledgement retried on every refresh; release builds can't use the fake repository.
- **Docs.** [DATA_SAFETY.md](DATA_SAFETY.md), [STORE_LISTING.md](STORE_LISTING.md), [CONTENT_RATING.md](CONTENT_RATING.md) and [BILLING.md](BILLING.md) match the code. The listing avoids ranking claims.
