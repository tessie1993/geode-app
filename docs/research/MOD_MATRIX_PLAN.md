# PR E plan: the Pro modulation matrix

Status: **agreed with the owner on 2026-11-01**, not built. `MOD_MATRIX_UX.md` beside this file holds the research
(patch bays, VJ tools, phone patching, Android MIDI) and the detailed UX spec (§6). `REACTIVE_VISUALS.md` holds the
earlier brief. Tempo comes from the sound engine's one beat clock, agreed with the sound engine session (PR #213,
`docs/research/sound-engine-v2.md` §8b, "stage 0").

## 1. Decisions

| # | Question | Decision |
|---|---|---|
| D1 | What is the matrix? | A **dictionary**: a native `ModRegistry` where every module registers its parameters as named **jacks** (id, label, group, range, unit, input or output). The matrix looks jacks up and routes between them, and gives one overview. Modules keep their own engines. |
| D2 | Existing modulators (Sound response's 3 LFOs and 2 envelopes, Reactions' 12 routes) | **Stay where they are**, engines and pages unchanged. They register their outputs (and their rate and depth) as jacks, so the matrix can use them. |
| D3 | How modulation adds up | **Like an LFO in Ableton, chained.** A knob keeps the base value the user set. Each cable adds `amount × source` as a share of the destination's range, around that base. Cables into one jack stack into **one cumulative float**, clamped to the jack's range. A modulator's own parameters (rate, depth, phase …) are jacks too, so modulators chain. |
| D4 | Loops (LFO1 → LFO2 rate → LFO1 depth) | A cable whose source is a modulator reads that modulator's **output from the previous frame**. Order never matters, loops cannot hang, export equals live. No graph sort. |
| D5 | Tempo sync | **In.** The KDoc in `engine/scenes/.../render/Lfo.kt` ("no tempo sync, a wrong estimate drags the look") predates the musical layer. `core/sound/Rhythm` now runs a phase-locked beat clock in every frame (`beatPhase`, `barPhase`, `phrasePhase`, `beatConf`, `bpm`), and `MotionField` and the director already move on it. Synced rates read that grid. Rewrite the KDoc in E1. |
| D6 | Where it lands in the frame | **After Reactions**: director → Sound response LFO/ADSR → macros → Reactions → **matrix offsets** → `MotionField` → `safety::apply` (always last, whatever the patch) → `CameraRig`. |
| D7 | UI | **One new card**, Advanced › **Modulation**, nothing else changes: no "Modulate…" long-press on other pages, no separate Tempo card. Three tabs: **Patch** (cable bay), **Routes** (list, also the TalkBack path), **Modules** (cards). A tempo bar sits on top of the page. Spec: `MOD_MATRIX_UX.md` §6. |
| D8 | Where a patch lives | **One active patch** (global, every style, like the Sound response LFOs today) plus a **Patches library** in its own folder (`filesDir/modpatches/`, subfolders like presets): save, load, rename, duplicate, delete, share, built-in starters. The library goes into Backup. Presets do not carry patches. |
| D9 | Free tier | **Locked card, no taster**, like the rest of Advanced (`ProGated`). The 1-hour trial gets it in full. |
| D10 | Utilities | All of: Slew, S&H, Quantize, Math, Compare/Logic, Random, Constant, MIDI CC/note in. |
| D11 | MIDI | USB and virtual (other apps on the phone) first: clock in and out, CC and note in. Bluetooth MIDI in its own later PR. |
| D12 | Out of this plan | The Customize search idle bug and the in-app help pages. Noted for the main session; not modulation work. |

## 2. The registry (E0)

`core/viz/mod/ModRegistry.{hpp,cpp}`. Built once per renderer; a table, not a graph.

- **Jack:** a stable string id (`sound.kick`, `param.zoom`, `lfo2.rate`, `macro.3`, `cam.orbit`), a label, a group, a
  range, a unit (from `render/scene/ParamFormat` on the Kotlin side), a direction (out, in, or both), and a pointer or
  index into the module that owns it. Ids are the saved form, so old patches survive reordering.
