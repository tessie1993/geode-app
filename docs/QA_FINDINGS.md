# QA findings: deep pre-release pass (2026-10-31)

Branch `claude/deep-qa`. Eleven read-only audits, one per angle, each verified by reading the code paths; nothing here was found by a build or a device run. Every entry has a **Status**: fixed (with the commit subject), won't fix (with the reason), owned elsewhere (another session's open PR owns the file; the finding was sent to it), or open.

Severity is the auditor's, re-ranked where a duplicate or a false positive was found. Duplicates point at the entry that carries the fix.

## Security and secrets

Scope: secrets in the tree and history, signing, manifest components, deep links, FileProvider, network use, Play Billing, PendingIntents, MediaProjection, backup restore, JNI bridges. Skills applied: android-intent-security, android-permissions-security.

### SEC-1 Shared-link confirmation can be bypassed: a crafted geode:// link imports silently and can overwrite a saved template
- **Status: fixed (Shared links: nothing imports without the confirm dialog)**
- Severity: medium
- Location: app/src/main/java/dev/geode/ui/MainActivity.kt:180-193; app/src/main/java/dev/geode/ui/VisualsViewModel.kt:252-268; app/src/main/java/dev/geode/data/TemplateFormat.kt:195-200; app/src/main/java/dev/geode/data/TemplateRepository.kt:649-662
- Failure scenario: a web page or chat message carries `geode://preset/AAAA?geode://template/<valid base64url template>` (the preset part is junk). The user taps it and Chrome hands it to MainActivity (VIEW + BROWSABLE, host `preset` matches). `offerFor()` sees `isPresetLink(data)`, `PresetLink.decode(data)` fails, and it returns null without ever looking for the template. `receiveSharedLink` then takes the "unreadable" branch. `importSharedPreset` shows "unreadable", and `importSharedTemplate(data)` runs because `TemplateFormat.linkIn` finds the template link anywhere in the string. `importTemplateText` decodes it and `persist()` writes it with no dialog. If the template's `id` matches one of the user's saved templates, `persist()` overwrites it (`TemplateImport.Replaced`). The id is a UUID that is in every template the user has ever shared. A preset can get past the dialog the same way when the data has literal whitespace (`geode://preset/<valid> x`, sent by any app as an explicit intent, since MainActivity is exported): `decode(data)` fails on the space, but `importPresetLink` uses `findIn()`, which stops at the whitespace and decodes the valid part. Imported content can carry a custom GLSL shader or MilkDrop code (see SEC-4).
- Evidence: `offerFor`: `if (PresetLink.isPresetLink(data)) { val json = PresetLink.decode(data) ?: return null ... }`. In `receiveSharedLink`, the fallback is `if (PresetLink.isPresetLink(data) || visualsViewModel.isTemplateLink(data)) { importSharedPreset(...); importSharedTemplate(...) }`. Its KDoc says "an unreadable one goes straight to the import path, which writes nothing", which is false when the template half, or the whitespace-trimmed preset half, is readable.
- Suspected root cause: the confirm path (`offerFor`) and the import paths parse the same string differently. `decode(whole)` is used against `findIn`/`linkIn`, which search for a substring. The fallback assumes "no offer" means "nothing importable".
- Suggested fix: route every import through the offer: if `offerFor(data)` is null, show "unreadable" only and never call the import functions. Parse the link once (same extractor) for both the dialog and the import.

### SEC-2 Purchase signature check fails open on a missing or malformed licence key, and a signed manual release can ship that way
- **Status: in progress: CI half fixed (Release CI: every signed run needs a licence key that parses); runtime fail-closed in release chosen by the user**
- Severity: medium
- Location: app/src/main/java/dev/geode/billing/PurchaseVerifier.kt:25-45; .github/workflows/release.yml:89-107 and 131-145; app/build.gradle.kts:68-71
- Failure scenario: (a) `PLAY_LICENSE_KEY` is pasted with its PEM header, a stray quote, or a non-Base64 character. `Base64.decode`/`generatePublic` throws, `key` becomes null, and `isGenuine()` returns true for every purchase. `release.yml` only checks that the variable is non-empty, so a tagged release builds. (b) A `workflow_dispatch` run of release.yml (documented in docs/play/CONSOLE_CHECKLIST.md §6 as a valid way to get the upload AAB) signs with the upload key but only enforces the licence key when `IS_TAG=true`, so a Play-uploadable signed AAB can have verification turned off. The same goes for a local release build from `keystore.properties`. In either case any PURCHASED `Purchase` with any signature (a patched Play Store, a billing hook) grants Pro and gets acknowledged.
- Evidence: `if (publicKey == null) { ... RingLog.note(TAG, "no PLAY_LICENSE_KEY in this build; purchase signatures are not checked"); return true }`. An unparseable key goes through `.onFailure { RingLog.note(...) }.getOrNull()`, ending in the same `return true`. In release.yml: `if [ "$IS_TAG" = "true" ] && [ -z "${LICENSE_KEY:-}" ]`.
- Suspected root cause: fail-open was chosen for dev convenience ("would otherwise take money and grant nothing"), but release builds get it too, and the CI gate only checks that the key is present, on tags only.
- Suggested fix: when the key is missing or does not parse, fail closed in release builds (or fail the Gradle `bundleRelease` task when the key is blank or does not parse as an X.509 RSA key), and apply the licence-key check to every signed run, not just tags.

### SEC-3 Public GitHub pre-release APKs ship fake billing, so anyone can unlock every Pro feature for free
- **Status: won't fix: user decision, pre-release debug APKs keep fake billing**
- Severity: medium
- Location: .github/workflows/ship-apk.yml:112; app/build.gradle.kts:124-127; app/src/main/java/dev/geode/billing/FakeBillingRepository.kt:46; app/src/main/java/dev/geode/di/BillingModule.kt:53
- Failure scenario: the repo is PUBLIC (`gh repo view`: tessie1993/geode-app, visibility PUBLIC). Every merge to main or claude/geode-finish publishes `assembleDebug` as a GitHub Release that needs no login to download. The debug build type defaults `USE_FAKE_BILLING=true` (ship-apk does not pass `-Pmuzviz.fakeBilling=false`). `FakeBillingRepository.nextOutcome` defaults to `SUCCEED`, so tapping "Buy" on the paywall grants Pro (4K/60 fps, no watermark, batch and loop renders, all family styles, wallpaper and dream) with no payment. It writes the same `geode-pro` cache that `ProSnapshot` reads. The build is also `isDebuggable = true`.
- Evidence: `val fakeBilling = (findProperty("muzviz.fakeBilling") as String?)?.toBoolean() ?: true`; `./gradlew :app:assembleDebug -PreleaseNumber=...`; `val nextOutcome = MutableStateFlow(FakePurchaseOutcome.SUCCEED)`.
- Suspected root cause: the debug APK made for sideload testing is published as a public product download, with test billing left in.
- Suggested fix: make ship-apk releases private (or artifacts only), or build them with fake billing off, or with the fake defaulting to "not owned" and the debug panel hidden.

### SEC-4 Custom GLSL and MilkDrop code in shared presets/templates is not covered by the "unconditional" flash clamp [PR183]
- **Status: partly fixed (Shared links: warn when a link brings shader or MilkDrop code); output-luma limiter sent to PR #183's owner**
- Severity: low
- Location: core/viz/VisualSafety.hpp:15-31 [PR183 files: core/viz/Renderer.cpp:103-106, 501-502]; app/src/main/java/dev/geode/ui/VisualSettingsController.kt:98-114; app/src/main/java/dev/geode/ui/EnginePlumbing.kt:126-133
- Failure scenario: a shared preset or template link carries `customShader` (or `milkPreset`), e.g. a fragment shader that returns `vec4(step(0.5, fract(iTime*15.0)))`, a 15 Hz full-screen strobe. After import (with no dialog at all via SEC-1) and apply, `VizApply(customShader=...)` goes straight to `submitShader` and replaces that scene's fragment source. `safety::apply`, `FlashBudget` and `limitLfoRate` only limit SceneParams, LFO rates and the `flash` uniform. Nothing limits frame-to-frame luminance in the shader's output, so content made by a third party goes past the photosensitivity protection the app advertises. A shader that never terminates (`for(;;)`) can also hang the GL thread every time the app restores that state.
- Evidence: `safety::apply(const SceneParams&...)`; `in.flash = fx.flash * flashBudget_.gainFor(...)`; `Renderer::setCustomShader` → `pendingShaders_.emplace_back(sceneId, fragmentSource)` with no validation; `preset.customShader?.let { _vizApply.tryEmit(VizApply(customShader = it, ...)) }`.
- Suspected root cause: the safety layer works on parameters, not on rendered output, and the share format treats code as data.
- Suggested fix: warn in the import dialog when a shared preset/template carries shader or Milk code (or strip it), and think about an output-luma flash limiter in the composite pass.

### SEC-5 Pro cache rides Auto Backup and device transfer, is honoured without a Play check by the wallpaper and dream, and never expires once the clock goes back
- **Status: in progress: user chose guard-clock, no backup**
- Severity: low
- Location: app/src/main/AndroidManifest.xml:111-115; app/src/main/res/xml/data_extraction_rules.xml; app/src/main/res/xml/backup_rules.xml; app/src/main/java/dev/geode/billing/EntitlementStore.kt:23; app/src/main/java/dev/geode/billing/ProSnapshot.kt:17-19
- Failure scenario: the `geode-pro` prefs (`pro_owned=true`, `pro_verified_at`) are not excluded from cloud backup or device transfer. If they are restored onto another device or Google account, the live wallpaper and dream (`ProSnapshot`, which never asks Play) run Pro for up to 30 days. The main app also keeps Pro while Play cannot be reached (e.g. Play Store disabled). Separately, `vouchesForPro` is `nowMs - verifiedAtMs < GRACE_MS`. If the clock is set before the last confirmation, the difference is negative, so a refunded user who keeps Play unreachable and moves the clock back keeps Pro with no end, not just for 30 days.
- Evidence: there is no `<exclude domain="sharedpref" path="geode-pro.xml"/>` in either rules file. `fun vouchesForPro(nowMs: Long): Boolean = owned && nowMs - verifiedAtMs < GRACE_MS`.
- Suspected root cause: an install-local entitlement cache is treated as user data; a backwards clock is read as "fresh".
- Suggested fix: exclude `geode-pro.xml` from cloud-backup and device-transfer (the trial key in it is install-local by design anyway), and treat `nowMs < verifiedAtMs` as needing re-verification (or clamp it to the grace window).

#### Checked and OK
- Secrets: no keys, tokens, passwords or private keys in tracked files or in history (`git log --all --diff-filter=A`). Only `app/debug.keystore` (debug, intentional) and `keystore.properties.example` (empty values). `.gitignore` covers `keystore.properties`, `*.jks`, `*.keystore`, `.env*`, `local.properties`, `google-services.json`.
- Release signing: `signingConfigs.release` is built only from `keystore.properties` or `GEODE_*` env. With neither, release is unsigned and never falls back to the debug key. release.yml keeps signing secrets out of `pull_request` runs, decodes the keystore to `$RUNNER_TEMP` and deletes it, uses `permissions: contents: read`, and verifies the jarsigner output. No `pull_request_target`, and no `${{ github.event.* }}` interpolated into `run:` scripts. arch-map only uses SHAs; ship-apk `inputs.ref` is dispatch-only.
- Network: INTERNET and ACCESS_NETWORK_STATE are removed with `tools:node="remove"`, and release.yml checks the merged manifest. No WebView, URL, HttpURLConnection, socket or OkHttp anywhere in app/engine.
- Manifest: PlaybackCaptureService, ExportService and the FileProvider are not exported. Wallpaper is gated by BIND_WALLPAPER, dream by BIND_DREAM_SERVICE, notification listener by BIND_NOTIFICATION_LISTENER_SERVICE (exported=false matches the platform sample). MediaButtonReceiver and the widget receiver only take transport/update broadcasts.
- PlaybackService (exported MediaLibraryService): untrusted controllers get `DEFAULT_UNTRUSTED_SESSION_COMMANDS` (checked in the Media3 1.11.0 sources: read-only, no library commands), and the legacy MediaBrowserCompat stub enforces the same command checks. Transport only, with no timeline, and no set or add of media items. Browsing is limited to trusted callers, verified system apps, and Google surfaces installed by Play.
- PendingIntents: all are explicit and FLAG_IMMUTABLE (PlaybackService session activity, capture open/stop, export open). Media3 `PlaybackPendingIntentBuilder` for PiP; Glance actions for the widget. No mutable or implicit PendingIntent, no sticky or implicit broadcasts, no dynamic receivers.
- MediaProjection: the consent Intent comes only from `createScreenCaptureIntent()` with RESULT_OK, goes to a non-exported service, and `startForeground(MEDIA_PROJECTION)` is called before `getMediaProjection`. Failures are caught and the service stops itself.
- Deep links: PresetLink caps link length at 8000, payload length, and the gzip inflate at 4 MB (no zip bomb). Template links reuse the same decoder. Preset file names go through `safeFileName` (`[^A-Za-z0-9-_ ]` → `_` plus a hash), so there is no path traversal into presets/ or milk/. Template export names strip `\/:*?"<>|` and are capped at 80 characters. `MEDIA_PLAY_FROM_SEARCH` only does a library search and play. `onNewIntent` calls `setIntent` and applies the same handling.
- FileProvider: `preset_paths.xml` exposes only `files-path presets/`. Shares use ACTION_SEND with FLAG_GRANT_READ_URI_PERMISSION on a single content URI.
- Backup restore (data/backup): rejects absolute paths, `..`, backslashes, NUL and drive letters. Each entry is canonicalized inside staging, must be listed once in the manifest, and must match its size and SHA-256. Entry count is capped, total bytes are capped on the real stream (archive copy +64 MB slack), the manifest is capped at 8 MB, then checked against the section allowlist. The billing prefs (`geode-pro`) are not restorable. Device-local keys are filtered both ways.
- Billing: the product is filtered to `muzviz_pro`. Only PURCHASED and verified purchases grant and get acknowledged (retried on refresh). Pending purchases do not grant, and a full-list query revokes. Release has `USE_FAKE_BILLING=false` hardcoded, and the paywall debug panel only appears when the repository is the fake. The obfuscated account id is not needed without a backend.
- JNI (`app/src/main/cpp/*_jni.cpp`): strings go through RAII `Utf8Chars` and null-checked. Arrays are bounded by `GetArrayLength` (`vizPushPcm`, `vizSetTouch`, `vizWriteAudio`, `macros*` capped at 16, `paramsRandomize`/`ResetTab` sized to the param count, overlay/underlay check `w*h <= length` in 64-bit, resampler checks in 64-bit). Direct buffers: `playerReadTap` clamps to the capacity, and `dspProcess` frames match the scratch buffer the Kotlin caller sizes from the same channel count. Native `geode_viz_set_lfo`/`adsr`/`reactions`/`params` check slot, count and enums. Tag text crosses as byte[], not NewStringUTF, so malformed UTF-8 in tags cannot abort.

## Data safety and privacy

Scope: Auto Backup rules, the manual backup zip, MediaStore pending rows and the sweeps, store write atomicity and corrupt-file handling, privacy policy and docs/play vs the code, permissions. The `play-policy-insights` skill ran in full (7 workers, 1 critic, report at `scratchpad/qa/ppi/.scratch/play_policy_insights_6f18ec3a-0d83-45aa-b205-75029450a497/compliance_report.md`). Its result: **Compliant**, no violations. The one suggestion is the Play Console Foreground Service declaration for mediaPlayback, mediaProjection, mediaProcessing and dataSync, which CONSOLE_CHECKLIST §7 already covers. Its suggested Data safety label matches docs/play/DATA_SAFETY.md: purchase history only; shared videos are user-initiated and exempt.

None of the findings below touch files under PR #183 or #184.

### DATA-1 The orphaned-pending-export sweep cannot see pending rows, so it never deletes any
- **Status: duplicate of EXP-3, fixed**
- Severity: medium
- Location: app/src/main/java/dev/geode/GeodeContainer.kt:134 (also :138)
- Failure scenario: A render is killed mid-write by a crash, a force-stop, an LMK or an FGS timeout. Its MediaStore row stays `IS_PENDING=1` in Movies/MuzViz with a partial file of up to hundreds of MB. On the next launch the sweep runs `resolver.query(collection, …, "… IS_PENDING = 1 …", …)` with no pending-match option. MediaStore filters pending items out of queries by default. The API 29 `setIncludePending` doc says "By default no pending items are returned", and on API 30+ `QUERY_ARG_MATCH_PENDING` defaults to excluding them. So the cursor is empty, and the row and its storage are never reclaimed. That is exactly the leak the KDoc says this sweep exists to prevent.
- Evidence: The repo already knows this. `export/StudioClips.kt:241-262` (`pendingVisibleName`) has to re-query with `QUERY_ARG_MATCH_PENDING = MATCH_INCLUDE` (R+) or `MediaStore.setIncludePending(uri)` (Q) because a plain query (`currentName`) returns nothing for a pending row. The sweep has no such option. The per-id `resolver.delete(uri, null, null)` is filtered the same way.
- Suspected root cause: The selection names `IS_PENDING = 1`, but MediaProvider applies its default pending filter before the caller's selection.
- Suggested fix: On R+, query (and delete) with a Bundle that sets `QUERY_ARG_MATCH_PENDING = MATCH_INCLUDE` plus `QUERY_ARG_SQL_SELECTION`/args; on Q, use `MediaStore.setIncludePending(collection)`. Confirm on an API 29 and an API 30+ device (DEVICE_CHECKS).

### DATA-2 StudioExporter.publish leaves a pending MediaStore row behind when the copy throws
- **Status: open**
- Severity: medium
- Location: app/src/main/java/dev/geode/export/StudioExporter.kt:205-235
- Failure scenario: A Studio export finishes into the scratch file, then `publish()` inserts a pending row and copies the scratch into it. If the copy throws partway (disk full is the realistic case for a large 4K file, or an `IOException` from the provider), the `runCatching { … }.getOrNull()` swallows it and returns null. The row is never deleted. The caller deletes the scratch (`:127`), so the only copy left is a partial, unplayable, `IS_PENDING` video in Movies/MuzViz. Because of DATA-1, nothing ever removes it. The same happens if the final `update(IS_PENDING=0)` throws: the full video stays invisible.
- Evidence: `resolver.openOutputStream(uri)?.use { out -> file.inputStream().use { it.copyTo(out) } } ?: run { resolver.delete(uri, null, null); return null }` deletes only on a null stream. An exception skips straight to `getOrNull()`. Compare `VideoExporter.kt:246-249`, which deletes the row on any `Exception`.
- Suspected root cause: The cleanup runs only on the null-stream path, not on exceptions.
- Suggested fix: Wrap the copy and the publish update in try/catch so that any failure (CancellationException included) deletes `uri` before returning null.

### DATA-3 A failed restore rollback, or a process death mid-restore, permanently deletes the user's original data
- **Status: open**
- Severity: medium
- Location: app/src/main/java/dev/geode/data/backup/BackupRepository.kt:112-114, 120-123, 182-188; also :86 (`inspect`)
- Failure scenario (a): `swapFiles` renames e.g. `presets` into `noBackupFilesDir/backup-restore/aside/presets`, then a staged rename fails. The rollback's `move(aside → filesDir)` fails too, and it only logs: `"rollback left $target in ${asideDir.path}"`. `restore()`'s `finally { discardStaging() }` then runs `workDir.deleteRecursively()`. `asideDir` is `File(workDir, "aside")`, so the only remaining copy of the user's presets is deleted moments after the code reports it was "left" there.
- Failure scenario (b): The process dies (LMK, crash, user swipes away) after the aside rename and before the staged rename. After relaunch, filesDir has no `presets`, and the originals sit in `aside`. The next time the user tries a restore, `inspect()` calls `discardStaging()` first, which deletes `aside` and the originals with it. Nothing at startup moves an orphaned `aside` back.
- Evidence: `private val asideDir: File get() = File(workDir, "aside")`; `private fun discardStaging() { staged = null; workDir.deleteRecursively() }`, called unconditionally in `restore()`'s finally and at the start of `inspect()`.
- Suspected root cause: The rollback holding area is inside the scratch directory that every exit path wipes.
- Suggested fix: Keep `aside` outside `workDir`, or skip deleting it when a rollback failed. On startup and in `inspect()`, move any leftover `aside/*` back into filesDir before wiping staging.

### DATA-4 Restoring while music plays: live in-memory stores overwrite the restored files before the forced restart
- **Status: open**
- Severity: medium
- Location: app/src/main/java/dev/geode/data/backup/BackupRepository.kt:102-116, app/src/main/java/dev/geode/data/HistoryStore.kt:125-169, app/src/main/java/dev/geode/ui/ListeningTracker.kt:58-86, app/src/main/java/dev/geode/ui/BackupSettings.kt:160-181
- Failure scenario: The user restores the Music section while a track is playing. `PlaybackService` keeps playing during and after the restore. `RestartDialog` is modal but does not stop playback, and the user can press Home and leave the process running. On the next track transition (`PlayerSession.kt:1178` → `listening.recordPlay`) or pause (`accrueListenTime` → `historyStore.flush()`), `HistoryStore.writeNow()` serialises its in-memory `entries`, which still hold the pre-restore history, and `AtomicWrite`s them over the `history.json` the restore just placed. The restored listening history (play counts, recently played) is lost, and the restart then loads the old one. The same applies to any controller that re-saves its in-memory copy of a Settings or Creative prefs key on its next change.
- Evidence: The KDoc on `RestartDialog` states the hazard: "every store read its data at startup and still holds the old copy, and the next save from any of them would write it back over the restore". But `restore()` neither pauses playback nor stops the stores from writing. `HistoryStore` loads once in `init` and rewrites the whole file from memory.
- Suspected root cause: The restore relies on the user tapping Restart before any background write happens, and playback continues in the same process.
- Suggested fix: Pause playback and call `exitProcess` (or block store writes) in the same step that commits the restore, instead of waiting for a user tap.

### DATA-5 A multi-section restore that fails partway leaves earlier sections swapped in, with no restart prompt
- **Status: open**
- Severity: low
- Location: app/src/main/java/dev/geode/data/backup/BackupRepository.kt:107-111; app/src/main/java/dev/geode/ui/BackupViewModel.kt:68-75, 95-107
- Failure scenario: The user restores Settings, Creative and Music. Settings and Creative swap and their prefs commit. Then Music's `swapFiles` or `restorePrefs` throws. `launchStep` maps that to `BackupUiState.Failed`, and the user sees an error and dismisses it to Idle. `RestartDialog` is never shown. The app keeps running on stores that hold the old Settings and Creative data in memory. The next save writes old values over the newly restored ones, and file-reading stores now see the new data. The app ends up with a mix of backup and pre-backup state, and the user believes nothing changed.
- Evidence: The loop commits each section in turn and has no rollback across sections. Only the `Restored` state leads to the restart.
- Suspected root cause: Each section is atomic on its own; the multi-section restore as a whole is not.
- Suggested fix: When `restore()` fails after at least one section landed, report that partial result and still force the restart, or roll back the sections already swapped.

