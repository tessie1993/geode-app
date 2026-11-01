# Release and musical-reactivity plan (2026-11-01)

Working notes from the research pass on `claude/geode-finish` @ e915574. This is a draft,
pending the owner's answers. Nothing here has been built yet.

## 1. Play Store: what still stands between us and production

| Tier | Item | Status | Owner |
|---|---|---|---|
| Blocker | Public privacy-policy URL. The Pages URL returns 404 and there is no Pages workflow. The in-app copy is done. | Open | Code (Pages workflow) + you (Pages source) |
| Blocker | Release secrets `MUZVIZ_UPLOAD_*` and the `PLAY_LICENSE_KEY` variable | Couldn't check | You |
| Blocker | Console: FGS declarations with videos for 4 types, Data safety, content rating, audience, ads | Open | You |
| Blocker | Closed test with 12 testers for 14 days (personal account) | Open | You |
| New | Developer verification / app registration (enforced from 2026-09-30, global in 2027) | Not in checklist | You |
| New | Trader status declaration (from Nov 2026) | Not in checklist | You |
| New | Japan seller authorization by 2026-12-01, if Japan is a sales country | Not in checklist | You |
| Should | Emulator CI job red on the last 3 merges (a different journey test fails each time; the emulator dies) | Open | Code |
| Should | `dataSync` FGS type: consider declaring only `mediaProcessing` (simpler declaration video) | Open | Code |
| Should | Bugs from the deep-QA and bug-hunt PRs still live (list below) | Open | Code |
| Done | targetSdk 36, 16 KB alignment, predictive back, edge-to-edge, S1 controller trust, S2 link confirm, N1 mic release, PBL 9.1 | Done | — |

Still-live bugs, all verified in code:
- >2-channel files fail to play (`NativeDspProcessor.kt:61`).
- Duplicates crashes on a repeated title|artist (`DuplicatesTab.kt:53`).
- The pending-row sweep can't see pending rows (no `MATCH_PENDING`).
- The 30-day Pro grace freezes the wallpaper and dream.
- Export Cancel after returning to a running render only sets a flag.
- The projectM notice says "does not modify it" (`third_party_notices.txt:82`), but we patch it.
- Crafted-link NaN/1e39 values.
- The 4 GB muxer limit on 4K.

## 2. Why visuals "flash and come closer"

- At least 8 independent writers push "closer":
  - `MotionField.cpp:152,178`
  - `CameraRig.cpp:172-179`
  - `ReactionDna.cpp:179`: every style's big moment is always `CamFly`.
  - `ReactionDna.cpp:203,256,264`
  - `CameraRig.cpp:258`: flat styles fold dolly and FOV into a single zoom.
- 246 shaders hand-wire `SND_LEVEL` and `SND_KICK_ENV` to brightness and zoom. Only 3 read timbre or pitch.
- Every driver correlates with loudness, so the separate routes move together.
- There is no tempo, bar, phrase or section. `MotionField.cpp:108` says "the engine has no tempo tracker". Phrase-level state was lost in the sound-engine rewrite.
- MilkDrop3 adds nothing musically smarter: its "beat" is a band threshold. The gain has to come from MIR technique: PLP beat tracking, Foote novelty, Yadati drop detection, chroma, and Spence's crossmodal mappings.

## 3. Proposed engine work (pending answers)

New SoundFrame fields:
- `bpm, beatPhase, beatConf, beatCount, downbeat, barPhase, barCount`
- `phraseIndex, phrasePhase`
- `chroma[12], keyHue, harmonicChange`
- `novelty, sectionChange, sectionId`
- `buildUp, drop`
- `fluxLow/Mid/High, hfc`

Mapping (layer × timescale → visual):

| Layer | Frame | Beat | Bar | Phrase | Section |
|---|---|---|---|---|---|
| Rhythm | hat → sparkle | kick → local displacement; snare → lateral or rotation; anticipate with beatPhase | downbeat → camera angle or palette step | swap route bank | cut + camera mode |
| Energy | | | intensity → speed | buildUp → tension | drop → release |
| Timbre | centroid → sharpness; flatness → grain | fluxHigh → glitch | | | sectionId → material |
| Harmony | pitchHeight → height | noteChange → hue step | harmonicChange → palette rotate | keyHue → base hue | |
| Space | pan → yaw; width → spread | | | | |

Section state machine:
- States: Intro, Verse, Build, Drop, Breakdown, Outro.
- Hysteresis: at least 8 bars in a state; transitions only on a downbeat.
- Each state picks the DNA bank, camera mode, palette and motion/luminance budget.
- A returning section restores its look.

## 4. Redo list from the closed stale PRs

- #188 notice
- #187 endless zoom (Shepard dive)
- #190 colour engine (key, chord, tension)
- #183 Drive mode + full param matrix + camera, layers, particles
- #185 event decks
- #191 Mutate/Randomize + music-weighted auto-visuals
- #186 phone motion
- #189 Glass/Truchet redo
- #181 NativePlayer removal decision

## 5. UX: top findings

- 238 styles as text rows: no thumbnails, search, favourites or recents.
- Two stacked scrollable tab rows. Customize has 9–11 tabs, and Fluid shows on every style.
- Slider labels are 11sp, with raw `0.73` values and jargon (DNA, Slot, Polarity, GLSL, FX).
- The toolbar eats the first screen; touch targets are under 48dp; there are two save paths.
- Suggestions:
  - Thumbnail grid with chips and search.
  - Universal "Look" tab plus per-style controls; advanced controls hidden.
  - Preview-then-commit.
  - Bottom sheet with detents.
  - 14sp labels with units.
  - Plain-language names.
  - Card presets with long-press actions.

## 6. Where help/docs pages live

The app has no INTERNET permission, so in-app help must ship as assets:
- Markdown in `app/src/main/assets/help/`, rendered like the privacy policy.
- The same files are published by a GitHub Pages workflow from `docs/` (the privacy URL needs this anyway).
- One source and two outputs, with a JVM test keeping them equal (as `PrivacyPolicyTest` does).

## 7. Owner decisions (2026-11-01)

- Release and engine work run **in parallel sessions**.
- Main user: audio-visual lovers who must not be overwhelmed. A simple layer comes first; **Advanced is Pro and tucked away at the back**.
- **Remove NativePlayer**, so playback is ExoPlayer only.
- Music drive: **strong, but each style keeps its home palette**. "Follow music" defaults to about 75%.
- Silence: **a user setting; the default is a slow ambient drift** ("Still when silent" is the option).
- Redo, from the stale PRs:
  - Event decks + phrase lock
  - Endless zoom
  - Mutate/Randomize + smart auto-visuals
  - Phone motion (**off by default, opt-in**, live only)
  - Camera/layers/Drive/particles/reaction grid **all in one go**
  - Glass/Truchet redo **before release**
- More options for users:
  - A per-feature on/off and amount.
  - Pick which sound drives what (Advanced, Pro).
  - Reaction "moods" presets.
  - "How complex" (interpreted as a global Simple ↔ Rich reaction-complexity control; to confirm).
- Thumbnails: **pre-baked WebP rendered on CI**.
