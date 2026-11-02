# Play Console checklist: from nothing to production

Every step is yours to do in your own browser and accounts; nothing here needs anyone
else to hold your credentials. Tick them in order: later steps depend on earlier ones.
App: **MuzViz Studio Pro**, package **`com.muzviz.studiopro`**, versionName `1.8.1.N`,
versionCode `33 + N` (N = the release number, see step 6).

## Start the closed test now (decided 2026-11-02)

The 14-day clock for production access (step 9) runs while wave 1 is built, so start it with
today's build instead of waiting for the release candidate. Shortest path, in order:

1. Step 1 (account, payments profile, verification, trader status) and step 2 (create the app).
2. Steps 3 and 4: the upload key, the four `MUZVIZ_UPLOAD_*` secrets and `PLAY_LICENSE_KEY`.
   Without them the bundle is unsigned and Play refuses it.
3. Step 5: create and activate `muzviz_pro`, and add the testers as licence testers.
4. Steps 7 and 8: Play won't roll out to a closed track until the dashboard's setup tasks
   (privacy policy, app access, ads, content rating, target audience, Data safety, store
   listing) are done. Today's build is offline, so [DATA_SAFETY.md](DATA_SAFETY.md) applies
   as written. Screenshots of today's UI are fine; replace them at the release candidate.
5. Step 6, then step 9's first item: run **Play release bundle**, upload the AAB to a
   **Closed testing** track, add at least 12 testers and keep them opted in for 14 days.
   Ask them to open the app on several days: production access asks how the test went.
