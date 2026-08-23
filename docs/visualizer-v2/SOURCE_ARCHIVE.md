# Source archive and adoption policy

Required by the V2 overhaul plan §3. Every external source that informs the V2 engine is
recorded here with its repository URL, pinned commit, licence, role, the files imported
from it, the modifications made, and its attribution requirements.

[`provenance.json`](provenance.json) is the machine-readable form of the same data and is
now the one that gates the build, in two halves. `EngineProvenanceRegistryTest` asks whether
the registry itself is sound — schema, licence hashes, and coverage against
[`MASTER_PLAN.md`](MASTER_PLAN.md) §3.1 in both directions. `checkEngineProvenance`, a Gradle
task on `check` in every module, asks whether the source tree obeys it: an adapted file
carries an SPDX line and an `Origin:` marker, that origin is a registered source at its
pinned commit under an adoptable tier, and no STUDY or EXCLUDE repository is named as an
origin anywhere in shipped source. **Where this prose and the registry disagree, the registry
is right** — it is the one under test.

An adapted file is marked like this, and the gate reads both lines:

```kotlin
// SPDX-License-Identifier: Apache-2.0
// Origin: https://github.com/google/swissgl@489dfcf437702d6e2446f3e36beadecb34cc81ca
```

**Working clones** live outside the repository at
`C:/Users/tessi/Claude/repos/_visualizer-research/`. They are reference material, not a
submodule and not a dependency. Restricted-licence clones are quarantined in
`_REFERENCE_ONLY_DO_NOT_COPY/` so the boundary is a filesystem fact rather than a note.

Every licence below was read from the licence file itself, never from a repository badge.
As of slice V2-A-02a each one is also **pinned and hashed**: the registry records the
commit the file was read at, its path, its SHA-256 and its length, so "MIT" is a claim with
evidence behind it rather than a recollection. Three earlier claims were wrong — KarmaViz
is plain MIT, rreusser/sketches has no licence at all, and RDPE's declared MIT has no
licence text in the repository — and all three are corrected below.

This file is distinct from `THIRD_PARTY_NOTICES` and
`app/src/main/assets/third_party_notices.txt`, which are the shipped legal notices for
code already in the app. This one covers sources under evaluation and records the tier,
the pinned commit and the obligation each would create.

---

## 1. Adoption tiers

| Tier | Meaning |
|---|---|
| **ADAPT** | Source may be adapted into V2 with attribution, after a per-file licence check. Every adapted file carries an SPDX line and an origin comment. |
| **REIMPLEMENT** | Algorithms and parameter values may be reimplemented from the source. No code, shader text or constant table is copied verbatim. |
| **ORACLE** | Used only to generate test fixtures and expected values. Never shipped, never linked. |
| **BENCHMARK** | Adoption candidate pending a measured win and an ADR. Nothing enters the tree before that evidence exists. |
| **RETAIN** | Already shipped and stays, under its existing boundary and obligations. Not a foundation for new engine code. |
| **STUDY** | Read for architecture and behaviour. Nothing derived from it enters the tree. |
| **EXCLUDE** | Licence forbids reuse, or there is no licence. Mood-board and mathematics research only. |

`ORACLE`, `BENCHMARK`, `STUDY` and `EXCLUDE` are the **no-code tiers**: the registry
rejects any of them recording an adopted file, so "we only borrowed a little" cannot be
written down, let alone shipped.

An earlier revision of this file used `ALGORITHM` and `EXCLUDED`; both are renamed to the
`MASTER_PLAN.md` §3 vocabulary so the plan, this document and the registry use one set of
words.

---

## 2. Direct adaptation

