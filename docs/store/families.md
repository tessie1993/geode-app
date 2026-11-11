# Families policy: what a target audience of 10 and up requires

Owner decision, 2026-11-11, verbatim: "no age limit =, targert 10 +". Recorded in #994.

Play has no "10 and up" band. Its bands are 5 and under, 6-8, 9-12, 13-15, 16-17 and 18 and
over, so the answer in the Console is **9-12, 13-15, 16-17 and 18 and over**. Any band that
includes children puts the whole app under the Families policy, as an app for "both children
and older users", and 9-year-olds are in the band whether or not the app is aimed at them.

Read on 2026-11-11:

- Families policy ([answer/9893335](https://support.google.com/googleplay/android-developer/answer/9893335)
  and the same text in [answer/17122218](https://support.google.com/googleplay/android-developer/answer/17122218))
- Target audience and content ([answer/9867159](https://support.google.com/googleplay/android-developer/answer/9867159),
  [answer/9285070](https://support.google.com/googleplay/android-developer/answer/9285070))
- "Build age-appropriate experiences on Google Play" and "Upcoming Android and Google Play
  deadlines" (`android docs search`, `kb://android/distribute/age-appropriate-experiences`,
  `kb://android/distribute/play-policies`)
- Google's Cast SDK data disclosure (developers.google.com/cast/docs/android_sender/data_disclosure)
- The code facts are those of [policy-audit.md](policy-audit.md) (the `play-policy-insights`
  run), [data-safety.md](data-safety.md) and [permissions.md](permissions.md), re-read against
  the Families rules; the skill has no Families mode, so it was not run a second time.

Not legal advice. The policy itself tells developers to ask counsel which child-protection laws
(COPPA, GDPR and others) apply; that is the owner's to do.

## A. Already met by the code

| Families rule | State | Evidence |
|---|---|---|
| Ads to children only from self-certified ads SDKs, no interest-based ads, a neutral age screen for ads | Not applicable: no ads and no ads SDK | CI fails the release build if an ads or analytics SDK is added (AGENTS.md › Conventions) |
| No AAID, SIM serial, build serial, BSSID, MAC, SSID, IMEI or IMSI sent from children or users of unknown age | Met: the app reads none | No `AD_ID`, `READ_PHONE_STATE`, `ACCESS_WIFI_STATE`, Bluetooth or location permission in `AndroidManifest.xml`; `tools/check-manifest.sh` fails on `AD_ID`; the only `Settings.Secure` reads are the notification-listener check (`audio/NowPlayingBridge.kt:28`) |
| No device phone number from `TelephonyManager`; no location; Bluetooth only through Companion Device Manager | Met: none of the three is used | Same manifest |
| Disclose personal and sensitive data collected from children (microphone and device data count as sensitive) | Met by not collecting: microphone and other apps' audio are analysed in memory and never stored or sent | `audio/MicCapture.kt:78`, `ui/CaptureController.kt:71-96`; [data-safety.md](data-safety.md) |
| Not a webview of a website, no affiliate traffic | Met | Native app |
| No login through a service that forbids child users | Met: there are no accounts | No account code |
| Social apps and features: safety reminder, adult action before children exchange information | Not applicable: no chat, no profiles, no content from other users. Sharing a preset or video goes through Android's share sheet, started by the user | `ui/PresetActions.kt:382-392`, `ui/RenderBar.kt:232` |
| Augmented reality safety warning | Not applicable | |
| Must not require a non-child-approved API or SDK to reach app content | Met: Cast is optional and nothing else depends on it | `cast/` |
| Monetisation: real money told apart from virtual currency, no deceptive or accidental purchase | Met: one purchase, priced in real money on the button ("Unlock Pro · price"), "One-time purchase, billed by Google Play. No subscription.", a close button, no countdown, no virtual currency. The purchase itself is Google Play's sheet, with the account's purchase authentication and Family Link approval | `res/values/strings_pro.xml:28,63-69`, `ui/pro/PaywallScreen.kt:296,408` |
| Content appropriate for children | Abstract visuals; no violence, sexual content, gambling or user content | [content-rating.md](content-rating.md) |

## B. Needs a change in the repo before the 9-12 band is ticked

Nothing in app code was changed for this list.

| # | Gap | Kind | What to change |
|---|---|---|---|
| 1 | **The privacy policy says the opposite.** "MuzViz is not directed at children under 13" (`docs/privacy-policy.md:142-145`, the `.html` and the copy in `app/src/main/assets/privacy_policy.md`). A Families app must have a policy that matches its declared audience | Text | Rewrite the Children section: the app is suitable for ages 10 and up, has no accounts, ads or chat, the app collects no personal information from children or anyone, the microphone is only analysed on the device, and what the Cast component logs (see 2). Add a contact for parents. Together with the four edits in [data-safety.md](data-safety.md) |
| 2 | **Google Cast starts for every user at every launch.** `MainActivity.kt:302` and `cast/CastOptionsProvider.kt:29` build `CastController`, which starts the Cast SDK; Google says the SDK logs discovery and session events with device and app information and that this cannot be turned off. Families: an app for children and older users may use an API or SDK "not approved for use in child-directed services" only if it does "not result in the collection of data from children" or sits behind a neutral age screen. Google's Cast page says nothing about child-directed use either way | Code | One of: (a) start Cast only when a Pro owner turns Cast on (no SDK start at launch, and the `MediaTransferReceiver` path with it), so a child who never casts never reaches it; (b) a neutral age screen (free entry of a birth date, nothing pre-set) before Cast is started; (c) written confirmation from Google that the Cast SDK may run in a Families app. (a) is the smallest and also removes the judgement call in the data safety form |
| 3 | **The first-run safety notice overclaims** (#947): "Nothing on screen can flash more than three times a second ... the video you export is held to the same limit" (`res/values/strings.xml:29,85`), while the engine limits parameters and no longer measures the finished frame. With children in the audience, "content accessible to children must be appropriate" and "no misrepresentation" make this more than a wording issue | Text or code | Make the text say what the engine does, or bring the output limiter back. Add a sentence for parents ("If a child uses this app, an adult should read this first") |
| 4 | **The notice and the policy still say MuzViz** on `main` | Text | Lands with the rename (PR #1002) |
| 5 | **A style kind is named "Acid"** (`core/viz/Catalogue`, shown in Styles). Play's examples of Families violations include glamorising controlled substances | Text | Rename the display label (the id can stay), or accept the risk; keep it out of store pictures either way |
| 6 | **The paywall opens by itself** when a locked style or control is tapped and once when the trial ends (`ui/pro/PaywallViewModel.kt:116-120`). Not a named violation; Play may reject "overly aggressive commercial tactics" at its own judgement | Code, optional | Keep as is and see what review says, or drop the automatic "trial ended" paywall. A parental gate in front of "Unlock Pro" is not required by Play (its own purchase approval covers it) but is common in apps for children |
| 7 | **"Search the web" for lyrics** opens the browser on a search for the track (`ui/LyricsSettings.kt:91-92`). Allowed (the rule is against links to content that breaks Play's policies), but it is the one place a child leaves the app for the open web | Code, optional | Leave, or move it behind a confirmation |
| 8 | **Store listing warning** | Text, done here | The photosensitivity warning in `fastlane/metadata/android/en-US/full_description.txt` now speaks to parents |

## C. Only the owner can do, in the Play Console

1. **Target audience and content › Target age:** tick 9-12, 13-15, 16-17 and 18 and over. Do
   not tick 5 and under or 6-8: the policy says to tick a band only if the app is designed for
   it, and the bands below 9 bring the strictest reading of every rule above.
2. Answer the rest of that form (App details, Ads: none, Store presence). Google reviews the
   declared audience against the app and the listing; the page says up to 7 days, longer in
   some cases. A later change of audience shows in the store only after an app update.
3. **Data safety:** the form must be accurate for children too. With gap 2 open, the Cast
   SDK's logging has to be declared ([data-safety.md](data-safety.md), the recommended answer);
   once Cast starts only on request, the owner can revisit it.
4. **Content rating (IARC):** the answers in [content-rating.md](content-rating.md) do not
   change; the Families policy only adds that they must be kept accurate. The rating that
   comes back should be the lowest band (for example PEGI 3 or ESRB Everyone); if it comes back
   higher than the audience ticked, fix whichever is wrong before publishing. Digital purchases:
   yes. Users interact or share inside the app: no.
5. **Privacy policy URL:** live, public, renamed, with the new Children section (gap 1).
6. **Legal:** ask counsel whether COPPA (United States), the GDPR's rules for children
   (the age of consent differs by country) or other laws apply to the owner as the publisher,
   and whether the Cast logging matters under them.
7. **Cast and Families:** ask Google (Play Console help, or the Cast developer support
   channel) whether the Cast SDK is approved for child-directed services, unless gap 2 is
   closed by (a).
8. Optional: opt in to be considered for Teacher Approved; the page says Families apps for
   mixed audiences are eligible and promises nothing.

## What the decision costs, in one line

Everything in A is already true because the app has no ads, accounts or tracking. The one code
blocker is Cast starting at launch (gap 2); the rest is text. Ticking 13-15 and up instead
would take the app out of the Families policy and leave gaps 1 to 3 as ordinary honesty fixes.
