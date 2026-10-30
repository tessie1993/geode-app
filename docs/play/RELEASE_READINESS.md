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
| B1 | **Privacy policy is not hosted.** Play requires a public privacy policy URL for any app that asks for a sensitive permission (`RECORD_AUDIO`, MediaProjection capture) and for the Data safety form. `https://tessie1993.github.io/geode-app/privacy-policy.html` returns 404: Pages is switched on, but it builds from the stale branch `claude/repo-audit-fix-plan-rfw176-u12` and no workflow on `claude/geode-finish` or `main` publishes the policy. The text itself (`docs/privacy-policy.md`/`.html`) is complete and matches the code. | Repo Pages settings, `.github/workflows/` | Code (a Pages workflow) + you (Pages source) |
| B2 | **No release secrets or variables in the repo.** `gh secret list` and `gh variable list` are empty. Without `MUZVIZ_UPLOAD_*` the tag build is unsigned; without the `PLAY_LICENSE_KEY` variable `release.yml` refuses a `v*` tag build, by design. | Repo settings, [CONSOLE_CHECKLIST.md](CONSOLE_CHECKLIST.md) | You |
| B3 | **Console declarations.** Foreground service declaration for all four types, each with a short video (mediaPlayback, mediaProjection, mediaProcessing, dataSync). Data safety, content rating, target audience and ads declaration ("no ads") are all prepared in this folder but must be entered. | Play Console > App content | You |
| B4 | **Personal developer accounts** need a closed test with 12 opted-in testers for 14 consecutive days before production access. | Play Console > Testing | You |

Code-side, nothing found would get the build rejected: targetSdk 36 meets the current
requirement, every `.so` is 16 KB aligned and CI checks it, INTERNET and AD_ID are kept
out of the merged manifest by a CI check, every foreground service has a type, every
`PendingIntent` is `FLAG_IMMUTABLE`, and PBL 9.1.0 is current (no upgrade needed).

## Should fix

| # | Finding | Where | Fix |
|---|---|---|---|
| S1 | **Any installed app can read the music library without permission.** `PlaybackService` is exported (it must be, for System UI, Bluetooth and Android Auto) and accepts every controller. `onGetLibraryRoot`/`onGetChildren`/`onGetItem` serve the whole library tree (titles, artists, albums, folders) to any caller, so an app with no `READ_MEDIA_AUDIO` grant can enumerate it. `onAddMediaItems`/`onSetMediaItems` also accept caller-supplied URIs and play them with this app's own access. | `playback/PlaybackService.kt` | Override `onConnect`: give the library commands only to trusted controllers (`ControllerInfo.isTrusted`: this app, System UI, apps with `MEDIA_CONTENT_CONTROL` or an enabled notification listener) and to Android Auto, Wear and the Assistant verified by package **and signing certificate**. Untrusted controllers keep transport only. Drop caller-supplied `localConfiguration` from untrusted controllers. |
| S2 | **Shared links import silently and can overwrite.** `geode://preset` and `geode://template` are `BROWSABLE`, so any web page or app can fire them. The import runs on arrival with a toast after the fact; a template link with an existing id **replaces** the user's template, and a template may carry a custom GLSL shader. Decoding is bounded (8 kB link, 4 MB inflate), so this is a consent problem, not a crash. | `ui/MainActivity.kt`, `ui/VisualsViewModel.kt` | Show a confirm dialog ("Import preset *X*?" / "Replace template *X*?") before writing anything. |
| S3 | **Android Auto opt-in is missing from the checklist.** The listing promises Android Auto, but Auto only shows apps opted in under *Release > Advanced settings > Form factors*, which triggers a separate Auto quality review. | [CONSOLE_CHECKLIST.md](CONSOLE_CHECKLIST.md) | Add the step, or drop Auto from the listing for the first release. |

## Nice-to-have

| # | Finding | Where |
|---|---|---|
| N1 | Live input keeps its `AudioRecord` thread open while the app is in the background. On targetSdk 36 without a microphone FGS type the platform hands it silence, so nothing is heard, but the loop still runs. Pause on `ProcessLifecycleOwner` ON_STOP, resume on ON_START (already in POLICY_AUDIT.md). | `ui/CaptureController.kt` |
| N2 | `NowPlayingWidgetReceiver` is exported for `APPWIDGET_UPDATE`, as Glance requires. Any app can send it an explicit update; the worst case is a redraw. Leave it. | Manifest |
| N3 | The entitlement cache is in Auto Backup. Restored to a device on a different Google account it reads "owned" until the first foreground refresh asks Play. The refresh corrects it within seconds; excluding the file would also drop the useful case (same account, offline). Leave it unless the grace window grows. | `billing/EntitlementStore.kt`, `res/xml/*backup*` |

## Checked and fine

- **Exported components.** `MainActivity` (launcher, deep links, `MEDIA_PLAY_FROM_SEARCH`, which only searches the local library); `PlaybackService` (see S1); `MediaButtonReceiver` (Media3); wallpaper and dream services, protected by `BIND_WALLPAPER` and `BIND_DREAM_SERVICE`; the widget receiver. Everything else is `exported="false"`.
- **Notification listener.** Not exported, protected by `BIND_NOTIFICATION_LISTENER_SERVICE`, does nothing but enable `getActiveSessions`. The settings screen explains it before sending the user to system settings.
- **MediaProjection.** `PlaybackCaptureService` is not exported, typed `mediaProjection`, asks for consent through `createScreenCaptureIntent` each time, and shows a notification with a Stop action. The projection result is read only from its own start intent.
- **FileProvider.** Not exported, `grantUriPermissions` only, scoped to `files/presets/`.
- **Intents.** No nested-intent redirection, no sticky broadcasts, no dynamic receivers. `onNewIntent` calls `setIntent` and applies the same handlers as `onCreate`.
- **Billing.** PBL 9.1.0 with pending purchases, auto reconnection, on-device signature check, acknowledgement retried on every refresh; release builds can't use the fake repository.
- **Docs.** [DATA_SAFETY.md](DATA_SAFETY.md), [STORE_LISTING.md](STORE_LISTING.md), [CONTENT_RATING.md](CONTENT_RATING.md) and [BILLING.md](BILLING.md) match the code. The listing avoids ranking claims.