| Source | Commit | Licence | Role |
|---|---|---|---|
| [google/swissgl](https://github.com/google/swissgl) | `489dfcf` (2026-03-09) | Apache-2.0 | **ADAPT** — primary GLES 3.0 simulation reference. Port the texture-backed state histories, fragment-shader GPGPU, deterministic seeds and instanced particle rendering. Not the JavaScript wrapper. |
| [markusfisch/ShaderEditor](https://github.com/markusfisch/ShaderEditor) | `513e79f` (2026-08-11) | MIT | **ADAPT** — Android shader lifecycle, backbuffers, wallpaper behaviour, resolution choices, low-battery throttling. |
| [PavelDoGreat/WebGL-Fluid-Simulation](https://github.com/PavelDoGreat/WebGL-Fluid-Simulation) | `a2d2929` (2024-11-12) | MIT | **ADAPT** — already partially ported and attributed in `THIRD_PARTY_NOTICES`. Consolidate shared advection, pressure, splat, bloom and format-fallback infrastructure. |
| [QC20/Colourful-Attraction](https://github.com/QC20/Colourful-Attraction) | `6e502d3` (2026-04-14) | MIT | **ADAPT the GLSL only** — basis for the first signature scene: particles morphing between velocity fields without resetting state. See the caveat below. |
| [kevinraymond/fosfora](https://github.com/kevinraymond/fosfora) | `09132c0` (2026-08-10) | Apache-2.0 **and** MIT (dual) | **STUDY** in practice — the plan lists it for adaptation, but its runtime is Rust/wgpu and nothing is portable. Its value is the architecture: lock-free audio delivery, multi-resolution analysis, HDR layering, data-driven effects, modulation and audio textures. Algorithms must be independently validated. |
| projectM | tag `v4.1.7` = `e0b0a96` + local patch | LGPL-2.1 (`LICENSE.txt`) | **RETAIN as-is.** Dynamically linked, notices shipped, patch and build recipe in-tree. Not the architecture for new visuals. See `tools/build-projectm.md`. |

## 3. Algorithm references — reimplement, do not copy

| Source | Commit | Licence | Role |
|---|---|---|---|
| [fogleman/physarum](https://github.com/fogleman/physarum) | `704dda7` (2020-12-04) | MIT | **REIMPLEMENT** — agent sensor/rotation/deposit/decay rules and their working parameter ranges. |
| [Chakazul/Lenia](https://github.com/Chakazul/Lenia) | `adfc542` (2022-03-16) | MIT | **REIMPLEMENT** — continuous CA kernel and growth function. |
| [keijiro/RDSystem](https://github.com/keijiro/RDSystem) | `7899466` (2025-08-11) | Unlicense | **REIMPLEMENT** — minimal GPU reaction-diffusion kernel. Public domain, but still recorded. |
| [hunar4321/particle-life](https://github.com/hunar4321/particle-life) | `2562787` (2024-08-29) | MIT | **REIMPLEMENT** — interaction-matrix ecology. |
| [jonradoff/threelab](https://github.com/jonradoff/threelab) | `9b37d76` (2026-07-06) | MIT | **REIMPLEMENT** — Lorenz/Rössler/Halvorsen/Thomas/Aizawa/Dadras formulations, curl noise, 4-pass GPU Physarum, wave interference, electric fields. |
| [niko-dellic/wavefield](https://github.com/niko-dellic/wavefield) | `3858a65` (2026-06-09) | MIT | **REIMPLEMENT** — spectral-peak → Chladni eigenmode mapping with attack/release/phase persistence. |
| [BrutPitt/glChAoS.P](https://github.com/BrutPitt/glChAoS.P) | `f3b604a` (2025-02-08) | BSD-2-Clause | **REIMPLEMENT** — attractor parameter ranges, point-sprite glow, tone mapping. |
| [meyda/meyda](https://github.com/meyda/meyda) | `ecf2566` (2024-04-21) | MIT | **REIMPLEMENT** — spectral feature formulas (centroid, rolloff, flatness, spread, ZCR, flux, MFCC, chroma). |
| [wizgrav/clubber](https://github.com/wizgrav/clubber) | `a8dad7c` (2018-06-27) | MIT | **REIMPLEMENT** — musical log/MIDI band mapping and feature→visual mapping. |
| [xsoophx/Kymatik](https://github.com/xsoophx/Kymatik) | `bbeca37` (2025-03-30) | MIT | **REIMPLEMENT** — Kotlin FFT/BPM pipeline structure on the JVM. |
| [sandner-art/Audio-Shader-Studio](https://github.com/sandner-art/Audio-Shader-Studio) | `3d5a6f1` (2025-09-14) | MIT | **REIMPLEMENT** — shader-side audio uniform/texture contract. |
| [s-macke/WebGPU-Lab](https://github.com/s-macke/WebGPU-Lab) | `ea1a8c0` (2026-04-07) | MIT | **REIMPLEMENT** — fluid and SDF techniques. Per-shader provenance review required. |
| [Hornfisk/vgalizer](https://github.com/Hornfisk/vgalizer) | `faa19ee` (2026-04-13) | MIT | **REIMPLEMENT** — effect recipes; Rust/wgpu so nothing is portable. |
| [gijzelaerr/spectrageist](https://github.com/gijzelaerr/spectrageist) | `0a177a4` (2025-12-21) | MIT | **REIMPLEMENT** — real-time feature extraction. |
| [karmatripping/KarmaViz](https://github.com/karmatripping/KarmaViz) | `67e8a15` (2025-07-15) | MIT | **REIMPLEMENT**. Earlier research flagged "conflicting licence, avoid" — **that was wrong**; `LICENSE.md` is plain MIT. |
| [muhamedamin308/wavora](https://github.com/muhamedamin308/wavora) | `cd49ad3` (2026-06-21) | MIT | **STUDY** — Android Media3 + clean-architecture example. Small project; treat as illustration, not authority. |
| [lostjared/acidcam-gpu](https://github.com/lostjared/acidcam-gpu) | `4969e8c` | BSD-2-Clause | **REIMPLEMENT** — temporal history textures, FFT history, shader chains, glitch feedback and HDR history effects for the Recursive Temple family. Desktop capture architecture is not adopted. |

## 4. Test oracles

| Source | Licence | Role |
|---|---|---|
| [michaelkrzyzaniak/Beat-and-Tempo-Tracking](https://github.com/michaelkrzyzaniak/Beat-and-Tempo-Tracking) | MIT — pinned `c039090` (2023-12-17) | **ORACLE + REIMPLEMENT.** Benchmark its causal onset, tempo and predicted-beat behaviour against the Kotlin tracker. Port methods only after corpus evaluation. |
| [librosa/librosa](https://github.com/librosa/librosa) | ISC — pinned `e40ded3` | **ORACLE.** Formula reference and fixture generator for RMS, descriptors, onset strength, HPSS, chroma, YIN, tempo. **Never embedded in the app** — used via Python at fixture-generation time. |
| [jiixyj/libebur128](https://github.com/jiixyj/libebur128) | MIT — pinned `67b33ab` (`COPYING`) | **ORACLE.** BS.1770 / EBU R128 reference values. Runtime meter implemented allocation-free in Kotlin; vendor the C only if benchmarks justify it. |
| [marton78/pffft](https://github.com/marton78/pffft) | BSD-like — pinned `a4b0359` | **BENCHMARK.** Compared against JTransforms on arm64. Adopt only for a material latency or battery win, target ≥2× FFT speedup, and only through an ADR. |

## 5. Study only — nothing derived enters the tree

| Source | Commit | Licence | Why study-only |
|---|---|---|---|
| [jberg/butterchurn](https://github.com/jberg/butterchurn) | `fbac2f6` (2026-04-19) | MIT | Permissive, but a second MilkDrop runtime duplicates projectM. **And the product ships no web technology** — no WebView, no JS, no WebGL runtime, by explicit decision. Study its feedback and preset-transition behaviour only. |
| [sqrew/rdpe](https://github.com/sqrew/rdpe) | `28db17f` (2025-12-29) | MIT *declared*, **LICENSE file absent** | Long-term reference for typed simulation rules, structure-of-arrays state, GPU-only updates and spatial hashing. `Cargo.toml` declares `license = "MIT"` and the README badge links to a `LICENSE` that **does not exist in the repository**. Re-checked at this commit in V2-A-02a: still absent. Treat as study-only until the text is present. Do not make its wgpu rule compiler a dependency. |
| [rorygallagher2024/velo-visualiser](https://github.com/rorygallagher2024/velo-visualiser) | `bebf723` | **GPL-3.0** | Native Android scene lifecycle, Oboe/exact-PCM concepts, dynamic resolution, staged warmup, secondary display and a 48-scene coverage checklist. The checklist is the only thing that crosses: GPL-3.0 cannot ship here, and reading its source before writing the same effect is where a clean-room claim dies. Effects taken from its list are implemented from published mathematics, and their coverage row says so. |
| [pudnax/pilka](https://github.com/pudnax/pilka) | `7a90cd4` | MIT | Tooling reference — desktop prototype runner, shader hot reload, previous-frame textures, FFT input, recording workflow. Informs the Shader Studio authoring loop (§9.5), never a production dependency. |
| Geno-1 | **unresolved** | MIT *declared in the plan* | Named in `MASTER_PLAN.md` §3.1 for clock-accurate A/V scheduling and host-testable engine separation, but §21 gives no URL and the repository could not be located. **No Geno-1-derived idea may cite provenance until a URL, commit and licence file are recorded.** |

## 6. Excluded

| Source | Licence | Status |
|---|---|---|
| [rreusser/sketches](https://github.com/rreusser/sketches) | **NONE** — pinned `82eb3ce` (2026-03-11) | **EXCLUDE.** Earlier research recommended this as "MIT". It has no LICENSE file, no `package.json` licence field and no README licence section — therefore all rights reserved. Published mathematics may be learned from; **no code or shader text may be copied.** |
| `BaryonOfficial/Baryon` | PolyForm Strict 1.0.0 | **EXCLUDE** from reuse. Commercial licence required. Concepts only. |
| `adityarajashekaran/orphic`, `elder-plinius/ENTHEA`, [`hydra-synth/hydra`](https://github.com/hydra-synth/hydra) | AGPL-3.0 | **EXCLUDE.** Scene-design documents only. Public mathematics may be traced back to permissive papers and implemented from those; shader text and closely copied scene descriptions may not. |
| `mardous/BoomingMusic`, `OxygenCobalt/Auxio` | GPL-3.0 | **EXCLUDE.** Player feature reference. Outside the §3.1 ledger — they informed the music app, not the visual engine, so they carry no registry entry. |
| [patriciogonzalezvivo/lygia](https://github.com/patriciogonzalezvivo/lygia) | **Prosperity Public License 3.0.0** | **EXCLUDE.** Recorded by name because a shader library this widely used gets assumed permissive. It is not: Prosperity 3.0.0 is noncommercial-only, which is what `MASTER_PLAN.md` §3.1 means by "non-commercial default". Read from the licence file, not a badge. |
| Shadertoy and ambiguously licensed collections | Unstated by default | **EXCLUDE.** Shadertoy's default is all-rights-reserved unless the author says otherwise. A single separately licensed shader may enter only through its own provenance slice recording author, licence text and URL. |

### A caveat on Colourful-Attraction

It is genuinely MIT and its GLSL is the closest published match to the morphing-vector-field
idea. It also has **zero stars**, and its GitHub description terminates mid-sentence in what
appears to be a leaked authoring prompt (`…ribes the content specifications for how I want
you to make the readme`). Read and verify its shader maths; do not treat it as
battle-tested.

---

## 7. Obligations this creates

- **Apache-2.0 (SwissGL, fosfora)** requires the licence text, the copyright notice, a
  NOTICE file if one is present upstream, and a statement of changes in modified files.
  Every adapted file gets `// SPDX-License-Identifier: Apache-2.0` plus an origin and
  modification comment.
- **MIT / BSD-2 / Unlicense** require the copyright line and licence text to travel with
  any copied portion.
- **LGPL-2.1 (projectM)** requires dynamic linking (satisfied), the licence notice
  (satisfied), and the corresponding source of the *modified* library. The FBO backport
  patch and the full build recipe are in `tools/`; the CI workflow that produces the
  binaries is `.github/workflows/native-libs.yml`.

**Known gap:** `checkThirdPartyNotices` is registered in `app/build.gradle.kts` and wired
into `:app:check`. It is `:app`-scoped. Anything adapted into a new engine package is
covered only while the engine lives inside `:app`. [MASTER_PLAN.md](MASTER_PLAN.md) §4.1
extracts six engine modules at V2-1-02, so this gap stops being hypothetical then: **the
notice task must move into the convention plugin in that same commit, or Apache-2.0
attribution silently stops being enforced.** §3.3 says the same about
`checkEngineProvenance` — it scans every module, not only `:app`.