### DATA-6 An imported Milk pack can push Auto Backup over its quota, so nothing gets backed up
- **Status: open**
- Severity: low
- Location: app/src/main/res/xml/backup_rules.xml:5-27, app/src/main/res/xml/data_extraction_rules.xml:6-28; app/src/main/java/dev/geode/data/MilkPackImporter.kt:20-51
- Failure scenario: The user imports a typical community Milk pack (thousands of `.milk` presets, tens of MB of text) into `filesDir/milk`. The rules exclude `milk/textures` and `milk/generated` but not the presets in `milk/` itself, and `MilkPackImporter.import` has no count or size cap. The app's backup then exceeds the ~25 MB Auto Backup quota. The rules' own comment describes the result: "backup of EVERYTHING (presets, playlists, history) silently stops".
- Evidence: The textures exclusion exists for this exact quota reason; the presets directory beside it is unbounded and included.
- Suspected root cause: The quota analysis covered textures and takes but not imported presets.
- Suggested fix: Exclude `milk` from `<cloud-backup>` (it is in the manual zip already), or cap imports and show the user how much space they take.

### DATA-7 Auto Backup restores device-specific state that the manual backup strips on purpose
- **Status: open**
- Severity: low
- Location: app/src/main/res/xml/data_extraction_rules.xml:6-28 (no shared_prefs excludes); app/src/main/java/dev/geode/data/backup/BackupFormat.kt:117-130; app/src/main/java/dev/geode/data/PerformanceStore.kt:37; app/src/main/java/dev/geode/billing/EntitlementStore.kt:73
- Failure scenario: A cloud or device-transfer restore copies every shared_prefs file. That includes (a) the first-run `perf_*` quality pick, which the KDoc says "the device decides once", so a new low-RAM phone inherits a flagship's HIGH render settings and never re-detects; (b) `gui_safety_acknowledged`, `gui_setup_done` and the tutorial flags, which `BackupFormat.deviceLocalKeys` says "a new phone should show again", including the photosensitivity notice; (c) the `geode-pro` entitlement cache (`pro_owned`, `pro_verified_at`), which vouches for Pro for up to 30 days until Play answers. `ProSnapshot` (the wallpaper and dream) reads only that cache; (d) SAF URIs (`roots`, `background_uri`, `preset_mirror_uri`) whose grants did not survive.
- Evidence: `deviceLocalKeys` lists (b) and (d) as device-local for the manual zip. No `<exclude domain="sharedpref" …>` exists in either rules file.
- Suspected root cause: The device-local key policy was implemented for the manual backup only.
- Suggested fix: Add `<exclude domain="sharedpref" path="geode-pro.xml"/>` to both rule sets. Move the device-local keys (perf_*, gui_* seen flags, URIs) into a separate prefs file and exclude that file too.

### DATA-8 The privacy policy is stale and incomplete about storage and backup
- **Status: open**
- Severity: low
- Location: docs/privacy-policy.md:26, 93-97 (identical copy in app/src/main/assets/privacy_policy.md; same text in docs/privacy-policy.html:45, 82-83)
- Failure scenario: The policy says "a compact analysis cache is kept in the app's private storage". The code no longer writes one and deletes the old one at startup (`GeodeContainer.sweepRetiredAnalysisCache`). The Backups section says only "settings, presets and playlists" go to the Google backup. In fact it also includes listening history (`history.json`: track titles, artists, play counts, listen time), the track library with edited tags, the Pro-owned flag and the trial start time. The policy also never mentions the manual backup zip that Settings › Backup writes to a location the user picks. A policy that lists less than the app does is what reviewers flag.
- Evidence: The backup rules exclude only analysis, crash-latest.txt, milk/generated, milk/textures and takes. All of `shared_prefs` and `history.json` / `library.json` are included.
- Suspected root cause: The policy text predates the sound-engine change and the history and backup features.
- Suggested fix: Drop the analysis-cache sentence. List history, library edits and the Pro/trial flags under Backups, and add a line on the manual backup file.

### DATA-9 Restore staging (up to ~576 MB) leaks in noBackupFilesDir if the process dies between inspect and restore
- **Status: open**
- Severity: low
- Location: app/src/main/java/dev/geode/data/backup/BackupRepository.kt:83-99, 118-123
- Failure scenario: The user picks a large backup. `inspect()` unpacks up to 512 MB into `no_backup/backup-restore/staged` and shows the section chooser. The app is then backgrounded and killed, or crashes, before Restore or Cancel. Nothing cleans `backup-restore` at startup, so the space stays used until the user starts another restore, possibly forever. Settings › Storage shows it as app data the user cannot clear without "Clear data".
- Evidence: `discardStaging()` is called only from `inspect()`, `restore()`'s finally and `discard()`. `BackupRepository` is created lazily and has no init sweep.
- Suspected root cause: There is no startup cleanup for the staging directory.
- Suggested fix: Wipe `staged` (not `aside`, see DATA-3) in the `GeodeContainer` sweeps at startup.

### DATA-10 Editor projects (and the session) from a newer format are quarantined, and the app never reads them again
- **Status: open**
- Severity: low
- Location: app/src/main/java/dev/geode/data/EditorProjectStore.kt:26-34; app/src/main/java/dev/geode/data/EditorProjectJson.kt:48; app/src/main/java/dev/geode/data/SessionStore.kt:26-35, 67-68
- Failure scenario: `EditorProjectJson` throws on `version > VERSION` ("editor project from a newer app"), and `load()` then renames the file to `.json.corrupt`. `names()` lists only `*.json`, so the project disappears for good, including after the user updates to the build that can read it. The realistic path is the manual backup: its format stays version 1 when a store's own schema version rises. A backup made on a newer build therefore restores into an older one, such as a Play staged-rollout rollback or a sideloaded older APK. TemplateRepository deliberately does the opposite ("A file this build cannot parse is SKIPPED, never deleted, quarantined").
- Evidence: `require(o.optInt("version", VERSION) <= VERSION)`, then `onFailure { AtomicWrite.quarantine(file) }`.
- Suspected root cause: A file from a newer version is treated the same as a corrupt one.
- Suggested fix: Skip newer-version files and leave them in place (as TemplateRepository does); quarantine only files that fail to parse.

### DATA-11 The backup rules cite a test that does not exist
- **Status: open**
- Severity: low
- Location: app/src/main/res/xml/data_extraction_rules.xml:4
- Failure scenario: The comment says "BackupRulesCoverageTest pins both, and the difference between them". No file in the repo defines that test, so the include/exclude sets in DATA-6 and DATA-7 can drift with no check.
- Evidence: A repo-wide grep for `BackupRulesCoverage` matches only this XML comment.
- Suspected root cause: The test was planned or removed, and the comment stayed.
- Suggested fix: Add the JVM test (parse both XMLs and assert the exclude sets), or remove the claim.

### DATA-12 A backup export that fails on an IOException leaves a manifest-less zip in the user's chosen location
- **Status: open**
- Severity: low
- Location: app/src/main/java/dev/geode/data/backup/BackupRepository.kt:60-80
- Failure scenario: Export writes to a SAF document. If the write fails with an `IOException` (destination full, Drive provider error), the exception skips the `catch (e: BackupException)` that deletes the partial document. `guarded` turns it into IO_FAILED afterwards, outside that catch, so a truncated `muzviz-backup-YYYYMMDD.zip` stays behind. Later a restore of it fails with "not a backup", and the user may already have deleted their only good copy.
- Evidence: Only `BackupException` triggers `DocumentsContract.deleteDocument`. The `IOException` is caught by `guarded`, which wraps the whole block.
- Suspected root cause: The cleanup catch is narrower than the set of failures that can happen.
- Suggested fix: Catch `Exception` (rethrow after `deleteDocument`) around `pack`.

#### Checked and OK
- Atomic writes: every JSON store (presets, palettes, templates, editor, playlists, smart playlists, library, history, session, takes, textures links) writes through `AtomicWrite` (temp, then fsync, then rename, with a per-path lock). `.tmp`/`.corrupt` files are filtered from listings and from the manual zip. The only plain `writeText` calls are the crash file and the starter-pack marker, both acceptable.
- Corrupt-file handling: per-file stores (presets, palettes, playlists, smart playlists, takes) skip an unparseable file and leave it on disk. `TrackLibrary`, `HistoryStore` and `SessionStore` quarantine a corrupt file before starting fresh; none overwrite it in place. `HistoryStore` refuses to write after a read I/O failure (`readable=false`).
- Manual backup archive: rejects zip-slip, absolute, backslash and NUL paths; canonical-path containment; duplicate entries; entries missing from the manifest; size and SHA-256 per entry; total-bytes and entry-count caps counted on real bytes (bomb-safe); an 8 MB manifest cap; NEWER_VERSION checked first; an allowlist after migration. Prefs values are type-tagged. Device-local keys are stripped. Staging lives in `noBackupFilesDir` (same filesystem, outside Auto Backup). Prefs are written with `commit()`. `geode-pro` is not in any section, so a zip cannot grant Pro.
- Auto Backup: `crash-latest.txt`, the retired `analysis/`, `milk/generated`, `milk/textures` and `takes` are excluded from the cloud backup; device-transfer drops only the crash file.
- FileProvider exposes only `files/presets/`. No `android:process`, so SharedPreferences are single-process.
- Render scratch sweep: the prefixes `geode_aac_`, `geode_loop_` and `studio-` match exactly the three producers (AudioTranscoder, LoopRender, StudioExporter). Only top-level cacheDir files are swept, so it cannot touch `share/` or the widget art. The pending-row sweep's selection is scoped to OWNER_PACKAGE_NAME = this app, the MuzViz paths and a 5-minute grace period, so it can never delete user files (its problem is the opposite, see DATA-1).
- VideoExporter and LoopExtend delete their pending row on cancel and on any exception.
- Manifest: INTERNET and ACCESS_NETWORK_STATE are removed, and `release.yml` fails the build on INTERNET or AD_ID. READ_EXTERNAL_STORAGE has maxSdk 32. The mic is an optional feature. `<queries>` is narrow (media apps only, no QUERY_ALL_PACKAGES). Each FGS type has a notification.
- Notification listener: `GeodeNotificationListener` is empty (no `onNotificationPosted`). `NowPlayingBridge` reads only the MediaSession title, artist and package, held in memory, which matches the policy.
- Mic and playback capture go to the in-memory `PcmSink` only, with no file sink. Crash file: local, read and copied only by user action, deleted on dismiss.
- DATA_SAFETY.md (purchase history only, everything else on-device) agrees with the code and with the skill's independent audit.

## Playback and audio chain

Scope: `app/src/main/java/dev/geode/playback`, `app/src/main/java/dev/geode/audio` (+ `dsp`), `engine/audio-android`, `engine/audio-core`, `core/audio/player`, `app/src/main/cpp/geode_player_jni.cpp`, plus the callers in `ui/` (QueueController, PlayerSession, PlayerSettingsController, CaptureController, PlaybackFades). Media3 1.11.0 behaviour was checked against the sources jar in the Gradle cache, not assumed.

None of the files cited below are on the open PR #183/#184 list.

Many of the findings affect only the opt-in "Native audio engine" (`PlayerPrefs.nativeEngine`, off by default). Each finding says which engine it hits.

---

### PLAY-1 Android Auto / Assistant voice search ("play X on MuzViz") crashes the app
- **Status: open**
Severity: high
Location: app/src/main/java/dev/geode/playback/PlaybackService.kt:206-226 (onSetMediaItems / onAddMediaItems / resolve)
Failure scenario: In the car, the user says "Hey Google, play Daft Punk on MuzViz". Android Auto (a trusted browser, so `COMMAND_SET_MEDIA_ITEM` is granted) calls `playFromSearch`. Media3's legacy stub builds a `MediaItem` with `mediaId = ""`, no `localConfiguration` and `requestMetadata.searchQuery = "Daft Punk"` (MediaSessionLegacyStub.createMediaItemForMediaRequest), then calls `onSetMediaItems`. `queueFor("")` returns null (empty parent), and `resolve()` calls `tree.playable("")`, which returns null for an empty uri, so the original URI-less item is handed back. Media3 passes it to `ExoPlayer.setMediaItems`, and `DefaultMediaSourceFactory.createMediaSource` runs `checkNotNull(mediaItem.localConfiguration)`. The NullPointerException is thrown on the main looper from a Guava callback, so the process crashes. `playFromUri` takes the same path (mediaId "", URI only in requestMetadata).
Evidence: media3-session 1.11 `MediaSessionLegacyStub.onPlayFromSearch` → `handleMediaRequest` → `MediaUtils.setMediaItemsWithStartIndexAndPosition(player, …)`. media3-exoplayer `DefaultMediaSourceFactory.java:551` `checkNotNull(mediaItem.localConfiguration)`. Media3's default `onAddMediaItems` returns `immediateFailedFuture(UnsupportedOperationException)` when `localConfiguration == null` (MediaSession.java:1959); the override drops that guard.
Suspected root cause: `resolve()` falls back to the unresolved item instead of failing, and there is no search handling (`requestMetadata.searchQuery` / `mediaUri`).
Suggested fix: Resolve `requestMetadata.searchQuery` / `mediaUri` against the LibraryTree, and return `Futures.immediateFailedFuture(UnsupportedOperationException())` when any item still has no `localConfiguration`.

### PLAY-2 Native engine: "Play next", removing or moving the next-up track leaves the old track pre-rolled, so the wrong track plays and the UI names another
- **Status: open**
Severity: high (native engine only)
Location: app/src/main/java/dev/geode/playback/NativePlayer.kt:336 (`if (queuedId >= 0 && queuedIndex == index) return`), plus handleAddMediaItems 219-235, handleRemoveMediaItems 237-260, handleMoveMediaItems 262-275
Failure scenario: Queue [A, B]; A is playing and B is pre-rolled (queuedIndex = 1). The user taps "Play next" on X (`QueueOps.insertNextIndex` → index 1), so the queue becomes [A, X, B]. `queueNext()` computes nextIndex = 1, finds `queuedIndex == 1` and returns without re-queueing. The engine still has B in `next_`. When A ends, B plays, and `syncFromEngine` maps B's token to `loads[id] = 1`, so `currentIndex = 1` and the UI, notification and history all say X. X never plays, and B then plays a second time. The same happens on removing the next item ([A,B,C] minus B: the deleted B still plays, labelled C) and on a drag that puts a different track at the next index.
Evidence: The skip check compares a positional index across a list mutation. `loads` records the index at queue time and is never remapped when entries shift.
Suspected root cause: Pre-roll identity is tracked by playlist index, not by entry uid.
Suggested fix: Store the queued entry's `uid` and re-queue whenever `entries[nextIndex()].uid` differs. Remap `loads` by uid instead of index.

### PLAY-3 Native engine: a stale engine token during a track switch snaps the UI back to the old track and freezes error detection
- **Status: open**
Severity: high (native engine only)
Location: app/src/main/java/dev/geode/playback/NativePlayer.kt:277-287 (syncFromEngine), 296-301 (openCurrent)
Failure scenario: Track 2 is playing and the user taps track 5. `openCurrent` sets `loadedId = newId` and `loads[newId] = 5`, but the worker's fd open and the engine's `doOpen` (extractor and codec creation, tens of ms) take a while. If the 200 ms poll runs inside that window, `playerCurrentToken` still returns track 2's token. It differs from `loadedId` and `loads` still holds it (the prune runs after this check), so the code takes it for a gapless join. It sets `loadedId = old`, `currentIndex = 2` and queues track 3. The prune then drops `newId`. Once the engine finishes opening track 5, every poll hits `loads[newId] ?: return` and returns early. While track 5 plays, the UI shows track 2, track 3 plays next instead of 6, and engine errors, duration updates and `dsp.sync()` are skipped. A decode failure in track 5 is never surfaced, so playback goes silent.
Evidence: Tokens come from `nextLoad++` and only grow. The code treats any `playing != loadedId` as a join (line 279). `loads.keys.retainAll` (line 287) runs after the early return.
Suspected root cause: The poll cannot tell an old token from a join into the queued track.
Suggested fix: Accept a token change only when `playing == queuedId` (or `playing > loadedId`), and never `return` before the error, duration and dsp checks.

### PLAY-4 Native engine: with "Gapless playback" off and crossfade off, the queue stops after every track
- **Status: open**
Severity: high (native engine only)
Location: app/src/main/java/dev/geode/playback/NativePlayer.kt:328; core/audio/player/Player.cpp:368-369
Failure scenario: The user enables the native engine and turns off "Gapless playback" (PlaybackSettings shows that switch only under the native engine), leaving crossfade at 0. `queueNext()` then never pre-rolls (`index = -1`). At the end of each track the mixer sets `ended_`, the engine reports `Ended` and `NativePlayer` reports `STATE_ENDED`. Nothing advances to the next item: no `onPlaybackStateChanged` handler in the app advances the queue, and `SimpleBasePlayer` does not auto-advance. A 12-track album stops after track 1. REPEAT_ONE and REPEAT_ALL stop as well.
Evidence: grep shows `STATE_ENDED` is only read by SleepTimer and `playbackWanted`. The engine only joins tracks through `next_`.
Suspected root cause: "Gapless off" was implemented as "no next track" instead of "next track after a gap".
Suggested fix: When the engine reports Ended and `nextIndex() >= 0`, call `openCurrent` on the next index, or always pre-roll and just insert silence when gapless is off.

### PLAY-5 Native engine: no audio focus and no becoming-noisy handling (unplugging headphones keeps playing on the speaker)
- **Status: open**
Severity: high (native engine only)
Location: app/src/main/java/dev/geode/playback/NativePlayer.kt (whole class); app/src/main/java/dev/geode/ui/PlayerSettingsController.kt:59-62; core/audio/player/Player.cpp:117,235-257
Failure scenario: With the native engine on, the user unplugs wired headphones or a Bluetooth headset drops. Oboe reports a disconnect, `reopenOutput()` opens a stream on the speaker, and `updateState()` restarts it because `playWhenReady` is still true, so the music continues out loud. An incoming call, a navigation prompt or another music app gets no pause or duck either, because nothing ever requests audio focus.
Evidence: `setHandleAudioBecomingNoisy` is applied only when `player as? ExoPlayer`. No `AudioFocusRequest`, `requestAudioFocus` or `ACTION_AUDIO_BECOMING_NOISY` code exists anywhere in the app (grep). AAudio/Oboe streams do not take focus by themselves.
Suspected root cause: The ExoPlayer behaviours (focus via `setAudioAttributes(…, true)`, noisy handling) were never ported to `NativePlayer`.
Suggested fix: Have `NativePlayer` request and abandon `AudioFocusRequest`(USAGE_MEDIA) on play and pause, handle loss and duck, and register an `ACTION_AUDIO_BECOMING_NOISY` receiver that pauses when `pauseOnNoisy` is set.

### PLAY-6 Every multichannel (>2 ch) file fails to play on the default ExoPlayer engine
- **Status: open**
Severity: medium
Location: app/src/main/java/dev/geode/audio/dsp/NativeDspProcessor.kt:61
Failure scenario: The user plays a 5.1 FLAC, WAV or AAC file. On API 32+, `MediaCodecAudioRenderer` sets `KEY_MAX_OUTPUT_CHANNEL_COUNT = 99`, so the decoder outputs 6 channels (raw WAV/AIFF/FLAC PCM are multichannel on every API). `NativeDspProcessor.onConfigure` throws `UnhandledAudioFormatException` for `channelCount !in 1..2`. `AudioProcessingPipeline.configure` propagates it, `DefaultAudioSink.configure` turns it into a `ConfigurationException`, and the renderer raises `ERROR_CODE_AUDIO_TRACK_INIT_FAILED`. The track shows "could not be played" and is skipped, even with the equalizer off.
Evidence: DefaultAudioSink.java:776-780 (`catch (UnhandledAudioFormatException e) { throw new ConfigurationException(e, inputFormat); }`); MediaCodecAudioRenderer.java:1152-1153 and 724-726. The processor has no `isActive` opt-out and is always in the chain (`PlaybackEngine.kt:80`).
Suspected root cause: Throwing is the wrong way to decline a format. Returning `AudioFormat.NOT_SET` makes a `BaseAudioProcessor` inactive (bypassed).
Suggested fix: Return `AudioProcessor.AudioFormat.NOT_SET` from `onConfigure` for unsupported channel counts and encodings, so the stage is bypassed instead of killing playback.

### PLAY-7 Native engine: after any playback error, the next (good) tracks are skipped as failures too
- **Status: open**
Severity: medium (native engine only)
Location: app/src/main/java/dev/geode/playback/NativePlayer.kt:192-194, 288-290; core/audio/player/Player.cpp:163-181, 352-366; app/src/main/java/dev/geode/ui/PlayerSession.kt:1196-1215
Failure scenario: A corrupt track fails, so the engine sets `state_ = Error` and the Kotlin poll raises the error. `PlayerSession.onPlayerError` then calls `seekToNextMediaItem()`, and `handleSeek` clears `error` and starts `openCurrent` on the worker. The engine stays in `Error` until `doOpen` completes the fd open, extractor and codec start (often 20-100 ms) and stores `Buffering`. If the next 200 ms poll lands in that window, it sees `ENGINE_ERROR` again and re-raises an error for the healthy track. `consecutivePlaybackFailures` reaches 2, the healthy track is skipped, and after three failures playback stops with "Several tracks in a row are unavailable". The same happens when the user picks a track by hand after an error.
Evidence: `syncFromEngine` raises an error whenever `playerState == ENGINE_ERROR && error == null`, and `handleSeek` sets `error = null` before the engine has moved on. In Player.cpp the Error state survives until `doOpen` stores Buffering.
Suspected root cause: Engine error state is not tied to a load token.
Suggested fix: Have `openCurrent` record the pending load id, and raise an engine error only when `playerCurrentToken == loadedId`, or reset the engine state to Buffering synchronously in `Player::open`.

### PLAY-8 Native engine: shuffle uses two different orders (random for auto-advance, sequential for Next, Previous and the queue screen)
- **Status: open**
Severity: medium (native engine only)
Location: app/src/main/java/dev/geode/playback/NativePlayer.kt:117-126 (getState publishes no shuffle order), 350-365 (private `shuffleOrder`), 231 (`reshuffle()` on every add)
Failure scenario: Shuffle is on and the queue is A–J, with A playing. The engine pre-rolls a random next track (G) from the private `shuffleOrder` and plays it at the end of A. When the user then presses Next, `SimpleBasePlayer`'s timeline (base `Timeline`, which ignores shuffle) gives `currentIndex + 1`, which is H, in sequence. `QueueController.playOrder()` reads the same sequential timeline, so the "Up next" list shows the unshuffled order while the engine plays something else. Every `enqueue` or `playNext` also calls `reshuffle()`, so the random next track changes under the user.
Evidence: `SimpleBasePlayer` 1.11 has no shuffle-order support in `State.Builder` (only `setShuffleModeEnabled`), and its playlist timeline delegates `getNextWindowIndex` to `Timeline`'s sequential default.
Suspected root cause: The shuffle order lives only inside NativePlayer. It should drive the published timeline, or seekToNext/Previous should be overridden.
Suggested fix: Override `handleSeek` for `COMMAND_SEEK_TO_NEXT*`/`PREVIOUS*` to follow `shuffleOrder`, expose that order to the queue UI, and keep it stable across inserts (insert new entries instead of reshuffling).