- **Who registers:**

  | Module | Outputs (sources) | Inputs (destinations) |
  |---|---|---|
  | Sound frame (`core/sound`) | every `ReactSource` (117 today; #211 adds note, key and mood ones, which appear by themselves) | none |
  | Rhythm | beat, bar and phrase phase and triggers, bpm, beatConf (already `ReactSource`s) | none |
  | Macros (`core/viz/Macros`) | the 6 knobs | the 6 knobs |
  | Scene params | every `SceneParams::fieldNames()` field with a catalogue range, previous frame's final value | the same fields (154 today; #216 deletes `glitch`, and the table follows) |
  | Camera rig | its offsets | `CamFly … CamShake` |
  | Sound response | LFO 1–3 and envelope 1–2 outputs | their rate and depth |
  | Matrix modulators (E1) | each one's outputs | each one's settings |
  | MIDI (E4) | learned CCs, note gate, velocity, pitch, clock | none |
  | Tempo | bpm, locked | none |

- Generated, not hand-listed: params from `fieldNames()` plus the catalogue's ranges and `ParamScope`, sources from the
  `ReactSource` table, so a new field or source is a jack without a code change. Jacks the current style does not read
  (by `ParamScope`) are flagged, so the UI can hide them.
- `LfoTarget` ordinals stay as they are for the Sound response saves; the registry maps them to jack ids.
- JNI: one text table (like `geode_catalogue_text`) for Kotlin to read jacks, and one float snapshot per frame for meters,
  read at most 30 Hz.
- No UI in E0. Test: every field with a range is a jack; every `ReactSource` is a jack; ids are unique and stable.

## 3. Modulators and routes (E1)

In `core/viz/mod/`, stepped once per frame on the GL thread before the offsets are applied; export steps them at
1/fps through `OffscreenSceneRenderer`, so export equals live.

- **6 LFOs:** sine, triangle, ramp up, ramp down, square with pulse width, sample-and-hold, smooth random. Rate in Hz
  (0.01–30, shown as "every 12 s" when slow) or **synced**: 1/32 … 8 bars, dotted and triplet. Phase 0–360°.
  Polarity bipolar or unipolar. Restart: Free, Trig, Hold, One, Half (Elektron's set), with a Reset input. Fade-in.
  - Synced rates build a continuous beat position from the frame's `beatCount`, `beatPhase`, `barPhase`. While
    `beatConf` is low the LFO keeps its last tempo. A synced LFO never jumps: a phase error is slewed out over a beat,
    and a hard reset happens only when the tempo clock's `epoch` changes (a source change, a MIDI Start, a Song
    Position jump).
- **4 ADSHR envelopes:** attack, decay, sustain, hold (a minimum gate time, so a one-frame hit still reaches sustain),
  release, amount; linear or exponential curves; retrigger from zero or from the current level. The **Gate** is an input
  jack any source can feed (kick, onset, a sequencer gate, an LFO square, a MIDI note), with a threshold.
- **2 step sequencers:** 1–16 steps, each with a value (−1…1), a gate, a probability and a condition (1:2, 2:2, 1:4 …);
  glide per step or global; length, direction (forward, back, ping-pong, random), swing; clock from a synced division
  or a Clock input. Outputs: Value, Gate, End of cycle. Randomness is seeded and saved, so an export replays it.
- **Utilities:** Slew (rise, fall), S&H (In on Trigger), Quantize (N levels or divisions), Math (A×B, A+B, min, max,
  invert, offset, scale), Compare/Logic (A>B gate, AND, OR, every Nth), Random (new value per beat, bar or trigger),
  Constant, MIDI CC/note in (filled in E4). Slots: 8 utilities per patch.
- **Routes (cables):** source jack → destination jack, amount −100…+100 % of the destination's range (the attenuverter),
  an optional curve and an optional smoothing time. When the destination is a scene param, the cable may take a
  Reactions mode (Follow, Step, Spin, Jump, Toggle). At most **32 user cables** per patch.
- **Summing (D3):** per destination, `final = clamp(base + Σ amount·shape(source) · range)`. The base is whatever the
  earlier stages set (the user's knob, macros, Reactions). Modulator inputs read last frame's sum (D4).
- **Bypass:** one switch skips the whole matrix (an A/B check); stored in the patch, default on.
- **Storage:** `ModPatchCodec`, plain text `modpatch/1` in the style of `render/ReactionRouteCodec` (pure Kotlin,
  forgiving decode, not `org.json`), in `:engine:scenes`. The active patch is a prefs key beside `LfoStore`'s; the
  library is one file per patch in `filesDir/modpatches/` (subfolders allowed), added to the Backup allowlist.
- **Tests:** `core/tests/modmatrix_test.cpp` (divisions, restart modes, ADSHR hold, sequencer probability replay,
  loop determinism, live vs 1/fps equality, clamp, safety last), a JVM codec test.
- Hidden in the UI until E2.

## 4. Tempo (E3, after #213's stage 0 merges)

The sound engine session builds the clock and its API; we build the controls.

- **One beat clock,** `Rhythm`'s phase-locked oscillator. Sources: 0 AUTO (default), 1 MANUAL (typed BPM or the
  track's BPM tag), 2 TAP, 3 MIDI. An outside period replaces the detected one; phase comes from the music except TAP
  (owns it at first) and MIDI (always). Half and double fold to the nearest octave.
- **Rhythm always runs** (reactions, `Form` and the director use the beat); with MANUAL or MIDI only the tempo estimate
  is skipped. This replaces the earlier "detect tempo only when something uses it".
- **Process-wide.** A source reaches the screen, wallpaper and dream. An export pins its own: the take's, else the tag,
  else AUTO. TAP and MIDI are recorded into the take.
- **Native API (theirs):** `geode_tempo_set_source(int, float bpm)`, `geode_tempo_tap(int64 nanos)`,
  `geode_tempo_midi_clock(nanos)`, `geode_tempo_midi_start/stop(nanos)`, `geode_tempo_midi_song_position(int)`,
  `geode_tempo_clock(GeodeTempoClock*)` (bpm, source, locked, next beat and downbeat in presentation nanos, barPhase,
  beatsPerBar, beatConf, epoch). The BPM tag is read in `core/library` (ID3 TBPM, Vorbis/FLAC `BPM`, MP4 `tmpo`).
- **Ours:** the tempo bar (UX §6.6): source chip, tap button, typed BPM, ×2 and ÷2, calling the tag read. Taps arrive in
  system time; native maps them through the output latency.

## 5. MIDI (E4; Bluetooth later)

- `android.media.midi` (`MidiManager`), USB and virtual devices. No runtime permission and no INTERNET.
  `<uses-feature android:name="android.software.midi" android:required="false"/>`.
- **In:** clock (24 ppqn, Start, Stop, Continue, Song Position) to `geode_tempo_midi_*`; CC (learn by moving a knob),
  note gate, velocity and pitch as jacks in the MIDI group, through the MIDI CC/note utility.
- **Out:** clock, 24 pulses per beat about one beat ahead with `MidiInputPort.send(bytes, 0, n, timestamp)` from the
  clock's next-beat times, never from the frame callback. Start and Stop follow playback.
- MIDI is live input; exports hear it only through a take. The UI says so.
- **Later PR, Bluetooth MIDI:** `MidiManager.openBluetoothDevice`, `BLUETOOTH_SCAN` (`neverForLocation`) and
  `BLUETOOTH_CONNECT` on API 31+, asked in context through `ui/PermissionRequest.kt` when the user taps "Add Bluetooth
  device"; location on API 26–30 or Companion Device Manager (check first). Privacy policy (three copies plus
  `PrivacyPolicyTest`) and `docs/play` Data safety notes.

## 6. PRs

Each is shippable alone and updates `AGENTS.md` when structure changes (the arch-map gate).

1. **E0, registry.** `ModRegistry`, the generated jack table, the JNI text table, tests. No UI.
2. **E1, modulators and routes.** §3, plus the stale `Lfo.kt` KDoc. Hidden.
3. **E2, Modulation UI.** The card un-hidden (`MODULATION_MATRIX`, with its own icon, not Reactions' `Bolt`), the
   Patch, Routes and Modules tabs, the Patches library, starters, Pro gate, TalkBack actions, a Compose UI test that
   patches through semantics only, and a journey. Tempo bar shows AUTO only.
4. **E3, tempo.** After #213 stage 0. Source chip, tap, typed BPM, tag.
5. **E4, MIDI.** USB and virtual, clock in and out, CC and note in.
6. **E5, Bluetooth MIDI.** Permissions and policy.

## 7. Depends on other open PRs

- **#213 (sound engine v2)** owns the tempo API and the BPM tag read; E3 waits for its stage 0.
- **#211 (note and key reactions)** adds sources; they become jacks through the `ReactSource` table.
- **#216** deletes the `glitch` param and `LfoTarget.Glitch`; the jack table is generated, so nothing to remove here.
- **macro-reset** edits the macros; the matrix only reads and writes the 6 knobs as jacks.
