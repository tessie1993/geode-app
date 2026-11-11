# Content rating, target audience, ads and the other App content declarations

Play Console › Policy and programs › App content. Sources: "Content ratings for apps and games"
([answer/9859655](https://support.google.com/googleplay/android-developer/answer/9859655)),
"Target audience and app content"
([answer/9867159](https://support.google.com/googleplay/android-developer/answer/9867159)), read
2026-11-11.

Play's help pages do not print the IARC questionnaire, so the question wording below is from
the questionnaire as it is generally known, not checked against the live form. The answers are
facts about the app and hold however a question is phrased; where the live form asks something
not listed here, answer from the "Facts" column.

## Content rating questionnaire (IARC)

Category to choose at the start: **Utility, Productivity, Communication or Other** (the app is
a music player and video tool, not a game, a social network or a news product).

| Topic | Answer | Facts and evidence |
|---|---|---|
| Violence, blood, gore | No | Abstract generated visuals; no characters, no depictions |
| Sexuality or nudity | No | None in the app |
| Profanity or crude humour | No | None in the app's own text. The user's own music and lyrics files are the user's content, not the app's |
| Controlled substances (drugs, alcohol, tobacco) | No | None. One simulation style is named "Acid" (a reaction-diffusion look, `core/tests/catalogue.txt`, kind `ACID`); it depicts and references no drug use. If the owner would rather not explain a name to a reviewer, rename it |
| Gambling, simulated gambling | No | None |
| Fear, horror | No | None |
| Users can interact or exchange content with each other | No | No accounts, chat, comments or feeds. Sharing is Android's share sheet to another app (`ui/RenderBar.kt:232`) |
| Shares the user's location with others | No | No location permission (`AndroidManifest.xml`) |
| Allows purchases of digital goods | **Yes** | One in-app product, `muzviz_pro`, a one-time unlock through Google Play Billing (`pro/BillingRepository.kt`, `PRO_PRODUCT_ID`) |
| Unrestricted internet access, a browser | No | The app opens the system browser for a lyrics search the user taps (`ui/LyricsSettings.kt:91-92`); it has no browser of its own |
| Content the app itself supplies that is not rated | No | Bundled content is shaders, six MilkDrop presets and interface sounds |

Expected result: the lowest band in each scheme (IARC 3+, ESRB Everyone, PEGI 3), with the
"in-app purchases" descriptor. The rating is whatever the live form returns.

Photosensitivity is not an IARC topic and does not change the rating. It is covered by the
warning in the listing, the first-run safety notice (`ui` › `SafetyConsent`) and the engine's
limits (`core/viz/VisualSafety`).

## Target audience and content

| Question | Answer | Why |
|---|---|---|
| Target age groups | **9-12, 13-15, 16-17, 18 and over** | Owner decision, 2026-11-11: "no age limit =, targert 10 +". Play has no 10+ band; 9-12 is the one that holds ten-year-olds. Ticking it puts the app under the Families policy |

What the Families policy then requires, what the repo already meets and what has to change
first is in [families.md](families.md). Two things there block the tick: the privacy policy
says the app is not directed at children under 13, and Google Cast starts for every user at
launch.

## The other declarations

| Declaration | Answer | Evidence |
|---|---|---|
| Ads | **No, the app does not contain ads** | No ads SDK; CI fails the release build if one is added (AGENTS.md › Conventions) |
| App access | **All functionality is available without special access** (no login). Pro features sit behind a purchase: tell reviewers they can use the one-hour trial from the paywall, or add licence testers | No accounts anywhere in the code |
| Advertising ID | **No**, the app does not use an advertising ID | No `AD_ID` permission; `tools/check-manifest.sh:14-15` fails if it appears |
| Government app | No | |
| Financial features | None | |
| Health | None | The sensor use is the proximity sensor and the gyroscope (`wallpaper/DreamCover`, `GyroLook`) |
| News app | No | |
| Data safety | See [data-safety.md](data-safety.md) | |
| Foreground service permissions | See [permissions.md](permissions.md) | |
| Privacy policy | The public URL of `docs/privacy-policy.html` | `pages.yml` publishes it |
