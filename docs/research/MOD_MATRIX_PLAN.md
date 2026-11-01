# PR E plan: the Pro modulation matrix

Status: plan, not built. Written 2026-11-01 from the owner's answers and the agreement with the sound engine
session (A3, PR #213 §8b). `MOD_MATRIX_UX.md` beside this file holds the UX research (patch bays, VJ tools,
phone patching), if it was finished before the handoff. `REACTIVE_VISUALS.md` holds the earlier brief.

## What the owner asked for (2026-11-01, verbatim intent)

- **Where:** Customize › Advanced (Pro, shown locked to free users). The `MODULATION_MATRIX` slot in
  `ui/CustomizeAdvanced.kt` (`AdvancedSection`) is reserved for it and hidden today.
- **Look:** a modular-synth patch bay with routing cables. Take inspiration from modular synth patch bays,
  VJ software, and other software and hardware. Research what works on a phone.
- **Sources:** every sound value (the 72 `ReactSource`s of `GeodeSoundFrame`), every visual and Customize
  parameter, and Macros. Also 6 LFOs, 4 ADSHR envelopes (attack, decay, sustain, hold, release),
  2 step sequencers, and "other MIDI tools and modulators".
- **Destinations:** every visual and Customize parameter, plus the modulators' own parameters, so users can
  chain modulators and build complex clusters.
- **LFO rate:** Hz, or a tempo division: 1/16, 1/8, 1/4, 1/2, 1, 2, 4 or 8 bars (plus dotted and triplet).
- **Tempo sources:** auto-detect, tap tempo, typed BPM, track BPM tag, MIDI clock in. Also MIDI out.
- The modulators in Reactive stay where they are. They are envelope-followers that must respond to the
  audio. The matrix ones are the tempo/Hz kind.

## Agreed with A3 (sound engine); their code, PR #213 §8b, "stage 0"

- **One beat clock:** `core/sound/Rhythm`'s phase-locked oscillator. There is no second tracker. Sources:
  0 AUTO, 1 MANUAL (typed or tag), 2 TAP, 3 MIDI.
  - An outside period replaces the detected one.
  - Phase comes from the music, except TAP (which owns the phase at first) and MIDI (which always owns it).
  - Half and double time fold to the nearest octave of the source.
- **Process-wide, with no engine handle.** A source reaches the screen, wallpaper and dream. An export pins
  its own source: the take's, else the tag, else AUTO. TAP and MIDI are recorded into the take.
- **Native API (A3 builds it, with JNI and `GeodeNative` externs):**
  - `geode_tempo_set_source(int, float bpm)`
  - `geode_tempo_tap(int64 nanos)` (median of the last 4–8 taps; snaps the phase)
  - `geode_tempo_midi_clock(int64 nanos)`
  - `geode_tempo_midi_start/stop(nanos)`
  - `geode_tempo_midi_song_position(int sixteenths)`
  - `geode_tempo_clock(GeodeTempoClock*)`
- **What `GeodeTempoClock` holds:** bpm, source, locked, and the presentation nanos of the next beat and
  the next downbeat. Requested from A3: barPhase, beatsPerBar, beatConf, and an epoch that increments on a
  source change, a Start or a Song Position jump.
- **Taps and MIDI** arrive in system time. Native maps them to sample time through the output latency.
- **Rhythm always runs.** With MANUAL or MIDI, only the tempo estimate (the autocorrelation) is skipped.
  This replaces the owner's "only run when an LFO uses it". Explain it to the owner, because reactions, Form
  and the director always use the beat.
- **What is ours:**
  - the source picker, the tap button, the typed BPM box, and reading the BPM tag
  - MIDI in and out (`android.media.midi`). MIDI out sends 24 pulses per beat, about one beat ahead, through
    `MidiInputPort.send(bytes, 0, n, timestamp)` from the next-beat times. It is never sent from the frame
    callback.
  - `<uses-feature android:name="android.software.midi" android:required="false"/>`
  - BLUETOOTH_CONNECT (API 31+), asked in context through `ui/PermissionRequest.kt`. USB MIDI needs no
    permission.
  - privacy policy (three copies plus `PrivacyPolicyTest`) and `docs/play` Data safety notes for Bluetooth.
    No INTERNET.

