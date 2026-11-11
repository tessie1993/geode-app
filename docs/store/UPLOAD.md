# Getting BeatRealms 3D onto Google Play

The owner's steps, in order. Package `com.beatrealms3d.app` (permanent once uploaded), one
in-app product `beatrealms3d_pro`. What to type into each form is in the other files here;
[README.md](README.md) maps them.

## 1. Before you start

- [ ] Fill the placeholder `CONTACT_EMAIL` in `docs/privacy-policy.md`,
      `app/src/main/assets/privacy_policy.md` (identical to the first; a unit test checks) and
      `docs/privacy-policy.html`, in one PR, and merge it.
- [ ] Turn on GitHub Pages (repository Settings › Pages › Source: GitHub Actions). `pages.yml`
      publishes on the next push to `main`. Open the privacy policy URL from the header of
      `.github/workflows/pages.yml` and check it shows the new text. (Pages on a private
      repository needs a paid GitHub plan; otherwise host the same HTML anywhere public.)
- [ ] A Google Play developer account (play.google.com/console, one-time fee, identity
      verification). Note whether it is a **personal** or an **organisation** account: it
      decides step 6.

## 2. Make the upload key (on your own computer, once)

```bash
keytool -genkeypair -v -keystore beatrealms-upload.jks -alias upload \
  -keyalg RSA -keysize 4096 -validity 10000
```

It asks for a keystore password, a key password and a name. Keep the `.jks` and both
passwords in a password manager and a second safe place. **Never commit the file or the
passwords**, never paste them in an issue or chat. If the upload key is lost, Play can reset it
(Play App Signing holds the real signing key), but it takes a support request.

## 3. Create the app in Play Console

- [ ] Home › Create app: name "BeatRealms 3D Music Visualizer" (`fastlane/.../title.txt`),
      default language English (United States), App, Free (Pro is an in-app purchase), accept
      the declarations.
- [ ] Monetize with Play › Monetization setup › Licensing: copy the **Base64 RSA public key**.
      The build needs it (step 4) to check purchase signatures.

## 4. Repository secrets and variable, then the bundle

The names are the ones `.github/workflows/release.yml` reads. They keep the old `MUZVIZ_`
prefix on purpose: the old product name stays in secret names.

```bash
base64 -w0 beatrealms-upload.jks > upload.b64        # macOS: base64 -i beatrealms-upload.jks > upload.b64
gh secret set MUZVIZ_UPLOAD_KEYSTORE_BASE64 < upload.b64
gh secret set MUZVIZ_UPLOAD_STORE_PASSWORD           # prompts; paste the keystore password
gh secret set MUZVIZ_UPLOAD_KEY_ALIAS --body upload
gh secret set MUZVIZ_UPLOAD_KEY_PASSWORD             # prompts; paste the key password
gh variable set PLAY_LICENSE_KEY --body '<the Base64 key from step 3>'
rm upload.b64
```

(Or in the browser: Settings › Secrets and variables › Actions; the four are **Secrets**,
`PLAY_LICENSE_KEY` is a **Variable**.)

- [ ] Build: Actions › **Play release bundle** › Run workflow on `main`, or
      `gh workflow run release.yml --ref main`. It only builds a commit that has shipped as a
      pre-release (`ship-apk.yml` runs on every push to `main`) and whose checks are green.
- [ ] Download the artifact `beatrealms-3d-release-<run>`: `app-release.aab` and `mapping.txt`
      (`gh run download <run id>`).

## 5. Internal testing (first upload)

- [ ] Test and release › Testing › Internal testing › Create new release. Accept **Play App
      Signing** (Google holds the app signing key; your key is the upload key). Upload the AAB,
      paste the notes from `fastlane/metadata/android/en-US/changelogs/default.txt`, and upload
      `mapping.txt` under the bundle's App bundle explorer › Downloads, so crash traces read.
- [ ] Add testers (an email list), roll out, install from the opt-in link.
- [ ] Monetize with Play › Products › One-time products (needs a bundle with the billing
      permission uploaded, which this is): create product ID **`beatrealms3d_pro`**, name
      "BeatRealms 3D Pro", a description, a buy option at your price, then **Activate**.
- [ ] Settings › License testing: add your testers' Google accounts, so they can buy Pro
      without being charged. Check that buying, restoring after reinstall and the one-hour
      trial all work.

## 6. Closed testing

- [ ] Testing › Closed testing › create a track, promote the same release, add testers.
- [ ] **Personal accounts created after 13 November 2023:** Play requires a closed test with
      **at least 12 testers opted in for at least 14 days in a row** before you can apply for
      production; a tester who leaves restarts their count
      ([answer/14151465](https://support.google.com/googleplay/android-developer/answer/14151465),
      read 2026-11-11). Then Dashboard › Apply for production and answer the questions about
      the test. Organisation accounts skip this rule (a closed test is still wise).

## 7. Fill the store page and App content (while the test runs)

| Console page | Fill from |
|---|---|
| Grow users › Store presence › Main store listing: name, short and full description | `fastlane/metadata/android/en-US/title.txt`, `short_description.txt`, `full_description.txt` |
| Same page › Graphics: icon, feature graphic (with alt text), screenshots | `docs/brand/beatrealms-icon-512.png`, `docs/brand/beatrealms-feature-graphic-1024x500.png`, [screenshots.md](screenshots.md) (run the **Store assets** workflow for the screenshots) |
| Store settings: category, tags, contact email, website | [category-tags.md](category-tags.md) |
| App content › Privacy policy | the Pages URL checked in step 1 |
| App content › App access, Ads, Content rating, Target audience, News, Health apps, Government, Financial, Advertising ID | [content-rating.md](content-rating.md) |
| App content › Data safety | [data-safety.md](data-safety.md) |
| App content › Foreground service permissions (one video per type, unlisted on YouTube) | [permissions.md](permissions.md) |
| Monetize › the Pro product's price and countries | yours |

Before pasting, run `bash tools/store-check.sh` and `bash tools/store-stats.sh`.

## 8. Production

- [ ] Once production access is granted: Production › Create new release, promote the tested
      release (or a newer bundle from step 4), choose countries, roll out. A first review can
      take several days; the Target audience form alone may take up to 7.
- [ ] Every later release: a new bundle from step 4 (its version code comes from the release
      number, so it is always higher), new notes in `changelogs/`, and re-check
      [permissions.md](permissions.md) if the manifest changed.
