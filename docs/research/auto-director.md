# The auto-director: the music's feeling directing every style

The musical layer (`docs/research/musical-layer.md`) hears key, scale, chords, tension, tempo, sections,
build-ups and drops. The auto-director turns that into visual decisions, the way a VJ would: palette,
motion character, camera, shape and density, and what reacts to what, on every style at once. This is the
spec. Each section records the owner's decision from the planning interview, then what is built.

## What the owner asked for

| Question | Decision |
|---|---|
| Who is in control | Opt-in: a switch in Settings › Behaviour › Auto-director. Pro, with a free taste. |
| Free taste | The first minute of each song (`FreeTier.FREE_AUTO_DIRECTOR_TASTE_MS`). This equals the free export cap, so a free export is directed all the way through. |
| Emotion | Every key + scale combination is its own emotion. It mixes measured music science with the historical key-character tradition. |
| Shown to the user | Nothing. No mood names, no key labels. The feeling is only seen in the visuals. |
| Palette | A common feeling in the colours, never the same or nearly the same twice. Unique to key and scale, and new on each play. |
| On a section change or drop | Same style, new behaviour: what reacts to what, motion character, camera moves, shape and density, detail, variety. |
| Pace | Commits only at sections and drops. Between commits the look breathes. The melody's scale degree adds short accents. |
| Genres | All of them, with no bias. |

## How other tools do it

- **TouchDesigner**'s `audioAnalysis` component outputs levels, kick/snare/rhythm pulses, spectral centroid
  and density, and nothing more. The artist wires each channel to a parameter by hand.
- **Synesthesia, Resolume and VDMX** do the same with band levels, hits, BPM and beat phase.
- None of them detects key or mood, and none makes decisions. In these tools the VJ is the mood detector.
  The auto-director's job is to be that VJ.

## The research it rests on

Evidence strength: **strong** (replicated, controlled), **moderate** (one or two studies, or a sound
theory), **tradition** (historical or genre convention only).

| Finding | Strength | Used for |
|---|---|---|
| Cues add up, with no interactions: mode, tempo, register, dynamics, articulation and timbre explain 77–89 % of happy/sad/scary/peaceful ratings (Eerola, Friberg & Bresin 2013; Juslin & Lindström 2010) | strong | every axis is a weighted sum |
| Mode carries about half of happy (sr² .48) and of sad (.54); the third decides most of it | strong | valence comes from the mode |
| Happiness falls as scale degrees are lowered: Lydian/Ionian, Mixolydian, Dorian, Aeolian, Phrygian (Temperley & Tan 2013; Tan & Temperley 2017) | strong to moderate | the mode vector's valence |
| Minor sounds sad because degrees 3 and 6 sit lower than expected, like sad speech (Parncutt 2014; Huron & Davis 2012) | moderate | each lowered degree counts |
| Pentatonic is most positive; whole-tone and chromatic are tenser and less "movable" (Poudrier et al. 2024) | moderate | tension from semitones, tritones and symmetry |
| GEMS: nine music-specific feelings (wonder, transcendence, tenderness, nostalgia, peace, power, joy, tension, sadness) (Zentner, Grandjean & Scherer 2008) | strong | the "what kind" layer |
| Valence and arousal capture most of rated music emotion (Eerola & Vuoskoski 2011) | strong | the "how much" layer |
| Key characters (Schubart 1806; Mattheson 1713; Charpentier c. 1692) | tradition | the key's flavour |
| Listeners cannot hear key mood in equal temperament (Powell & Dibben 2005) | moderate | the key never moves valence and weighs a quarter |
| Scale-degree qualia: stable degrees feel happier, leaning ones tense (Huron 2006; Arthur 2018; Maimon, Lamy & Eitan 2021) | moderate | degree accents |
| Tension follows the trend of loudness, onset rate and pitch, and harmony over a longer window (Farbood 2012) | moderate to strong | the tension axis |
| Music–colour matches are mediated by emotion: fast/major is lighter, more saturated, yellower; slow/minor is darker, greyer, bluer (Palmer et al. 2013; Whiteford et al. 2018) | strong | lightness and saturation from the axes |
| Saturation drives arousal and brightness drives pleasure, more than hue does (Valdez & Mehrabian 1994; Wilms & Oberfeld 2018) | strong | saturation from energy, brightness from valence |
| Colour–emotion associations are universal across 30 nations (r = .88): joy is yellow, anger red, sadness blue/grey, calm green/blue (Jonauskaite et al. 2020) | strong | hue anchors per GEMS feeling |
| Rising intensity is imagined as speeding up (Eitan & Granot 2006); pitch maps to height and brightness, loudness to size (Spence 2011) | strong | motion and camera archetypes |
| Fixed emotion→preset lookups are too rigid; continuous blends work better (Zhao et al. 2025, stage lighting) | moderate | looks are drawn from regions, not presets |

