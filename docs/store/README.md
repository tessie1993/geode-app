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

Before pasting, run `bash tools/store-check.sh` (lengths, banned words) and
`bash tools/store-stats.sh` (the counts; if one differs from the listing, fix the listing).

## Steps in the Play Console

| # | Where in the Console | What to do | From |
|---|---|---|---|
| 1 | Create app | Name "BeatRealms 3D Music Visualizer", default language English (US), App, Free | `fastlane/metadata/android/en-US/title.txt` |
| 2 | Grow › Store presence › Main store listing | Paste App name, Short description, Full description | `title.txt`, `short_description.txt`, `full_description.txt` |
| 3 | Same page › Graphics | App icon 512, feature graphic 1024 x 500, phone screenshots (8), 7-inch tablet (8), 10-inch tablet (8), each with its alt text | [screenshots.md](screenshots.md); files from the **Store assets** workflow's artifact |
| 4 | Same page › Video | Paste the YouTube URL | [promo-video.md](promo-video.md) |
| 5 | Store settings | Category Music & Audio, tags, contact email, website | [category-tags.md](category-tags.md) |
| 6 | Policy › App content › Privacy policy | The public URL of `docs/privacy-policy.html` | [data-safety.md](data-safety.md) › Privacy policy |
| 7 | App content › Data safety | Answer as written | [data-safety.md](data-safety.md) |
| 8 | App content › Content rating | Answer the questionnaire | [content-rating.md](content-rating.md) |
| 9 | App content › Target audience, Ads, App access, News, Health, Government, Financial | Target age: 13-15, 16-17, 18 and over. The rest as written. See "A 13+ app" below | [content-rating.md](content-rating.md) |
| 10 | App content › Foreground service permissions (appears once a bundle is uploaded) | One description and one video per service type | [permissions.md](permissions.md) |
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
- Running the **Store assets** workflow, looking at the pictures and choosing which go up.
- Confirming the privacy policy's public URL shows the renamed text.

## Decisions waiting for the owner

1. **Title.** "BeatRealms 3D Music Visualizer" is exactly 30 characters. The alternative is
   the bare "BeatRealms 3D".
2. **Data safety.** Declare the Cast SDK's anonymous logging (recommended) or answer "no data
   collected" ([data-safety.md](data-safety.md)).
3. **Privacy policy.** Four edits before the form goes in: the "collects nothing" opening, the
   name, a line on deletion, the live URL ([data-safety.md](data-safety.md)).
4. **The first-run safety notice** says nothing can flash more than three times a second, on
   screen or in exported video. The code limits the parameters and no longer measures the
   finished frame. The store text makes the weaker claim; the in-app text should match what the
   code does, or the limiter should come back.
5. **Target age: decided 2026-11-11, "13+"** (it supersedes "target 10+" of the same day):
   tick 13-15, 16-17 and 18 and over. The Families policy does not apply.
   [families.md](families.md) keeps why 9-12 was dropped and the four things it would need.
6. **The content rating questions** are answered from the code, but Google does not print the
   questionnaire in its help pages, so the wording there will differ from the notes.
7. **The styles named "Acid"** in a store screenshot or the listing: harmless in the app,
   but keep it out of store pictures if a drug reference is a worry.
8. **Feature graphic.** With or without the wordmark; the old one says MuzViz
   ([screenshots.md](screenshots.md)).
9. **Screenshots: decided 2026-11-11**, "screenshots free, record pro": the screenshots show
   the free app, the recorded clips show Pro. Still open: which styles, and whether the player
   screenshot should be retaken on a phone with real cover art.
10. **Promo video.** Optional for Play. The workflow's clips are silent and software-rendered;
    a published video needs a recording from a real phone ([promo-video.md](promo-video.md)).
11. **Tags.** Play's tag list could not be read outside the Console; pick from the order in
    [category-tags.md](category-tags.md).

## What has been checked and what has not

Checked: the listing's lengths and banned words (`tools/store-check.sh`); every count
(`tools/store-stats.sh`, [stats.md](stats.md)); the icon's and old feature graphic's size and
format; the collect script on made-up pictures; each form answer against the code lines it
cites; Play's rules against the help pages named in each file, read on 2026-11-11.

Not checked: the screenshot tests and the workflow have never run. They were not compiled
here either (the Gradle compile ran out of memory in the agent container), so the first CI
build is also the first ktlint, detekt and compile check of `dev.geode.store`.
[screenshots.md](screenshots.md) › Honest limits lists what a first run could show.

`policy-audit.md` is the raw report of the `play-policy-insights` skill, kept as the source the
data-safety and permissions answers were checked against.
