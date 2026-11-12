# The Play Store page: what to paste where

Everything the Play Console asks for when the app is set up, kept in the repo so it can be
redone when the app changes. The text was written against the code on `origin/main`; every
number comes from `tools/store-stats.sh`.

Two folders, on purpose:

- `fastlane/metadata/android/en-US/` holds the text that is pasted as it is, one file per field.
  It is the layout upload tools read (fastlane supply, Gradle Play Publisher), so a later
  automated upload needs no move, and one file per field lets `tools/store-check.sh` count
  each against Play's limit.
- `docs/store/` holds what a person reads: answers to the forms with their evidence, and how
  the pictures are made.

Before pasting, run `bash tools/store-check.sh` (lengths, banned words, emoji, capitals, the old
name) and `bash tools/store-stats.sh` (the counts; if one differs from the listing, fix the
listing).

**The owner's step-by-step to get on Play is [UPLOAD.md](UPLOAD.md).** This file is the map of
what goes in which form.

## Google Play's rules, as read on 2026-11-11

| Rule | Page |
|---|---|
| Title 30, short description 80, full description 4000 characters; contact email required | [answer/9859152](https://support.google.com/googleplay/android-developer/answer/9859152) |
| Metadata policy: no emoji or shouting capitals, no ranking, award, price or "new" claims, no repeated or unrelated keywords, no anonymous testimonials, accurate description, general-audience imagery | [answer/9898842](https://support.google.com/googleplay/android-developer/answer/9898842) |
| Icon 512 x 512 32-bit PNG ≤ 1024 KB; feature graphic 1024 x 500 no alpha; screenshots, alt text | [answer/9866151](https://support.google.com/googleplay/android-developer/answer/9866151) |
| Data safety: what counts as collected, SDK data counts, privacy policy required for every app | [answer/10787469](https://support.google.com/googleplay/android-developer/answer/10787469) |
| Google Cast SDK's own data disclosure | [developers.google.com/cast/docs/android_sender/data_disclosure](https://developers.google.com/cast/docs/android_sender/data_disclosure) |
| Content rating (IARC) | [answer/9859655](https://support.google.com/googleplay/android-developer/answer/9859655) |
| Target audience and content; Families policy | [answer/9867159](https://support.google.com/googleplay/android-developer/answer/9867159), [answer/9893335](https://support.google.com/googleplay/android-developer/answer/9893335) |
| Foreground service declaration: description, impact, use case, video per type | [answer/13392821](https://support.google.com/googleplay/android-developer/answer/13392821) |
| Health apps declaration, required of every app | [answer/14738291](https://support.google.com/googleplay/android-developer/answer/14738291) |
| New personal accounts: closed test with 12 testers opted in for 14 days in a row before production | [answer/14151465](https://support.google.com/googleplay/android-developer/answer/14151465) |
| Category and tags | [answer/9859673](https://support.google.com/googleplay/android-developer/answer/9859673) |
| Release notes 500 characters | [answer/9859348](https://support.google.com/googleplay/android-developer/answer/9859348) |

## Steps in the Play Console

| # | Where in the Console | What to do | From |
|---|---|---|---|
| 1 | Create app | Name "BeatRealms 3D Music Visualizer", default language English (US), App, Free | `fastlane/metadata/android/en-US/title.txt` |
| 2 | Grow › Store presence › Main store listing | Paste App name, Short description, Full description | `title.txt`, `short_description.txt`, `full_description.txt` |
| 3 | Same page › Graphics | App icon `docs/brand/beatrealms-icon-512.png`, feature graphic `docs/brand/beatrealms-feature-graphic-1024x500.png`, phone screenshots (8), 7-inch tablet (8), 10-inch tablet (8), each with its alt text | [screenshots.md](screenshots.md); screenshots from the **Store assets** workflow's artifact |
| 4 | Same page › Video | Paste the YouTube URL | [promo-video.md](promo-video.md) |
| 5 | Store settings | Category Music & Audio, tags, contact email, website | [category-tags.md](category-tags.md) |
| 6 | Policy › App content › Privacy policy | The public URL of `docs/privacy-policy.html` | [data-safety.md](data-safety.md) › Privacy policy |
| 7 | App content › Data safety | Answer as written | [data-safety.md](data-safety.md) |
| 8 | App content › Content rating | Answer the questionnaire | [content-rating.md](content-rating.md) |
| 9 | App content › Target audience, Ads, App access, News, Health apps, Government, Financial | Target age: 13-15, 16-17, 18 and over. The rest as written. See "A 13+ app" below | [content-rating.md](content-rating.md) |
| 10 | App content › Foreground service permissions (appears once a bundle is uploaded) | One description and one video per service type: media playback, microphone, media projection, media processing, data sync | [permissions.md](permissions.md) |
| 11 | Monetize › Products › In-app products | Create the one-time product with id `beatrealms3d_pro`, set its price, activate it | owner |
| 12 | Release › Production (or a test track first) | Upload the AAB from `release.yml`, paste the release notes | `fastlane/metadata/android/en-US/changelogs/default.txt` |

The product id is `beatrealms3d_pro`, as in the code
(`BillingRepository.PRO_PRODUCT_ID`); a buyer never sees it.

## A 13+ app: what Play still asks

From "Target audience and content"
([answer/9867159](https://support.google.com/googleplay/android-developer/answer/9867159)) and
the Families policy ([answer/9893335](https://support.google.com/googleplay/android-developer/answer/9893335)),
read 2026-11-11:

- **No neutral age screen.** Play requires one only when the target ages include children
  and the app shows ads. Neither is true here.
- **The listing must not look made for children.** If the app is not for children but the
  listing has "marketing elements that suggest otherwise" (the page names youthful animation
  and young characters), Play "may reject your app"; the ways out are to remove those elements
  or to target children and meet the Families policy. The listing text and the planned
  pictures are abstract visuals and app screens, with nothing of that kind. If the form asks
  whether the listing could appeal to children, the answer is no.
- **Tick only what the app is designed for.** Do not add a band under 13 "to be safe": any
  band with children brings the Families policy back.
- **13-15 and 16-17 can still be "children" in some countries.** The page says a developer
  targeting anyone under 21 "must consider if they would be considered children under their
  local laws". That is a question for the owner's counsel, not a Console field. The app helps
  its own case: no ads, no accounts, no tracking.
- **Content rating stays separate.** The IARC answers in [content-rating.md](content-rating.md)
  do not change, and the rating that comes back may well be lower than 13; that is normal and
  not a conflict with the target age.
- **Nothing to switch on for minors.** "Build age-appropriate experiences on Google Play"
  (`android docs`, `kb://android/distribute/age-appropriate-experiences`) names three tools:
  the IARC rating (required, see above), Restrict Minor Access (required only for
  age-restricted categories, optional otherwise and only with 18 and over as the sole band)
  and the Play Age Signals API (optional). None is needed for this app.
- **The form is reviewed.** Google checks the declared audience against the app and listing;
  the page says up to 7 days, longer in some cases.

## Only the owner can do these

- The Play developer account, its identity checks and, for a new personal account, Play's
  closed test before production.
- The price of Pro, the countries, and tax and payout settings.
- The upload key and Play App Signing (`release.yml` signs with the upload key from the
  repository's secrets).
- The contact email and website shown on the store page.
- Uploading the promo video to YouTube and the foreground-service clips.
- Creating the app and the in-app product `beatrealms3d_pro`, and accepting Play's terms and
  declarations.
- Running the **Store assets** workflow, looking at the pictures and choosing which go up.
- Confirming the privacy policy's public URL shows the renamed text.

## Placeholders the owner fills

- `CONTACT_EMAIL` in `docs/privacy-policy.md`, `app/src/main/assets/privacy_policy.md` (the two
  must stay identical, `PrivacyPolicyTest`) and `docs/privacy-policy.html`: the address shown on
  the store page. Replace it in one commit before the policy URL goes into the Console.
- The store listing's contact email and website (Store settings).
- The price of Pro.

## Decisions waiting for the owner

1. **Title.** "BeatRealms 3D Music Visualizer" is exactly 30 characters and describes the app.
   The alternative is the bare "BeatRealms 3D".
2. **Data safety.** Declare the Cast SDK's anonymous logging (recommended, because Play counts
   an SDK's transmissions as the app's) or answer "no data collected"
   ([data-safety.md](data-safety.md)). The privacy policy is worded for the recommended answer.
3. **Target age: decided 2026-11-11, "13+"**: tick 13-15, 16-17 and 18 and over. The Families
   policy does not apply. [families.md](families.md) keeps why 9-12 was dropped.
4. **The content rating questions** are answered from the code, but Google does not print the
   questionnaire in its help pages, so the wording there will differ from the notes.
5. **The styles named "Acid"** in a store screenshot or the listing: harmless in the app, but
   keep them out of store pictures (the metadata policy bans depictions of drug use, and a
   reviewer reads names).
6. **Screenshots: decided 2026-11-11**, "screenshots free, record pro" (and the hidden easter eggs are free; the listing does not name them). Still open: which styles,
   and whether the player screenshot is retaken on a phone with real cover art.
7. **Promo video.** Optional. The workflow's clips are silent and software-rendered; a
   published video needs a recording from a real phone ([promo-video.md](promo-video.md)).
8. **Tags.** Play's tag list could not be read outside the Console; pick from the order in
   [category-tags.md](category-tags.md).
9. **"Your colours stay yours"** (the Auto mode bullet). True in the code (the director touches
   no colour param, `core/tests/director_test.cpp`, `resolver_test.cpp`), but it is a promise;
   drop the sentence if Auto should be free to change colours later.
10. **"MilkDrop" and "Google Cast" in the listing.** Both are other parties' names, used only to
   say what the app is compatible with ("a MilkDrop-compatible engine", "with Google Cast").
   If a reviewer objects, drop the words; nothing else depends on them.
11. **Android TV.** The manifest now declares TV support (`android.software.leanback`, a
   `LEANBACK_LAUNCHER` entry, a TV banner, touchscreen not required). Play then offers the app
   to Android TV only if the owner opts in under Advanced settings › Form factors, which needs
   TV screenshots and passes Play's TV quality review. The listing says nothing about TV; this
   folder has no TV screenshots.
12. **"Show on a computer" in the listing.** Named as a Pro bullet ("in a web browser on a
   computer on the same Wi-Fi, through a link or a QR code"); #1099 changes the link (no IP
   address) and adds WebRTC, so reread the bullet and the privacy section when it merges.

## Listing lines to change when the open PRs merge

The store text is a sketch; the code is the source of truth (owner, 2026-11-12). The listing
describes `origin/main` at 8fd87553d, rechecked against the files on 2026-11-12. Landed since
the first version and now in the text: real-time Record (#1035), the knob editor (#1062), the
connected display (#1039), the Cast microphone type (#1037), Show on a computer (#1048, Pro
through `ProFeature.CAST`), the Flame, Opal and Hollow families (#1088: 274 styles, thirteen
families), Customize's Dive and Engine categories (#1065: eleven categories). #1024 retired
pulse, shake and flash, which the listing never named. The easter eggs (#1055: shake for a
screenshot, tap along) are free and hidden, so the listing does not name them; the privacy
policy does (sensors, screenshots).

| PR | Change when it merges |
|---|---|
| #1036 Pool replaces Water | The count stays 274 if it is one style for one (recount `core/tests/catalogue.txt`); the listing names neither style, keep it so. Free or Pro follows its kind (`pro/FreeTier.kt:45`: only loose shader styles are free); either way "more than 60" free stays true. Liquid's ripple controls go: screenshot alt text that names ripples needs a look |
| #1089, #1099 Show on a computer: the phone's own picture, WebRTC, a link without the IP address | Recheck the privacy policy's "Show on a computer" section (three copies), [data-safety.md](data-safety.md) › the casting row and the listing's "through a link or a QR code" bullet: #1099 adds WebRTC (DTLS-SRTP, no STUN or TURN, local network only) and a new `tools/network-gate.sh`, and moves socket code to `core/cast/` as well as `cast/`, so the evidence paths change |
| #1100 Remove hidden hit reactions | None (the listing names no hit reaction) |
| #1094 90 and 120 fps on fast screens | None (the listing quotes no live frame rate) |

## What has been checked and what has not

Checked: the listing's lengths and banned words (`tools/store-check.sh`, at b1ac6a75a; on 2026-11-12 the lengths were recounted with `wc -m` only, the script was not rerun); every count
(`tools/store-stats.sh`, [stats.md](stats.md)); the icon's and the new feature graphic's size
and format; each claim in the full description against the code on `main` at b1ac6a75a (Graphify, then the files), and again by reading the files at 8fd87553d on 2026-11-12; the collect script on made-up pictures; each form answer against the code lines it
cites; Play's rules against the help pages named in each file, read on 2026-11-11.

Not checked: the first run of the **Store assets** workflow (run 43112454833, 2026-11-11, two
hours) got only part of each set: `UiAutomation.takeScreenshot()` returned null on some
Visuals captures under software GL (`StoreStage.shot`, `the system gave no screenshot`), the
emulator went offline during the 10-inch set, and the collect step never ran because
`pillow==11.0.0` has no wheel for the runner's Python 3.14 (fixed: Pillow 12.0.0, wheels only).
[screenshots.md](screenshots.md) › Honest limits lists what a first run could show.

`policy-audit.md` is the raw report of the `play-policy-insights` skill, kept as the source the
data-safety and permissions answers were checked against.
