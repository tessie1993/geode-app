# 02: Musical time and form, fosfora against Geode

Scope: beat, tempo, downbeat, phrase, sections, build-up/drop, drop prediction, render-side phase interpolation.
Fosfora @ `2e85d3b` (paths below are under `crates/fosfora-app/src/` unless they start with `docs/`, `bench/` or
`scripts/`). Geode @ `37761e8a` (paths start with `core/`, `docs/`, `app/` or `engine/`). Fosfora structure claims were checked
in the Graphify index of `tessie1993/fosforatess` (same commit): `advance_phase` is called only by
`FeatureInterpolator::advance_{beat,bar}_phase` (`audio/interp.rs:274,315`); `bar_clock` only by
`DownbeatTracker::process` (`audio/downbeat.rs:238`); the one hop chain is `HopAnalyzer::process_hop` (`audio/hop.rs:200`).

Experiments: `rhythm_exp.cpp`, `rhythm_exp2.cpp`, `rhythm_exp3.cpp`, `mono_exp*.cpp`, throwaway harnesses written for this
study (not committed; the tests recommended in §5 recreate them). They build against Geode's real `core/sound/*.cpp` (g++ -O2, as `android.yml:140` does) and drive `SoundEngine`
with synthetic audio, like `core/tests/musical_test.cpp`. `musical_test` passes at this commit.

---

## 1. How fosfora works, detector by detector

### 1.1 Onset (`audio/beat/onset.rs`)
- SuperFlux (Böck & Widmer 2013) on the 4096-point spectrum: 64 log-spaced bands from 20 Hz to 16 kHz, log-magnitude,
  half-wave-rectified difference against a **±1-band frequency max filter** of the previous frame (`onset.rs:15-19,137-151`).
  The max filter is there so vibrato and wobble bass stop firing.
- Three partitions with **fixed weights low .60 / mid .28 / high .12**, split at 250 Hz and 2 kHz (`onset.rs:23-27,155-160`).
  The comment says the weights "preserve the kick/bass-vs-snare-vs-hat balance". This choice matters a lot in §4.
- Adaptive threshold: median + 2·MAD over 0.5 s, capped at 0.5 × the 4 s maximum and 0.8 × the 0.5 s maximum
  (`onset.rs:177-196`). 50 ms cooldown (`beat/mod.rs:163,309-315`). "Sustained silence" means 30 hops under the −55 LUFS gate
  (`onset.rs:198-200`).

### 1.2 Kick model (`audio/kick_model.rs`) and beat activation (`audio/beat_model.rs`)
- **Kick:** gradient-boosted trees, 300 trees of depth ≤ 6, 72 inputs: per-band log-flux and relative level in 11 bands from
  30 Hz to 10 kHz, plus low-span flatness and peakiness, for this hop and the two before it (`kick_model.rs:32-46`). Labels came
  from Demucs drum stems of CC BY/CC0 FMA tracks, with hits that are mostly bass bleed rejected
  (`scripts/kick_model/README.md`). Held-out MDB Drums F1 is **.71** against the old band-flux kick's .51; a kickless stretch
  that fired the old kick 57 times fires this one 0 times (`kick_model.rs:3-23`). The tuned threshold maps to 0.5, so the old
  0.5/0.3 hysteresis keeps its meaning (`kick_model.rs:131-139`).
- **Beat activation (#4163):** 200 trees of depth 5, 168 inputs (24 columns × hop offsets −6..0). It reports P(a beat landed
  **3 hops ago**): the lag is a deliberate target that `build_dataset.py --lag-check` chose. It was trained on 3.96 M rows
  with a Gaussian σ = 1-hop target (`beat_model.json` "trained"). Inputs are resolved by name at load, look-ahead is refused,
  and a missing column panics at the first hop (`beat_model.rs:89-188`). Its causal peak picker (k = 6, thr .45) is
  `beat_model.rs:244-261`. It is **evidence, not a tracker**: its events go to the phase arbiter at a stricter .70
  (`beat/scheduler.rs:230-234`), and since #4250 its ACF picks the tempo period (§1.3).

### 1.3 Tempo (`audio/beat/tempo.rs`, `kalman.rs`, `control.rs`)
- 8 s history (`beat/mod.rs:148`). The ACF runs every 6 hops (~14 Hz, `tempo.rs:472`), FFT-based (Wiener–Khinchin) and
  mean-subtracted, with the **(n−lag) bias divided out**, the factor capped at 4. The biased ACF had a "permanent structural
  bias toward double-time (~+16%)" (`tempo.rs:866-880`).
- Candidates use **continuous lag**: a parabolic vertex per local peak, the top 5 peaks, each projected through 9 metrical
  ratios (1:4 … 2:1). Integer lags cannot hit the 4 % Acc1 tolerance, since bins are 2.5–3.4 % apart at EDM tempi
  (`tempo.rs:884-950`). Score = comb (1/h-weighted **mean** of ACF at h·lag, h = 1..4; a mean so short-history candidates are
  not penalised, `tempo.rs:134-153`) × log-Gaussian prior × continuity bonus (+25 % within σ = 0.06 log2, `tempo.rs:31-32,1001-1007`).
  Confidence = (peak − median floor)/(1 − floor) (`tempo.rs:960-972`).