### PLAY-9 Native engine: title resolution restarts the current track from 0:00 and double-records history
- **Status: open**
Severity: medium (native engine only)
Location: app/src/main/java/dev/geode/ui/QueueController.kt:230-234 (`player.replaceMediaItem`); app/src/main/java/dev/geode/playback/NativePlayer.kt:237-260
Failure scenario: The user opens files whose titles are unknown (a share or file intent, or a folder of untitled files), which goes through `open()`/`playFrom()` → `resolveTitlesAsync`. When the DISPLAY_NAME queries return, `replaceMediaItem(currentIndex, …)` runs. `NativePlayer` does not override `handleReplaceMediaItems`, so `SimpleBasePlayer`'s default calls `handleAddMediaItems(index+1)` and then `handleRemoveMediaItems(index, index+1)`. That removes the current entry, sets `loadedId = -1` and calls `openCurrent(0L)`, so the playing track jumps back to 0:00, possibly several seconds in for a big folder. The new uid also fires `EVENT_MEDIA_ITEM_TRANSITION`, so `PlayerSession` records a second play in history and reruns `onTrackChanged`.
Evidence: SimpleBasePlayer.java:3583-3591 (default `handleReplaceMediaItems` = add then remove). ExoPlayer updates in place, so only the native engine is hit.
Suspected root cause: `handleReplaceMediaItems` is missing for a metadata-only replace.
Suggested fix: Override `handleReplaceMediaItems` in `NativePlayer`. When the URIs match, swap the `Entry.item` and keep its uid, without touching the engine.

### PLAY-10 A cold service-only session ignores playback prefs (no pause on unplug, no ReplayGain, no speed, skip-silence or crossfade)
- **Status: open**
Severity: medium
Location: app/src/main/java/dev/geode/playback/PlaybackEngine.kt:75-100; app/src/main/java/dev/geode/ui/PlayerSettingsController.kt:57-67
Failure scenario: After process death, the user resumes playback from the System UI media carousel, presses play on a Bluetooth headset (media-button resumption), or starts from Android Auto without opening the app. `PlaybackService.onCreate` builds the `PlaybackSession`. The ExoPlayer comes from `ExoPlayer.Builder`, whose default `handleAudioBecomingNoisy` is false, and `applyPlaybackPrefs` (noisy handling, `ReplayGain.configure`, speed and pitch, skip-silence, native crossfade and gapless) runs only from `PlayerSession.attachPlayback`, which needs the activity. Unplugging wired headphones then keeps playing on the speaker, although "Pause when unplugged" defaults to on. ReplayGain stays `MODE_OFF`, so levels jump between tracks, and the user's speed, pitch and skip-silence settings are ignored until the app is opened.
Evidence: `setHandleAudioBecomingNoisy` and `replayGain.configure` are called only in PlayerSettingsController (grep). The services don't inject and construct nothing from `PlayerPrefs` (AGENTS.md › DI).
Suspected root cause: Prefs are applied in the UI layer, not where the player is built.
Suggested fix: Load `PlayerPrefs` in the `PlaybackSession` constructor and apply noisy handling, ReplayGain, parameters and native prefs there. Keep `applyPlaybackPrefs` only for later changes.

### PLAY-11 The ReplayGain change for the next track lands late (the first 0.25 to 0.75 s or more plays at the previous track's gain)
- **Status: open**
Severity: medium
Location: app/src/main/java/dev/geode/playback/ReplayGain.kt:44-63
Failure scenario: Track A is tagged +6 dB and track B -8 dB (peak 1.0). With ExoPlayer, the DSP stage processes PCM ahead of playout by the AudioTrack buffer (DefaultAudioTrackBufferSizeProvider: 250-750 ms for PCM). `onMediaItemTransition` fires only once B is audible, and only then does the tag read start on IO (TagLib), before `setGainDb` runs. So B's opening plays about 14 dB too hot (limiter-squashed) and then audibly drops. On the native engine the delay is the 200 ms poll plus the tag read, and with crossfade on, B's gain arrives only after the whole fade.
Evidence: The gain is pushed from a main-thread transition callback into a stage that runs on already-buffered audio. Tags are not read ahead for the queued or next item.
Suspected root cause: The gain is keyed to playout transition time, not to when the next item's audio enters the chain.
Suggested fix: Read the next item's tags ahead of time and switch the gain at the stream boundary in the processor (ExoPlayer `onFlush`/format boundary, native per-deck gain), not on `onMediaItemTransition`.

### PLAY-12 With shuffle on, "Play next" and queue drag-reorder land at random positions (ExoPlayer)
- **Status: open**
Severity: medium
Location: app/src/main/java/dev/geode/ui/QueueController.kt:150-154 (playNext), 326-336 (moveQueueItem)
Failure scenario: With shuffle on, the user taps "Play next" on a track. `addMediaItem(currentIndex + 1, …)` inserts it at timeline index currentIndex+1, but ExoPlayer's `DefaultShuffleOrder.cloneAndInsert` puts it at `random.nextInt(len + 1)` in the play order, so it plays at a random later point (the queue UI, built from `playOrder()`, shows it there too). Dragging an item in the shuffled queue maps the play-order positions to timeline indices and calls `moveMediaItem`. `DefaultShuffleOrder` inherits `cloneAndMove` = `return this` (ShuffleOrder.java:283), so the permutation stays the same over a reordered timeline, and the item never ends up where it was dropped.
Evidence: media3-exoplayer `ShuffleOrder.java:113-118` and `:283-285`; `ExoPlayerImpl.moveMediaItems:767`.
Suspected root cause: Queue edits are expressed in timeline indices while the user sees and edits the shuffle order.
Suggested fix: Supply a custom `ShuffleOrder` via `setShuffleOrder` (insert right after the current play position, honour moves), or rebuild the timeline in play order when shuffled.

### PLAY-13 `ensureRunning()`'s app-side startForegroundService can crash with ForegroundServiceDidNotStartInTimeException when playback stops quickly
- **Status: open**
Severity: medium
Location: app/src/main/java/dev/geode/playback/PlaybackService.kt:338-348; app/src/main/java/dev/geode/ui/PlayerSession.kt:1221-1227
Failure scenario: On Android 12+, the service is not running, or is running but out of the foreground after Media3's paused timeout. The user taps play: `onIsPlayingChanged(true)` → `ContextCompat.startForegroundService` puts the app under the 5-10 s startForeground obligation. Media3 calls `startForeground` only from its async notification update, and only if `shouldRunInForeground()` is still true at that moment (playWhenReady and READY/BUFFERING). If the user pauses right away (cold service: the internal MediaController connects asynchronously first), or the track hits a fatal error and `PlaybackErrors` pauses, Media3 runs `notify` + `stopForeground` instead. `startForeground` is never called and the system kills the app with ForegroundServiceDidNotStartInTimeException.
Evidence: `MediaSessionService.onStartCommand` (1.11, lines 520-585) does nothing for a plain start intent with no media action or `SELF_INTENT_UID` extra. `MediaNotificationManager.updateNotificationInternal` calls `startForeground` only when `startInForegroundRequired`. `runCatching` only catches synchronous `ForegroundServiceStartNotAllowedException`, not the later timeout.
Suspected root cause: The app takes on the FGS obligation itself instead of letting Media3, which calls `startForegroundService` together with an immediate `startForeground`, own it.
Suggested fix: Start the service with `startService` (allowed while the activity is in front) or bind to it. Let Media3's `MediaNotificationManager` do the foreground promotion; it already starts itself in the foreground once a session plays.

### PLAY-14 The Android Auto browse tree breaks on album, artist or playlist names containing "|"
- **Status: open**
Severity: low
Location: app/src/main/java/dev/geode/playback/LibraryTree.kt:68, 76, 153
Failure scenario: The album "Live | Studio" holds track uri U, so its playable id is `albums/Live | Studio|U`. When it is tapped in Android Auto, `queueFor` takes `substringBefore("|")`, giving `albums/Live `, a folder with no rows, so it returns null. `playable()` takes `substringAfter("|")`, giving ` Studio|U`, which is not a URI. The resulting item fails with "could not be played" and its siblings are not queued.
Evidence: SEPARATOR "|" is not escaped, and names are user/tag data.
Suspected root cause: An ambiguous delimiter in mediaId.
Suggested fix: Split on the last "|" (`substringBeforeLast`/`substringAfterLast`; content URIs carry no "|"), or URL-encode the names in folder ids.

---

#### Checked and OK