Sources: [Eerola et al. 2013](https://pmc.ncbi.nlm.nih.gov/articles/PMC3726864/),
[Temperley & Tan](https://labs.sonicfield.org/library/emotional-connotations-of-diatonic-modes),
[Parncutt 2014](https://static.uni-graz.at/fileadmin/_Persoenliche_Webseite/parncutt_richard/Pdfs/Parncutt2014EmotionalConnotations.pdf),
[Poudrier et al. 2024](https://music.ubc.ca/?p=25244),
[GEMS](https://en.wikipedia.org/wiki/Geneva_Emotional_Music_Scale),
[Eerola & Vuoskoski 2011](https://durham-repository.worktribe.com/output/1477246),
[Schubart via Steblin](https://wmich.edu/mus-theo/courses/keys.html),
[Powell & Dibben 2005](https://sonicfield.org/library/key-mood-association-a-self-perpetuating-myth),
[Arthur 2018](https://sonicfield.org/library/a-perceptual-study-of-scale-degree-qualia-in-context),
[Maimon et al. 2021](https://arxiv.org/abs/2103.09709),
[Farbood 2012](https://bpb-us-e1.wpmucdn.com/wp.nyu.edu/dist/f/11865/files/2020/08/Farbood_2012_Musical_Tension.pdf),
[Palmer et al. 2013](https://pmc.ncbi.nlm.nih.gov/articles/PMC3670360),
[Whiteford et al. 2018](https://pmc.ncbi.nlm.nih.gov/articles/PMC6240980),
[Valdez & Mehrabian 1994](https://pubmed.ncbi.nlm.nih.gov/7996122/),
[Wilms & Oberfeld 2018](https://www.staff.uni-mainz.de/oberfeld/downloads/Wilms-Oberfeld2018_Article_ColorAndEmotionEffectsOfHueSat.pdf),
[Jonauskaite et al. 2020](https://biopen.bi.no/bi-xmlui/bitstream/11250/2761614/1/Jonauskaite_etal_2020_PsychScience.pdf),
[Spence 2011](https://www.psy.ox.ac.uk/research/crossmodal-research-laboratory/publication_modal/115721),
[Eitan & Granot 2006](https://scholars.huji.ac.il/ronigranot/node/17921),
[Zhao et al. 2025](https://arxiv.org/pdf/2506.01482),
[TouchDesigner audioAnalysis](https://derivative.ca/UserGuide/Palette:audioAnalysis).

Some entries in the tables below came from secondary sources: part of Schubart's list (taken from a
forum copy of Steblin's translation, because the Western Michigan page was down), Charpentier's list
(only quoted in Steblin's book), and Huron's words per degree. They are marked tradition or moderate,
and each weighs little.

## 1. The emotion of a key + scale (`tools/music_emotion.py`, `core/sound/EmotionTable.inc`)

**Mode vector.** Computed for each of the 46 scales from its notes, so all of them are covered:

- valence = 0.6 × (0.35 × third − 0.1 × lowered degrees − 0.15 × ♭2 − 0.2 × ♭5 + 0.1 × pentatonic) + 0.4 × the database's brightness
- tension = half the structure (semitones, tritones, augmented seconds, symmetry, no perfect fifth) and half the database's tension
- GEMS from the axes, blended half and half with an idiom table for the scales with a known character
  (Lydian wonder, Dorian nostalgia, Phrygian dominant power, whole-tone transcendence, …)

**Key flavour.** Schubart's character of each of the 24 keys, coded as GEMS, is shrunk toward neutral where
Mattheson and Charpentier disagree. It also carries the key signature's sharpness (−6 flats … +6 sharps):
sharp keys read brilliant, flat keys mellow.

**Fingerprint** = ¾ mode + ¼ key, in GEMS. The key never touches valence, energy or tension.
`docs/musical-intelligence/emotions.csv` lists all 552, and `key-characters.csv` the 24 key characters with
their sources. Run `python3 tools/musical_intelligence.py` to regenerate both and the `.inc`.

## 2. The live feeling (`core/sound/Emotion`)

| Axis | From | Smoothing |
|---|---|---|
| Valence −1..1 | fingerprint × confidence + tempo + brightness + degree pull | 3 s |
| Energy 0..1 | loudness 30 %, tempo 20 %, onset rate 20 %, spectral centroid 10 %, mode 20 %, plus the build-up | 1.5 s |
| Tension 0..1 | the scale's tension, chord roughness, loudness rise (the trend), build-up, degree strain | 1 s |
| GEMS-9 | fingerprint × confidence; energy × tension adds power, positive valence × energy joy, calm adds peace, negative valence × stillness sadness | 2 s |

- **Confidence** comes from the scale fit, or from the key's clarity when no scale fits. Without
  confidence the feeling falls back to neutral GEMS, so noise and drums alone never get a "sad" palette.
- **Degree** is the strongest note in the fast chroma against the tonic. It counts only if it stands at
  least 1.6× above the mean for at least 0.12 s.

## 3. Decisions (`core/viz/Director`)

**When** (the owner's "Sections + drops"):

| Reason | Rule | Glide |
|---|---|---|
| First | after 4 s of music, once the key is trusted (confidence > 0.5) or after 12 s | 2 s |
| Section | the form tracker's section change, at least 4 s after the last decision | one bar, 1–4 s |
| Drop | always | 0.25 s, a cut |
| Modulation | the *key* (tonic + major/minor) differs for 6 s, then lands on a downbeat. Scale-label flips inside one key (Hungarian minor ↔ hirajoshi) do not count | one bar |
| Long | 32 bars and a phrase start, or 75 s without a decision | one bar |

**What** (a *look*). A look is drawn per decision from the feeling, by splitmix64 seeded with the play's
seed and the decision number.

- **Hue.** A GEMS feeling is drawn with probability ∝ weight². Its anchor is set by the colour–emotion
  studies:

  | Feeling | Hue |
  |---|---|
  | joy | yellow-orange |
  | power | red |
  | tension | crimson |
  | sadness | blue |
  | peace | green-teal |
  | tenderness | pink |
  | nostalgia | amber |
  | wonder | violet |
  | transcendence | sky blue |

  The key turns the anchor ±4 % by its place on the circle of fifths, plus a little warmth for sharp keys.
  A further ±5 % comes from the seed.
- **Palette shape:**
  - Tension > 0.55: split complementary.
  - Melancholy (sadness + nostalgia > 0.45, low energy): near monochrome.
  - Otherwise analogous. The span widens with joy, wonder and energy.
- **Saturation** follows energy, minus melancholy, plus joy. **Brightness** follows valence.
  **Temperature** follows valence and nostalgia. **Contrast** follows tension and power.
- **Behaviour** uses the six macro knobs (Energy, Speed, Complexity, Space, Chaos; Mood stays 0 because the
  palette is the director's), the camera rig's music moves, camera spring softness, and the motion field's
  breath, orbit and drift. Each is set from the GEMS archetypes and carries seeded variety:

  | Feeling | Behaviour |
  |---|---|
  | Wonder | slow expansion |
  | Transcendence | vast, orbiting, slow camera |
  | Tenderness | close, breathing, soft spring |
  | Nostalgia | warm drift |
  | Peace | horizontal, low motion |
  | Power | dolly impacts, cuts, shake |
  | Joy | bounce, reframes, cuts |
  | Tension | tight spring, shake, chaos |
  | Sadness | breath, low, sparse |

- **What reacts to what.** Five routes are dealt from the archetypes: two from the leading feeling and one
  each from the next two, with one route per target. For example, power gives kick → camera push, bass →
  zoom and downbeat → shake; wonder gives chord colour → colour shift, melody height → camera pitch and
  harmonic change → bloom. Route amounts grow with energy. The routes run in two reaction engines of the
  director's own that crossfade at each decision, so a target never pops.

**Never the same picture.** Each draw is compared with the last six looks:
distance = 2 × hue difference + span difference + ½ saturation difference.
A draw within 0.18 is rejected, and after 12 tries the farthest draw wins. A new seed per play
(`AutoDirectorStore.newPlay`) makes each play of the same song its own film of the same feeling.
`director_test` checks that 13 looks for one feeling stay apart, and that over 20 seeds happy stays
lighter and more saturated than sad.

**Between decisions:**
- Saturation follows live energy and contrast follows live tension, both against the values the look was
  made for.
- A build-up halves the palette span (contraction), adds Speed and takes Space.
- A drop releases with an Energy, Chaos and Space burst that fades over 3 s.
- The melody's degree adds a short tint (warm for stable degrees, cool for dark ones) and a little shake
  for leaning degrees.

## 4. Wiring into every style

There is no per-style code. The director acts through machinery every style already reads:

| Stage (`core/viz/RendererFrame.cpp`) | Director's part | Reaches |
|---|---|---|
| `resolveParams`, before the macros | `Director::apply` sets palette overrides, the second palette and mix, saturation, brightness, temperature, contrast, MilkDrop tint, camera softness, motion breath/orbit/drift | every shader style through `pal()` (all 175 include `lib_palette`), the simulations through `hue::base`, MilkDrop through `paletteTint` |
| The macros | `Director::knobs` adds the director's knobs to the user's | every style (`macros::apply`), plus the macro routes |
| After the macro routes | `Director::applyRoutes` | the reaction matrix's targets |
| `stepCamera` | `Director::moves` multiplies the rig's music moves | every 3D style through `lib_camera` |

The LFOs, the user's reactions, the motion field and the safety clamp all run after the director. The
flash and motion limits therefore still hold, and the user's own settings stay on top.

Shader styles run the colour wheel backwards in `pal()`, while MilkDrop and the simulations read the base
as a plain hue. `apply` takes the scene family, so every kind lands on the same colour (tested).

**Determinism.** The director is a pure function of (seed, sound frames, dt). There is no clock and no RNG
of its own, and it is reset with the renderer. Its configuration is process-wide, like the macros
(`geode_director_set`), so an export rendered during a play uses that play's seed.

**Cost.** About 0.004 ms per frame on a desktop CPU (measured on the Unholy test track), against a budget
of 0.3 ms.

## 5. The app side

| Piece | What |
|---|---|
| `pro/ProFeature.AUTO_DIRECTOR`, `pro/AutoDirectorPolicy` | amount = 0 when off; 1 for Pro and the trial; 1 for free only during the first minute of each song. Past the taste, the renderer eases out over about a second. |
| `data/AutoDirectorStore` | the switch (general prefs) and the play's seed; pushes `GeodeNative.directorSet` only when something changes |
| `playback/AutoDirectorFeed` | listens on the player: a new seed per track, and the position each second while playing (for the taste) |
| `ui/AutoDirectorSettings` | Settings › Behaviour › Auto-director: one switch, a Pro badge and the taste line for free users |
| Paywall | a title for the feature and a comparison row (free: first minute; Pro: whole song) |

## 6. Test on a real track

The owner's test track was the Unholy piano cover, run through the whole chain (sound engine → emotion →
director). The director made 6 decisions over 189 s:

| Time | Decision |
|---|---|
| 6.6 s | first look |
| 20.1 s | the key settling on F♯ |
| 26.9 s | a drop |
| 100.1 s | long stretch |
| 172.6 s | long stretch |
| 175.7 s | a drop |

- The feeling read sad and tense (valence about −0.5, tension 0.3–0.7) throughout, which matches the song.
- Before two fixes the run made 11 decisions:
  - Scale-label flips counted as modulations; modulations are now judged on the key.
  - The form tracker fired drops on swells. A drop now needs a build held for 2 s of the last 4 (`Form.cpp`, `kHeldBuildSamples`).

## Follow-ups

- **Shader-side mood.** A `uMood` uniform (valence, energy, tension, the leading GEMS feeling) in a new
  allow-listed lib, so styles can bend their own shapes: angular for tension, round for tenderness. The
  director reaches every style without it. This is for styles that want more.
- **Datasets.** Measure key accuracy on GiantSteps Key, mood on DEAM or PMEmo, and sections on SALAMI.
  Download them for host runs only (they are research licences) and commit only the scores.
- **Recorded takes.** A take records params, not the director. Recording the seed with the take would let
  it be replayed exactly.
- **Customize.** An "auto-director amount" knob beside the macros, once the Customize UI is free of
  parallel work.