- **Two ACFs, two jobs (#4250):** the flux ACF decides *when* a tempo is earned. The seed gate is conf ≥ .35, or two readings
  that agree within 5 % ≥ 2 s apart; white noise holds conf ≈ .26 and self-agrees, which is why the span is load-bearing
  (`tempo.rs:18-27,492-517`). The activation's ACF then decides *which period*, with flux as the fallback
  (`tempo.rs:519-531`). The activation must never seed: it is sparse on beatless audio, and the normalised ACF reads that as
  periodic. Replica forecast: 234/344 Ballroom on the annotated tempo vs 200 with flux (`bench/manifests/q4250_tempo_input_frozen.json`).
- **Kalman filter in log2-BPM** (`kalman.rs`): q = .001 and R = .01 + .5·(1 − conf), which is adaptive. A leaky divergence
  counter (>10 % off) hard-resets after 15 updates (~1 s), or 45 (~3 s) while anchored (`kalman.rs:90-109`). While locked, an
  innovation beyond .057 log2 (~4 %) gets R × 6 (`tempo.rs:42-43`, `kalman.rs:137-141`). This was measured as up to 10 excursions
  per GiantSteps track before the gate.
- **Lock:** an EMA (τ 2.5 s) of "winner agrees within 4 % and conf ≥ .2", with a binary target, not confidence-scaled ("a
  clean signal's absolute confidence sits near 0.5"). Enter at .6, exit at .35 (`tempo.rs:33-38,676-705`). Published as
  `tempo_confidence` / `beat_locked`; the readout holds the last locked BPM through unlocks (`tempo.rs:365-371`).
- **Metrical anchor (Q2b):** 8 s of lock earns an anchor level. After that, winners at ×2, ½, 3:2, 2:3, 4:3 or 3:4 (±.08 log2)
  are **folded onto the anchor** and do not reset the filter (`tempo.rs:44-69,533-602`). Displacement needs a challenge EMA
  > .7 for 10 s at one level, and only inside a 30 s probation. **Octaves never displace** (owner-ruled after logged live runs:
  "half-level evidence mid-track is a half-time FEEL"). Ghost anchors (60 s TTL, ≥ 30 s tenure) stop a level war from minting
  a wrong ⅔ anchor (`tempo.rs:88-121,707-759`). The operator's escape is tap and octave-shift, which re-aim the offset
  (`tempo.rs:376-442`).
- **Prior:** centre **150 BPM**, σ = 1 octave. #78 benched 120, 130 and 120/σ.75: every lower centre halved fast EDM
  (GiantSteps Acc1 69.9 % → 53.6–60.1 %) for ≤ +2.5 points on Ballroom (`control.rs:197-210`). Genre presets exist
  (House 127/.35, DnB 172/.3, Hip-hop 90/.4, Ambient 70/.6, `control.rs:234-243`). The range is 40–300 BPM (`control.rs:160-161`).
  Ableton Link "Follow" pins the prior to the session tempo at σ .08, so phase still comes from audio (`link/mod.rs:8-38`).
- Measured (`docs/BENCHMARKS.md`): GiantSteps Acc1/Acc2 .739/.861 (offline Schreiber CNN .730/.893). Ballroom .689/.964;
  Harmonix .626/.965. Mean lock time 11.3 s / 18.5 s / 27.1 s.

### 1.4 Beat grid (`audio/beat/scheduler.rs`)
- **Beats fire at predicted grid instants. Onsets only correct phase** (`scheduler.rs:11-32`). This inverts an older
  "fire when we notice" machine, which fired 80–92 ms late and over-fired 1.5–1.7×.
- **Modes:** locked and supported (fire on the grid); locked and unsupported (**muted freerun**: support is an EMA α .25 per
  beat, mute below .2, unmute above .4, `scheduler.rs:74-77`); unlocked acquisition (fire only when onset and grid agree, at the
  onset's time, ≥ 0.6 period apart; re-anchor after 4 silent periods, `scheduler.rs:79-82,807-850`); no tempo (no beats). The grid
  count includes muted instants, so bar rotation stays coherent through breakdowns (`scheduler.rs:275-278`).
- **PLL:** each slot takes bids within ±25 % of a period. Score = √strength × exp(−½(e/60 ms)²), and bids under .05 are
  ignored. Each of those three choices fixes a measured failure: first-capture-wins, crescendo walking the grid early, and a
  dead zone (`scheduler.rs:34-48,649-687`). Late-side bids close at the slot deadline. PI correction: phase gain .3, rate trim
  .02, clamp ±2 %, trim decaying .85 per supported beat and .5 per unsupported one (without decay: a −20 ms/beat runaway,
  `scheduler.rs:63-73`). Support counts onsets within ±10 % of a period, capped at 70 ms.
- **Phase arbiter (#4160/#4234/#4402):** this is the fix for a grid locked on the off-beat. **28 % of emitted beats sat on the
  half-beat with the tempo right** (`scheduler.rs:84-127`). Kick events plus activation peaks are folded over 8 and 32 beats.
  The grid moves half a beat later if half ≥ 2 × on over the last 8 (≥ 4 events), the 32-beat window agrees (half ≥ 1.5 × on,
  on ≤ 40 %), and this holds 3 instants in a row. Quarter offsets need 3:1 and ≥ 24 events (`scheduler.rs:162-173,422-515`).
  Kicks alone were wrong on house off-beat basslines; activation alone missed others. The union gained 17 Harmonix holdout
  tracks and lost none (.620 → .654).
- **Emission lead:** beats are emitted **10 ms before** their instant, because the grid phase-locks onto late-seen evidence:
  +19 ms late against Harmonix annotations, +20–29 ms on exact synthetic renders. The 10 ms was a pre-registered round: Harmonix
  .753 → .758 tune, .711 → .717 holdout, no track lost (`scheduler.rs:49-61`, `bench/manifests/q4474_lead_frozen.json`). The
  per-beat trackers key on `settled_beat_time`, not on the early emission (`beat/mod.rs:46-50`).
- **User sync offset (#80):** a positive offset (≤ 200 ms) is *prediction*: the beat and the phases are led, capped at 0.45 of
  a period (`control.rs:281-290`, `scheduler.rs:535-541`). A negative offset (≥ −250 ms) holds **every** feature back in a
  sample-clock queue (`audio/sync_delay.rs:1-39`), since levels cannot be predicted.
- **Phase monotonicity (#293):** the locked phase is (t − last)/(next − last), floored per interval so a correction can never
  move it back. It reaches 1 only as the instant settles (`scheduler.rs:861-882`).
- Measured: Harmonix beat F .737 (CMLt .543, AMLt .819). Ballroom .549 (Ballroom is out of genre, with 30 s clips against an
  ~11 s lock). Beatless set: 155 FMA ambient clips, where the pre-#4153 baseline published a tempo on 53 and fired 28 beats/min
  (`q4153_lock_frozen.json`).

### 1.5 Downbeat and meter (`audio/downbeat.rs`)
- At each settled beat the tracker records three cues for the *previous* beat. That beat's attack lands after it fires;
  forward attribution "rotated every fixture downbeat exactly one beat late" (`downbeat.rs:316-327`). The cues are low-band flux
  integrated over the beat (.60), chroma L2 change (.25) and RMS rise (.15) (`downbeat.rs:48-53`). They go into a 16-beat ring,
  each normalised by the ring max.
- **Scoring:** every (meter ∈ {3,4}, phase) by accent contrast = mean at the phase minus mean off it (`downbeat.rs:491-506`).
  Below .08 the challenger is 4/4 on the strongest phase. A challenger must lead the lock's own contrast by **.08**
  (`SWITCH_MARGIN`) for **8** consecutive beats (`downbeat.rs:30-47,420-465`). #4499 measured 7.78 bar-line moves per locked
  minute on Harmonix, 38 % undone within 10 s, 62 % of them from the un-hysteresised fallback. The margin and fallback-hysteresis
  fix forecast 2.3–2.6 per minute and +.02 downbeat F (`bench/manifests/q4499_bar_line_frozen.json`). Longer hysteresis (12–24)
  *lost* F: "slower correction of a wrong lock costs more than the noise it filters".
- `bar_clock` holds just below 1 when the next instant is the "one" but the bar runs long, so `bar_index + bar_phase` never
  dips (`downbeat.rs:265-293`). `bar_duration` changes only on a downbeat, which lets the render side share the rate
  (`downbeat.rs:115-121`). `predicts_downbeat(grid_idx)` lets an early-emitted beat carry its downbeat (`downbeat.rs:253-263`).
- Honest numbers: the doc claims "~70–80 % on 4/4 EDM" (`downbeat.rs:21`). The bench shows **downbeat F .183 Ballroom, .310
  Harmonix**, against .86–.95 for offline models and BeatNet's .474 online (`docs/BENCHMARKS.md`).

### 1.6 Structure (`audio/structure.rs`)
- Decimated to a **10 Hz tick** on a fixed sample-clock grid. Re-arming from the hop gave 9.6/9.4 Hz (#53, `structure.rs:383-399`).
- **Novelty:** a 15-dim timbre vector (7 bands + MFCC 1–8). Each block is divided by its measured RMS (bands .342, MFCC 6.197,
  CV .13–.14 over 120 tracks; before this, MFCC held 97 % of the energy) and the vector is unit-normed (`structure.rs:63-82,818-845`).
  It feeds a 60 s ring and a Gaussian-tapered checkerboard of half-width 3 s (σ = half/2, `structure.rs:463-476`). **Normalised
  absolutely** by the kernel's weight: the old running-max normalisation "saturated to 1.0 on steady material", so nothing could
  threshold it (#1973/#2080, `structure.rs:580-586`). It is published ×33 and clamped (`structure.rs:112-125`); the picker reads
  the unclamped curve.
- **Boundary event:** a local max over ±3 s that clears mean + 1σ of the trailing 45 s, ≥ 6 s after the last boundary in
  **back-dated** time. Confidence = (v − mean)/(8σ), with 8 calibrated so 3.9 % saturate (`structure.rs:84-111,608-662`). Lag =
  kernel 3 s + confirm 3 s = **6 s**, a constant published with every event (`structure.rs:377-381`; `signal/emitter.rs:150-160`).
  Measured: F .500 at 3 s when back-dated, against .201 if the age is ignored. Precision by confidence third is .384/.493/.656
  (`docs/BENCHMARKS.md`, `structure.rs:103-110`).
- **Build-up:** σ(−2.2 + 2.2·loudness_trend + 1.4·centroid rise + 1.2·onset-density rise + 1.6·sub-bass withdrawal), then an
  EMA with τ .5 s (`structure.rs:131-153,693-723`). **AUC .932** for the 8 s before a hand-labelled drop vs background (#2374).
- **Drop:** armed by build > .40 for 3 s (the timer drains at 2×), then fired by a loudness_m jump ≥ .06 (≈ 3.6 LU) over a 1.5 s
  running minimum, sub-bass ≥ .5 of its 10 s decaying peak, and a level within .05 of the 30 s P90 ceiling. The ceiling test
  removed 7 of 23 false drops on a radio hour. Refractory is 16 s (`structure.rs:155-214,726-808`). The header is candid: recall
  **.265**, precision **.225**, and 372 configs, 768 gate combinations and five weight sweeps all failed to beat it. "A rejected
  build-up is a build-up"; the next step needs a learned model (`structure.rs:28-37`).
- **Offline counterpart** (`analyze/structure_offline.rs:1-51`): a centred 4 s kernel (zero lag), 27-dim fingerprints with
  chroma, complete-linkage clustering (.06), a merge of near-duplicate neighbours (.015), an 8 s minimum section, and an energy
  rank over the whole song.

### 1.7 Signal layer: musical time, phrase, sections, drop prediction (`signal/`)
- `BarClock` gives monotonic bars from `bar_index` deltas, capped at 4 per step. After a 4 s stall it falls back to **2 s per
  bar**, so dwell rules still work on ambient material (`signal/clock.rs:9-56`).
- **Phrase:** scores for 8/16/32 bars by anchor, fed by drops (1.0), boundaries (.6) and build onsets (.4, hysteresis
  .6/.4). They decay with τ = 64 bars. The longest length within 80 % of the best wins. Confidence = sharpness × evidence, with
  evidence saturating at 3 (`signal/phrase.rs:18-126`). The admission: confidence is **~0 on most real music**, and no constant
  fixes that honestly (`phrase.rs:7-9`).
- **Section label** (behind the `SectionEstimator` trait, `tier()` = "heuristic-v1", `signal/section.rs:52-57`): intro / build
  / drop / break / steady. Every rule is measured in bars of `BarClock` (`section.rs:61-98`). A drop pulse always wins. The live
  heuristic never emits "outro", which is causally indistinguishable from a break (`section.rs:10-13`). Label-change F was .161
  against .500 for the boundary stream, so boundaries, not labels, carry cues (`docs/SIGNAL.md` honesty notes).
- **Drop prediction** (`signal/predict.rs`): an episode machine. Idle is capped at .32 (tension telemetry). It commits at ≥ .5
  after build ≥ .40 for 1 bar. Evidence then adds persistence, sub-bass withdrawal against an 8-bar median, phrase-boundary
  proximity and imminence (a kick gap ≥ 1.25 bars, or withdrawal ≥ .5), and the value is **ratcheted** so no input can dent it
  mid-build. It collapses to .15 on the drop (`predict.rs:32-230`). Measured on chorus-onset proxies: ≥ .5 covers .511 with a
  median lead of **20.5 beats**; ≥ .8 covers .298 at 12.9 beats; 2.49 false alarms per minute (`docs/BENCHMARKS.md`).

### 1.8 Render-side interpolation (`audio/interp.rs`)
- The playhead runs **1.5 hops** behind the newest frame. The audio thread delivers 0/1/2 hops per 10 ms sleep, so ≤ 1 hop
  degrades to a hold (`interp.rs:22-32`). A 4-frame ring, slewed by τ .25 s and capped at 10 % of real time, so it never stalls
  or reverses; it snaps beyond 250 ms (`interp.rs:34-51,195-213`). Each slot has a Lerp or Hold policy from `schema::FEATURES`.
  Triggers, phases and indices hold from the **older** frame, so the vector describes one instant (`interp.rs:373-387`).
- **Beat and bar PLLs:** the local phase advances at bpm/60 or 1/bar_duration and soft-resyncs (τ .15 s, ≤ .1 cycle/frame).
  The snap threshold is **min(τ·rate, .25)**, derived so a soft correction can never run the sawtooth backwards; a flat .25 had
  swept the bar phase backwards for ~180 ms (`interp.rs:324-371`). `reconcile_index` re-pairs the held counters with the
  locally wrapped phases (`interp.rs:389-409`). Pulses bypass the interpolator through atomic counters (`audio/mod.rs:121-125,1121-1142`).

---

## 2. Contracts

| Contract | Fosfora | Where |
|---|---|---|
| **Event counting** | beat, downbeat and drop are atomic counters; the consumer compares against `*_seen`, so a pulse survives queue overflow. Wire events carry a running count, so the receiver detects its own loss. A golden test reconciles emitter totals with the pulse counters | `audio/mod.rs:121-125,1121-1142`; `signal/emitter.rs:120-160,560-574` |
| **Confidence on every stateful output** | `tempo_confidence` + hysteretic `beat_locked`, `beat_strength` (0.5 = unbacked grid beat), section label conf, boundary conf (calibrated), phrase-length conf, key conf (announced ≥ .3), drop-predict tiers (.5/.8) | `beat/mod.rs:55-60`; `structure.rs:661`; `phrase.rs:122-125`; `docs/SIGNAL.md` |
| **Sample clock** | Every timestamp is a sample count; no wall clock in analysis or `--signal-dump`; ticks on a fixed grid (`tick_due`); live equals offline, golden-proven | `structure.rs:383-399`; `ARCHITECTURE-NOTES.md` |
| **Latency budget, stated** | hop 11.6 ms; window ~93 ms; interp +17 ms (phases and pulses bypass it); beat emission −10 ms (measured); boundary age 6 s on the wire; novelty peaks ~3 s late; user offset +200/−250 ms | `interp.rs:22-32`; `scheduler.rs:49-61`; `structure.rs:381` |
| **Interpolation / PLL** | first-order PLL with a provable no-reverse bound; counters reconciled; bar rate changes only at a downbeat | `interp.rs:324-409`; `downbeat.rs:115-121` |
| **Evidence culture** | Every constant cites an issue and a measured delta. Rounds are pre-registered (`bench/manifests/q*.json`: arms, tune/holdout split by sha256 parity, guards, "on_miss: not re-tuned on holdout"). Dev sidecars (`FOSFORA_*_SIDECAR`) dump internals for exact Python replicas (0 mismatches over 426 k beats in #4499) | `bench/manifests/` |

---

## 3. Excellent vs weak

**Excellent (take the idea):**
1. **Predict-and-confirm grid with muted freerun** (`scheduler.rs:21-32`). Beats are a property of the grid, not of hits, and
   a breakdown mutes instead of strobing.
2. **Phase arbiter** against off-beat lock (`scheduler.rs:84-127`). The failure is common: 28 % of beats on the half-beat.
   Geode has it too (§4).
3. **Fixed band weights in the onset function** (`onset.rs:23-27`), which keep the kick dominant.
4. **The anchor and fold of related levels** with "octave never displaces" (`tempo.rs:44-121`): a half-time feel is not a
   tempo change.
5. **Unbiased ACF, continuous lag, comb mean** (`tempo.rs:134-153,866-950`): each is a small fix with a measured octave bias.
6. **Absolute novelty normalisation** (#1973) and **back-dated boundaries with the age on the wire**
   (`structure.rs:377-381,580-586`).
7. **Confidence that predicts correctness** (boundary precision by tercile) rather than confidence as decoration.
8. **Monotonic phase as a stated, tested invariant** (#293 floor, bar hold, the derived PLL snap bound).
9. **The bench and manifest discipline**: honest negative results (drop recall .265, phrase conf ~0, downbeat F .18–.31).

**Weak (do not imitate):**
- Downbeat is weak (F .18–.31) despite the "70–80 %" doc claim (`downbeat.rs:21`). Accent contrast over 16 beats with a
  3/4 option loses to a neural model by 3–5×.
- Drop is at its ceiling (`structure.rs:28-37`), and the phrase tracker's confidence is ~0 (`phrase.rs:7-9`). `predict/drop` leans
  on phrase proximity only through a floor (`predict.rs:57-60`).
- Lock is slow: 11–27 s mean to a stable tempo (§1.3), which costs a phone visualiser its first verse.
- Complexity: the anchor, ghost, probation, challenge and tenure rules (`tempo.rs:44-121`) are a stack of owner-ruled patches,
  each right for one logged case. They are hard to reason about, and the anchor never re-levels after 30 s by design.
- The section label quantises to downbeats (`section.rs:63-78`), with a known fix deferred.
- The beat activation is a 214 KB JSON of trees with only Fosfora-chain inputs. Its features are not portable, and its
  oracle-relabelled retrain did not carry (`scripts/beat_model/README.md`, #4288).

---

## 4. Geode today, side by side

Geode's design: `Rhythm` runs a 10 ms hop of written samples inside `SoundEngine::write` (`core/sound/SoundEngine.cpp:161-180`,
`core/sound/Rhythm.hpp:10-25`). `Form` runs per rendered frame from `musical()` (`SoundEngine.cpp:803-806`). Detection is
reset after 2 s of silence (`SoundEngine.cpp:222-231`).

| Detector | Geode | Fosfora | Measured here (Geode) |
|---|---|---|---|
| Onset function | 7 filter-bank bands, each `log1p(100·p/own 4 s mean)`, 20 ms positive change, **equal-weight mean** (`Rhythm.cpp:75-92`), minus a 0.3 s local mean (`:99-100`) | 64 bands, SuperFlux max filter, fixed weights .60/.28/.12 | **kick + off-beat open hat at −20 dB never locks**: 0 beats in 50 s, while the tempo reads 127.8 correctly (`rhythm_exp3`). Each band is normalised to its own mean, so a quiet hat weighs as much as the kick, and in the PLP at period P the kick (phase 0) and the hat (phase .5) **cancel**: `plpConf = |Σ|/mass` (`Rhythm.cpp:215`) collapses `raw` (`:221`). A clap on 2 and 4 rescues it (locks at 4.25 s) |
| Tempo | 6 s ring, biased time-domain ACF every 0.25 s from 3 s (`Rhythm.cpp:14-15,108-130`); score (ac + .5·ac(2L))·prior (`:132-134`); prior **120 BPM, σ 1 oct** (`:19-20`); range **60–200** (`:42-43`); "half-lag ≥ .6 → faster level" (`:150-158`); integer lag + parabola (`:160-164`); replace after 4 votes, or 8 for related ratios (`:177-193`); no lock hysteresis on the tempo itself | 8 s, unbiased, continuous lag, 9 ratios × comb mean, prior 150, Kalman, anchor fold | Click tracks 70–190 BPM: within 0.3 %, locked at **3.4 s** at every tempo and frame rate. Synthetic half-time DnB at 174 reads **86.96** (Acc2 only). 120 → 140 change: back on grid within ~10 s (84/93 beats on-beat in 30–40 s, 70/70 after) |
| Phase / grid | PLP (Fourier coefficient at the period, 4 s window, e^(−age/1.5 s)) every 50 ms, +1.5-hop latency compensation (`Rhythm.cpp:196-218,29`); PLL gain .2, spread ≤ ½ step per hop (`:253,269-275`); snap after 1 s off by > ¼ beat (`:246-251`); beats tick while unlocked, triggers only when locked (`:289-308`) | grid PLL with bids, PI trim, muted freerun, acquisition fires, arbiter | Phase-derived beat time is **−11.4 ± 0.9 ms (early)** against click onsets at 24/30/60/120 fps, identical across frame rates, so the hop design works (`rhythm_exp`). Triggers are frame-quantised (±4.8 ms at 60 fps, ±11.7 at 24). **House with an off-beat bass (any level ≥ .1): 107/107 beats on the half-beat** (+225–230 ms at 128 BPM). This is fosfora's #4160 failure, and Geode has no arbiter |
| Lock / confidence | conf = EMA 1 s of clamp(acConf)·clamp(plpConf); lock > .35 for 1 s, unlock < .2 for 2 s (`Rhythm.cpp:221-245`) | support EMA 2.5 s, .6/.35 | Beatless pad and white noise: **0 % locked, 0 beats**. That is better than fosfora's pre-#4153 baseline (28 beats/min on ambient) |
| Downbeat | kick − ½·snare accent per slot, relative to an 8-beat mean, leaky .85/bar, 4/4 only (`Rhythm.cpp:311-340`); harmony accent (`:342-348`); a drop forces the slot (`:358-382`); new slot must lead by .15·scale for 4 evaluations | 16-beat accent contrast, 3/4 and 4/4, margin .08 × 8 beats | Click tracks: 100 % of downbeats on the accented beat. An accent moved by 2 beats at 30 s: the bar line moved at 38.75 s (a +0.5 bar jump in `barCount + barPhase`) |
| Phrase | Fixed 8 bars; restarted by Form's section change (`sectionStarted(1)`) and by a drop (`Rhythm.cpp:294-300,350-356`); no confidence | 8/16/32 inferred, conf ~0 measured | n/a |
| Phase monotonicity | `read()` = θ + sinceHop/P, capped at lastBeat + .999 (`Rhythm.cpp:390-414`); `snapPhase` may jump **back** ("harmless", `:256-267`); `barPhase` = (slot + phase)/4 with no hold when `downSlot_` moves | floored per interval; bar hold; reconciled counters | Steady 128 click: `beatCount + beatPhase` ran backwards twice, both at acquisition (3.8 s by .72 beat, 4.35 s by .60 beat), then never. Bar clock: one backward step of .40 bar at 4.35 s; a moved accent gave a +0.5 bar jump (`mono_exp2`) |
| Novelty / sections | per-beat 30-dim means (16 EQ + centroid/flatness + chroma) in a 32-beat ring, uniform checkerboard of 4 beats after / 16 before (`Form.hpp:12-19`, `Form.cpp:192-215`). **Normalised by a running "typical distance"** `scale_` (`Form.cpp:292-295`), so steady material is self-normalised (fosfora's #1973 shape). Fires on a downbeat ≥ 8 bars after the last when novelty/.5 > .4 and > 1.6 × its mean (`:323-334`); fires one bar late and is told `barsAgo=1`; no age on the event | 10 Hz, 60 s ring, Gaussian kernel, absolute normalisation, age 6 s on the wire | Steady click at 100/128/150 BPM for 180 s: 0 stray changes. **At 174 BPM: 13 stray changes, periodic every ~14 s**. Beatless pad: 3 changes |
| Section id / state | 8 stored centroids, nearest within .3 × spread, LRU (`Form.cpp:217-248`); intro/verse/build/drop/breakdown/outro rules per bar (`:250-269,344-360`); no confidence | label + conf; no outro live | `musical_test` A-B-A passes (`core/tests/musical_test.cpp:260-312`) |
| Build-up | 4 cues from 8 s slopes at 4 Hz (loud/6 dB, bright/.08, rate/4, bass withdrawal), two must agree, EMA 1 s (`Form.cpp:134-180`) | logistic of 4 cues, AUC .932 | Synthetic build peaks .98–.99. **Beatless pad: build .71** |
| Drop | build peak ≥ .4 with ≥ 2 s of the last 4 at ≥ .55, then bass share > max(2 × its 2 s min, .6 × its 16 s mean); refractory **8 s**; no loudness-jump or ceiling test (`Form.cpp:96-123`). Output = build peak, 0.4..1 | arm/jump/sub-bass/ceiling, 16 s refractory | Synthetic drop at 40.025–40.042 s for 24–120 fps (frame quantisation only; build peak varies by .014), so Form is near frame-rate independent in practice. **Beatless pad: 1 false drop** |
| Drop prediction | none | `predict/drop` episode machine | n/a |
| Interpolation | none: Rhythm extrapolates θ to the frame instant (`Rhythm.cpp:393`); other fields are computed per frame | 1.5-hop playhead, per-field policy, PLLs | n/a |
| Event counting | beat/downbeat/phrase triggers = **max strength since the last frame**, cleared per frame (`Rhythm.cpp:302-307,416-421`); ticks capped at 8 per frame (`Rhythm.hpp:41`); counts `beatCount/barCount/phraseIndex` wrap at 4096; `StyleSchedule` counts beats by difference, correctly (`core/viz/StyleSchedule.cpp:28-32,42-70`) | counters everywhere | — |

**Contract bugs found by reading (all verifiable at the cited lines):**
1. **Mixed trigger semantics.** Triggers carry a strength: `beat`/`downbeat`/`phrase` = clamp(conf, .05, 1) (`Rhythm.cpp:303-306`);
   `drop` = build peak, ≥ .4 (`Form.cpp:110`); `sectionChange` = novelty/.5, ≥ .4 (`Form.cpp:326`). Consumers disagree on the
   threshold. `NoteResponse` uses `> 0` (`core/viz/NoteResponse.cpp:281`). `Director` uses `> 0.5` for drop, section, downbeat and
   phrase (`core/viz/Director.cpp:370-391`), and so does `RendererFrame` (`core/viz/RendererFrame.cpp:489-491`). So **a drop whose
   build peaked at .40–.50, or a section change at novelty .4–.5, moves the rhythm and the section state but never reaches the
   auto-director**. Downbeats only count toward `barsSinceCommit_` while beatConf > .5. `director_test` injects 1.0 and cannot see
   this (`core/tests/director_test.cpp:150-163`).
2. **Stale comments:** `MotionField.cpp:108-110` says "the engine has no tempo tracker": its beat and bar waves run on
   `hitTime`/`bassTime`, not on `Rhythm`'s grid. `StyleSchedule.hpp:25-26` says `beatCount` runs "on the hit clock" while unsure;
   in fact it is the free-running oscillator at the last (or a default 120 BPM) period (`Rhythm.cpp:73,269-301`).
3. **Latency is unmeasured and probably a lead of ~100–250 ms.** The PCM tap is ahead of the sink. `SoundCursor` hands the engine
   everything written (`engine/audio-core/.../SoundCursor.kt:37-62`). `PlaybackEngine` builds an `AudioPresentationClock` and a
   `SinkClockDriver` (`app/src/main/java/dev/geode/playback/PlaybackEngine.kt:44-46`), but no render code reads them. So by
   reading the code, the visual beat fires *before* the listener hears it. `sound-engine-v2.md` §2.4 and §7 (stage 4) plan the
   fix; this needs a device check.

---

## 5. Recommendations for Geode

Ordered by priority. Each one is C++ in `core/sound` or `core/viz` (ADR 0003), with host tests in `musical_test.cpp` or
`director_test.cpp`.

| # | What | Why (evidence) | Files | Behaviour change | Test that proves it | Cost | Pri |
|---|---|---|---|---|---|---|---|
| R1 | **Fix the onset weighting**: weight bands in the ODF (low-dominant, like `onset.rs:23-27`) or take the PLP from a kick-weighted envelope while the ACF keeps the full one. Add the ±1-band max filter that §5 of `sound-engine-v2` already plans | kick + off-beat hat at −20 dB never locks (§4) on the most common EDM pattern | `core/sound/Rhythm.cpp:86-100,196-218` | four-on-the-floor with off-beat hats locks; triggers fire | new `testOffbeatHats` in `musical_test.cpp`: kick + open hat at 0.03–0.2 locks within 6 s and ≥ 95 % of beats sit within 50 ms of a kick (`rhythm_exp3` is the template) | S | **P0** |
| R2 | **Phase arbiter**: fold the last 8 and 32 beats of kick onsets (`hopKick_` peaks, or the planned `onsetHard`) onto the grid; move θ by .5 when half ≥ 2 × on (≥ 4 events), the 32-beat guard agrees (≥ 1.5×, on ≤ 40 %), and this holds 3 instants in a row. Quarters can come later | 107/107 beats on the off-beat with an off-beat bass (§4); fosfora measured 28 % of beats there and +.034 F from the fix (`scheduler.rs:84-127`) | `Rhythm.cpp` (new `arbitrate()` called from `advance()` when a beat ticks; move θ forward, never back) | grids locked on the "and" move to the beat in ~3 beats once evidence holds | `testOffbeatBass`: bass .1–.55 → ≥ 90 % on-beat by 15 s; click tracks unchanged; a syncopated pattern (kick on the "and" of 2) is not flipped | M | **P0** |
| R3 | **One trigger contract**: triggers are 0/1 events plus counts; strength goes in its own fields (`beatStrength`, `dropStrength`, `sectionConf`). Or, at least, every consumer tests `> 0` | Director and RendererFrame drop real drops and sections with strength .4–.5 (§4 bug 1) | `core/sound/SoundFrame.h:136-174`, `Rhythm.cpp:301-307`, `Form.cpp:110,326`, `Director.cpp:370-391`, `RendererFrame.cpp:489-491`; `lib_sound` defines + `SoundFrameLayout` (schema step 3 of the structure plan) | the director commits on every drop and section Form reports | `director_test`: drop = .45 and sectionChange = .42 each cause a commit; `musical_test` build/drop asserts the Director saw it | S | **P0** |
| R4 | **Latency: align to the presentation time** (sound-engine-v2 §7). Feed the engine PCM up to `presentationClock` "now" (+ a measured display latency), not up to `written`. Mic and capture keep prediction (fosfora's lead idea, `scheduler.rs:535-541`) | the visual most likely leads audio by the sink buffer (§4 bug 3); fosfora treats ±200 ms as a first-class setting | `SoundCursor.kt` (a `take(upTo)` bound), `PlaybackEngine.kt:44-46`, `core/sound` beat lead | beats on screen land on audible beats | `DEVICE_CHECKS.md` (flash on beat vs a click on speaker and on Bluetooth); JVM test that the cursor never passes the presentation frame | M | **P1** |
| R5 | **Absolute novelty**: unit-norm per-block (RMS-equalised) vectors with cosine similarity, a Gaussian-tapered kernel divided by its weight; threshold mean + 1σ of a trailing window; confidence = excess/(kσ), calibrated | 13 stray section changes in 180 s of a steady 174 BPM click, 3 on a beatless pad (§4); same mechanism as fosfora #1973 | `core/sound/Form.cpp:192-215,292-298,323-334` | steady material quiet; changes carry a usable confidence | `testSteadyIsQuiet`: 180 s clicks at 100–190 BPM → 0 changes; A-B-A still passes | M | P1 |
| R6 | **Drop gates**: add the loudness jump over a 1.5 s minimum and the near-ceiling test (P90 of 30 s, −.05); refractory 8 → 16 s | false drop on a beatless pad (§4); fosfora's ceiling test removed 7 of 23 radio false drops (`structure.rs:207-214`) | `Form.cpp:96-123` | fewer false drops; synthetic drop unchanged | `testBeatlessNoDrop` (pad, 60 s → 0 drops, 0 build state); `testBuildAndDrop` unchanged | S | P1 |
| R7 | **Monotonic phase invariant**: never move θ back (snap forward only, or hold `beatPhase` until the oscillator catches up, like fosfora's floor `scheduler.rs:867-882`); hold `barPhase` below 1 when the downbeat slot moves (`downbeat.rs:281-293`) | measured backward steps of .6–.7 beat at acquisition and a .5-bar jump on a bar-line move (§4) | `Rhythm.cpp:256-267,390-414` | `beatCount + beatPhase` and `barCount + barPhase` never decrease | `testMonotonicClocks` (`mono_exp` as template) on steady, tempo change, accent move and loose timing | S | P1 |
| R8 | **Tempo hygiene from fosfora, each a small measured win**: unbiased ACF; continuous-lag ratio projection; comb as a mean; continuity bonus; then reconsider prior 120 → 150 and range 60–200 → 40–300 once a bench exists (#78 measured −10–16 points of GiantSteps Acc1 from a lower centre) | Geode's biased ACF is chosen on purpose (`Rhythm.cpp:123-124`); its half-time DnB reads 87 | `Rhythm.cpp:108-194` | fewer octave errors on fast EDM | needs R10's bench; host tests: click 60–240 BPM, half-time DnB at 174 reads 174 or 87 consistently | M | P2 |
| R9 | **Related-level fold**: after ~8 s of lock, fold ×2/½/3:2 winners onto the locked period and never switch octave on evidence alone (tap/MIDI/tag from §8b override). Geode's vote counter (`Rhythm.cpp:177-193`) switches octave after 8 agreeing estimates (2 s) | fosfora's measured octave flapping on double-kick material (`tempo.rs:44-58`) | `Rhythm.cpp` | no mid-track octave flips | `testHalfTimeSection`: 32 bars of 174, 16 of half-time feel, back → bpm constant | S | P2 |
| R10 | **Bench + dump** (structure-plan step 8): host `sound_dump` JSONL of beat/downbeat/section events on the sample clock, scored with mir_eval against Ballroom / GiantSteps / Harmonix outside the repo; pre-registered tune/holdout split by sha256 parity; CI fixture with floors | every Geode rhythm constant is tuned by ear (structure plan F13); fosfora's constants each carry a measured delta | `core/tests/` driver, `tools/bench/` | none | the bench itself; CI floors on a synthetic fixture | M | P1 |
| R11 | **Musical-time fallback and back-dated section time**: Form announces `sectionChange` one bar late; publish `sectionAge` (bars or seconds), and give Director's bar maths `Rhythm`'s free-running bar instead of its own `2.0 s` fallback (`Director.cpp:371`) | fosfora: boundary F .500 back-dated vs .201 naive | `SoundFrame.h`, `Form.cpp:323-334`, `Director.cpp:371` | export cue placement and MotionField can land on the true boundary | `musical_test` A-B-A checks `t − sectionAge` within 0.5 bar of 32 s | S | P2 |
| R12 | **MotionField on the grid**: drive beat and bar oscillators from `beatPhase/barPhase` when `beatConf` > lock, else from the current hit clocks | MotionField ignores the tracker (`MotionField.cpp:108-120`), so the "D5 tempo sync" premise (`MOD_MATRIX_PLAN.md`) is half true | `core/viz/MotionField.cpp` | camera and motion breathe with the beat | director/viz math test: locked frame → `beatOsc` peaks at beatPhase 0.25 | S | P2 |
| R13 | **Drop prediction from the map, not live**: use the planned TrackMap (sound-engine-v2 §6) for known drops. Live, add only fosfora's idle/commit tiers (build ≥ .4 for 1 bar → "committed", ratcheted) as `dropLikely` | fosfora's live predictor gives a 20-beat median lead but 2.49 false/min; a map is exact | `Form.cpp`, TrackMap | the director can pre-commit a look | synthetic build → `dropLikely` ≥ .5 ≥ 4 bars before the drop | M | P3 |
| R14 | **Render-side PLL**: only after HopAnalyzer (structure plan step 4); copy `advance_phase` with the min(τ·rate, .25) snap bound and `reconcile_index` | today `Rhythm::read` extrapolates exactly, so it is not needed yet | `core/sound/drive/` | none now | 30 vs 60 fps identical phases | S | later |

How these fit the existing plans: R1 and R2 belong in sound-engine-v2 stage 2 (onsets) and should land **before** the
"BTrack-style cumulative score" (§5 there), since the arbiter fixes a measured failure and the score is speculative. R3 rides
structure-plan step 3 (the feature schema): declare each trigger's kind as an event with a count. §8b's TAP/MIDI phase-owning
sources fit fosfora's model, where an outside source pins the prior narrowly and audio keeps phase (`link/mod.rs:8-38`); that is
exactly §8b's "phase-locked override".

---

## 6. Do not copy

- **The tree-ensemble beat activation and kick model as artefacts.** Their inputs are fosfora-chain columns (`pn.*`, `kf.*`,
  `beat_model.json` inputs), the training audio is FMA plus Demucs labels, and the weights are megabytes of JSON. Copy the
  *idea* (a learned per-hop "is this a beat or a kick" feeding a vote) and train our own on our own features if R2's maths
  falls short. Sound-engine-v2 already chooses Beat This! (MIT) for pre-scan maps.
- **The anchor, ghost, probation and tenure stack** (`tempo.rs:44-121,707-759`). It is six interacting timers built from logged
  live runs. Take the fold rule (R9) and the "octave never displaces" ruling; skip the rest unless a bench shows the case.
- **The 3/4 meter search and accent-contrast downbeat** (`downbeat.rs`): F .18–.31. Geode's kick/snare slot score already
  handles 4/4 clicks perfectly. Better downbeats should come from the map (Beat This!), not from a port.
- **The phrase-length inference** (`signal/phrase.rs`): confidence ~0 on real music by its own admission.
- **The OSC Signal layer, Link, `--signal` UDP, 1 Hz re-broadcast**: Geode is offline by design (no INTERNET permission).
  Keep only the JSONL dump shape for the bench.
- **Live-tunable structure sliders** (`StructureConfig`, `structure.rs:216-272`): Fosfora itself keeps section thresholds as
  consts "so bench numbers stay meaningful" (`section.rs:59-60`). On a phone they would be user-facing noise.
- **The negative sync offset that delays every feature** (`sync_delay.rs`): with file playback Geode can *read ahead*
  (presentation alignment, R4) rather than delay. Keep prediction for mic and capture only.
- **The 1.5-hop playhead now**: Geode's oscillator already extrapolates the phase exactly to the frame instant
  (`Rhythm.cpp:393`). Adding latency buys nothing until the FFT path moves onto the hop.