- **Chain order and float output:** `MvzAudioProcessorChain` is tap → SilenceSkipping → Sonic → DSP. `applyPlaybackParameters`/`applySkipSilenceEnabled` are wired, and `getMediaDuration`/`getSkippedOutputFrameCount` are delegated. `TapRenderersFactory` passes the factory's `enableFloatOutput` through, and nothing in the app enables it. DefaultAudioSink adds `ToInt16Pcm` ahead of the chain, so the tap sees 16-bit (24/32-bit sources included).
- **PcmTap and the rings:** `PcmTap` handles 16-bit and float, writes in 4096-frame chunks (under SampleRing's 16384 `maxWriteFrames`), and on channel or format change at flush, re-sizes staging and starts a new ring epoch. `SampleRing` takes the first two channels of 5.1. Mono is centred by `SoundCursor`. Readers tolerate a concurrent writer (single volatile publish of `written`, stall clamp to `oldestAvailable`).
- **Presentation clock:** `AudioPresentationClock` is bounded to 64 segments. The clock currently has no consumer, so `SinkClockDriver` distrust paths have no user-visible effect.
- **PlaybackEngine holds:** UI and service counts are balanced, and `PlaybackService.onDestroy` releases the service hold. The session survives activity teardown while the service holds it, and a UI re-acquire before the service dies is safe.
- **Oboe callback real-time safety:** `Mixer::render` and the DSP chain take no locks and do no allocation (SPSC rings, atomics). The DSP swap is deferred to `reclaimDsp` on the engine thread. `postCommand` spin is bounded.
- **Oboe disconnect:** handled outside the error callback (`disconnected_` flag, `reopenOutput` on the engine thread). Rate changes rebuild the decks at the saved position.
- **Teardown:** `NativeTapPump.stop()` joins before `playerDestroy`. The worker is shut down and `released` is checked in its bodies.
- **Decoder:** the native `Decoder` handles format change (rate, channels, float vs int16) and seek pre-roll skipping. fds are closed on every failure path.
- **PlaybackCaptureService:** declares the `mediaProjection` FGS type and calls `startForeground` with the type on 14+ before `getMediaProjection`. The stop action, projection `onStop`, and `onDestroy` all publish null and stop capture.
- **AudioCapturePump:** the generation counter prevents a late-unblocked read from writing into a later run, and the worker always releases its AudioRecord. `CaptureController.shutdown` stops both pumps.
- **Notifications:** the media notification is exempt from POST_NOTIFICATIONS. `ensureRunning` swallows `ForegroundServiceStartNotAllowedException` (PLAY-13 covers the remaining timeout case).
- **SleepTimer:** restores volume in `finally`. finish-track waits on transition, auto-discontinuity, end, idle or pause. Its scope is cancelled on session release (not persisted across process death, but playback dies with the process too).
- **ReplayGain maths:** clip guard (`min(gain + preamp, -20·log10(peak))`). The gain stage runs regardless of the EQ enable flag, and the limiter is on by default. (The timing problem is PLAY-11.)
- **Android Auto paging and access:** `onGetChildren` paging handles `Integer.MAX_VALUE` page sizes. Untrusted controllers get transport only and no timeline. Resumption builds valid items with clamped indices (`SessionStore`).
- **QueueController:** `next`/`previous` wrap and restart thresholds are correct. remove, move and play-index map through play order correctly when shuffle is off. Title resolution checks the generation counter.
- **ExoPlayer setup:** audio focus is set via `setAudioAttributes(…, true)`. WAKE_LOCK is present in the merged manifest (via WorkManager/Glance). All ExoPlayer access found is on Main.
- **Studio clip preview:** the ExoPlayer is muted, does not take audio focus, and is released on dispose.

## Sound engine, GL render and JNI

Scope: engine/scenes GL adapter (VisualizerView, VisualizerRenderer, NativeViz, MilkPresetMemory, StyleReactionsCache, OffscreenSceneRenderer, TrackAudio), core/viz Renderer + scenes, core/sound, core/api, app/src/main/cpp/*_jni.cpp, projectM integration, thread rules.
Tags: [PR183/184] = file is in the open-PR list (the list does not say which PR).

### REN-1 Background image, custom GLSL shader and fluid injection shaders vanish after any tab switch (native renderer rebuilt, state never re-sent)
- **Status: open**
Severity: high
Location: engine/scenes/src/main/kotlin/dev/geode/render/VisualizerView.kt:87-117; app/src/main/java/dev/geode/ui/EnginePlumbing.kt:73-84 and :125-134; app/src/main/java/dev/geode/ui/BackgroundController.kt:95-104; app/src/main/java/dev/geode/ui/VisualsHub.kt:1288
Failure scenario: In Visuals, pick a background image (or Apply a GLSL shader in the GLSL tab, or apply fluid injection shaders). Switch to Library or Settings, then back to Visuals (or Player-expanded -> Library -> Player-expanded). The background image is gone, the scene is back on its built-in shader and the fluid styles on the built-in splat. Saving a template afterwards (TemplatesSheet.kt:121 reads `customShaderFor`) also saves no shader.
Evidence: `onDetachedFromWindow` -> `releaseScenesOnGlThread` -> `VisualizerRenderer.releaseScenes()` = `nativeViz.destroy()`; the reattach gets a fresh `geode_viz` from `onSurfaceCreated` -> `nativeViz.surfaceCreated()` -> `create()`. Only the MilkDrop preset is re-applied (`onCreated`), and the `native*` identity caches are reset so params/LFO/ADSR/reactions/quality/tuning are re-sent. The underlay is pushed only from `LaunchedEffect(background)` at AppRoot level, which does not re-run, and the only re-push trigger, `onSurfaceSizeChanged` -> `BackgroundController.setRenderSize`, returns early when the size is unchanged (`if (width == renderWidth && height == renderHeight) return`). The custom shader and the fluid injection shaders exist only as one-shot calls (`vizApply` SharedFlow -> `submitShader`; FluidTab -> `submitFluidInjectionShaders`). Kotlin keeps no copy to re-send. The native `customShaders_` / `fluidForceSrc_` / `underlayPixels_` that would survive a context loss die with the handle.
Suspected root cause: The per-detach handle destroy (added for the projectM leak) was paired with a restore path for the MilkDrop preset only. The rest of the native-only state was missed.
Suggested fix: Keep the last underlay, custom shaders and fluid injection sources in `VisualizerRenderer` (Kotlin) and re-send them in `NativeViz.onCreated` / `onSurfaceCreated`, as `milkPreset.restorePath` is.

### REN-2 A preset or template with a custom shader for a style not yet drawn loses its shader silently
- **Status: owned elsewhere: core/viz files in PR #183, sent to its session**
Severity: medium
Location: core/viz/RendererFrame.cpp:51-62 [PR183/184]; core/viz/Renderer.cpp:103-106, 474-509 [PR183/184]; app/src/main/java/dev/geode/ui/VisualSettingsController.kt:96-114
Failure scenario: The app is on style A. The user applies a saved preset, a template or a `geode://preset` link whose look is style B with a `customShader`. It is worse on a cold start or after REN-1, when no scene is built at all. B draws its built-in shader, no error is shown, and the GLSL tab shows an empty source.
Evidence: `applyPreset` switches `sceneId` to B and emits `VizApply(customShader, sceneId = B)`. The collector calls `Renderer::setCustomShader`, which only queues `pendingShaders_`. At the next frame, `beginFrame` swaps the queue out and does `if (Scene* scene = builtScene(id)) scene->setFragmentSource(src);`, so an unbuilt B drops the entry. `resolveActiveScene()` runs after `beginFrame` and only then builds B. `buildScene` seeds only from `customShaderFor(id)` (`customShaders_`), which is filled only after a successful compile (`rememberCustomShader`), so it is empty.
Suspected root cause: Pending user shaders are applied only to scenes that already exist, and they are not retained for scenes built later.
Suggested fix: When `builtScene(id)` is null, keep the pending source, for example in a map that `buildScene` consults ahead of `customShaderFor`, instead of dropping it.

### REN-3 MilkDrop exports animate on wall-clock time, not video time (wrong speed in exports and loop renders)
- **Status: open**
Severity: medium
Location: core/viz/scenes/MilkdropScene.cpp:143, 271; third_party/projectm/src/libprojectM/TimeKeeper.cpp:19 (v4.1.7); engine/scenes/src/main/kotlin/dev/geode/render/offscreen/OffscreenSceneRenderer.kt:113-133
Failure scenario: Export or loop-render the milkdrop style. The encoder pulls frames slower than real time (4K or 60 fps on a mid device) or faster (a short 720p clip on a fast GPU). Preset motion in the video is sped up or slowed relative to the audio, and every render of the same range differs. A loop render of a MilkDrop look does not loop seamlessly.
Evidence: Every other scene is stepped by the export's `dt` (from `timeMs / 1000.0` -> `Renderer::beginFrame`). projectM 4.1.7's `TimeKeeper::UpdateTimers()` uses `std::chrono::high_resolution_clock::now()` for `ctx.time`, preset time and soft-cut progress, and 4.1.7 has no `projectm_set_frame_time`. `MilkdropScene` only calls `projectm_set_fps(60)`. Nothing feeds video time to the engine, and the backport patch only adds `render_frame_fbo`.
Suspected root cause: projectM's internal clock is not driven by the offscreen frame time.
Suggested fix: Patch TimeKeeper (as the FBO backport is patched) to take a host frame time, and pass the renderer's frame time from `MilkdropScene::draw`.

### REN-4 `dspProcess` JNI has no bounds check, and NativeDspProcessor swaps to the new-format chain before old-format audio has drained (possible native heap overflow)
- **Status: open**
Severity: medium
Location: app/src/main/cpp/geode_jni.cpp:85-90; app/src/main/java/dev/geode/audio/dsp/NativeDspProcessor.kt:57-78, 80-102
Failure scenario: Playback speed is not 1.0 (Sonic active) or silence skipping is on, and the queue moves from a mono track to a stereo track. Media3 calls `configure()` on the shared processors (`onConfigure` -> `ensureChain` makes `handle` a 2-channel chain) while the old pipeline drains: Sonic's buffered mono tail still reaches `NativeDspProcessor.queueInput`. There `inputAudioFormat` is still the active mono format (BaseAudioProcessor only promotes the pending format in `flush()`), so `frames = samples / 1`. The native chain then processes `frames * 2` floats out of a direct buffer sized for `samples` floats, reading and writing past the end of `scratch`.
Evidence: The JNI function checks only `GetDirectBufferAddress != null && frames > 0` and never compares `frames * channels * 4` with `GetDirectBufferCapacity(buffer)`. `geode_dsp_process` uses the chain's own channel count. `ensureChain` runs in `onConfigure`, not in `onFlush`.
Suspected root cause: The chain swap happens at configure time instead of flush time, and the JNI does not enforce capacity.
Suggested fix: Build the new chain in `onConfigure` but install it in `onFlush`, and clamp `frames` in the JNI to `GetDirectBufferCapacity / (4 * geode_dsp_channels)`.

### REN-5 Exports ignore the on-screen custom shader, fluid injection shaders and the Layer
- **Status: open**
Severity: medium
Location: engine/scenes/src/main/kotlin/dev/geode/render/offscreen/OffscreenSceneRenderer.kt:81-105; engine/scenes/src/main/kotlin/dev/geode/render/SceneFactory.kt:9-14; engine/scenes/src/main/kotlin/dev/geode/render/VisualizerRenderer.kt:311-319
Failure scenario: Apply a GLSL shader to a shader style (or injection shaders to a fluid style, or enable a Layer), then render from Studio › Render. The video shows the built-in style with no layer, unlike the screen. The KDoc claims "export equal live".
Evidence: `SceneFactory` carries only `sceneId` and `milkPresetPath`. `prepare()` never calls `setCustomShader`, `setFluidInjectionShaders` or `setLayer`, and no export or ExportHost code references a custom shader, fluid injection or layer state (grep finds nothing).
Suspected root cause: The export snapshot (`exportSceneFactory`) was never extended beyond the MilkDrop preset.
Suggested fix: Add the custom fragment source, the injection sources and the layer settings to `SceneFactory` / `OffscreenRenderSpec`, and apply them in `prepare()`.

### REN-6 NativeViz handle used from Dispatchers.Default with no guard against the GL-thread destroy (use-after-free window)
- **Status: open**
Severity: low
Location: app/src/main/java/dev/geode/ui/ExportHost.kt:198-203; app/src/main/java/dev/geode/ui/ExportController.kt:157,184-185; engine/scenes/src/main/kotlin/dev/geode/render/bridge/NativeViz.kt:67-69, 47-65
Failure scenario: An export with a performance take calls `sceneFactoryFor` on `Dispatchers.Default`, which calls `renderer.availableSceneIds()` -> `nativeViz.create()` + `GeodeNative.vizSceneIds(handle)`. If at that moment the live view detaches (the user collapses the expanded fullscreen player, or the activity tears down), the GL thread runs `vizDestroy(handle)`. The background thread already read `handle != 0L` and calls into the freed `geode_viz`, so the native process crashes.
Evidence: `create()` / `destroy()` are `@Synchronized`, but the `if (handle != 0L) GeodeNative.x(handle, ...)` calls are not. They read the field twice with no lock, and the "Main is blocked during detach" reasoning does not cover a Default-dispatcher caller.
Suspected root cause: Catalogue queries go through a GL-surface-bound handle, and they run off Main and the GL thread.
Suggested fix: Answer `availableSceneIds()` from the static catalogue (`GeodeNative.catalogueText` / `Catalogue`) and never create or read the live handle from export threads.

### REN-7 Native renderer (plus its ProgramBinaryCache writer thread) leaked when the handle is created while the view is detached
- **Status: open**
Severity: low
Location: app/src/main/java/dev/geode/ui/EnginePlumbing.kt:62; app/src/main/java/dev/geode/ui/AppShell.kt:103; engine/scenes/src/main/kotlin/dev/geode/render/VisualizerRenderer.kt:141-150, 159-183
Failure scenario: A user who only uses the music player (Player tab, never Visuals or expanded) opens and closes the app repeatedly while the playback service keeps the process alive. On each Activity instance, AppRoot's `LaunchedEffect(Unit)` calls `availableSceneIds()`, which calls `nativeViz.create()` on Main and builds a `geode::viz::Renderer` with its own `std::thread` writer. The `VisualizerView` is never attached, so it is never detached, and nothing ever destroys the handle. The same happens after Studio › Render (`availableSceneIds`) or a preset apply (`loadMilkPreset`/`submitShader` -> `create()`) when the user backs out without reopening Visuals. Each cycle leaks one native renderer, one native thread and the never-started GLSurfaceView GLThread.
Evidence: The only `vizDestroy` path for the live renderer is `VisualizerView.onDetachedFromWindow`. NativeViz has no Cleaner or finalizer, and AppRoot's `onDispose` only calls `visualizerView.onPause()`.
Suspected root cause: Lazy `create()` from non-GL call sites, with destroy tied to window detach.
Suggested fix: Do not create the handle for catalogue queries (see REN-6), and destroy any handle from AppRoot's `onDispose` (queueEvent or direct, since no context is needed for the native heap).

### REN-8 Program-binary crash sentinel is shared by concurrent renderers: a live renderer's in-flight `glProgramBinary` can permanently disable the cache
- **Status: open**
Severity: low
Location: core/viz/ProgramBinaryCache.cpp:177-185, 215-222
Failure scenario: The process runs several `Renderer`s with separate `ProgramBinaryCache` instances on one directory: live view, export, wallpaper and dream. Every live re-attach builds a fresh one (REN-1 path), so it arms the sentinel again on its first load. If an export or batch-track renderer reaches `openStore()` while another renderer is inside its armed `glProgramBinary`, it sees the `loading` file and logs "previous run died". It then `remove_all`s the namespace and writes `disabled`. The cache stays off for this driver forever, which means slower shader warm-up and first-frame hitches on every launch afterwards. The other renderer's `std::remove(sentinel_)` can also delete a sentinel that a concurrent renderer armed, which hides a real crash.
Evidence: The sentinel path is per-namespace (per driver), not per instance or process run. `sentinelSpent_` is per instance, and no cross-instance lock exists.
Suspected root cause: The crash guard assumes one cache user per process.
Suggested fix: Make the cache a process-wide singleton (one writer thread, one sentinel), or name the sentinel per process start and only treat sentinels from an earlier PID or boot as crashes.

#### Checked and OK
- JNI symbol coverage: all 100 `external fun` in `GeodeNative` have a matching `Java_dev_geode_engine_bridge_GeodeNative_*` JNIEXPORT, with no extras and no RegisterNatives (script diff). `NativeViz` has no `external`s; it wraps GeodeNative. Parameter types match the Kotlin signatures for every viz, player, tags, dsp and export function I read.
- GetStringUTFChars and Get*ArrayElements: all go through the RAII `Utf8Chars`/`FloatElements` (null-safe, JNI_ABORT), or are released on every path (`vizSetOverlay`/`vizSetUnderlay`). Region copies are clamped to array length (`vizWriteAudio`, `vizPushPcm`, `vizSetTouch`, `loudnessFeed`, `resamplerProcess` in 64-bit maths, `paramsRandomize`/`paramsResetTab`, `macros*` capped at 16). `tagsRead`/`tagsWrite` delete local refs in their loops. Struct-cast copies are guarded by static_asserts (`GeodeSoundFrame`/`GeodeSoundTuning` sizes).
- Null and zero handles: every `geode_viz_*` / `geode_sound_*` / `geode_dsp_*` C entry checks for null. `NativeViz` short-circuits on 0. `vizTakeMilkPresetLoaded` is a single call (take-once is safe). `geode_style_reactions` respects capacity.
- `onSurfaceChanged(0,0)`: clamped to 1 in `geode_viz_surface_changed` and `Renderer::surfaceChanged`. MilkDrop skips engine creation at ≤1px. Zero or negative transition duration is clamped (no NaN progress). `setUserQuality` rejects NaN. A NaN thermal headroom maps to the Full tier.
- Shader compile failure: `ShaderScene::compilePendingIfAny` keeps the previous program on failure and reports it. A user shader that fails does not black the scene. MilkDrop falls back to the last good preset, and a projectM create failure retries on a timer.
- Thread rules: Renderer setters lock `stateLock_` and the frame reads snapshots. LFO, ADSR and reaction configs are only set from the frame's own thread (`syncNativeState`, the offscreen thread). Kotlin cross-thread fields are `@Volatile` with immutable values. `LfoEngine`/`AdsrEngine.configs` are volatile. The underlay goes through `queueEvent`. Main is blocked in `onDetachedFromWindow` while the GL thread destroys the handle, so Main-thread NativeViz calls cannot race the destroy (REN-6 is the off-Main exception).
- Surface recreation on the same handle (wallpaper `onPause` drops the context): `Renderer::onSurfaceCreated` releases scenes, FBOs, palette LUT, VAO and composite textures, then rebuilds them in an order that does not delete freshly generated names in my reading. Overlay and underlay are re-armed from retained pixels, and MilkDrop re-arms its last preset.
- core/sound: FFT is radix-2 with size asserted. Ring indexing, window and waveform offsets fit at all rates (n ≥ 512; 192 × n/512 ≤ n). Bin ranges are clamped to [1, n/2+1] and neighbour reads stay in range. Biquads and envelopes flush denormals. Relative levels guard their divide (kFloor). Trigger thresholds are clamped (≥0.1) and decays clamped (≥0.01). A sample-rate change rebuilds the engine and the tuning survives.
- SoundCursor/SampleRing: a single reader, a write-count snapshot and epoch restarts. A stall longer than a take skips forward and never reads overwritten slots.
- TrackAudio: pending chunks are compacted before the sink reads from index 0, and leading samples before the seek are dropped correctly. Float and PCM16 decoder output are handled.
- StyleReactionsCache, BuildStyleReactionsUseCase and ModConfigCodec: identity caching on the GL thread. Unknown enum ordinals are dropped. Native clamps amounts and times, and LFO rate and ADSR times are clamped natively.
- MilkPresetMemory: the restore runs through `onCreated` on every new handle. `firstRun` is synchronized across the GL threads of the app, wallpaper and dream.
- Doc note (not a defect): AGENTS.md still lists `AnalysisEngine`, `OfflineAnalyzer`, `FeatureTimeline`, `AnalysisCache` and `core/analysis`, but none of them exist any more. Live and export analysis is now the per-renderer `core/sound` engine.

## Export, loop render and Studio

Scope: `app/src/main/java/dev/geode/export/*`, `engine/scenes/.../render/offscreen/*`, `ui/ExportController.kt`, `ui/StudioRender.kt`, `ui/ExportHost.kt`, `ui/RenderBar.kt`, `ui/StudioScreen.kt`, `ui/studio/*`, `editor/*`, plus the pending-row sweep in `GeodeContainer.kt`. None of the files below are in the open-PR list (#183/#184), so no finding carries a PR tag.

### EXP-1 Cancel does nothing after the UI comes back to a render that is still running
- **Status: in progress: cancel fixed (Export: cancel through ExportRun); showing an outlived render's result is with the export fix**
- Severity: high
- Location: app/src/main/java/dev/geode/ui/ExportController.kt:123-134 (init mirror), :356-358 (`cancelExport`), :632-634 (`cancelLoopRender`); the job lambdas that read the flags are at :214, :332, :561, :613. The session gets rebuilt here: app/src/main/java/dev/geode/di/PlayerSessionProvider.kt:89-93
- Failure scenario: The user starts a long render (4K, a batch, or a loop) and leaves the app with Back. `PlayerViewModel.onCleared` calls `PlayerSessionProvider.shutdown()`, which drops the session. The job keeps running in `ExportRun.scope` under the foreground service. The user then taps the notification. A new `PlayerSession` builds a new `ExportController`, and its `init` mirrors `ExportRun.state` into `_exportState`, so the Render bar shows progress and a Cancel button. Pressing Cancel calls the new controller's `cancelExport()`, which sets that instance's `exportCancelled`. The running job reads `exportCancelled || ExportRun.cancelRequested` from the old instance, so it never sees the flag. The render cannot be stopped short of a force-stop. The notification has no cancel action. When the job ends, the mirror resets to `ExportUiState()`, so the saved file or the failure is never shown. If the orphaned job is a loop render, the Render bar shows it as a normal export, and the loop sheet's `_loopState` reads Idle, so Start on that sheet is silently ignored because `ExportRun.running` is true.
- Evidence: `exportCancelled` and `loopCancelled` are `@Volatile private var`s on each controller. Only `ExportService.onTimeout` ever calls `ExportRun.requestCancel()`. The init block runs `takeWhile { it.running }` and then sets `_exportState.value = ExportUiState()`.
- Suspected root cause: The cancel state lives on the per-session controller, but the job lives in the process-wide `ExportRun.scope`.
- Suggested fix: Make `cancelExport`/`cancelLoopRender` call `ExportRun.requestCancel()`, which every job's `isCancelled` already reads. Keep the last result in `ExportRun` so a new controller can show it.

### EXP-2 Every Studio export's result (Saved, or the error) is wiped right away
- **Status: fixed (Export: cancel through ExportRun, keep Studio results on screen)**
- Severity: high
- Location: app/src/main/java/dev/geode/ui/ExportController.kt:440-441 and :466-467 (`refreshStudioClips()` straight after the result is set); :364-373 (`refreshStudioClips` writes `Loading`, then `Idle`)
- Failure scenario: The user exports a clip (Studio › Edit clip editor) or the whole project (timeline). When the export ends, `_studio.phase` is set to `Done(uri)` or `Failed(msg)`. `refreshStudioClips()` then sets `phase = Loading` at once (`Main.immediate`, so it happens before the next recomposition) and `Idle` after the IO list. The UI branches only on `Done`/`Failed`: `StudioScreen.kt:687` `ClipEditorDone` (Open/Share) and `TimelineLanes.kt:530-531`. Neither branch is ever drawn. A successful export shows no confirmation and no Open button. A failed one shows no message: the screen just returns to Idle.
- Evidence: `_studio.update { it.copy(phase = result.toPhase()) }` is followed by `refreshStudioClips()`, which runs `_studio.update { it.copy(phase = ExportPhase.Loading) }` and then `_studio.update { it.copy(clips = clips, phase = ExportPhase.Idle) }`.
- Suspected root cause: The clip-list refresh shares the `phase` field with the export result.
- Suggested fix: Have `refreshStudioClips` update only `clips`, or give the list its own loading flag, so the result phase survives the refresh.

### EXP-3 The orphaned-pending-render sweep can never match a row
- **Status: fixed (Startup sweep: ask MediaStore for pending rows)**
- Severity: medium
- Location: app/src/main/java/dev/geode/GeodeContainer.kt:105-139 (query at :134)
- Failure scenario: The process dies mid-render (LMK, crash, force-stop) after `VideoExporter` or `LoopExtend` has inserted an `IS_PENDING=1` row and streamed hundreds of MB to GB into it. On the next launch, `sweepOrphanedPendingExports` queries with `IS_PENDING = 1` in the selection but without `QUERY_ARG_MATCH_PENDING`/`MATCH_INCLUDE` (API 30+) or `MediaStore.setIncludePending` (API 29). MediaStore drops pending items from queries by default, for the owner too. The cursor is therefore always empty and nothing is deleted. The partial file stays invisible on disk until MediaProvider's own `DATE_EXPIRES` cleanup, about 7 days later. The KDoc claims the opposite.
- Evidence: `resolver.query(collection, arrayOf(_ID), selection, args, null)` takes no extras Bundle. `StudioClips.kt:252/257` in the same app does pass `QUERY_ARG_MATCH_PENDING = MATCH_INCLUDE` / `setIncludePending`, which shows the requirement is known.
- Suspected root cause: The filter flag for pending rows was never passed.
- Suggested fix: Query with a Bundle carrying `QUERY_ARG_SQL_SELECTION`/`ARGS` and `QUERY_ARG_MATCH_PENDING = MATCH_INCLUDE` on API 30+, and use `MediaStore.setIncludePending(collection)` on API 29.

### EXP-4 4K and wide ratios have no size fallback, though the UI says "it falls back automatically"
- **Status: open**
- Severity: medium
- Location: app/src/main/java/dev/geode/export/VideoExporter.kt:389-396 and :578-594; app/src/main/java/dev/geode/export/LoopRender.kt:558-572; app/src/main/java/dev/geode/export/VideoExporter.kt:76-91 (`ExportAspect.of`); app/src/main/java/dev/geode/ui/StudioRender.kt:395 (hint `export_4k_fallback`)
- Failure scenario: A Pro user on a device whose AVC/HEVC encoder tops out at 1920x1088 (common on MediaTek/Unisoc mid and low-end) picks 4K, or picks 1080p 21:9 (2520x1080). Every fallback attempt keeps the same `aspect.width x aspect.height`: only the codec, fps and bitrate change. `configure()` rejects all of them, and the render fails with the raw codec exception. The loop render fails the same way with `export_error_loop_encoder_rejected`. The options page tells the user it "falls back automatically if unsupported". Nothing checks `VideoCapabilities.isSizeSupported`/`areSizeAndRateSupported`.
- Evidence: The attempts are `EncoderAttempt(c, fps, bitRate)` and `EncoderChoice(fps, bitRate, codec)`. No attempt shrinks the size. A grep shows no `isSizeSupported`/`videoCapabilities` anywhere in app or engine.
- Suspected root cause: The fallback ladder covers frame rate and codec only.
- Suggested fix: Before rendering, clamp the aspect to what the encoder supports (`MediaCodecList` → `VideoCapabilities`, keeping the ratio, aligned to `widthAlignment`/`heightAlignment`), or add smaller-size attempts and tell the user what was used.

### EXP-5 Studio exports run outside the single-render guard and the foreground service
- **Status: open**
- Severity: medium
- Location: app/src/main/java/dev/geode/ui/ExportController.kt:421-470 (`startStudioExport`, `startProjectExport`); app/src/main/java/dev/geode/export/StudioExporter.kt:101-129
- Failure scenario: (a) The user starts a 4K visualizer render in Studio › Render, switches to Edit and exports a clip. `startStudioExport` checks only `_studio.phase.isBusy`, not `ExportRun.running`. A Media3 Transformer decode/encode now runs alongside the MediaCodec encoder and the offscreen GL render. On devices with limited codec instances, one of the two fails (`ERROR_CODE_ENCODER_INIT_FAILED`, or a `CodecException` in VideoExporter). (b) A long Studio export runs on the session scope (`Main`) without `ExportService`. When the user switches apps, the process drops to the cached state, where it can be frozen or killed, and the export is lost. Back from the app cancels it outright, because session shutdown cancels `scope`. The visualizer and loop renders are protected against both by design: the ExportRun KDoc says "heavy enough that they should not overlap".
- Evidence: There is no `ExportRun.begin`/`ExportService.start` in either Studio path, and `ExportRun` appears only in the visualizer and loop paths and in RenderBar.
- Suspected root cause: The Studio paths were never brought under ExportRun.
- Suggested fix: Run Studio exports under `ExportRun` (begin, publish, finish, and `ExportService.start`) in `ExportRun.scope`, and refuse to start when `ExportRun.running`.

### EXP-6 A picked SAF destination is left as an empty 0-byte file when the render then doesn't start
- **Status: open**
- Severity: low
- Location: app/src/main/java/dev/geode/ui/ExportHost.kt:224-243 (`rememberVisualizerExport`); app/src/main/java/dev/geode/ui/StudioScreen.kt:392-394; app/src/main/java/dev/geode/ui/ExportHost.kt:247-268 (loop, below Q)
- Failure scenario: A free user leaves 4K or 60 fps selected (the chips are not locked) and saves with "Ask where", or is on API < Q, where it always asks. `CreateDocument` creates `muzviz_<ts>.mp4` in the chosen folder. Then `viewModel.startExport` → `openPaywallIfLocked` returns early. The same happens when `ExportRun.running` (a loop render is in progress while the Render button is still enabled), and for a trial user's Studio "Render to chosen folder…" when `studioExportLocked()`. An empty, unplayable file stays in the user's folder each time.
- Evidence: The picker callback calls `start(req, dest)`. Only the export paths delete the document on failure (`VideoExporter.exportToDestination`, `StudioExporter.publishToDestination`). The early returns in `PlayerViewModel.startExport` (:259), `ExportController.startExport` (:148-149) and `StudioViewModel.startStudioExport` (:144) do not.
- Suspected root cause: The gate and busy checks run after the picker has created the document.
- Suggested fix: Run the gate and busy checks before launching the picker, or call `DocumentsContract.deleteDocument` when the start is refused.

### EXP-7 A failed Studio publish leaves its IS_PENDING row and partial file
- **Status: open**
- Severity: low
- Location: app/src/main/java/dev/geode/export/StudioExporter.kt:201-236
- Failure scenario: The disk fills or an I/O error hits during `file.inputStream().copyTo(out)` while copying the scratch MP4 into the inserted MediaStore row. `runCatching` swallows the exception and returns null, so the UI reports "save failed". The row inserted with `IS_PENDING=1` is never deleted (the delete only runs when `openOutputStream` returns null). Because of EXP-3, the startup sweep doesn't remove it either.
- Evidence: Only the `?: run { resolver.delete(uri…) }` branch deletes the row. An exception thrown inside `use { copyTo }` goes straight to `.getOrNull()`.
- Suspected root cause: The cleanup is tied to a null stream, not to every failure after the insert.
- Suggested fix: Wrap the write and the update after the insert in try/catch and `resolver.delete(uri, null, null)` on any failure.

### EXP-8 The foreground notification is rebuilt and re-posted on every rendered frame
- **Status: open**
- Severity: low
- Location: app/src/main/java/dev/geode/export/ExportService.kt:32-41 and :64-109; publishers at app/src/main/java/dev/geode/ui/ExportController.kt:212, :278, :629
- Failure scenario: `ExportRun.publish` runs once per frame, 30-60+ times a second and faster when the render outruns real time. Each new state makes the `collectLatest` on `Main.immediate` call `createNotificationChannel` (binder), `PendingIntent.getActivity` (binder) and `startForeground` (binder plus a notification post). While the app is open, the main thread spends most of its time on IPC. NotificationManager's per-package rate limit (about 5 posts/s) then sheds most of the updates and logs "Shedding" warnings. This adds UI jank during long renders.
- Evidence: There is no throttle or `distinctUntilChanged` on a coarser progress value. The channel and PendingIntent are recreated on each emission.
- Suspected root cause: The per-frame UI progress is shared with the notification as is.
- Suggested fix: Map state to whole-percent steps (or sample about every 500 ms), create the channel once in `onCreate`, and update with `NotificationManager.notify` after the first `startForeground`.

### EXP-9 TrackAudio leaks its extractor or decoder when construction fails
- **Status: open**
- Severity: low
- Location: engine/scenes/src/main/kotlin/dev/geode/render/offscreen/TrackAudio.kt:31-67; caller engine/scenes/src/main/kotlin/dev/geode/render/offscreen/OffscreenSceneRenderer.kt:99-104
- Failure scenario: The export's audio source has no audio track, or its decoder throws in `configure()`/`start()`, for example with an odd FLAC/ALAC variant. The `init` block throws after `MediaExtractor()`, and in the second case after `MediaCodec.createDecoderByType`, has run. `OffscreenSceneRenderer.prepare` catches the error with `runCatching` (the render goes on "silent to the engine"), but the half-built object is never closed. The decoder instance stays allocated until GC finalizes it, while the same export goes on to open its AAC and video codecs. AudioTranscoder's KDoc explains why a leaked codec breaks the next export.
- Evidence: There is no try/catch in `init`. `close()` is the only release path, and it needs a constructed instance.
- Suspected root cause: The constructor gives no exception safety for its native resources.
- Suggested fix: Wrap the init body in try/catch that releases the codec, the extractor and the AIFF reader before rethrowing.

#### Checked and OK
- VideoExporter: `IS_PENDING` is set on insert and cleared only after `encodeInto` succeeds. The row is deleted on cancel and on any exception, `CancellationException` included. The loudness re-measure cannot delete a saved file: it swallows non-cancellation errors, and `exportJob` is never cancelled.
- VideoExporter and LoopRender teardown: the encoder, input surface, muxer, EGL, renderer, watermark and AAC sidecar are all released in `finally` and in the right order (GL objects while the context is current, EGL last). A muxer that started with zero samples skips `stop()`. EOS is drained with a bounded wait, and a missing EOS fails via `check`.
- PTS: video uses `frame * (1e9/fps)` and audio is interleaved up to each frame's time. Range starts are trimmed in both AudioTranscoder (within about 1 ms) and TrackAudio (sample-exact). The AAC duration uses the encoder's actual output rate.
- Hi-res and mono: `PcmResampler.aacRateFor` maps 88.2/176.4 kHz to 44.1 kHz and 96/192 kHz to 48 kHz. Mono goes through as a 1-channel AAC. More than 2 channels are downmixed. Float PCM is converted. A mono/stereo mismatch in a loop mix is rejected with a message.
- Free tier: `capRange` always caps non-Pro single renders at 60 s (with or without a range, library or SAF). The watermark flag is threaded into VideoExporter and LoopRender. Batch, loop and Studio exports require `FULL_LENGTH_EXPORT` (which a trial user doesn't have). 4K and 60 fps are gated on the real short side, and 21:9 4K at 1756 px is still gated.
- Two concurrent visualizer or loop renders: `ExportRun.running` guards `startExport`, `startBatch` and `startLoopRender` on the main thread, with `begin` called synchronously. (Studio is the exception, see EXP-5.)
- Batch: the per-track exceptions are caught and recorded, cancellation propagates, `cancelRest` accounts for every track, progress is clamped, and duplicate URIs are removed.
- ExportService: `startForeground` is called in `onCreate`. It uses mediaProcessing on API 35+ and dataSync below, both declared in the manifest along with their permissions. `onTimeout` cancels, `START_NOT_STICKY` is set, and it stops itself when `ExportRun` ends, including when the run finished before `onCreate`.
- LoopRender seam maths (output frame j = source frame j + seamFrames, dissolve weight 0→1, the last loop frame lining up with source seamFrames-1), the GPU-budget seam shortening (always ≥1 frame), and LoopSpec/TimeOfDayDrift bounds. LoopExtend: the plan's repeat and segment maths, the 4 GB guard, one encoding signature, truncation at the soundtrack end, and discarding the target on cancel or failure.
- ExportAspect: dimensions are forced even and the long side is clamped to 4096. ExportRange rejects negative and zero lengths, and `ExportRange.of` enforces at least 1 s.
- Editor (pure): `trimmedToStart`/`trimmedToEnd` floors and ceilings, `splitAt` minimum lengths, `place`/`rippleIn`/`overwriteWith` (an enclosing clip asks for a split), the neighbour-clamped trim, the ripple keys/markers scope rules, the keyframe `blendBetween` span guard (≥1 ms), the Bezier solve with a bisection fallback, and `SpeedRamp` segment search. AutoCut, which AGENTS.md lists, does not exist under `editor/` (a doc drift only).
- StudioExporter: Transformer is built, polled and cancelled on Main, the scratch file is deleted in `finally`, and a SAF destination is deleted on write failure.
- ChapterMarkers timestamp formatting and chapters built from markers or a mix; TimedTextOverlay cue re-basing.

## Pro, billing, wallpaper, dream, widget

Branch claude/deep-qa. Paths are repo-relative to `app/src/main/java/dev/geode/` unless they say otherwise.

### PRO-1 Applying a template bypasses the Pro style gate (built-in starters included)
- **Status: fixed (Pro: gate template looks like every other style apply)**
- Severity: high
- Location: ui/PlayerSession.kt:192-195 (`TemplateController.Host.applyLook` → `visual.applyPreset(...)`), ui/TemplateController.kt:53, ui/TemplatesSheet.kt:149 and :168, data/TemplateRepository.kt:184 and :454
- Failure scenario: A free user (who never had a trial) opens Templates and taps Apply on the built-in starter "Reel Neon". The Pro `fluid` renderer goes live. The starter at TemplateRepository.kt:454 does the same with `SceneIds.DEFAULT` (`silk_web`, also Pro). The same happens with any `geode://template` link another user shares (imported, then applied from the library), and with any template saved during the trial. The style stays on screen for the rest of the session, is persisted to `live_state`, and can be exported (watermarked, 1 min), because the export takes `req.sceneId` from the current scene.
- Evidence: Every other style path checks the gate: `PlayerSession.selectScene`/`applyPreset` (lines 863/889) and the auto-visuals host (547/551) all call `allowsStyle(...)`. The template host calls the ungated `visual.applyPreset` directly. `TemplatesSheet` has no lock or ProBadge on templates. `PlayerSessionProvider.leaveLockedStyle` only runs when the gate emits, not when the scene changes, so nothing reverts the style afterwards. `fluid` and `silk_web` are not in `kLooseShaders` (core/viz/Catalogue.cpp:15), so they are not in `FreeTier.freeStyleIds`.
- Suspected root cause: The template host was wired to the ungated `VisualSettingsController` instead of the session's gated `applyPreset`.
- Suggested fix: Make `applyLook` call `this@PlayerSession.applyPreset(look.toPreset(name))`, which opens the paywall when the style is locked, and show a ProBadge on templates whose `look.sceneId` is locked.

### PRO-2 Replaying or exporting a performance take shows Pro styles after the trial ends
- **Status: open**
- Severity: medium
- Location: ui/PlayerSession.kt:424 (`TakeController.Host.selectScene = visual.selectScene`), ui/TakeController.kt:143 and :147, ui/ExportController.kt:187, ui/ExportHost.kt:198-203. The UI callers are in ui/VisualsHub.kt:1498/1507 [PR184] and ui/StudioRender.kt:184.
- Failure scenario: During the one-hour trial the user records a take while switching between Pro styles (fluid, MilkDrop, the 11 Pro styles of each family). After the trial ends, or after a refund:
  - (a) Tapping play on the take in Visuals switches the live visualizer to those Pro styles, scene by scene. `TakeController.playTake` calls the ungated `host.selectScene` and `host.applyMilk`.
  - (b) Choosing the take as the export take in Studio › Render (open to free users) renders the take's opening Pro style. `ExportController` builds the factory from `sceneFactoryFor(exportSceneIdFor(take, ...))`, and `ExportHost.sceneFactoryFor` checks only `availableSceneIds()`, never the gate.
- Evidence: The take host maps `selectScene` straight to `visual.selectScene`, unlike the auto-visuals host, which checks `allowsStyle`. `PlayerViewModel.startExport` checks only the size and frame rate (`openPaywallIfLocked`), never the scene id.
- Suspected root cause: Takes were added before the style gate and never routed through `allowsStyle`.
- Suggested fix: Have the take host skip a locked scene, as auto-visuals does. In `PlayerViewModel.startExport`, pass `sceneFactoryFor` only for allowed ids, or fall back to `req.sceneId` when `!gate.allowsStyle(id)`.

### PRO-3 A paying user's live wallpaper and screensaver lock up 30 days after the app was last opened
- **Status: in progress (data fix agent): wallpaper and dream re-verify with Play**
- Severity: medium
- Location: billing/EntitlementStore.kt:23 and :27 (`vouchesForPro`, `GRACE_MS`), billing/ProSnapshot.kt:21, wallpaper/VisualizerWallpaperService.kt:112, wallpaper/VisualizerDreamService.kt:33. `verifiedAtMs` is only rewritten by `PlayBillingRepository.grant` (billing/PlayBillingRepository.kt:224-227).
- Failure scenario: A Pro owner sets the live wallpaper or the screensaver and then rarely opens the app, which is typical for a wallpaper. Thirty days after the last `refresh()`, `ProSnapshot.gate` returns FREE. The wallpaper freezes on one still frame at every visibility change, and the dream calls `finish()` at once. Play is never asked again until the user happens to open the activity.
- Evidence: `refresh()` runs only from `PaywallViewModel` (its init, and `PaywallHost`'s `LifecycleStartEffect`), so only while `MainActivity` is started. Neither the wallpaper, the dream nor `PlaybackService` touches billing. `ProSnapshot` reads only the prefs cache.
- Suspected root cause: The offline grace period was designed for "Play unreachable", but it also expires while Play is fine and simply never asked, because the only refresh path is the UI.
- Suggested fix: Have the wallpaper and dream services trigger a background `BillingRepository.refresh()` (or `queryPurchasesAsync`) when the cache is older than a few days, before they degrade. Alternatively, run a periodic WorkManager refresh.

### PRO-4 At startup an expired or missing cache downgrades a Pro user's style before Play answers, and the change is permanent
- **Status: open**
- Severity: medium
- Location: di/PlayerSessionProvider.kt:55-73 (`gateKnown = true` on the first emission, then `leaveLockedStyle`), billing/PlayBillingRepository.kt:74
- Failure scenario: A Pro owner whose `CachedEntitlement` has expired (the PRO-3 situation: not opened for more than 30 days) or is missing opens the app. `PlayBillingRepository.isPro` starts `false`, so `GetFeatureGateUseCase` first emits FREE. `PlayerSessionProvider` treats that emission as authoritative: it sets `gateKnown = true` and `leaveLockedStyle` replaces their Pro style with `bars`. The change is persisted through `VisualSettingsController.selectScene` → `vizStateStore.persist()`, and so is `live_state`, which the wallpaper and dream read. About a second later `refresh()` grants Pro, but the style is not restored. The user's chosen look is lost, and the wallpaper changes to Bars too.
- Evidence: The KDoc says "False until the first real entitlement arrives; nothing is downgraded before that". But the first emission is only the cache's opinion, and for a stale cache that opinion is "free".
- Suspected root cause: A seed emission taken from the cache is not told apart from one Play has confirmed.
- Suggested fix: Have `BillingRepository` expose whether Play has answered since process start, and let `leaveLockedStyle` downgrade only after a confirmed answer or after a trial-end transition.

### PRO-5 Setting the clock back into the trial hour restarts the trial, as often as the user likes
- **Status: in progress: user chose guard-clock, no backup**
- Severity: low
- Location: pro/TrialRepository.kt:41-49 (`TrialState.at`), billing/StoredTrialRepository.kt:64-72, billing/ProSnapshot.kt:23
- Failure scenario: The trial starts at T and ends at T+1h. Later the user sets the device date and time to T+5min: `TrialState.at` returns `Active` again, both in the app (on the next launch or the next stream emission) and in `ProSnapshot`. That opens every Pro style, 4K and 60 fps, Studio Edit, the wallpaper and the screensaver. It can be repeated indefinitely. The KDoc means clock-back to end the trial ("ends the trial rather than extending it"), but that only holds for a clock before T.
- Evidence: The only state stored is `trial_started_at` and the notice flag. There is no high-water mark and no "ended" flag, so `Ended` is recomputed from the clock each time.
- Suspected root cause: Trial end is derived purely from the current wall clock.
- Suggested fix: Persist a `trial_ended` flag (or the latest wall time seen) the first time `Ended` is computed or `endsAtMs` passes, and treat it as final.

### PRO-6 A purchase is acknowledged only while the activity runs, so a pending payment that clears in the background can be auto-refunded
- **Status: open**
- Severity: low
- Location: billing/PlayBillingRepository.kt:85-94 and :207 (acknowledge only inside `handle`, which only `refresh`/`restore`/`onPurchasesUpdated` reach), ui/pro/PaywallScreen.kt:110-113
- Failure scenario: The user pays with a slow method (cash or bank transfer) and the status is `Pending`. The user closes the app. The payment clears two days later. Play does not call `onPurchasesUpdated` for a process that is not running, so the purchase stays unacknowledged until `MainActivity` next starts. If the user does not open the app within Play's 3-day acknowledgement window (for example they only use the live wallpaper, or the dream), Play refunds and revokes the purchase.
- Evidence: There is no WorkManager, no service-side query, and `PlaybackService` has no billing hook. The class KDoc itself says acknowledgement "is retried on every [refresh]".
- Suspected root cause: Acknowledgement is tied to the UI lifecycle only.
- Suggested fix: Schedule a one-off WorkManager job (`refresh()`; no network permission is needed, since this is IPC) whenever a purchase is seen `PENDING`, repeating daily until it is acknowledged or gone.

### PRO-7 The entitlement cache trusts a backwards clock without limit and travels in Android backup and device transfer
- **Status: in progress: user chose guard-clock, no backup**
- Severity: low
- Location: billing/EntitlementStore.kt:23. `geode-pro` prefs are not excluded in app/src/main/res/xml/backup_rules.xml or data_extraction_rules.xml.
- Failure scenario:
  - (a) Once `verifiedAtMs` is in the future relative to the clock (for example the date was set wrong at the last refresh, or deliberately), `nowMs - verifiedAtMs` stays negative. The cache then vouches for Pro without limit whenever Play cannot answer, including after a refund on a device where Play stays signed out or unavailable.
  - (b) A `<device-transfer>` or cloud restore copies `pro_owned=true` and `pro_verified_at` to a phone signed into a different Google account. That phone is Pro at launch, and for up to 30 days in the app, the wallpaper and the dream if Play returns BILLING_UNAVAILABLE, because the `connect()` failure path keeps the cache.
- Evidence: `vouchesForPro = owned && nowMs - verifiedAtMs < GRACE_MS`, with no lower bound. `refresh()` returns early on a connection failure without touching `isPro`.
- Suspected root cause: The cache has no clock sanity check and is treated as user data.
- Suggested fix: Bound the check (`nowMs - verifiedAtMs in -GRACE_MS..GRACE_MS`, or re-stamp to `nowMs` when the clock is behind), and exclude `sharedpref/geode-pro.xml` from cloud backup (keeping `trial_started_at` in a separate file if it should travel).

### PRO-8 The widget can show the previous track's artwork after a fast skip
- **Status: open**
- Severity: low
- Location: widget/WidgetPublisher.kt:58-67, widget/WidgetState.kt:53-59
- Failure scenario: The user skips A → B quickly, where A has large embedded art. `publish()` cancels A's `artJob`, but `MediaArtwork.decodeEmbedded` is blocking and does not check cancellation. A's decode finishes after B's job has already written `widget_art.png`. A's job then overwrites the one shared file with A's art and saves `artPath`. `updateAll` throws `CancellationException`, but the file stays changed, so the next redraw (play/pause) shows A's cover under B's title.
- Evidence: Both jobs run in parallel on `Dispatchers.IO` and write one fixed file, with no `ensureActive()` between the decode and `writeArt`.
- Suspected root cause: The art file write is not guarded against a superseded job.
- Suggested fix: Call `ensureActive()` (or check `uri == artUri`) after the decode and before `writeArt`/`save`, or write to a per-uri file name.

#### Checked and OK
- Release builds hard-code `USE_FAKE_BILLING=false` (app/build.gradle.kts:107). The fake and the debug panel appear only when `billing is FakeBillingRepository` (`PaywallScreen` renders the panel only when `state.debug != null`). release.yml fails a store build that has no `PLAY_LICENSE_KEY`.
- PBL usage: `enablePendingPurchases(enableOneTimeProducts)` and `enableAutoServiceReconnection` are set. `connect()` is serialised by a mutex. `queryPurchasesAsync` runs on each activity start. Refunds and revocations are handled: a successful full query without the product calls `grant(false)`. PENDING is not granted, and only a full list downgrades. `ITEM_ALREADY_OWNED` re-queries. Cancelling before the sheet opens resets `InProgress`. The signature is checked with SHA1withRSA before granting.
- `FeatureGate` policy: Pro beats Trial beats Free. Trial exports keep the watermark and the 1-minute cap. Loop and batch need `FULL_LENGTH_EXPORT`. Studio exports need full Pro. `capRange` keeps `ExportRange(start, duration)` valid.
- Export gates: every `session.startExport`/`startLoopRender`/`startBatch`/`startStudioExport`/`startProjectExport` caller goes through `PlayerViewModel` or `StudioViewModel`, including the `rememberSaveable` pending exports after the destination picker.
- Style paths that are gated: the `PlayerViewModel`/`VisualsViewModel` `selectScene`/`applyPreset` paths, VisualsHub stage swipe, `selectMilk`, auto-visuals (scene and preset skipped when locked; a locked milk entry only preloads a preset into a scene that is not showing). `geode://preset` and `geode://template` links only import, after confirmation, and never apply. The manual backup (`BackupSection`) does not carry `geode-pro` or takes, and a restored Pro `sceneId` is reverted by `leaveLockedStyle`.
- The trial gate closes on time (`transformLatest` + `delay`). The trial-ended paywall shows once, never to a Pro user. `PlayerSessionProvider` reverts to `bars` when the trial ends or after a refund.
- `ProGate` on Studio › Edit, and the wallpaper button gate. Setting the wallpaper through the system picker is still covered by `ProSnapshot` on each visibility change.
- Wallpaper: one renderer and pacer per engine (preview plus home). The pacer is stopped before `onPause`. Scenes are released on the GL thread with a bounded wait. Touch is cleared on hide. The surface-size touch mapping is correct. Dream: gated in `onAttachedToWindow`, and teardown is handled by `VisualizerView.onDetachedFromWindow`. `AudioBus` is a plain volatile `SoundInput` with no consumer registration, so the wallpaper and dream cannot leak consumers, and each renderer has its own cursor.
- Widget: `provideGlance` reads prefs off the main thread. Updates happen only on metadata, transition, isPlaying and state events. `clear()` redraws on `appScope` so it survives teardown. Transport goes through a `MediaController` that is released after each command, and the launch uses `appLaunchIntent`.

## Library, settings and UI flows

Paths are relative to `app/src/main/java/dev/geode/` unless they start at the repo root.

### UI-1 Duplicates tab crashes on a duplicate LazyColumn key when one title/artist splits into two duplicate groups
- **Status: fixed (Library: key duplicate groups by a member uri)**
Severity: high
Location: ui/DuplicatesTab.kt:53 (key), ui/LibraryDuplicates.kt:16-23 (grouping)
Failure scenario: The library holds two copies of "Intro" by Artist A at 1:05 (two folders) and two copies of a different "Intro" by Artist A at 2:40 (another album, also copied twice). This is common when a music folder was copied twice. Opening Library › Duplicates throws `IllegalArgumentException: Key "Intro|Artist A" was already used` and the app crashes. It crashes again every time the tab opens, and `tab` is rememberSaveable, so it can reopen on that tab.
Evidence: `LibraryDuplicates.find` groups by normalised (title, artist), then `splitByDuration` breaks that bucket into several groups when lengths differ by more than 2 s. Each group with more than one track is kept, and every kept group carries the same `group.first().title` / `.artist`. The header item uses `item(key = "${group.title}|${group.artist}")`.
Suspected root cause: The header key does not include the duration bucket or group index.
Suggested fix: Key the header by group index or first track uri, e.g. `key = "dup:${group.tracks.first().uri}"`.

### UI-2 Tracks imported by folder (SAF) never appear in the Library screen, which breaks the "no permission, pick a folder" onboarding path
- **Status: open**
Severity: high
Location: ui/LibraryScreen.kt:111, 121-151, 185-190; ui/FirstRun.kt:94-100
Failure scenario: (a) A user taps "Not now" on music access in FirstRun, then "Pick a folder" (FirstRun says "a folder picked by hand plays without any permission, so it is the way in"). The folder scans into `TrackLibrary`, but Library shows only the permission prompt (`return` at line 150), so the user's music is nowhere in Library. (b) A user with permission adds an SD-card or Downloads folder in Library › Folders or Settings › Folders. "Scanning…" runs, and no new track shows in Tracks, Albums, Artists or Folders. The tracks are reachable only through global Search and Android Auto (`playback/LibraryTree.kt:123-128` merges them; the phone Library does not).
Evidence: Every Library tab draws from `state.tracks`, which is `session.deviceTracks` (MediaStore) only (`LibraryViewModel.uiState`). `library.tracks` (TrackLibrary) feeds only `PlaylistTracks` titles and Search. The Folders tab lists device folders only (`FolderTree.rows(folders)` over device tracks).
Suspected root cause: The library screen was moved to MediaStore-only and the imported list was never merged back in.
Suggested fix: Merge `library.tracks` whose uri is not in deviceTracks into the ViewModel's track list (as LibraryTree.buildTracks does), and show tabs even without permission when imported tracks exist.

### UI-3 Hand-made playlists show "Track 1, Track 2…" for every device (MediaStore) track
- **Status: open**
Severity: medium
Location: ui/LibraryScreen.kt:490, 639-642
Failure scenario: In Tracks, open a song's ⋮ menu › Add to playlist › My Mix, then open Playlists › My Mix. The row reads "Track 1" instead of the song title. This happens for every track added from the device list, which is the normal case, unless the user also chose "Add to library list" or edited its info.
Evidence: `PlaylistTracks(pl, library.tracks, viewModel)` builds `byUri` from `library.tracks` (TrackLibrary, imported or edited tracks only). Device uris are not in it, so it falls back to `stringResource(R.string.playlist_untitled_track, i + 1)` ("Track %1$d").
Suspected root cause: The title lookup ignores `deviceTracks`.
Suggested fix: Build the lookup from deviceTracks plus library.tracks overrides (as TrackRow does).

### UI-4 Playlist file import (M3U/PLS/XSPF) resolves only against the imported library, so device music never matches
- **Status: open**
Severity: medium
Location: ui/MusicLibraryController.kt:449-453
Failure scenario: A user with a normal MediaStore library imports `roadtrip.m3u` listing files that are on the phone and visible in Library › Tracks. The result dialog reports "0 added, N unresolved", and an empty playlist is created.
Evidence: `urisByFileName` is built from `_library.value.tracks.filter { it.fileName.isNotBlank() }`, which holds only TrackLibrary entries (SAF imports or "Add to library list"). Device tracks are never consulted.
Suspected root cause: Same split as UI-2 and UI-3: the imported list is treated as the library.
Suggested fix: Also index deviceTracks by file name (DeviceTrackQuery already reads DATA; add DISPLAY_NAME or derive it from the path).

### UI-5 Library permission screen never re-checks on resume: granting in system Settings leaves Library stuck on "denied"
- **Status: open**
Severity: medium
Location: ui/LibraryScreen.kt:88-99, 121-151
Failure scenario: The user denies music access twice, and Library shows "Open settings". They grant it there and come back. Library still shows the denied text and button (and no tracks) until they leave the Library tab and return. The rationale logic also misreads a dismissed first dialog (tap outside, no rationale afterwards) as "denied forever", the ambiguity `statusAfterRefusal` exists to handle.
Evidence: `granted` is `remember { mutableStateOf(checkSelfPermission(...)) }` and is only updated by the launcher callback. There is no `LifecycleResumeEffect`. `ui/PermissionRequest.kt` (`rememberPermissionRequest`) handles both cases, and FirstRun, AudioSettings and PlayerScreen use it; LibraryScreen keeps its own copy.
Suspected root cause: LibraryScreen was not migrated to `rememberPermissionRequest`.
Suggested fix: Replace the local `granted`/`asked` logic with `rememberPermissionRequest(audioPermission) { libraryViewModel.refreshDeviceTracks() }`.

### UI-6 Lyrics: .lrc sidecars are never found for library tracks, and the "embedded lyrics" are the lyricist/composer tag
- **Status: open**
Severity: medium
Location: ui/Lyrics.kt:49-59, 74
Failure scenario: (a) A user puts `Song.lrc` beside `Song.mp3`, as the Lyrics panel's own help text tells them to (`lyrics_none_explainer`). The panel keeps saying "No lyrics". (b) Any track with a lyricist or writer tag (common in ID3 TEXT and MP4 ©wrt) shows that name as one "untimed lyric from the track's own tags", e.g. the panel body reads "Bernie Taupin". Real embedded USLT lyrics are never shown.
Evidence: Library tracks are `content://media/...` (MediaStore) or SAF single-document uris. `DocumentFile.fromSingleUri(...)` returns a SingleDocumentFile whose `parentFile` is always null (and `listFiles` throws), so `sibling` is always null and only `file://` uris could work. `embedded()` reads `MediaMetadataRetriever.METADATA_KEY_WRITER`, which is the writer/lyricist field, not lyrics.
Suspected root cause: The sidecar lookup assumes tree-capable documents; the lyrics tag uses the wrong metadata key.
Suggested fix: Resolve sidecars from MediaStore (query DATA/RELATIVE_PATH for `<stem>.lrc` in the same folder) and read USLT/©lyr through TagLib (core/library) instead of METADATA_KEY_WRITER.

### UI-7 Search results put "Artist · Album" into the queue's artist field
- **Status: open**
Severity: low
Location: ui/AppShell.kt:708
Failure scenario: Search "beatles", tap a result. The notification, lock screen, Android Auto and the mini player ("Title — Artist · Album") show the artist as "The Beatles · Abbey Road".
Evidence: `QueueTrack(r.uri, r.title, r.subtitle)`: the third parameter is `artist` (ui/PlayerModels.kt:66-70), and `subtitle` is `listOf(artist, album).joinToString(" · ")`.
Suspected root cause: SearchTrackRow does not keep the artist separately.
Suggested fix: Add `artist` to `SearchTrackRow` and pass it instead of `subtitle`.

### UI-8 Track-info edits (app-side overrides) are shown but ignored by Library search, sort and grouping
- **Status: open**
Severity: low
Location: ui/LibraryViewModel.kt:77-80; ui/LibraryScreen.kt:186-188
Failure scenario: The user edits a track's title from "Track 01" to "Sunrise" (Save without "write to file"). The row shows "Sunrise", but searching "sunrise" finds nothing, sort by title still places it under "T", and an edited album or artist does not move it in the Albums or Artists tabs.
Evidence: `LibraryBrowse.search` and `sort` and the `groupBy { it.album / it.artist }` all run on raw `DeviceTrack` fields. Overrides are applied only at draw time in `TrackRow` (`stored?.title ?: t.title`).
Suspected root cause: Overrides are not folded into the tracks before search, sort and grouping.
Suggested fix: Apply `overrides` to the DeviceTrack list inside the `uiState` combine before searching and sorting.

### UI-9 Duplicates tab is silently filtered by the hidden search query
- **Status: open**
Severity: low
Location: ui/LibraryScreen.kt:155, 190
Failure scenario: The user types "live" on Tracks, then switches to Duplicates. Only duplicates matching "live" appear (or "No duplicates"), and no search field is visible on that tab to explain or clear it.
Evidence: The search field is hidden for `DUPLICATES_TAB`, but `DuplicatesTab(state.tracks, …)` receives the already-searched `state.tracks`.
Suspected root cause: The tab reuses the filtered list.
Suggested fix: Pass the unfiltered `deviceTracks` to DuplicatesTab (or keep the query visible).

### UI-10 Library group view: a stale `open` group swallows Back and reopens later
- **Status: open**
Severity: low
Location: ui/LibraryScreen.kt:359-362
Failure scenario: On Albums, open album "X". Type a query in the (still visible) search field that excludes X. The group list returns, but `open` is still "X", so the predictive-dismiss handler stays enabled: the next Back does nothing visible instead of going to Player. Clearing the query later jumps straight back into album X.
Evidence: `rememberPredictiveDismiss(enabled = sel != null)` while the detail view is drawn only `if (sel != null && groups.containsKey(sel))`.
Suspected root cause: `open` is not cleared when its group disappears.
Suggested fix: Enable the dismiss only when `groups.containsKey(sel)`, or reset `open` in a LaunchedEffect when the key vanishes.

### UI-11 Track info "Write to file" can never succeed on API 26–29 for device tracks
- **Status: open**
Severity: low
Location: ui/TrackInfoEditor.kt:128-139; data/NativeTags.kt:149-167
Failure scenario: On Android 8–10, turn on "Write to file" for a library track and tap Save. It always shows "couldn't write" (`track_info_write_failed`). The switch is still offered.
Evidence: The manifest has no WRITE_EXTERNAL_STORAGE, so `openFileDescriptor(uri, "rw")` on a MediaStore uri throws SecurityException, giving `NeedsConsent`. `canRequestConsent` requires `SDK_INT >= R`, so it falls to `writeFailed = true`. On API 29 the thrown `RecoverableSecurityException.userAction` could grant access but is discarded.
Suspected root cause: The consent path covers only `createWriteRequest` (30+).
Suggested fix: On 29, launch `RecoverableSecurityException.userAction.actionIntent`. On 26–28, hide or disable the switch for MediaStore uris.

### UI-12 Smart playlist track counts go stale (0 on a cold open, never update for plays/favourites)
- **Status: open**
Severity: low
Location: ui/SmartPlaylistEditor.kt:50
Failure scenario: The app is restored onto Library › Playlists (tab is rememberSaveable), or the user goes there right after launch. The MediaStore query has not landed, so every smart playlist reads "0 tracks" and stays that way even after `deviceTracks` arrive. Favouriting tracks or playing them (FAVOURITE and PLAY_COUNT rules) also never changes the shown count. Tapping Play plays the real (non-zero) set.
Evidence: `remember(pl, library) { viewModel.resolveSmartPlaylist(pl).size }`. `resolveSmartPlaylist` reads `deviceTracks.value`, play counts and favourites, none of which are remember keys.
Suspected root cause: Wrong remember keys.
Suggested fix: Collect `deviceTracks` (and favourites) and add them as keys, or compute counts in the ViewModel.

### UI-13 Sleep timer "finish the track" toggle changes the label but not the running timer
- **Status: open**
Severity: low
Location: ui/PlaybackSettings.kt:146-161; playback/SleepTimer.kt:32-53
Failure scenario: Start a 15-minute timer with "finish the track" off, then turn it on. The text switches to "Pausing after this track in …", but the timer still fades and hard-pauses mid-song (and the reverse case, off after on, waits for the track end while saying it will pause on time).
Evidence: `sleepTimer.start(minutes, finishTrack)` captures `finishTrack` once. The toggle only writes `prefs.sleepFinishTrack`, which the label reads.
Suspected root cause: The running job does not observe the pref.
Suggested fix: Restart the timer with the remaining time when the toggle changes, or have the job read the pref each tick.

### UI-14 Settings writes from rapid slider drags can persist out of order
- **Status: open**
Severity: low
Location: data/UserDataRepository.kt:42-46; ui/PlayerSettingsController.kt:47-49
Failure scenario: The user drags Look › Bar opacity (or Audio › Reactivity, or any GuiPrefs slider) and releases. In memory the final value is right. On disk an earlier tick's `saveGui` can run last, so after a restart the slider comes back at a slightly earlier value.
Evidence: Each `setGuiPrefs` is its own `scope.launch` that sets `_guiPrefs` in order, then `withContext(Dispatchers.IO) { store.saveGui(prefs) }`. Dispatchers.IO is multi-threaded and these blocks are not serialized (unlike `PlayerPrefsRepository`, which uses a Mutex), so the `prefs.edit { … }` apply calls can commit in any order.
Suspected root cause: No serialization of GUI pref writes.
Suggested fix: Guard `saveGui` with a Mutex or run writes on `storeScope` (IO.limitedParallelism(1)).

### UI-15 `refreshNumericTitles()` is a no-op: it runs before the library is loaded
- **Status: open**
Severity: low
Location: ui/PlayerSession.kt:1138-1142; ui/MusicLibraryController.kt:272-277
Failure scenario: Imported tracks saved with hex or numeric placeholder titles (the case the repair targets) are never repaired at startup.
Evidence: `init` calls `musicLibrary.refreshNumericTitles()` before `musicLibrary.refresh()`. The former immediately filters `_library.value.tracks`, which is still the empty initial `LibraryState()`. `refresh()` only fills it later from disk.
Suspected root cause: Call order. The repair reads in-memory state that is not loaded yet.
Suggested fix: Call the repair after `refresh()` finishes, or have it read `trackLibrary.list()`.

### UI-16 Removing a library folder leaves all its tracks imported, and there is no way to remove imported tracks
- **Status: in progress: user chose removing a folder removes its tracks**
Severity: low
Location: ui/MusicLibraryController.kt:318-321; data/TrackLibrary.kt:39
Failure scenario: The user adds a folder by mistake and taps ✕ on it in Library › Folders or Settings › Folders. The root disappears, but every scanned track stays in Search, Android Auto and playlist resolution permanently. Deleted files are never pruned on rescan either.
Evidence: `removeMediaRoot` only edits the roots set. `TrackLibrary.remove` has no caller anywhere. `scanTreeBlocking` only `addAll`s.
Suspected root cause: Root removal was not wired to the track store.
Suggested fix: On root removal, drop TrackLibrary entries whose uri starts with that tree's document prefix, and release the persisted permission.

### UI-17 Tutorial "Resume" pill is drawn inside the picture-in-picture window
- **Status: open**
Severity: low
Location: ui/AppShell.kt:357-368, 376-382; ui/TutorialState.kt:194-196, 444-462
Failure scenario: In the tour, tap "Show me" on a Live view card. That calls `appState.expand()`, pauses the tour and shows the pill. Enter PiP (button, or auto-enter). The pill ("Resume / Skip all") covers the top-right of the small PiP window, which cannot be tapped.
Evidence: `TutorialHost` is composed after `VisualizerScreen` in the same window and draws `TutorialResumePill` (an in-window Box, not a Dialog) whenever `!state.open`. It ignores `VisualizerPipCoordinator.inPictureInPicture`, which only VisualizerScreen reads.
Suspected root cause: Overlays in AppRoot don't check the PiP mode.
Suggested fix: Skip the pill (and other in-window overlays) while `VisualizerPipCoordinator.inPictureInPicture`.

#### Unconfirmed (needs a device check, not counted)

- Compact↔rail shell swap on rotation (ui/AppShell.kt:195-196, 278-297). `destinationContent` (with its `SaveableStateProvider`) is called from different parents, `AppShellCompact` and `AppShellExpanded`. `rememberSaveable` keys inside are derived from the full composite-key path, so Library tab, open group, Settings tab and similar state may reset when a phone rotates (portrait compact, landscape ≥600 dp rail), despite the comment saying the holder prevents it. Rotate on Settings › Audio and see if it jumps back to Look.
- MainActivity re-processes the launch intent when relaunched from Recents after the activity finished (Back from Player): `savedInstanceState == null`, and FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY is not checked (ui/MainActivity.kt:317-320). A task started by a voice MEDIA_PLAY_FROM_SEARCH would replay the search and replace the queue. Confirm the task-root behaviour on device.

#### Checked and OK

- DeviceTrackQuery: the projection (_ID, TITLE, ARTIST, ALBUM, DURATION, DATA, DATE_ADDED) is valid on API 26–36. The READ_MEDIA_AUDIO (33+) / READ_EXTERNAL_STORAGE (maxSdk 32) split matches the manifest. Null tags fall back to strings, and a missing permission returns an empty list. No content observer, but every Library or Search entry re-queries.
- TrackInfoEditor consent flow on 30+: a MediaStore-authority check before `createWriteRequest`, one retry on RESULT_OK, and `writeFailed` on refusal. NativeTags detaches fds before TagLib closes them.
- TrackLibrary: locked read-modify-write, AtomicWrite, a corrupt file moved aside, identity-key de-duplication.
- MusicPlaylistStore: hashed safe file names (no collisions), bounds-checked move, de-duplicated addTrack. PlaylistNameDialog and rename validation. `playlistDropIndex` handles count/rowHeight 0.
- Search: `SearchMatcher.filterTracks` de-duplicates by uri, so the `"t:$uri"` keys are unique. Debounce and rememberSaveable query are fine.
- GeodeAppState saver: enum names, safe casts, `getOrNull(4)` for older saves. The root BackHandler only when off Player, and inner predictive-dismiss handlers take priority (group view, search, visualizer).
- Splash held until `userDataLoaded`, so onboarding and the tour never flash on defaults. ThemeStore migration (setupDone, reducedMotion, safety) and every GuiPrefs field with a UI is both persisted and read by its consumer (`clearVisualsMenu` and `micReactive` are vestigial, with no UI).
- Behaviour, Look, Performance and Playback toggles are each wired to a store and to their consumer (touch smear and transform → VisualizerScreen, secondScreen → AppRoot, autoEnterPip → MainActivity, keepScreenOn → EnginePlumbing and the dream, performance → PerformanceStore). CrystalSegmented tolerates `selected = -1`.
- TutorialState saver and resume: chapter names are parsed leniently, `autoOfferUsed` is saved, the first-run resume point is written only for the first-run tour, and Reset arms the offer for the next launch.
- FirstRun: rememberPermissionRequest with a resume re-check, the scan triggered by the grant, rememberSaveable skip.
- MainActivity: shared links are parked for confirmation and consumed only when they are ours. Data and action are cleared after use. onNewIntent calls setIntent. PiP aspect is clamped, and auto-enter is kept in sync on S+ with onUserLeaveHint below S.
- BackupViewModel: repository errors are wrapped as BackupException, so no uncaught crash.
- Artwork and VideoFrame caches are bounded, and API 26 downscaling recycles the source.

## Concurrency

Scope: every coroutine scope, thread, executor and GL/JNI hand-off in `app/` and `engine/`. Each finding below was traced on both sides of the race. None of these files are in the PR183/PR184 list except where tagged.

### CON-1 NativePlayer worker calls into the native player after `playerDestroy` (use-after-free on release)
- **Status: open**
- Severity: medium
- Location: app/src/main/java/dev/geode/playback/NativePlayer.kt:151-161 (handleRelease), :303-322 (openCurrent worker body), :342-347 (queueNext worker body)
- Failure scenario: with the native engine enabled, a track is opened (`openCurrent` or `queueNext` submits to `geode-native-player`). The worker passes the `if (released)` check and blocks in `resolver.openFileDescriptor` (a binder call into MediaProvider or a DocumentsProvider; a SAF or cloud-backed document, or a busy MediaProvider, can take more than 500 ms). On Main, `PlaybackSession.release()` then calls `player.release()`, which runs `handleRelease`: `worker.shutdownNow()` (an interrupt does not abort a binder call), then `awaitTermination(500 ms)` times out quietly inside `bestEffort`, then `tap.stop()`, `dsp.release()` and `GeodeNative.playerDestroy(handle)` free the `geode_player`. Once `openFd` returns, the worker calls `GeodeNative.playerOpen(handle, …)`, which reaches `Player::enqueue` and locks `lock_` in freed memory: SIGSEGV, or heap corruption. The detached fd also leaks.
- Evidence: `released` is checked once, before `openFd`, and never after it. `released` is a plain `var` (not `@Volatile`), written on Main and read on the worker. `geode_player_destroy` is `std::unique_ptr<geode_player> owned(p)`, a synchronous delete (core/api/geode_player_api.cpp:26). Nothing makes destroy wait for the worker beyond the 500 ms bound.
- Suspected root cause: the release path assumes worker tasks are short or interruptible. `openFileDescriptor` is neither.
- Suggested fix: make `released` `@Volatile`, check it again after `openFd` (close the fd when released), and destroy the handle only once the worker has terminated, for example by posting `playerDestroy` to the worker itself after `shutdown()`.

### CON-2 `PlayerSession.storeScope` does not serialise writes: `withContext(Dispatchers.IO)` leaves the single-slot view
- **Status: open**
- Severity: medium
- Location: app/src/main/java/dev/geode/ui/PlayerSession.kt:98 (scope), used by app/src/main/java/dev/geode/data/PresetRepository.kt:57-100, SessionRepository.kt:17-21, TakeRepository.kt:27-43, TemplateRepository.kt:580-672; callers ListeningTracker.kt:143, PresetLibraryController.kt:37-210, TakeController.kt:115-181, TemplateController.kt:68-126
- Failure scenario: `storeScope` is `Dispatchers.IO.limitedParallelism(1)`, documented as "one write at a time". Each repository method does its work inside `withContext(Dispatchers.IO)`. That is a different dispatcher, so the calling coroutine suspends and frees the one slot, and the next queued storeScope job starts while the first is still writing. Two concrete cases:
  1. Queue snapshot: `ListeningTracker.persistSession()` runs on every track change. Press Next twice quickly, or let auto-advance skip several unplayable files, and two `sessionRepository.save` calls run on two IO threads at once. `AtomicWrite`'s per-file lock serialises the bytes but not the order, so the older `Saved(index, position)` can land last. Playback resumption after process death (`PlaybackService.lastPlayedResumption`) then restores the wrong track or position.
  2. Lists: `FileTemplateRepository.refresh()` / `FilePresetRepository.list()` from job A can finish after job B's save and refresh, so `_templates.value` (or `updatePresets { listed }`) is overwritten with a list taken before B's write. The just-saved template or preset disappears from the UI until the next refresh. `persist()`'s `uniqueName` check-then-save can also give two adopted copies the same name.
- Evidence: kotlinx `withContext` takes the dispatch path when the `ContinuationInterceptor` differs (a LimitedDispatcher view vs `Dispatchers.IO`), and suspends the caller. The LimitedDispatcher worker then runs the next task. `PlayerPrefsRepository` and `FavouritesRepository` add a `Mutex` around their write and are unaffected. The repositories above have none.
- Suspected root cause: serialisation lives in the scope's dispatcher, but the repositories switch dispatcher on every call.
- Suggested fix: inject the store dispatcher into the repositories (or use `withContext(coroutineContext)` / no switch inside storeScope), or guard each repository's write-then-refresh with one `Mutex`, as PlayerPrefsRepository does.

### CON-3 Async title resolution restarts the current track on the native engine
- **Status: open**
- Severity: medium
- Location: app/src/main/java/dev/geode/ui/QueueController.kt:218-237 (resolveTitlesAsync → `player.replaceMediaItem`), app/src/main/java/dev/geode/playback/NativePlayer.kt:219-260
- Failure scenario: `open(uris)` (VIEW or share intent, file manager) or `playFrom` with untitled tracks starts playback with placeholder items, then patches each one with `replaceMediaItem` once the IO batch resolves, which can be seconds for a large folder. `NativePlayer` does not override `handleReplaceMediaItems`. Media3 1.11's default (SimpleBasePlayer.java:3583) runs `handleAddMediaItems(index+1)` and then `handleRemoveMediaItems(index, index+1)`. For the playing index, `removingCurrent` is true, so `loadedId = -1` and `openCurrent(0L)` runs. The track that has been playing for several seconds jumps back to 0:00 with an audible re-open. Every other patched item also clears and re-arms the gapless pre-roll (`queueNext` → `playerSetNext` churn). ExoPlayer is unaffected, because `canUpdateMediaItem` makes the same-URI replace seamless.
- Evidence: SimpleBasePlayer 1.11.0 source, extracted from the Gradle cache; NativePlayer.kt:243 `removingCurrent` → :251-259 reopen at 0.
- Suspected root cause: a metadata-only update reaches the native player as remove plus add of the playing item.
- Suggested fix: override `handleReplaceMediaItems` in NativePlayer to swap the `Entry.item` in place when the URI is unchanged, without reopening.

### CON-4 Widget art: a cancelled art job still writes the previous track's art, and two jobs write one file at once
- **Status: open**
- Severity: low
- Location: app/src/main/java/dev/geode/widget/WidgetPublisher.kt:22, :58-67; WidgetState.kt:49-60
- Failure scenario: `scope` is plain `Dispatchers.IO`, so jobs run in parallel. Skip from track A, which has large embedded art and decodes slowly, to track B. `artJob?.cancel()` has no effect, because `MediaArtwork.decodeEmbedded` blocks and there is no suspension point or `isActive` check before `writeArt`/`save`. If B has no art, its job finishes first (`writeArt(null)` deletes the file, saves null). A's job then writes A's PNG and saves its path, so the widget shows A's cover for B. If both have art, both `outputStream()` into the same `cacheDir/ART_FILE` concurrently, which truncates and interleaves the bytes, so the widget gets a corrupt or blank image.
- Evidence: there is no `ensureActive()` after the decode, all jobs share the fixed `ART_FILE`, and the dispatcher is multi-threaded.
- Suspected root cause: cancellation is assumed to stop blocking work, and nothing serialises the writes.
- Suggested fix: run art jobs on `Dispatchers.IO.limitedParallelism(1)` and call `ensureActive()` after the decode and before `writeArt`/`save`.

### CON-5 Background image decode publishes a stale blend and amount
- **Status: open**
- Severity: low
- Location: app/src/main/java/dev/geode/ui/BackgroundController.kt:126, :130-157
- Failure scenario: pick an image, or rotate. `redecode` captures `p = _prefs.value` and starts a decode of hundreds of ms on `storeScope`. While it runs, the user changes Blend or Amount. `setPrefs(redecode = false)` pushes `_push.value.copy(blend, amount)` but does not bump `decodeGeneration`. The decode then lands with `BackgroundPushState(decoded, p.blend, p.amount)`, the old values, because the generation still matches. The sheet shows the new blend while the renderer draws the old one until the next change.
- Evidence: line 155 uses the captured `p` and not the current `_prefs.value`. Blend and amount changes leave `decodeGeneration` untouched.
- Suspected root cause: the decode result carries prefs captured before the async work started.
- Suggested fix: on completion, build the push from `_prefs.value.blend/amount` instead of `p`.

### CON-6 GUI prefs: concurrent `saveGui` calls can persist an older value
- **Status: open**
- Severity: low
- Location: app/src/main/java/dev/geode/data/UserDataRepository.kt:42-46; caller PlayerSettingsController.kt:47-49
- Failure scenario: every slider tick in Settings (text scale, accent, sound tuning, morph seconds) launches `setGuiPrefs` and then `withContext(Dispatchers.IO) { store.saveGui(prefs) }`. These IO tasks run in parallel with no lock, and `SharedPreferences.edit().apply()` commits to memory in call order. Ticks a few ms apart on a loaded device can therefore apply out of order, leaving an earlier slider value on disk. In-memory state is right; after a restart the setting comes back slightly different. A fast double toggle can come back with the wrong boolean.
- Evidence: unlike `SharedPrefsPlayerPrefsRepository` and `SharedPrefsFavouritesRepository`, which take a `Mutex` around the save, this repository has none.
- Suspected root cause: there is no ordering between successive saves.
- Suggested fix: wrap the value set and the save in a `Mutex` (as PlayerPrefsRepository does), or conflate saves through a single-slot dispatcher.

### CON-7 ExoPlayer release timeout can let the playback thread use DSP chains that `dsp.release()` already freed
- **Status: open**
- Severity: low
- Location: app/src/main/java/dev/geode/playback/PlaybackEngine.kt:134-142; app/src/main/java/dev/geode/audio/dsp/NativeDspProcessor.kt:101-102, :127-135
- Failure scenario: `PlaybackSession.release()` calls `player.release()` and then `dsp.release()`, which runs `dspDestroy` on the live and retired chains and clears `retired`. `ExoPlayer.release()` returns after its default 500 ms release timeout even when the internal playback thread is still inside `doSomeWork` (stuck in an `AudioTrack` write or a slow decoder, which some BT and OEM stacks do). That thread can then run `queueInput`, so `dspProcess(h)` runs on a freed chain, and `ensureChain` touches `retired` while Main clears it (an `ArrayList` touched from two threads).
- Evidence: the class KDoc assumes chains are freed "after the player is gone", but `release()` does not guarantee that on timeout. No `setReleaseTimeoutMs` is set and the timeout result is not checked.
- Suspected root cause: a bounded release is treated as a completed release.
- Suggested fix: when release times out (`onPlayerError` with `ERROR_CODE_TIMEOUT`, or a listener), skip `dsp.release()` or defer it. At minimum, null `handle` before destroying and re-read it in `queueInput` under a lock.

#### Checked and OK
- `VisualizerView` / wallpaper GL teardown: `releaseScenesOnGlThread` waits at most 200 ms. Even on a timeout, `super.onDetachedFromWindow()` → `requestExitAndWait()` blocks Main until the GL thread exits, so no Main-side `NativeViz` call can overlap `vizDestroy`. The worst case is a dropped release event: the handle is reused on re-attach, or leaks if the view is discarded. No use-after-free.
- `NativeViz.create()/destroy()` are `@Synchronized`, and `handle` is `@Volatile`. The setters are any-thread and latched natively (core/api/geode_api.h:30). `configLock` guards the LFO/ADSR/reaction setters.
- `VisualizerRenderer`: every cross-thread field is `@Volatile`, and `LfoEngine/AdsrEngine.configs` are `@Volatile`. `StyleReactionsCache` and `soundCursor` are GL-thread-only. `onSurfaceSizeChanged` hops to Main through `mainScope.launch`. `onShaderError` and `noteMilkPreset` write StateFlows through `update{}` or a plain `value` set, so they are safe from the GL thread.
- `MilkPresetMemory`: `@Volatile` fields, `@Synchronized firstRun`, double-checked `shared()`.
- `NativeTapPump`: `running` is `@Volatile`, and `stop()` joins before `playerDestroy`. Native `Player` commands are queued under `lock_`, state is published through atomics, and the tap ring is SPSC.
- `NativeDspProcessor` setters vs `process`: the native setters use atomics (Equalizer, Gain, Crossfeed, Limiter), and `handle` is `@Volatile`.
- `AudioCapturePump`: start and stop are `@Synchronized`, the generation counter keeps a timed-out worker from writing into the next run's sink, and the worker owns and releases its `AudioRecord`.
- `PcmTap` chunks writes to 4096 frames, under `SampleRing.maxWriteFrames` (16384); the native tap and capture pumps write 2048 and 1024.
- `PlaybackEngine` hold counting is `@Synchronized`, and `AudioBus.soundInput` is `@Volatile`.
- `PlayerSession.shutdown`: pending store jobs are snapshotted after the flushes and drained on `appScope` IO with a budget. No `runBlocking` remains anywhere (grep).
- `ExportController` / `ExportRun`: `exportCancelled`, `loopCancelled` and `cancelRequested` are `@Volatile`. `CancellationException` is rethrown in every export catch (VideoExporter 247/313/347, ExportController 224/286/565, measureLoudness). `encodeInto`, `renderStop` and `drawFrames` are non-suspend, so the EGL context never changes thread.
- `StudioExporter`: the Transformer is built, started, polled and cancelled on Main (`withContext(Dispatchers.Main)`, `cancel()` from Main).
- `PlayBillingRepository`: Main scope, `connectLock` Mutex, and every `suspendCancellableCoroutine` resumes only when `cont.isActive`.
- `PlayerSessionProvider`'s ad-hoc scope is process-lifetime by design (a `@Singleton`). `ExportRun.scope` and `GeodeContainer.appScope` are process-lifetime by design.
- `SessionBitmapLoader.release()` shuts the `geode-art` executor down in `PlaybackService.onDestroy`, after `session.release()`. `LibraryTree.cachedTracks` is `@Volatile` with a synchronized build.
- `HistoryStore`: all entry mutation is under `lock`, with one static writer thread. Other instances (LibraryTree, resumption) only read.
- `VizStateStore.persist` debounce uses an `AtomicBoolean`. `ThermalGovernor` is `@Volatile` and `@Synchronized attach`.
- There is no `GlobalScope`, `callbackFlow` or inline-built flow under `collectAsState`; every `collectAsStateWithLifecycle` source is a stable StateFlow property.
- `MilkImportController`'s `catch (t: Throwable)` blocks wrap only blocking code (there is no suspension point inside), so cancellation is not swallowed.

## Dead code and slop

_Audit still running._

## Architecture and performance

Checks that came back clean, so no entry below:
- Layer rule: `bash tools/layer-gate.sh` passes. Nothing outside `ui/` and `di/` names `dev.geode.ui`.
- Pure packages: `pro/`, `model/` and `editor/` have no `android.*`/`androidx.*` imports and no `org.json`.
- ViewModels: none holds a `Context`, `Activity` or `View`. The exception is ARCH-5, a dead `Player` getter.
- Shader loop caps: all 210 `for` loops in the 231 `app/src/main/assets/shaders/*.glsl` files have compile-time constant bounds of 128 or less, and none uses `while`. That matches the `lib_sdf3` RAYMARCH rule, and `uSteps` is set natively in `core/viz/scenes/ShaderScene.cpp:105-106`.
- Lazy keys: the library track lists use `key = uri` plus `contentType`. The only `items` calls without keys are on small or static lists with no per-item state (search results, EQ presets, lyrics, scene ids, Studio clips). They are not worth reporting.
- `StyleReactionsCache.routesFor` is cached per scene and seed, and `syncNativeState` pushes params, LFO, ADSR and routes only when they change.

---

### ARCH-1 "Large files to split" counts are stale, and two files over 700 lines are missing [PR183/PR184]
- **Status: open: AGENTS.md updated at the end of this PR**
- Severity: low
- Location: AGENTS.md:121 (the "Large files to split" line)
- Failure scenario / impact: the map undercounts the biggest file by 132 lines and leaves out two files over 700 lines. `tools/arch-map-gate.sh` cannot catch this because it only checks that AGENTS.md was touched.
- Evidence (`wc -l`): `ui/VisualsHub.kt` 1676 (listed 1544), `ui/PlayerSession.kt` 1258 (listed 1268), `ui/CustomizeTabs.kt` 1491 (1494), `ui/VisualizerScreen.kt` 851 (849), `ui/AppShell.kt` 840 (841). Not listed although over 700: `ui/TutorialArt.kt` 802, `ui/pro/PaywallScreen.kt` 764. All other counts match.
- Suggested fix: update the line to the current counts and add TutorialArt.kt (802) and pro/PaywallScreen.kt (764).

### ARCH-2 "Engine logic in Kotlin" still lists work that is already native [PR183/PR184]
- **Status: open: AGENTS.md updated at the end of this PR**
- Severity: low
- Location: AGENTS.md:127 (the "Engine logic in Kotlin (ADR 0003)" bullet)
- Failure scenario / impact: the bullet says the scene and style catalogue and `ParamRandomizer` are "Still Kotlin". Both are now native, and `ParamBlend` no longer exists. An agent planning ADR 0003 work could redo finished ports. The same file's Tests bullet already says "Kotlin's `ParamRandomizer` and `ParamBlend` only call it", so the file contradicts itself.
- Evidence: `engine/scenes/.../render/scene/Catalogue.kt:4-12` says "Every fact here is native (core/viz/Catalogue, ADR 0003)" and parses `GeodeNative.catalogueText()`. `ParamRandomizer.kt` is 63 lines that wrap `GeodeNative.paramsRandomize`/`paramsResetTab`. No `ParamBlend.kt` exists anywhere under `engine/` or `app/src/main`.
- Suggested fix: list only the PCM ring (`SampleRing`) and the presentation clock (`AudioPresentationClock`) as still Kotlin, and drop `ParamBlend` from the Tests bullet too.

### ARCH-3 ViewModel count and the "every ViewModel reads PlayerSession" claim are wrong [PR183/PR184]
- **Status: open: AGENTS.md updated at the end of this PR**
- Severity: low
- Location: AGENTS.md:40, :82, :94 ("six ViewModels" / "six `@HiltViewModel`s"); AGENTS.md:125 (God object bullet)
- Failure scenario / impact: ADR 0001 § Consequences and the `PlayerSessionProvider` KDoc reasoning ("all ViewModels live in the Activity's store") rest on a list that no longer matches the code.
- Evidence: seven classes carry `@HiltViewModel`: `BackupViewModel`, `LibraryViewModel`, `PlayerViewModel`, `SettingsViewModel`, `StudioViewModel`, `VisualsViewModel` and `ui/pro/PaywallViewModel`. `BackupViewModel` and `PaywallViewModel` never reference `PlayerSession` (`grep -c PlayerSession` gives 0 for both).
- Suggested fix: say "seven `@HiltViewModel`s", and say that five of them (all except Backup and Paywall) read `PlayerSession`.

### ARCH-4 Data-layer I/O in `ui/` that the "breaks the guide" list does not name
- **Status: open**
- Severity: low
- Location: app/src/main/java/dev/geode/ui/Lyrics.kt:32-75; ui/AppShell.kt:141-160 and :324; ui/PlayerSession.kt:456-468
- Failure scenario / impact:
  - `LyricsLoader` is a whole data source sitting in `ui`. It reads `java.io.File` sidecar `.lrc` files, lists folders through `DocumentFile`, calls `contentResolver.openInputStream` and uses `MediaMetadataRetriever`. When ADR 0001 step 2 moves `data`, it must also move this file or `data` ends up depending on `ui`.
  - The `AppRoot` composable reads `crash-latest.txt` itself and deletes it inside a button `onClick`, which is file I/O on Main. The file name literal is duplicated in `GeodeApp.kt`, so the writer and the reader can drift apart.
  - `StudioViewModel` builds subtitles from `session.lyrics`, so this ui-owned data source feeds two ViewModels.
- Evidence: `Lyrics.kt:46-58` (`java.io.File(...).readText()`, `context.contentResolver.openInputStream(sibling.uri)`); `AppShell.kt:145` and `:324` (`java.io.File(context.filesDir, "crash-latest.txt").delete()` in onClick); `GeodeApp.kt` writes `File(filesDir, "crash-latest.txt")`.
- Suggested fix: move `LyricsLoader` to `data/LyricsRepository` (and `Lyrics` to `model`), add a small `data/CrashReportStore` used by both `GeodeApp` and `AppShell`, and add both to the "Data layer lives in `ui`" bullet.

### ARCH-5 `PlayerViewModel` exposes the Media3 `Player` to the UI, and nothing uses it
- **Status: fixed (Player: collect the style id alone; drop the unused Player getter)**
- Severity: low
- Location: app/src/main/java/dev/geode/ui/PlayerViewModel.kt:50 (`val player: Player get() = session.player`)
- Failure scenario / impact: this is a public route from any composable to the live ExoPlayer or NativePlayer, which skips the session's gates and fades (`PlaybackFades` owns `player.volume`, for example). It also keeps `androidx.media3.common.Player` on the ViewModel's API.
- Evidence: `grep -rnE "(ViewModel|vm|model)\.player\b" app/src` finds no callers in main or androidTest.
- Suggested fix: delete the getter.

### ARCH-6 Two low-risk PlayerSession extractions that remove real coupling
- **Status: open**
- Severity: medium
- Location: ui/PlayerSession.kt:350-357 (`pcmScratch`, `pcmCursor`, `latestPcm()`); ui/EnginePlumbing.kt:53; wallpaper/VisualizerWallpaperService.kt:71; wallpaper/VisualizerDreamService.kt:48; ui/PlayerSession.kt:456-468 (lyrics)
- Failure scenario / impact:
  - (a) The GL thread calls into the UI god object every frame. `VisualizerRenderer.pcmProvider = { viewModel.latestPcm() }` runs `PlayerViewModel` and then `PlayerSession.latestPcm()` on the GL thread, mutating the session field `pcmCursor`.
  - Because only the in-app renderer reaches the session, the wallpaper and the dream pass `pcmProvider = { null }`. Their MilkDrop scenes therefore fall back to the sound frame's short waveform instead of raw PCM (`core/viz/scenes/MilkdropScene.cpp:177-184`), so MilkDrop presets render with a different, thinner waveform there than in the app.
  - The in-app renderer also reads the ring twice per frame: once in `SoundCursor.take()` for `writeAudio`, and again in `ring.copyNewSince` for `pushPcm`.
  - (b) Lyrics loading (`_lyrics`, `loadLyricsFor`) is a self-contained feature with one input (`currentUri`) and one output (`StateFlow<Lyrics?>`).
- Evidence: `EnginePlumbing.kt:53-54` sets `pcmProvider` from the ViewModel while `soundInput` already comes from `AudioBus.soundInput`. `VisualizerRenderer.kt:294` (`pushPcm`) and `:304-310` (`feedSound`) both run inside one frame.
- Suggested fix: move the PCM cursor into the renderer and derive the mono `pushPcm` buffer from `SoundCursor.interleaved`. That gives one ring read per frame, no session on the GL thread, and the same MilkDrop PCM for the wallpaper and dream. Separately, move lyrics into a `LyricsController` backed by a data-layer `LyricsRepository`.

### PERF-1 Disk I/O and native library load on Main in the cold-start path
- **Status: open**
- Severity: medium
- Location: app/src/main/java/dev/geode/GeodeApp.kt (`onCreate`: `MacroStore.get(this)`); data/MacroStore.kt:41-43, :95-113, :126-129; ui/MainActivity.kt:311; di/PlayerSessionProvider.kt:77-83; ui/ListeningTracker.kt:33; data/HistoryStore.kt:27-34; ui/VizStateStore.kt (constructor `restore`); ui/PlayerSession.kt:93-413
- Failure scenario / impact:
  - On every process start, `Application.onCreate` runs `MacroStore.get`. That blocks Main on the first load of the general SharedPreferences XML and parses the macro presets. Its `init { push() }` then calls `GeodeNative.macrosSet`, which runs `System.loadLibrary("geode")` on Main. Process starts for the widget, a media-button resumption, Android Auto or `PlaybackService` all pay this, even when no renderer is ever built.
  - Then, before the first frame, `MainActivity`'s splash condition (`settingsViewModel.userDataLoaded`) and `AppRoot`'s `geodeViewModel()` build the `PlayerSession` on Main. Its constructor runs `PlaybackEngine.acquireForUi` (player build) and `HistoryStore` `init` (`file.readText()` plus JSON parse of the whole listening history, which grows without limit with use). It also runs `VizStateStore.restore` (viz prefs XML load plus JSON parse of the live state), `LfoStore(prefsFiles.general)`, `BookmarkStore(prefsFiles.player)` and about 20 controllers.
  - `GeodeContainer.warmPlaybackPrefs` warms `player` and `audioFx` on IO, but the session reads `player` on Main right away, so the warm-up can lose that race.
  - Cold-start time grows with history size. Nothing measures it: there is no `reportFullyDrawn` and no Macrobenchmark.
- Evidence: `GeodeApp.kt` calls `MacroStore.get(this)` directly in `onCreate`; `MacroStore.kt:113` (`GeodeNative.macrosSet`); `GeodeNative.kt:4-6` (`init { System.loadLibrary("geode") }`); `HistoryStore.kt:27-34` (`init { synchronized(lock) { readLocked() } }` then `file.readText()`); `PlayerSession.kt:93` (`PlaybackEngine.acquireForUi(application)` in the constructor).
- Suggested fix: push the macros when a renderer's `NativeViz.create()` runs instead of in `Application.onCreate`, and make `HistoryStore` and `VizStateStore` load asynchronously (state starts empty or default, then a `StateFlow` emits). Add `reportFullyDrawn` so the change can be measured.

### PERF-2 The hidden shell recomposes on every pointer event while the fullscreen visualizer is pinched [PR183/PR184 for VisualsHub.kt]
- **Status: partly fixed (PlayerScreen); VisualsHub half sent to PR #183's owner**
- Severity: medium
- Location: ui/PlayerScreen.kt:67 and :96; ui/VisualsHub.kt:144, :155, :180; ui/AppShell.kt:265-276; ui/VisualizerScreen.kt:175 and :829; ui/VisualSettingsController.kt:48-65
- Failure scenario / impact:
  - In fullscreen, pinch and twist (`touchTransform`, on by default) call `nudgeTransform` on every pointer event, which calls `setSceneParams`, which emits a new `VizUiState`.
  - The shell stays composed under the fullscreen view: `AppShell` applies `notPlaced()` to it but keeps it in composition.
  - `PlayerScreen` (716 lines) and `VisualsHub` (1676 lines) collect the whole `vizState` at their top level with `by`, yet both use only `viz.sceneId`. So whichever destination sits under the fullscreen view re-runs its whole body at pointer rate (60-120 Hz). The pointer handler runs on Main, so this competes with it and adds touch latency during the gesture.
  - The same happens in Visuals › Customize: every slider drag tick re-runs the `VisualsHub` root body only to read the style name.
- Evidence: `PlayerScreen.kt:67` (`val viz by viewModel.vizState.collectAsStateWithLifecycle()`) has one use, `sceneDisplayLabel(viz.sceneId)` at :96. `VisualsHub.kt:144` uses only `viz.sceneId` (:155, :180). `AppShell.kt:265-267`: "the shell stays composed ... but is not placed".
- Suggested fix: at those two sites, collect a `sceneId`-only flow (`vizState.map { it.sceneId }.distinctUntilChanged()`, or expose `sceneId: StateFlow<String>` from the ViewModel) instead of the whole `VizUiState`.

### PERF-3 Per-frame allocations and string conversions on the GL thread [PR183/PR184 for geode_viz_jni.cpp]
- **Status: owned elsewhere: geode_viz_jni.cpp in PR #183, sent to its session**
- Severity: low
- Location: engine/scenes/.../render/VisualizerRenderer.kt:247-256 and :274-310; engine/scenes/.../render/bridge/NativeViz.kt:81-94, :232, :277-283; app/src/main/cpp/geode_viz_jni.cpp:61-71, :289-291, :304-307; engine/audio-core/.../audio/SoundCursor.kt:37-62; ui/PlayerSession.kt:353-357
- Failure scenario / impact: every frame, even with nothing changing, the GL thread does all of the following:
  - creates a new Java String through `vizLastError` (`env->NewStringUTF(geode_viz_last_error(...))`, usually ""), only to compare it with the last one;
  - converts three unchanged ids to UTF-8 through `GetStringUTFChars` (`vizSetScene`, `vizSetLayer`, `vizSetTransition`);
  - allocates a `SoundCursor.Take` data class and a `PcmChunk`.
  
  That is about 4 heap objects and 3 JNI string copies per frame, or roughly 500-700 per second at 120 Hz. The result is steady GC churn on the thread whose frame budget matters most. Params, LFO, ADSR and routes already use an "only when changed" guard; these calls do not.
- Evidence: `onDrawFrame` calls `nativeViz.setScene(requestedSceneId)` and `nativeViz.pollError(onShaderError)` unconditionally. `syncNativeState` calls `setLayer` and `setTransition` unconditionally. `SoundCursor.take()` returns `Take(count, restarted)`, and `latestPcm()` returns `PcmChunk(pcmScratch, n)`.
- Suggested fix: push scene, layer and transition only when they change (compare references, as for params). Expose an error generation counter (an int) and fetch the string only when it moves. Return the take as a packed `Long` or as mutable fields.

### PERF-4 The baseline profile has a dead rule and misses classes built at startup
- **Status: fixed (Build: fix the dead baseline-profile rule)**
- Severity: low
- Location: app/src/main/baseline-prof.txt:25 (`HSPLdev/geode/ui/ThemeStore**`), and the file as a whole
- Failure scenario / impact:
  - `ThemeStore` moved to `dev.geode.data.ThemeStore` (data/ThemeStore.kt:102), so the rule matches nothing and the GUI-prefs reader on the first frame runs interpreted on a fresh install.
  - Several classes that run on the cold-start path are not covered at all:
    - data stores constructed in `PlayerSession`'s constructor: `HistoryStore`, `SessionStore`, `PresetStore`, `LfoStore`, `BookmarkStore`, `TakeStore`;
    - ui classes that do not end in `Controller` and so miss the `*Controller**` rule: `ListeningTracker`, `VizStateStore`, `AutoVisualsPrefsStore`, `PlaybackFades`;
    - `MacroStore`, which runs in `Application.onCreate`;
    - the engine bridge: `dev/geode/engine/**` (`GeodeNative`, `SampleRing`, `SoundCursor`) and `dev/geode/render/bridge/**` (`NativeViz`, `SceneParamsCodec`, which runs every frame or on every param change).
- Evidence: `find` shows no `ui/ThemeStore.kt`; `data/ThemeStore.kt` exists. The profile has no `dev/geode/data/**` or `dev/geode/engine/**` rule; the only data rules are `GeodePrefsFiles` and `PlayerPrefs**`.
- Suggested fix: change the rule to `dev/geode/data/ThemeStore**` and add HSP rules for `dev/geode/data/**`, `dev/geode/engine/**`, `dev/geode/render/bridge/**`, `ListeningTracker**`, `VizStateStore**` and `AutoVisualsPrefsStore**` (or generate the profile).

### PERF-5 Broad and redundant R8 keep rules, and a stale comment on a rule that is still needed
- **Status: partly fixed: stale SceneParams comment corrected; narrowing the media3/coroutines keeps needs analyzeReleaseR8Config on CI first, open**
- Severity: low
- Location: app/proguard-rules.pro:30 (`-keep class androidx.media3.exoplayer.audio.** { *; }`), :35-38 (coroutines), :17 (`-keep class dev.geode.engine.bridge.GeodeNative { *; }`), :53-60 (the SceneParams comment)
- Failure scenario / impact:
  - The package-wide Media3 keep stops R8 from shrinking or obfuscating all of `exoplayer.audio`: `DefaultAudioSink`, `AudioTrack` helpers, the processors and their inner classes. That costs dex size and R8 optimization in a release whose audio path is mostly the app's own `NativePlayer` and processor chain. The comment's reason (reflective lookup of platform audiofx wrappers) does not fit this package: Media3's reflective loads are for extension renderers in other packages, and Media3 ships consumer rules for them.
  - `-keepclassmembers class kotlinx.coroutines.** { volatile <fields>; }` repeats what coroutines 1.11 already bundles in META-INF/proguard.
  - `-keep class GeodeNative { *; }` also keeps the non-native members, on top of the `-keepclasseswithmembernames` native rule just above it.
  - The SceneParams comment names `VisualizerRenderer.LERPED_FLOATS`, which no longer exists. The rule is still load-bearing: `SceneParamsCodec.FIELD_BY_NAME` (SceneParamsCodec.kt:366-370 [PR183/PR184]) looks up `SceneParams.declaredFields` by name. Someone could delete the rule as dead and break `unpack` in release builds only.
- Evidence: the rules quoted above; `grep LERPED_FLOATS` matches only proguard-rules.pro:53. AGP is 9.3.1 (libs.versions.toml:2), so `:app:analyzeReleaseR8Config` is available to measure this. It was not run, per the no-build rule.
- Suggested fix: run `analyzeReleaseR8Config` and drop or narrow the media3 and coroutines rules. Fix the SceneParams comment to name `SceneParamsCodec.FIELD_BY_NAME` so nobody deletes the rule.

### PERF-6 Codec and catalogue queries on Main during composition
- **Status: open**
- Severity: low
- Location: app/src/main/java/dev/geode/ui/StudioRender.kt:414; export/VideoExporter.kt:40-44; ui/studio/TransitionSheet.kt:40; engine/scenes/.../render/TransitionCatalog.kt:46-58
- Failure scenario / impact:
  - The first composition of Studio › Render runs `ExportCodec.hasEncoder(HEVC)`, which builds `MediaCodecList(REGULAR_CODECS)` and walks every `codecInfo` on Main inside `remember {}`. On first use per process this is a binder round trip to media.codec, which causes a visible hitch when the Render page opens.
  - `TransitionSheet` similarly reads and JSON-parses the transitions asset inside composition the first time it opens.
- Evidence: `val hevcEncoder = remember { ExportCodec.hasEncoder(ExportCodec.HEVC.mimeType) }`; `TransitionCatalog.library()` calls `context.assets.open(ASSET)...readText()` and then `JSONArray(text)` on the calling thread.
- Suggested fix: compute both in `produceState` on `Dispatchers.Default`/`IO`, or cache them in a process-wide lazy that is warmed off-main.

### PERF-7 The tutorial artwork allocates paths and brushes on every animation frame
- **Status: open**
- Severity: low
- Location: app/src/main/java/dev/geode/ui/TutorialArt.kt:114-118 (`Canvas(modifier) { drawArt(art, time()) }`) and drawing helpers at :137-760
- Failure scenario / impact: the tour's infinite animation (`rememberTourClock`) redraws every frame, as intended. Each frame, though, builds new `Path()` objects (:245, :305, :466, :647, :752), new gradient `Brush`es (about 12 sites, many with constant colours), `Stroke`s and the `listOf(...)` colour lists. That is dozens of allocations per frame for as long as the tour dialog is open, and it causes GC pauses on low-end devices during onboarding, which is the first impression.
- Evidence: 29 `Path()`/`*Gradient(`/`Stroke(` sites in TutorialArt.kt, all inside draw code reached from `Canvas { drawArt(...) }`. `PaywallScreen.kt:690-700` already shows the right pattern (`drawWithCache` and `onDrawBehind`).
- Suggested fix: switch `TutorialArtwork` to `Modifier.drawWithCache`. Build the size-dependent brushes and the colour lists once per size, and keep one `Path` per art to `rewind()` each frame.

## Polish: insets, accessibility, copy, empty states

Read-only code audit of `claude/deep-qa` (84f49d82). Edge-to-edge checked against the `edge-to-edge` skill.
Files in open PR #183 are tagged [PR183]; #184 files are tagged [PR184].

#### Status of the previous audits

**Verified fixed in code:** E1 (`adjustResize` in the manifest; `imePadding` after `consumeWindowInsets(pad)` in `AppShell.kt:433-436`; Search uses `safeDrawingPadding`), E2 (rail `safeDrawingPadding` `AppShell.kt:454`; live view `safeDrawing` `VisualizerScreen.kt:275,519`), E3, E5 (`systemGestureExclusion` in `CrystalSlider`), A1 for Settings rows (`LabeledSwitch`/`LabeledCheckbox`) and Customize `CheckRow`, A3, A4, A5 (CTA 7C3AED→BE185D), A7 (tour is a `Dialog`; shell `clearAndSetSemantics` under onboarding), A8, A13 (Outline 74767F), A17, A18 (sort chips scroll), A19 (`CrystalButton` `minimumInteractiveComponentSize`), library "Up/Down" named per track. Both bar styles use `SystemBarStyle.dark(TRANSPARENT)`, which is right for an always-dark UI. The paywall and the tour are `decorFitsSystemWindows = false` dialogs that pad themselves. ModalBottomSheets keep M3's default `safeDrawing` insets (these include the IME). The AlertDialogs that hold text fields scroll.

**Still open from the earlier audits** (re-reported below where my angle covers them): A2 for every slider outside Customize (POL-7), A1 remnants (POL-8), A9 `StudioChip`, A10 `StudioSection`, A11 loop sheet and timeline export status, A14/A16 LockChip [PR183], A20 literal strings and descriptions in `VisualsHub` [PR183], A21 queue rows and the mini player's `onClickLabel` (the 2026-10-30 status table marked A21 fixed, but `AppShell.kt:516` has no label) (POL-14). Outside this angle, still open: D4 `readableWidth` for Settings and presets, D5 collapsing headers, D11 previews, P3 [PR183], P9, P17, P18. Emulator pass #17 and #22 still need a real phone.

---

### POL-1 Playback error banner is never seen while the live view or PiP is up
- **Status: open**
- Severity: medium
- Location(s): app/src/main/java/dev/geode/ui/AppShell.kt:197, :268-276, :797-805, :840; app/src/main/java/dev/geode/ui/VisualizerScreen.kt (does not read `playbackNotice`)
- User impact: A track that is missing or can't be decoded while the user watches fullscreen (or in PiP) gets skipped, or playback stops ("Several tracks in a row are unavailable…") with no explanation. The banner also clears itself before the user comes back.
- Evidence: `PlaybackNoticeBanner` lives only inside `destinationContent`. While `appState.expanded`, the shell is composed but not placed (`Modifier.notPlaced()`), so its `LaunchedEffect` still runs `delay(8_000)` and then `clearPlaybackNotice()`. Nobody sees it. Nothing in `VisualizerScreen` shows the notice.
- Suggested fix: Show the notice in `VisualizerScreen` too, as a chrome-independent toast or banner, and start the 8 s auto-dismiss only while the banner is actually placed.

### POL-2 Library stays on "Music access is off" after the user grants it in Settings
- **Status: duplicate of UI-5**
- Severity: medium
- Location(s): app/src/main/java/dev/geode/ui/LibraryScreen.kt:90-96, :121-150
- User impact: This is the "permission permanently denied" path. The user taps "Open app settings" and turns on Music and audio. On returning, Library still shows the denied text and button until they leave the tab and come back, so the fix looks like it didn't work. After a restart, the first "Allow music access" tap does nothing visible: the system refuses silently, and only then does the screen switch to "Open app settings".
- Evidence: `granted` is `remember { mutableStateOf(checkSelfPermission…) }`, which is never re-read on resume. `asked` is per-composition state. The screen does not use `rememberPermissionRequest` (`PermissionRequest.kt:84-128`), which re-reads the grant in `LifecycleResumeEffect` and tracks PERMANENTLY_DENIED. AGENTS.md › Conventions says to use it.
- Suggested fix: Replace the hand-rolled block with `rememberPermissionRequest(permission) { refreshDeviceTracks() }` and branch on its `status`.

### POL-3 Background renders end silently; the progress notification is thin
- **Status: open**
- Severity: medium
- Location(s): app/src/main/java/dev/geode/export/ExportService.kt:30-41, :64-108; app/src/main/java/dev/geode/ui/ExportController.kt:154, :532; app/src/main/res/values/strings.xml:671
- User impact:
  - A 4K render that finishes or fails while the app is in the background just makes its notification disappear. There is no "Video saved" or "Render failed" notice, so the user has to reopen the app to find out. The permission prompt promises they can "check on it without reopening".
  - The notification has no Cancel action.
  - For a normal library track, its text reads like "1234 · about 2 min": the label is `uri.lastPathSegment` of a `content://media/external/audio/media/1234` URI.
  - It uses the capture service's equaliser icon (`ic_stat_capture`), so the user can't tell "rendering" from "listening to other apps" in the status bar.
- Evidence: `watcher` calls `stopSelf()` as soon as `!state.running`, and no other code posts a notification (only `ExportService` and `PlaybackCaptureService` use `Notification.Builder`). `ExportRun.begin(uri.lastPathSegment…substringAfterLast('/'))`. `.setSmallIcon(R.drawable.ic_stat_capture)`.
- Suggested fix: Post a dismissible done or failed notification (tap opens Studio › Render, plus Share). Add a Cancel action that calls `ExportRun.requestCancel()`. Use the track title for the label, and `ic_stat_muzviz` or a render icon.

### POL-4 "Render failed" shows Java exception class names
- **Status: open**
- Severity: medium
- Location(s): app/src/main/java/dev/geode/ui/ExportController.kt:234-236, :572-574; app/src/main/java/dev/geode/export/LoopRender.kt:368; app/src/main/java/dev/geode/export/StudioExporter.kt:178, :279; app/src/main/res/values/strings.xml:746 (`export_error_no_message` = "no message"); shown by RenderBar.kt:67, LoopRenderSheet.kt:107, studio/TimelineLanes.kt:531
- User impact: An unexpected failure reads "Failed: IllegalStateException: no message" or "Failed: NullPointerException: …". This is developer text with nothing the user can act on.
- Evidence: `val detail = "${t.javaClass.simpleName}: $reason"` goes straight into `ExportPhase.Failed(detail)`. Raw `e.message` / `exception.message` is used as the user message in LoopRender and StudioExporter.
- Suggested fix: Map unknown throwables to `export_error_generic` plus a "Copy details" action (or RingLog). Never put `simpleName` in UI text.

### POL-5 Internal name "Geode" reaches users
- **Status: open**
- Severity: medium
- Location(s):
  - app/src/main/res/values/strings.xml:717, :718, :719, :720, :721 (loudness advice after every export), :754, :758
  - app/src/main/java/dev/geode/playback/PlaybackErrors.kt:54 (`:38` in the same function says "MuzViz")
  - app/src/main/java/dev/geode/ui/VisualsHub.kt:406, :427 [PR183]
  - app/src/main/assets/third_party_notices.txt:81 [PR184] (Settings › About › licences)
  - app/src/main/java/dev/geode/data/TemplateFormat.kt:160 (shared templates arrive as `<name>.geode.json`)
  - the `geode://preset/…` and `geode://template/…` links pasted into chats
- User impact: The loudness report says "Geode will take off 2.1 dB…". A bad file reads "…is not a file Geode can open". Preset import says "That file is not a Geode preset." These are visible branding errors in a store build.
- Evidence: Grep of strings and Kotlin literals. `LoudnessTarget.kt:84` and `LoudnessMeter.kt:311` also say Geode, but neither string is displayed today.
- Suggested fix: Replace with "MuzViz Studio Pro" or "MuzViz", and name shared template files `.muzviz.json`. The URI scheme can stay as long as the share text in front of the link names MuzViz.

### POL-6 Template import and save errors show raw parser diagnostics
- **Status: open**
- Severity: low
- Location(s): app/src/main/java/dev/geode/data/TemplateFormat.kt:173-176; app/src/main/java/dev/geode/data/TemplateRepository.kt:108, :616, :631; app/src/main/java/dev/geode/ui/TemplatesSheet.kt:126, :232-233
- User impact: Opening the wrong file in Templates shows "no 'format': 'geode.template' marker" or "this is not JSON". A failed save shows "could not save Reel_Neon--3fa9.geode.json". The text is lowercase, technical, says "geode", and isn't localisable.
- Evidence: `messageFor()` returns `outcome.why` verbatim, and the save path does the same with `result.why`. Shared links already use the friendly `template_link_unreadable` (`MainActivity.kt:142`).
- Suggested fix: Show string resources such as `template_file_unreadable` and `template_save_failed`, and send `why` to RingLog only.

### POL-7 Sliders outside Customize have no name or spoken value
- **Status: open**
- Severity: medium
- Location(s): app/src/main/java/dev/geode/ui/CrystalControls.kt:166-182 (`CrystalSlider` takes no label). Call sites:
  - AudioSettings.kt:43, :175, :210
  - AutoVisualsSettings.kt:50, :124
  - BackgroundSheet.kt:78, :86, :97
  - BehaviorSettings.kt:84
  - EqualizerSettings.kt:112, :124, :136
  - LookSettings.kt:64, :95, :187
  - PaletteMaker.kt:154, :164
  - PlaybackSettings.kt:56, :67, :82, :193, :246
  - studio/CurveEditor.kt:245
  - StudioScreen.kt:815
  - LoopRenderSheet.kt:214 (bare `Slider`)
  - CustomizeTabs.kt:334, :611, :1365 [PR183]
- User impact: TalkBack reads "50 percent" with no name. For example, an EQ band slider whose range is -1500..1500 mB is announced as a percentage of that range, not "60 Hz, +3 dB". This is the earlier A2 finding, and it is still open everywhere except Customize's `LabeledSlider`.
- Evidence: Only `CustomizeTabs.kt:949-951` sets `contentDescription` and `stateDescription` on a slider. The label `Text` sits beside each other slider as a sibling node.
- Suggested fix: Add `label: String` and `valueText: String` parameters to `CrystalSlider` that set `semantics { contentDescription; stateDescription }`, and pass the text already drawn above each slider.

### POL-8 Switch rows with no name (A1 remnants)
- **Status: open**
- Severity: low
- Location(s): app/src/main/java/dev/geode/ui/LoopRenderSheet.kt:219-222; app/src/main/java/dev/geode/ui/StudioScreen.kt:655-658; app/src/main/java/dev/geode/ui/studio/CurveEditor.kt:215-218
- User impact: TalkBack says "Off, switch" for Time-of-day drift, the clip's Mute and a keyframe toggle, and tapping the label does nothing.
- Evidence: A `Text` beside a `Switch(onCheckedChange = …)` in a plain `Row`.
- Suggested fix: Use `LabeledSwitch` (CrystalControls.kt:190).

### POL-9 A style that fails on the GPU goes black with no message
- **Status: owned elsewhere: VisualsHub in PR #183, sent to its session**
- Severity: medium
- Location(s): `shaderError` is shown only at app/src/main/java/dev/geode/ui/VisualsHub.kt:936 (MilkDrop), :1286 (Fluid tab), :1666 (custom shader editor) [PR183]. The messages are produced at core/viz/fluid/FluidSim.cpp:52, core/viz/fluid/RippleSim.cpp:30, core/viz/scenes/AcidScene.cpp:60/:66, core/viz/scenes/CurlFlowScene.cpp:16 and others.
- User impact: On a phone whose GPU rejects a Silk, Acid, Fluid or Curl Flow shader (emulator pass #3, #4 and #9 were exactly this), the Player, the live view and the style picker show a black canvas. The friendly "X unavailable on this GPU" text exists but is never shown there, so the app looks broken.
- Evidence: `VisualSettingsController.reportShaderError` writes `state.shaderError`. Grep finds only the three Visuals consumers above. `PlayerScreen` and `VisualizerScreen` don't read it.
- Suggested fix: Show `shaderError` as a small notice over the canvas in Player and the live view, with "Pick another style", and mark the failing style in the picker.

### POL-10 Screensaver: a Pro feature the user can't find, which fails silently on Free
- **Status: open**
- Severity: medium
- Location(s): app/src/main/java/dev/geode/wallpaper/VisualizerDreamService.kt:31-36; app/src/main/res/values/strings_pro.xml:23, :49; app/src/main/res/xml/dream_info.xml; app/src/main/java/dev/geode/ui/BehaviorSettings.kt:178-210
- User impact:
  - The paywall sells "Live wallpaper and screensaver", but no Settings entry or tutorial card says where to turn on the screensaver (Android › Display › Screen saver). Buyers won't find it.
  - A Free user who picks "MuzViz Studio Pro" in the system picker and taps Preview gets an instant blank exit with no explanation (`finish()`).
  - The wallpaper button has no PRO badge (a surprise paywall), and does nothing if neither wallpaper intent resolves (`runCatching` swallows it).
- Evidence: The only uses of "screensaver" are the Pro strings and `perf_export_note`. There is no `ACTION_DREAM_SETTINGS` launch. The dream calls `finish()` when the gate refuses it.
- Suggested fix: Add a "Screensaver" row under Settings › Behaviour that opens `Settings.ACTION_DREAM_SETTINGS`, with a PRO badge on it and on the wallpaper button. On Free, have the dream draw one still frame with "MuzViz Studio Pro: unlock to animate", as the wallpaper does, and toast when no wallpaper picker exists.

### POL-11 Missing or dead empty states in Library
- **Status: open**
- Severity: low
- Location(s):
  - app/src/main/java/dev/geode/ui/LibraryScreen.kt:396-418 (`GroupList`: Albums and Artists)
  - app/src/main/java/dev/geode/ui/LibraryScreen.kt:482-484, :488-490, :641-705
  - app/src/main/java/dev/geode/ui/PlayerSession.kt:665
  - app/src/main/java/dev/geode/ui/LibraryViewModel.kt:177-180
- User impact:
  - Albums or Artists with an empty library, or a search with no match, show a blank pane. Tracks has "No music found" and "Nothing matches that".
  - Expanding an empty playlist shows nothing under it, and its Play button silently does nothing.
  - Tapping a track inside an expanded playlist does nothing, though users expect it to start the playlist from that track.
- Evidence: `GroupList` has no `isEmpty()` branch. `playPlaylist` returns early on empty `uris`. `PlaylistTracks` rows have only drag and Up/Down/Remove, and `playPlaylist(startIndex)` is never called with an index.
- Suggested fix: Reuse `library_no_music` / `library_no_results` in `GroupList`. Add "This playlist is empty. Add tracks from any track's ⋮ menu." and disable Play when it's empty. Make playlist rows `clickable { playPlaylist(name, i) }`.

### POL-12 Hard-coded user-facing strings (about 430 lines, none localisable)
- **Status: owned elsewhere for VisualsHub/CustomizeTabs (PR #183); the rest open**
- Severity: low
- Location(s), worst first:
  - app/src/main/java/dev/geode/ui/VisualsHub.kt, about 100 [PR183]: e.g. :261, :270, :406, :424-432, :453, :473 (also a "📁" emoji that TalkBack reads), :493, :521, :555, :590-679, :726 ("Share preset" chooser), :750 (category tabs), :904-964, :1320-1409, :1501-1609, :1653-1673
  - app/src/main/java/dev/geode/ui/CustomizeTabs.kt, about 100 [PR183]: section headers :198-604, :163
  - app/src/main/java/dev/geode/ui/CustomizeMacros.kt, about 19: :226-237
  - engine/scenes/src/main/kotlin/dev/geode/render/scene/ParamKeys.kt, about 93 parameter labels; EventRecipe.kt, about 33; Reactions.kt, about 30; Lfo.kt, about 25; scene/CustomizeTab.kt:21-33 (tab titles)
  - app/src/main/java/dev/geode/playback/PlaybackErrors.kt:31-64 (every playback error)
  - app/src/main/java/dev/geode/data/TemplateRepository.kt:180-451 (built-in template names and notes)
- User impact: Today the app ships English only (`locales_config.xml`), so English users see nothing wrong. The real costs: any translation leaves Customize, Visuals, Events and every playback error in English; TalkBack descriptions like "Apply" and "Remove" can't name the preset; and US/UK wording drifts (see POL-13).
- Evidence: Grep of `Text("…")`, `contentDescription = "…"` and literal sentences. There are 89 `Text("…")` calls (VisualsHub 55, CustomizeTabs 29, CustomizeMacros 5) and 4 literal content descriptions.
- Suggested fix: One l10n pass: move the literals into `strings.xml`, give the engine labels `@StringRes` ids (or keys resolved in `:app`), and run `PlaybackErrors` through `getString`.

### POL-13 Copy: plurals, mixed spelling, odd spacing
- **Status: open**
- Severity: low
- Location(s):
  - app/src/main/res/values/strings.xml:859 `editor_tap_count` "%1$d taps" (studio/EditorToolbar.kt:100 shows "1 taps")
  - strings.xml:371 `playlist_import_summary` "track(s)"
  - strings.xml:600 `render_batch_label` "Rendering %1$d tracks" ("Rendering 1 tracks" for a one-track playlist or chosen set)
  - strings.xml:768-769 "Font color" / "Recolors" against "colour" in 7 other strings and the "Colour" tab (engine/scenes/.../CustomizeTab.kt:25)
  - strings.xml:554 "analyzer" and "analysed" in the same sentence
  - strings.xml:546-551 double spaces mid-sentence ("Note start needs  1.5× the usual change", "Silence below  -60 dB")
- User impact: Small grammar slips and inconsistent spelling in a paid app.
- Evidence: As quoted above.
- Suggested fix: Make the first three `<plurals>`, pick UK or US spelling throughout (the strings lean UK), and keep the double space only between a label and its value.

### POL-14 Accessibility remnants: unlabelled clickables, colour-only selection, unnamed row actions
- **Status: open**
- Severity: low
- Location(s):
  - app/src/main/java/dev/geode/ui/AppShell.kt:516: the mini player panel has `clickable(onClick = onExpand)` with no role or `onClickLabel`. A21 was marked fixed, but it isn't.
  - app/src/main/java/dev/geode/ui/PlayerScreen.kt:229 (hero card) and :489 (Up next rows): same.
  - app/src/main/java/dev/geode/ui/StudioScreen.kt:820-845 `StudioChip`: no `selectable`/`selected` (selection shown by colour only), and about 30 dp tall with no `minimumInteractiveComponentSize`. Same at `StudioScreen.kt:780-797`: `StudioSection` titles aren't headings (A9/A10 remnants).
  - app/src/main/java/dev/geode/ui/LookSettings.kt:150-160 font-colour swatches: no `selectable(role = RadioButton)`, so the selection is colour/border only; the target is about 34 dp wide; and an unusable colour is only dimmed (alpha 0.35) with no stated reason.
  - app/src/main/java/dev/geode/ui/PlayerPanels.kt:341-348: queue "Move up" and "Remove from queue" don't name the track (Library rows do).
  - app/src/main/res/values/strings.xml:906 `editor_transition_current` "%1$s ✓" (TalkBack reads "check mark"; studio/TransitionSheet.kt:59).
  - app/src/main/java/dev/geode/ui/LoopRenderSheet.kt:105-109 and studio/TimelineLanes.kt:531: failed render text has no `liveRegion` (A11 remnant; RenderBar already has one).
  - app/src/main/java/dev/geode/ui/VisualsHub.kt:493, :521, :541, :1525: "Apply" / "Remove" / "Delete" with no preset name [PR183]. CustomizeTabs.kt:156: LockChip text at alpha 0.45 (3.75:1) [PR183].
- User impact: TalkBack users hear "double-tap to activate" with no target. They can't tell which Studio look, ratio or quality, or which text colour, is chosen. A run of identical "Move up" buttons tells them nothing.
- Evidence: As listed.
- Suggested fix: Add `onClickLabel` and `role` on the expand surfaces, `selectable(selected, role = …)` on the chips and swatches, `semantics { heading() }` in `StudioSection`, the track name in queue action descriptions, and `liveRegion = Polite` on the two failed-render texts.

### POL-15 Fixed-height text clips at large font scale
- **Status: open**
- Severity: low
- Location(s): app/src/main/java/dev/geode/ui/CustomizeMacros.kt:228-234
- User impact: The macro knob's hint (`labelSmall`, centred, up to two lines) sits in `Modifier.height(32.dp)`. At 200% font size it shows less than one line, cut mid-word.
- Evidence: `Text(hint, …, modifier = Modifier.height(32.dp))`.
- Suggested fix: Use `heightIn(min = 32.dp)` (or `minLines = 2`) so the knobs still align and the text can grow.

### POL-16 Paywall close button can sit under a landscape display cutout
- **Status: open**
- Severity: low
- Location(s): app/src/main/java/dev/geode/ui/pro/PaywallScreen.kt:211-218
- User impact: In landscape with the camera cutout on the right edge, the close "X" (TopEnd, `systemBarsPadding().padding(8.dp)`) can be partly under the cutout and hard to hit. It is the only way out besides Back. Needs a device check.
- Evidence: The paywall `Dialog` is `decorFitsSystemWindows = false`, and the close button pads for system bars only, not `displayCutout`. The rest of the app moved to `safeDrawing` for E2.
- Suggested fix: Use `.windowInsetsPadding(WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.End))` on the close button.

### POL-17 Android Auto lists untitled albums and artists as blank rows
- **Status: open**
- Severity: low
- Location(s): app/src/main/java/dev/geode/playback/LibraryTree.kt:110-115
- User impact: Tracks with no album or artist tag make a folder with an empty title in Android Auto's browse list. In the app the same group reads "(no name)" (`library_group_unnamed`).
- Evidence: `names.distinct()…map { folder("$kind/$it", it, mediaType) }` passes `""` as the title.
- Suggested fix: Pass `it.ifBlank { context.getString(R.string.library_group_unnamed) }` as the display title, and keep the id as is.

### POL-18 Licence notice says projectM is unmodified, but the build patches it
- **Status: owned elsewhere: third_party_notices.txt in PR #184, sent to its session**
- Severity: low
- Location(s): app/src/main/assets/third_party_notices.txt:81-82 [PR184]; tools/projectm-v4.1.7-render-fbo-backport.patch (applied at configure time, per AGENTS.md)
- User impact: Settings › About › licences says "Geode links it dynamically … and does not modify it". The bundled libprojectM is built with a backport patch. That is wrong for an LGPL-2.1 notice (§2a asks for modified files to carry a notice of the change), and it uses the internal name.
- Evidence: AGENTS.md: "projectM gets `tools/projectm-v4.1.7-render-fbo-backport.patch` at configure time."
- Suggested fix: Say "MuzViz Studio Pro links it dynamically; this build applies a small render-to-FBO backport (source of the change: …)". Have legal check the wording.

