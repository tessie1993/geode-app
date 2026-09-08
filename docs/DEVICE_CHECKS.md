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
- **28–36** — *unknown.* Never described in the surviving references.
- **37–39** — added for v1.2.0 (Visual safety / photosensitivity round).
  Items 37–38: content unknown beyond covering that round. Item 39: the
  "Safe visuals off" comparisons — **warning:** the changelog notes these
  deliberately produce fast full-screen flashing.
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
  chrysanthemum behind each (never black), for satellites (`curl_bloom`,
  `morphogen`) budding and dissolving without a pop, for the rods in
  `rod_tunnel` doing the same, for the tunnel bending in `rod_tunnel` and
  `nectar_flow`, and for the material's rim staying coloured rather than
  white at grazing angles. Frame time on `morphogen` and `curl_bloom` at
  Detail maximum is the thing most likely to have moved: the bank costs up
  to six containment tests per march step.

## See also

Several early fluid rounds carried their own inline ON-DEVICE checklists in
their changelog entries rather than numbered items here — see the
"Fluid round 1–3", v0.12.x, and v0.12.1 entries in
[CHANGELOG.md](../CHANGELOG.md).