## Engine (C++, ADR 0003; ours)

- The modulators live in `core/viz` beside `Lfo`, `Adsr` and `Reactions`, and are stepped on the GL thread
  once per frame before `Reactions`. They read the frame's `bpm`, `beatPhase`, `beatCount`, `barPhase`,
  `barCount` and `phrasePhase`, build a continuous beat position, and take any division from it. Nothing
  sub-beat is computed natively by A3.
- **Modules:**
  - 6 LFOs: sine, triangle, saw up and down, square with pulse width, sample-and-hold, smooth random.
    Each has a rate (Hz or division), phase, retrigger (free, beat, bar, or epoch), and a unipolar or
    bipolar output.
  - 4 ADSHR envelopes, triggered by beat, downbeat, phrase, a hit source, a MIDI note, or a threshold on
    any source.
  - 2 step sequencers: 1–16 steps, a rate division, per-step value, glide and probability.
  - Utilities ("other modulators"), to agree with the owner in the plan: slew, quantize, invert/offset,
    multiply, sample-and-hold, and a MIDI CC/note input.
- **Routes:** source → destination, with an amount (−1…1, an attenuverter) and an optional curve.
  - A destination can be a visual param, a Customize param, a macro, or any modulator param.
  - Evaluation is in topological order. A cycle is allowed but delayed one frame on its back edge. It is
    marked in the UI.
- **Storage:** extend the saved-reactions codec (`render/ReactionRouteCodec`, `LfoStore`), or add a sibling
  `ModPatch` codec in pure Kotlin (JVM-testable, not `org.json`), with presets saving it.
- **Exports** step it at 1/fps through `OffscreenSceneRenderer`, so export equals live.
- **Tests:** `core/tests/viz_math_test.cpp` (divisions, retrigger, ADSHR, the step sequencer, cycle delay),
  a JVM codec test, a Compose UI test of patching, and a journey.

## UI plan (to show the owner as sketches before code)

- Advanced › **Modulation**, with one page holding three tabs: **Patch** (cable view), **Routes** (list
  with amount sliders, also the TalkBack path), and **Modules** (cards to edit each LFO, envelope and
  step sequencer).
- **Patch view:** jacks in columns (Sound, Modulators, Macros on the left; Visual, Customize and modulator
  params on the right). Drag from a jack, or tap a jack then tap another, to patch. Cables are coloured by
  source kind and sag a little. Zoom and pan. Long-press a cable to set its amount or delete it.
  Categories collapse so it never shows 200 jacks at once.
- Advanced › **Tempo:** the source picker (Auto, Typed, Tag, Tap, MIDI), a big tap button, the BPM readout
  (dimmed while unlocked), and MIDI in/out device pickers.
- Free users see it locked (`ProLock`), as with the other Advanced cards.

## Order of PRs and next steps

1. **Plan for approval:** finish `MOD_MATRIX_UX.md` (if unfinished), draw phone sketches, and ask the owner
   to choose the layout and the utility module list.
2. **E1, engine:** `core/viz` modulators, routes and the cycle delay, JNI, the Kotlin codec, and tests.
   Hidden in the UI.
3. **E2, matrix UI:** the Patch, Routes and Modules tabs, un-hiding `MODULATION_MATRIX`, Pro gating, and
   a UI test.
4. **E3, tempo and MIDI:** after A3's stage 0 merges. The source UI, tap, BPM tag, MIDI in and out, the
   permission, and the policy and Data safety notes.
5. **Then the Customize search idle bug:** on the emulator, Compose never goes idle while a query is typed
   on Customize. It is a possible continuous-recomposition battery drain. Already ruled out: the soft
   keyboard, the shader editors and `ProGated`. Next, look at the search results composable for state
   written during composition, or an infinite animation (Layout Inspector recomposition counts, or a
   `Snapshot.registerApplyObserver` log in a debug journey).
6. **Last, the in-app help pages** (owner: build when everything else is done). Markdown in
   `assets/help/`, rendered like the privacy policy, and mirrored to Pages `/help/` (`pages.yml` already
   publishes `docs/help/`).