6. Ship wave 1 merges to the same track (a higher versionCode each time). **Before the first
   build with Cast reaches the track, update Data safety and the privacy policy**: Cast adds
   internet ([RELEASE_READINESS.md § Wave 1](RELEASE_READINESS.md#wave-1-changes-to-re-check)).

## 1. Developer account (one-time)

- [ ] Create a Google Play developer account at play.google.com/console (USD 25 one-time),
      complete identity verification, and verify the contact phone and email.
- [ ] **Personal accounts:** new personal accounts must run a **closed test with at least
      12 testers opted in for 14 consecutive days** before they can apply for production
      access (step 9). Start recruiting testers now. Organisation accounts are exempt.
- [ ] Set up a **payments profile** (Monetize with Play > Monetization setup / Payments
      profile). You need it before you can create the paid product in step 5.
- [ ] **Android developer verification** (Play Console > Android developer verification):
      confirm the account's identity shows as verified and register the package
      `com.muzviz.studiopro` with its upload-key and app-signing certificates. From 2026,
      certified devices in Brazil, Indonesia, Singapore and Thailand (more countries
      through 2027) refuse apps from unregistered developers. The Play build is covered by
      this; the debug APKs on GitHub releases are sideloaded, so register their package
      and the committed `app/debug.keystore` certificate too, or stop sharing them there.
- [ ] **Trader status** (Settings > Developer account > Account details): declare that
      you are a **trader** under the EU Digital Services Act (you sell `muzviz_pro`) and
      verify the address, phone and email Play will show on the listing. From November
      2026 apps without a declaration are removed from EU store listings.
- [ ] **Japan** (only if Japan is in the countries list in step 9): accept the updated
      Japan terms and authorise Google as seller of record for Japan by **2026-12-01**
      (Mobile Software Competition Act). If you skip it, remove Japan from the countries
      or Play removes it for you.

## 2. Create the app

- [ ] Home > **Create app**: name `MuzViz Studio Pro`, default language English (United
      States), **App** (not game), **Free** (it's free to download, with an in-app
      purchase; a paid app can't later become free), and accept the declarations.

## 3. Upload key and GitHub secrets

Play App Signing is on by default for new apps: Google holds the app signing key, and you
sign uploads with an **upload key** you create and keep.

- [ ] Create the upload key on your own machine (keep the file and passwords in a password
      manager; losing them means a key reset request to Google):

      keytool -genkeypair -v -keystore muzviz-upload.jks -alias muzviz -keyalg RSA -keysize 4096 -validity 10000

- [ ] In GitHub: repository **Settings > Secrets and variables > Actions > Secrets**, add:

      | Secret | Value |
      |---|---|
      | `MUZVIZ_UPLOAD_KEYSTORE_BASE64` | `base64 -w0 muzviz-upload.jks` (macOS: `base64 -i muzviz-upload.jks`) |
      | `MUZVIZ_UPLOAD_STORE_PASSWORD` | keystore password |
      | `MUZVIZ_UPLOAD_KEY_ALIAS` | `muzviz` (or the alias you chose) |
      | `MUZVIZ_UPLOAD_KEY_PASSWORD` | key password |

- [ ] For local signed builds instead, copy `keystore.properties.example` to
      `keystore.properties` (git-ignored) and fill it in.

## 4. Licence key (for purchase verification)

- [ ] Play Console > the app > **Monetize with Play > Monetization setup > Licensing**:
      copy the Base64 RSA public key.
- [ ] GitHub > Settings > Secrets and variables > Actions > **Variables**: add
      `PLAY_LICENSE_KEY` with that value. It's a public key, so a variable is right; a
      secret works too if you prefer, but the workflow reads `vars.PLAY_LICENSE_KEY`.

## 5. The Pro product

- [ ] Monetize with Play > Products > **One-time products > Create**: product ID
      **`muzviz_pro`**, price USD 9.99, everything else as in [BILLING.md](BILLING.md).
      **Activate** it.
- [ ] Settings > **License testing**: add your Google account and your testers' accounts.

## 6. Build and upload

- [ ] Actions tab > **Play release bundle** > Run workflow, on the commit you're releasing.
      It runs only on request (not on pull requests or tags). The workflow
      (`.github/workflows/release.yml`) builds the signed AAB, checks it (no INTERNET
      permission, targetSdk ≥ 36, R8 mapping, baseline profile, signature) and uploads it as
      the `muzviz-release-<n>` artifact. Without the upload key it builds unsigned with a
      warning; with the key but no `PLAY_LICENSE_KEY` it fails.
- [ ] Download `app-release.aab` (and `mapping.txt`) from the run.
- [ ] Testing > **Internal testing** > Create release > upload the AAB. The R8 mapping is
      embedded in the bundle by AGP; if Play still asks, upload `mapping.txt` under
      App bundle explorer > Downloads > ReTrace mapping file.
- [ ] Add yourself as an internal tester, install from the opt-in link, and run the
      billing checks in [BILLING.md § Testing](BILLING.md#testing-before-release).
- [ ] Every later upload needs a higher `versionCode`. It is `geode.versionCodeBase` in
      `gradle.properties` plus the release number (`-PreleaseNumber=N`).

## 7. App content (Policy > App content)

- [ ] **Privacy policy**: paste
      `https://tessie1993.github.io/geode-app/privacy-policy.html`. It is published by
      `.github/workflows/pages.yml` from `docs/privacy-policy.html` on every push to
      `claude/geode-finish` or `main`. One-time setup: GitHub > Settings > Pages >
      Source = **GitHub Actions** (and, to deploy from `main`, add it under Settings >
      Environments > github-pages > Deployment branches). Open the URL before pasting it.
      It must be a live HTML page (not a PDF, not editable by others) and name the app.
      The app also shows the same text offline (Settings > About > Privacy policy) and the
      store description points there, but Play still requires this URL: the in-app copy
      doesn't replace it.
- [ ] **Ads**: No.
- [ ] **App access**: "All functionality is available without special access". There's no
      login. Pro is a purchase, and the free 1-hour trial opens every Pro feature, which
      reviewers can use. Optional note to reviewers: *"Settings > Go Pro > Try everything
      free for 1 hour unlocks all Pro features."*
- [ ] **Content rating**: answer per [CONTENT_RATING.md](CONTENT_RATING.md).
- [ ] **Target audience**: 13+ only, per [CONTENT_RATING.md](CONTENT_RATING.md).
- [ ] **Data safety**: answer per [DATA_SAFETY.md](DATA_SAFETY.md).
- [ ] **Advertising ID**: "No, my app does not use advertising ID" (none of the app's code or
      libraries reads it; the release workflow fails if the AD_ID permission appears).
- [ ] **Government apps / Financial features / Health / News**: No to each.
- [ ] **Foreground service permissions** (required for targetSdk 34+): declare each type
      with a short description and a video link (an unlisted YouTube screen recording,
      10–30 s each, showing the feature start and its notification):
      | Type | Feature | What to say |
      |---|---|---|
      | `mediaPlayback` | Music player | Plays the user's local music while the app is in the background, with the media notification |
      | `mediaProjection` | Visualize other apps | Captures audio another app is playing (with the system consent dialog) to animate the visuals; nothing is recorded or stored |
      | `mediaProcessing` | Video export (Android 15+) | Renders a music video (can take tens of minutes at 4K) while the user leaves the app |
      | `dataSync` | Video export (Android 14 and below) | The same render on versions without `mediaProcessing` |
      `dataSync` stays declared (decided 2026-11-01): Android 14 requires a type for every
      foreground service and `mediaProcessing` only exists from Android 15, so dropping it
      would let Android 14 kill a background render. The service picks `mediaProcessing`
      on 15+.
- [ ] **Photosensitivity**: no console form, but keep the warning text from
      [CONTENT_RATING.md](CONTENT_RATING.md) in the listing.

- [ ] **Android Auto** (the listing promises it): Release > Advanced settings > **Form
      factors** > add Android Auto and accept its terms. Auto apps go through a separate
      Auto quality review on every release; it checks the browse tree, voice search
      ("play X on MuzViz", handled by `MEDIA_PLAY_FROM_SEARCH`) and that nothing visual
      plays on the car screen. Until it's approved the app doesn't appear in Auto. If you'd
      rather ship without that review, remove "Android Auto" from the full description in
      [STORE_LISTING.md](STORE_LISTING.md) and skip this step.

## 8. Store listing

- [ ] Grow users > Store presence > **Main store listing**: text from
      [STORE_LISTING.md](STORE_LISTING.md), graphics per [GRAPHICS.md](GRAPHICS.md).
- [ ] **Store settings**: category Music & Audio, tags, contact email.

## 9. Closed test, then production

- [ ] Testing > **Closed testing** > create a track, add at least 12 testers (an email
      list or a Google Group), roll out the same AAB, and keep it running for 14 days with
      them opted in (personal accounts).
- [ ] Dashboard > **Apply for production access** and answer the questions about the test.
- [ ] Production > Create release > the AAB > **staged rollout** (start at 20%), and
      watch Android vitals (crash and ANR rate) before going to 100%.
- [ ] **Memory** (from the closed test on): watch Android vitals for low-memory kills and
      out-of-memory crashes. Play's technical-quality bar adds an excessive-memory
      threshold in **February 2027**; apps above it get a listing warning.

## Release-day checks

- [ ] The paywall shows the real price from Play (not "Getting the price…" forever: that
      means the product isn't active or the app isn't installed from Play).
- [ ] A purchase with a test card unlocks Pro and survives an app restart in airplane mode.
- [ ] Restore works on a second device.
- [ ] A free export stops at 1 minute and carries the full-frame mark; a Pro export runs the
      whole track and is clean.
