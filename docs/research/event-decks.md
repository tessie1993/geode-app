# Event decks: no visual response twice in a row

Customize › Events, rebuilt around one rule: **an audio event never gets the same visual response twice
in a row.** Kick 1 zooms, so kick 2 may do anything but zoom. This replaces the old Events tab and its
`EventRecipe` chips. Agreed with the user on 2026-10-31; the choices marked *(user)* are theirs.

## What lives where

| Place | Holds | Repeats? |
|---|---|---|
| **Events** (this doc) | Musical *hits*: kick, snare, every 4th kick, drop, section change, build-up, silence, hi-hat. One row per hit, each with a response deck. Set presets (Club, Hip-hop, Ambient, Chaos), Variety. | Never, by construction |
| **Reactivity** (moved from Events, *(user)*) | The reaction grid from #183, reaction presets by feel and the route list: the smooth links (levels, clocks, melody → parameters). | Continuous, so "twice" has no meaning |
| **Macros** | The global knobs, plus the new **Variety** knob. | n/a |

### The deck owns the hit *(user)*

The style DNA wires many routes to the same hits (`KickEnv → CamPush`, `Kick → Spawn`, `SnareEnv →
CamShake`…). Each of those fires identically on every kick, which breaks the rule whatever the deck does.
So while a row's deck is on, **every DNA route whose source is that hit** (its trigger, its Nth counts and
its envelope) leaves the matrix, and the DNA's response to that hit becomes the row's first cards
instead. Smooth sources (`BassAtt`, `Intensity`, clocks, melody) stay in the matrix and in the grid.

Hit families (`EventDecks::family`):

| Row | Reads | Family it owns in the matrix |
|---|---|---|
| Kick | `kick` | `Kick, KickEnv, KickToggle, KickRandom, Kick2` |
| Snare | `snare` | `Snare, SnareEnv, SnareToggle, Snare2, Snare4` |
| Every 4th kick | `kick4` | `Kick4, Kick8, Kick16` |
| Drop | `surge`, `returned` | `Surge, Returned` |
| Section change | `hardCut` | `HardCut` |
| Build-up | `energyTrend` rising past 0.7 | (none: `EnergyTrend` stays a smooth link) |
| Silence | `silence` | `Silence` |
| Hi-hat | `hat` | `Hat, HatEnv` |

Kick, snare, every 4th kick and drop start with decks. The others start empty, so they keep the DNA's own
routes until the user deals cards into them.

## Cards

A card is one visual response: a target, a shape, a size and amounts.

- **Shapes**: *pulse* (a punch that decays, the old Follow-on-envelope), *step* (lands somewhere new and
  stays: a hue jump, a quarter turn), *toggle* (flips between two states: palette, mirror), *jump* (a new
  seeded value per hit). Each card keeps its own attack and release.
- **Size**: *small* or *big*. Soft hits (trigger strength under 0.75) skip big cards, which stay in the
  bag for later. Drops and section changes take big cards first.
- **Catalogue** (`EventDecks::kCards`): zoom punch, colour shift, twist, camera jolt, camera push, roll,
  new angle, fractal spawn, palette flip, warp, ripple, lens, glitch split, strobe (soft: brightness goes
  through the luminance floors and `safety::apply`, like every route), and the Layers effects: fall
  through, fractal grow, spawn, melt, freeze, shatter, particle burst. A card whose target the style can't
  show is left out of that style's deck.

## The draw

