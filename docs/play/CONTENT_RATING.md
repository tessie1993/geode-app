# Content rating, target audience and photosensitivity

## Content rating questionnaire (App content > Content ratings)

IARC questionnaire. Category: **"All Other App Types"** (not a game, not a social or
communication app). Answers below follow from the code: the app shows the user's own
music, art and visuals and nothing from other people.

| Question | Answer | Why |
|---|---|---|
| Violence, blood, gore | No | Abstract visuals only |
| Sexual content, nudity | No | |
| Profanity / crude humour | No | Track titles and lyrics come from the user's own files; the app adds none |
| Controlled substances (drugs, alcohol, tobacco) | No | |
| Gambling, simulated gambling | No | |
| Fear / horror themes | No | |
| Users can interact or exchange content with each other | **No** | No accounts, chat or feeds. Sharing a preset link or a video goes through the Android share sheet to another app the user picks |
| Shares the user's location with others | No | No location permission |
| Allows purchase of digital goods | **Yes** | The one-time MuzViz Pro unlock through Google Play Billing |
| Unrestricted internet access / web browser | No | No INTERNET permission |

Expected result: **Everyone / PEGI 3 / USK 0 / ESRB Everyone**, with the
"In-App Purchases" interactive element.

## Target audience and content (App content > Target audience)

- **Target age groups: 13–15, 16–17, 18 and over.** Do *not* tick any group under 13:
  doing so puts the app under the Families policy (teacher-approved design, extra ad and
  data rules, and stricter review), which a creator tool doesn't need.
- "Could the app unintentionally appeal to children?" **No**: no cartoon characters, no
  child-directed copy; the store art is abstract visuals.
- Ads: **No, the app does not contain ads.**

## Photosensitivity (keep this on the listing and in the app)

Music visualizers are a known photosensitive-epilepsy risk, and reviewers look for how
an app handles it.

What the app already does:
- **First-run notice** (`SafetyConsent`, strings `safety_*`): shown before any visuals,
  must be acknowledged.
- **Hard flash limit in the engine**: nothing on screen flashes more than **three times
  per second**. That is the WCAG 2.3.1 "three flashes" threshold. It also applies to
  every exported video, can't be switched off, and no preset or imported MilkDrop file can
  exceed it.
- **"Slow the motion down"** (Settings > Behavior > Visual safety) for motion comfort,
  applied to exports too.
- **Help topic** (`help_topic_safety_body`) restating the above.

Listing text: the full description has a **SAFE TO WATCH** paragraph. Keep it. A short
warning is also good practice in the first screenshot's caption set or the promo video
description:

> Contains flashing lights and fast motion. Flashing is limited to three flashes per
> second. If you have photosensitive epilepsy, consult a doctor before use.

Exported videos: other people may watch them without seeing the app's notice. The flash
limit holds in the files themselves, which is the strongest guarantee available.
YouTube's guidance for flashing content still applies to uploads.
