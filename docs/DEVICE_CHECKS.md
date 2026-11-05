# On-Device Checks

> **Reconstructed document.** The original `docs/DEVICE_CHECKS.md` was lost;
> this file was rebuilt on 2026-08-10 from the references to it in the
> project changelog (see [CHANGELOG.md](../CHANGELOG.md)). Only items whose
> content those references actually describe are filled in — the rest are
> numbered placeholders marked *unknown*. A placeholder does not mean
> "nothing to check": the original item existed, its text did not survive.

The checklist was introduced in v0.9.6 as the list of on-device checks that
cannot be verified headless — GL behavior in particular. Item numbers follow
the original numbering as cited by the changelog.

## Items

- **1–12** — *unknown.* Never described in the surviving references.
- **13** — *content unknown.* Cited by v0.13.0 ("Fluid & particle REBUILD:
  spawn/catch journey choreography") as the check to run on-device before
  release of that round.
- **14–15** — *unknown.* Never described in the surviving references.
- **16–20** — cover the v0.13.1 visuals/appearance work (WATER style + ripple
  overlay, glass UI, boot intro, restructured Appearance settings). The
  changelog does not record the per-item split; run all five before a release
  touching those areas.
- **21–27** — merged and renumbered in v0.14.0. The changelog lists their
  topics, in order: fluid colour, water controls, composite grading, Curl
  Flow trails, export grading, beat sensitivity, randomize locks — i.e.
  item 21 = fluid colour … item 27 = randomize locks, assuming the
  parenthetical lists them in numbered order. Detailed steps are lost.
  Item 26 (beat sensitivity) is **retired**: that setting no longer exists
  (the sound engine's tuning is Settings › Audio).
- **28–36** — *unknown.* Never described in the surviving references.
- **37–39** — added for v1.2.0 (Visual safety / photosensitivity round).
  Items 37–38: content unknown beyond covering that round. Item 39 (the
  "Safe visuals off" comparisons) is **retired**: the flash clamp has no off
  switch any more, so there is nothing to compare. The number is kept so
  older references still line up.
- **40** — Fluid styles (Styles > Fluid). Scrub all nine looks on one Adreno
  and one Mali device while audio plays and while silent; every
  `fluid_display_frag` variant compiles under SwiftShader, which says nothing
  about a driver's `LOOK` preprocessing or its `mediump` handling of the
  gradient-derived normal (chrome, oil slick, marble). Confirm the Fluid
  Customize controls still move on every style (the style values are
  multipliers), that a `fluid ·` preset applies on any fluid style, that a
  saved preset naming `fluid_ink` etc. survives process death, and that the
  GLSL injection editor reaches a styled fluid scene.
- **41** — The raymarched styles on `lib_dmt`. On the same two devices:
  `kifs`, `noneuclid`, `morphogen`, `vanishing`, `curl_bloom`, `rod_tunnel`,
  `nectar_flow`, `nebula` at Detail minimum and maximum. Look for the
  chrysanthemum behind each (never black), for satellites (all but
  `rod_tunnel` and `nectar_flow`, which carry rods and dye instead) budding
  and dissolving without a pop and never popping into view in front of the
  camera, for the rods in
  `rod_tunnel` doing the same, for the tunnel bending in `rod_tunnel` and
  `nectar_flow`, and for the material's rim staying coloured rather than
  white at grazing angles. Frame time on `morphogen` and `curl_bloom` at
  Detail maximum is the thing most likely to have moved: the bank costs up
  to six containment tests per march step.
- **42** — The Glass family (`glass_*` on `lib_glass`, rebuilt before
  release). On one Adreno 6xx and one Mali-G57-class device, at Performance
  Balanced, 1080p, default Detail: every style holds 60 fps with music
  playing (Settings › Performance's adaptive step-down must not kick in for
  long). The heaviest are `glass_cubes`, `glass_ice` (marched; on High their
  second interior march switches on at `uSteps` 96) and `glass_gem` (up to
  four exact internal bounces); `glass_lens` reads its LED wall three times
  per lens pixel. Look for: no black or NaN pixels at a body's grazing rim
  (total internal reflection takes the mirror direction), the gem's
  rainbow fire widening on bright mixes, highlights rolling off rather than
  clipping to flat white (`glassTone`), and frost (flatness) blurring the
  reflected lights rather than adding noise. With a beat-heavy track: the
  cubes' snare flip wave, the knot's snare bulge, the lens's snare flip, the
  pool's kick ripple and the drops sliding on onsets, none of them stepping
  the whole frame's brightness.
- **43** — Bit-perfect USB (#353, #489). Needs Android 14+ and a USB DAC
  that lists a `MIXER_BEHAVIOR_BIT_PERFECT` entry (`adb shell dumpsys
  audio` shows the mixer attributes per device). With Settings › Playback ›
  Bit-perfect on and ReplayGain, crossfade, the equalizer, bass boost,
  loudness, crossfeed and the limiter all on: (a) cold start the app and play
  a gapless album of 44.1 kHz FLACs; from the very first track,
  `adb shell dumpsys media.audio_flinger` must show the output thread as
  bit-perfect at 44100 Hz, and it must stay so through every gapless item
  without a seek (the bug was a mixed first track reused for the album);
  (b) a DAC that shows the stream's rate (most do) shows the file's rate,
  not 48 kHz; (c) the EQ and ReplayGain make no audible difference and no
  pause, skip or sleep fade is heard; (d) the visuals still react; (e) play
  a 96 kHz file next: the output reopens at 96 kHz, still bit-perfect if the
  DAC lists that rate, otherwise mixed with the EQ back; (f) switch the
  setting off mid-track: playback jumps once (the seek that reopens the
  track) and the EQ and ReplayGain are heard again at once; on again, the
  same jump and they pause. If the DAC can capture its input (or a loopback
  recorder is at hand), a null test against the file is the definitive
  check: the difference must be exactly zero.

- **43** — Per-Scene frame time (#470). CI's visual gate (glslcheck stills,
  `core/tests/visual_gate.cpp`) judges how each style *looks* on SwiftShader;
  it cannot say what a frame costs on a phone GPU. On one Adreno 6xx and one
  Mali-G57-class device, at Performance Balanced, 1080p, default Detail, with
  a beat-heavy track playing and Auto off, for each Scene touched by the
  change (every style for a release; for a PR, the styles it edits and every
  style that shares the `lib_*` it edits):
  - Load the style, wait 10 s for the build (`ScenePrep`) and the thermals to
    settle, then record 30 s of Perfetto (`gfx` and `view` categories, or
    Android Studio's System Trace). Read the `GLThread`'s frame slices
    (`VisualizerRenderer.onDrawFrame`) and the SurfaceView layer's actual
    frame timeline; `dumpsys gfxinfo` does not see a `GLSurfaceView`'s
    thread. Record the median and the 95th percentile per style in the PR.
  - Pass: median under 16.6 ms and p95 under 20 ms (60 fps with headroom),
    and Settings › Performance's adaptive step-down does not kick in during
    the 30 s. A style over budget is lowered (`uSteps`, Detail's cap, fewer
    taps) before it ships, never left to the step-down.
  - Watch the drop and the build too: a style whose cost rises with the
    sound (more march steps, more blooms on a loud bar) is measured during
    the loudest stretch, not in a verse.
  - Then switch to the next style without pausing: the switch must not
    hitch (a scene builds across frames, `ProgramStager`), and the old one
    keeps drawing until the new one is ready.

## See also

Several early fluid rounds carried their own inline ON-DEVICE checklists in
their changelog entries rather than numbered items here — see the
"Fluid round 1–3", v0.12.x, and v0.12.1 entries in
[CHANGELOG.md](../CHANGELOG.md).