Each row is a **shuffle bag** (Tetris's "7-bag", the standard no-streak randomiser):

1. The bag holds every card in the row's deck (scaled by Variety, below). It is shuffled with
   Fisher–Yates from a seeded xorshift.
2. Each hit draws the next card. Every card is used once before the bag refills.
3. **The refill never puts the last card first.** If the new order would start with the card just drawn,
   the first card swaps with a random later one. So no card repeats across the seam either, and with three
   or more cards there is no two-step ping-pong (A B A B).
4. **Cross-source block.** A card any row drew is blocked for the *other* rows for one beat. The beat is
   the smoothed time between kicks, clamped to 0.3–1 s (0.5 s before any kick). A blocked card is skipped,
   not lost: it stays at the head of the bag. That way a kick zoom and a snare zoom on the same beat
   don't read as one response twice.
5. **Hi-hats** rotate at most once per bar (four kick periods): hats within that window replay the held
   card. That card is a small shimmer, and a new card on every 16th note is noise, not variety.

### Phrase lock (on by default)

With phrase lock on, the bag order repeats every phrase, so a four-bar groove gets a four-bar
choreography, the way a VJ loops a cue. A phrase is 16 kicks (four bars of four-to-the-floor), or 32 on
the Ambient set. At each phrase start the row's bag is rebuilt from the same seed. If its first card equals
the last one drawn, the order starts one card later: the seam is still never a repeat, and the result is
still a function of the hit history, so it stays deterministic. A **section change** (`hardCut`) bumps the
section number, which reseeds every bag.

### Seeding

`seed = hash(trackSeed, styleSeed, section, row) ^ phraseCount` (phrase lock off: plus the bag count).
`trackSeed` is a hash of the track's media id, sent once per track (`geode_viz_set_track_seed`).
`styleSeed` is `SceneParams.reactSeed`. Nothing reads a clock or `rand()`, so the live screen, an export,
the wallpaper and the dream all deal the same cards for the same song.

## Vary and Fixed

Each row is **Vary** (deals from its deck) or **Fixed** (always its first card). The style DNA's rows
default to Vary. A recipe converted from the old tab, or any row the user pins, is Fixed: a Fixed row
repeats on purpose, because the user chose it.

**Variety** (Macros, 0..1, default 1) sets how many of each row's cards are dealt:
`n = 1 + round(variety · (size − 1))`. At 0 every row is effectively Fixed.

Each row has a **Test** button that fires the hit once through the live engine, so the next card
shows on screen without music.

## Set presets

Whole-set presets replace every row's deck at once:

| Set | Kick | Snare | Every 4th kick | Drop | Notes |
|---|---|---|---|---|---|
| **Club** | zoom punch, camera push, colour shift, twist, lens, warp | glitch split, roll, ripple, strobe | palette flip, new angle, fractal grow | shatter, spawn, fall through | phrase 16, hats on |
| **Hip-hop** | camera jolt, zoom punch, warp, ripple | colour shift, roll, glitch split, twist | new angle, palette flip | freeze, shatter | phrase 16, snare-led |
| **Ambient** | colour shift, warp, fall through, fractal grow | (off) | palette flip, new angle | melt, spawn | phrase 32, small cards only |
| **Chaos** | every card | every card | every card | every big card | phrase lock off, block ½ beat |

## Old recipes *(user: convert)*

`EventRecipe` is removed. Saved recipes were only ordinary routes in the user's reactions
(`LfoStore`), so migration reads those routes once (`EventDeckMigration`):

- a route whose source is in a row's hit family becomes that row's card (its target, amount and times),
  and the row turns **Fixed**;
- every other route stays where it is, as a grid route.

## Bars without a tempo tracker *(user: research MilkDrop3)*

The sound engine has no tempo or downbeat tracker. MilkDrop3 doesn't need one either:

- Its beat modes (F8, "hardcut1–7") are **relative-energy thresholds with a minimum gap**. `bass` and
  `treb` read 1.0 at the song's average. hardcut1 loads a preset when bass passes 1.5, at least 0.2 s
  apart; hardcut2–5 use treble over 2.9 with 0.5–5 s gaps. **hardcut7** (v3.31) *injects an effect into
  the running preset* on treble over 2.9, at least 1.5 s apart, which is the closest thing in MilkDrop to
  a deck. None of them counts bars.
- The big-moment hard cut is MilkDrop2's own: when the sum of `imm_rel` passes a threshold (2.5 × 3), the
  threshold doubles, then decays back with a 60 s half-life. `core/sound`'s `hardCut` already copies this
  (`hardCutLevel` 2.5, `hardCutHalfLife` 60), so it is our section change.
- MilkDrop3 has no grid. Everyday beat behaviour is one mode you cycle with a key; expert editing is the
  MilkPanel. That is why Events shows decks and sets, and the grid moves to Reactivity.

So decks follow MilkDrop: hits are the engine's own thresholded triggers, each row has a minimum gap
(kick 0.1 s, snare 0.1 s, 4th kick 0.5 s, drop 1.5 s like hardcut7, hats one bar), and phrases count
hits rather than finding bars. "Downbeat" is labelled honestly as **every 4th kick**. A real causal beat
and downbeat tracker in `core/sound` is a separate follow-up: it would lock "every 4th kick" to the real
bar on hip-hop and half-time.

## Judged on three genres

- **House / techno (124 BPM, four-to-the-floor).** Kick every beat: a six-card deck cycles every 1.5 bars.
  Phrase lock makes the 4-bar loop read as choreography. Every 4th kick is the true downbeat.
  Hats rotate once a bar, so the shimmer doesn't strobe.
- **Hip-hop (boom-bap, ~90 BPM, kick on 1 and the "and" of 3, snare on 2 and 4).** About two kicks per
  bar, so "every 4th kick" lands every second bar, not on each downbeat, and a 16-kick phrase is about
  eight bars. The no-repeat rule still holds exactly; only the phrase period is longer than the musical
  one. The Hip-hop set leads with the snare deck, which does sit on the backbeat.
- **Ambient (few or no drums).** Kick and snare rows rarely fire. Section changes (`hardCut`), drops
  (`returned` after silence) and the Ambient set's 32-hit phrases carry it, with small, slow cards (colour
  shift, warp, fall, melt).

## Tests (`core/tests/viz_math_test.cpp`)

- The same synthetic song (seeded kick/snare/hardCut pattern) gives the same card sequence twice.
- No row ever deals the same card on two consecutive hits, across bag seams and phrase seams, for every
  deck size from 2 to 8 and Variety from 0.25 to 1 (Fixed rows excepted).
- No A B A B ping-pong with three or more cards.
- A card one row drew is not drawn by another row within the block window.

## Sources

- MilkDrop3 README, beat-detection modes hardcut1–7 and the v3.31 notes:
  https://github.com/milkdrop2077/MilkDrop3
- MilkDrop2 hard-cut detector (`m_fHardCutLoudnessThresh`, `m_fHardCutHalflife`, the doubling
  threshold): `code/vis_milk2/milkdropfs.cpp` in the same repository.
- projectM Preset Authoring Guide, `bass`/`mid`/`treb` "1 is normal":
  https://github.com/projectm-visualizer/projectm/wiki/users/Preset-Authoring-Guide
- The 7-bag randomiser (Tetris Guideline random generator): https://tetris.wiki/Random_Generator
- Fisher–Yates shuffle: Knuth, *TAOCP* vol. 2, § 3.4.2, Algorithm P.
