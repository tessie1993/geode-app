# Play Console checklist: from nothing to production

Every step is yours to do in your own browser and accounts; nothing here needs anyone
else to hold your credentials. Tick them in order: later steps depend on earlier ones.
App: **MuzViz Studio Pro**, package **`com.muzviz.studiopro`**, versionCode 33 / 1.8.1.

## 1. Developer account (one-time)

- [ ] Create a Google Play developer account at play.google.com/console (USD 25 one-time),
      complete identity verification, and verify the contact phone and email.
- [ ] **Personal accounts:** new personal accounts must run a **closed test with at least
      12 testers opted in for 14 consecutive days** before they can apply for production
      access (step 9). Start recruiting testers now. Organisation accounts are exempt.
- [ ] Set up a **payments profile** (Monetize with Play > Monetization setup / Payments
      profile). You need it before you can create the paid product in step 5.

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

- [ ] Push a tag: `git tag v1.8.1 && git push origin v1.8.1`. The **Play release bundle**
      workflow (`.github/workflows/release.yml`) builds the signed AAB, checks it (no
      INTERNET permission, targetSdk ≥ 36, R8 mapping, baseline profile, signature) and
      uploads it as the `muzviz-release-<n>` artifact. You can also run it by hand from the
      Actions tab (workflow_dispatch). Pull-request builds of the same workflow are always
      unsigned: the upload key is only handed to tag and manual runs.
- [ ] Download `app-release.aab` (and `mapping.txt`) from the run.
- [ ] Testing > **Internal testing** > Create release > upload the AAB. The R8 mapping is
      embedded in the bundle by AGP; if Play still asks, upload `mapping.txt` under
      App bundle explorer > Downloads > ReTrace mapping file.
- [ ] Add yourself as an internal tester, install from the opt-in link, and run the
      billing checks in [BILLING.md § Testing](BILLING.md#testing-before-release).
- [ ] Every later upload needs a higher `versionCode` (`app/build.gradle.kts`).

## 7. App content (Policy > App content)

- [ ] **Privacy policy**: host `docs/privacy-policy.html` (or `.md`) at a public URL and
      paste it. Options, all free:
      - **GitHub Pages from this repo** (it's public): Settings > Pages > Deploy from
        branch `main`, folder `/docs`. URL:
        `https://tessie1993.github.io/geode-app/privacy-policy.html`.
      - **Google Sites**: paste the text of `docs/privacy-policy.md` into a new site and
        publish it.
      - **A public GitHub Gist** of `docs/privacy-policy.md`.
      - Your own domain.
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

## Release-day checks

- [ ] The paywall shows the real price from Play (not "Getting the price…" forever: that
      means the product isn't active or the app isn't installed from Play).
- [ ] A purchase with a test card unlocks Pro and survives an app restart in airplane mode.
- [ ] Restore works on a second device.
- [ ] A free export stops at 1 minute and carries the full-frame mark; a Pro export runs the
      whole track and is clean.
