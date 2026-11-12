# Families policy: why the 9-12 band was dropped

Dated note, so the decision can be revisited.

- 2026-11-11, owner: "no age limit =, targert 10 +". Play has no 10+ band; the answer would
  have been the 9-12 band with every older one, which puts the whole app under the Families
  policy ([answer/9893335](https://support.google.com/googleplay/android-developer/answer/9893335)).
- 2026-11-11, later the same day, after reading the gap list, owner: "13+". **This supersedes
  the entry above.** The target audience is 13-15, 16-17 and 18 and over, and the Families
  policy does not apply ([content-rating.md](content-rating.md)). Both entries are in #994.

## What the 9-12 band would have needed

The app has no ads, accounts, chat or tracking, so most of the policy was already met. These
four were not, as the code stood on 2026-11-11:

1. **Cast at launch.** The Google Cast SDK starts for every user when the app opens
   (`ui/MainActivity.kt:397`, `cast/CastOptionsProvider.kt:29`) and logs discovery and session
   events to Google, which nobody can turn off. A Families app may use an SDK "not approved
   for use in child-directed services" only if no data is collected from children or it sits
   behind a neutral age screen, and Google's Cast page does not say the SDK is approved. The
   fix would be to start Cast only when a Pro owner turns it on. This was the one code blocker.
2. **The privacy policy's Children section** says the app "is not directed at children under
   13" (`docs/privacy-policy.md:142-145`). Under 13+ that sentence is correct and stays; under
   9-12 it would have had to be rewritten, with a contact for parents.
3. **The first-run safety notice** used to promise that nothing could flash more than three
   times a second, while the engine limits parameters and no longer measures the finished frame
   (#947). Closed (#994): `safety_prompt` now says what is limited and that MilkDrop presets only
   get the brightness cap.
4. **The style kind labelled "Acid"** (`core/viz/Catalogue`). Play's examples of Families
   violations include glamorising controlled substances.

To revisit: close 1, rewrite 2, decide 4, then tick 9-12 in Target audience and content
and re-read the Families policy, which changes often.
