# BeatRealms 3D (internal name Geode): Changelog

All release notes, newest first, kept verbatim from the project README (the
history used to sit above the README's own introduction; it now lives here).
One block of entries appeared twice in the old file — the exact duplicates
were removed, nothing else was changed. Historical entries reference files as
they existed at the time: `todo.md` and the planning docs were retired once
their content shipped, and the original `docs/DEVICE_CHECKS.md` was lost —
a partial reconstruction, rebuilt from the references in these entries, is at
[docs/DEVICE_CHECKS.md](docs/DEVICE_CHECKS.md).

## Unreleased

- **No more heat throttle.** The app no longer reads the phone's thermal
  status or headroom, and never lowers the frame rate, render scale, march
  steps, post FX, Liquid mode, the physics layers, kept scenes or Record's
  capture rate because the phone is warm. The "Phone is warm" note, Settings ›
  Performance › Adapt to frame time and Liquid on a hot phone are gone; your
  own fps cap and quality choices are drawn as set.

- **Six new style families, ten styles each (334 styles).** Prism
  (`prism_*`, `lib_prism`): kaleidoscope portals in liquid chrome. Fluor
  (`fluor_*`, `lib_fluor`): carved ornament under blacklight, its raised
  edges glowing. Loom (`loom_*`, `lib_loom`): woven light, threads, nets and
  skins. Shard (`shard_*`, `lib_shard`): broken glass with thin-film faces.
  Petal (`petal_*`, `lib_petal`): translucent petals lit from within. Fold
  (`fold_*`, `lib_fold` and five `lib_fold_march_*`): fractals made by
  folding space, each drawn by its own march. All sixty are 3D on the camera
  rig, bounded by Detail, and hear the sound as Flame, Opal and Hollow do;
  none spins. The first of each (Prism Portal, Fluor Whorl, Loom Braid, Shard
  Splinter, Petal Rose, Fold Lace) is free. A family whose every style
  marches is now named once in the native catalogue (`kMarchedFamilies`).

- **Three new style families, ten styles each (274 styles).** Flame
  (`flame_*`, `lib_flame`): fractal-flame veils and threads on black. Opal
  (`opal_*`, `lib_opal`): oil-film bubbles, shells and melting drops. Hollow
  (`hollow_*`, `lib_hollow`): blacklight flights through sinew, caves, ribs
  and bone. All thirty are 3D on the camera rig, bounded by Detail, and read
  Morph, Warp, Turbulence, Intensity, the six knobs and the palette through
  `lib_neon`; none reads the sound. The first of each (Flame Veil, Opal
  Bubbles, Hollow Sinew) is free. A family now carries its own count in the
  native catalogue (`catalogue::Family::ids` is a span; it was an array of
  twelve).

- **Summary of the work since v1.8.0, newest first.** The entries below this
  one are kept as written; several describe code that has since been replaced.
  - **Oboe is back, as the playback output only.** Settings › Playback ›
    Output and more has a new switch, "Native output (Oboe)",
    off by default and experimental. ExoPlayer still decodes, keeps the queue,
    Android Auto, the notification and its whole audio sink (the tap, the DSP,
    speed and pitch, gapless joins); only the AudioTrack at the end is
    replaced, by an Oboe stream fed through a lock-free native FIFO
    (`core/audio/output`: `OutputFifo`, `OboeOutput`, C ABI
    `geode_audio_out_*`; Kotlin's `OboeAudioOutput` only forwards). It plays
    plain 16-bit or float PCM in mono or stereo, in the player's audio
    session, and reopens its stream when the route changes. Everything else
    stays on the AudioTrack by itself: surround files, bit-perfect USB,
    offload, a stream the device will not open, and a stream that fails
    mid-play (the sink rebuilds its output on the AudioTrack). The FIFO and
    the stream's buffer together are the size Media3 asks an AudioTrack for,
    so fades and the crossfade keep their timing; the switch is about the
    output path, and latency is about what it was. Oboe 1.11.0 comes as a
    Prefab AAR (`liboboe.so`, 16 KB aligned), not a submodule. New host test:
    `core/tests/output_fifo_test.cpp`. Not yet checked on a phone.
  - **Dead code and doc cleanup.** Unused native and JNI entry points are
    gone (the `geode_sound_*` C API, `geode_params_blend`, and the JNI
    `dspBandCount`, `dspBand`, `dspSetSampleRate`, `dspSampleRate`,
    `vizSetParam`, `version`, `tagsLastError`, `vizSoundFrame` and
    `vizSetOverlay`), as are unused Kotlin helpers, the duplicate `PcmSink`
    and the library's leftover "analyzing" state, the unused overlay pipeline,
    the video template job planner (templates apply their look only), the sink
    clock's diagnostics, the Kotlin `VisualSafety` mirror and unused compute
    helpers. Broken tools are deleted (`tools/import-theme-pack.sh`,
    `check_style_collection.py`, `oracle/`, `shaderpreview/`, `smoke.c`);
    `tools/glslcheck` reads the catalogue again. The unused kissfft submodule
    is removed. `THIRD_PARTY_NOTICES` gains TagLib and loses woscope and
    kissfft. The
    privacy policy, store listing, AGENTS.md, README and docs now describe the
    sound engine instead of the old analysis layer.
  - **Playback is ExoPlayer only (#193).** The native player (AMediaCodec and
    Oboe), crossfade and the gapless toggle are gone; ExoPlayer joins tracks
    gaplessly by itself. Files with more than two channels (5.1) play: the
    native DSP steps aside for them. Bit-perfect USB output is done around
    ExoPlayer's sink with `setPreferredMixerAttributes` (API 34+).
  - **Style families.** Ten families of twelve styles on a shared
    `lib_<family>`: Arbor, Bulb, Chrysanthemum, Lucid, Dive, Glass, Nebula,
    Terrain, Truchet and Sphere Foam (#184). The first of each is free.
  - **The sound engine (`core/sound`).** Every renderer (screen, wallpaper,
    dream, export) runs its own engine, stepped once per frame, in place of
    the old live and offline analysers and their cache. Reactions, style DNA,
    the camera and the scenes read it; export decodes the track and steps its
    engine at 1/fps, so it reacts like the screen.
  - **The catalogue and the randomizer are native** (`core/viz/Catalogue`,
    `core/viz/ParamRandomizer`); Kotlin reads the catalogue once as text.
  - **Customize** opens on Macros (six global knobs, with macro presets) and
    Events (more recipes); the A/B compare is removed (#173, #176, #170).
  - **Visuals is a stage** (#171): the live canvas over a panel that runs the
    app's first Navigation 3 back stack.
  - **MilkDrop keeps the shown preset** across renderers and restarts (#178).
- **Form drive: the superformula as the one driver every visual is fed
  through.** *Superseded: `FormDrive` was removed later; `core/viz/MotionField`
  replaced it, and `formDrive` is an inert parameter.* `core/viz/FormDrive` sits in `Renderer::resolveParams`, after
  the LFO and ADSR slots and before the safety clamp, so every family's
  `SceneParams` pass through it: the fragment styles read the result through
  the shared `view()` uniforms, the particle and simulation families through
  the composite pass and their own speed/turbulence/particle/trail reads,
  Fluid, Water and Cymatics through their solver parameters, MilkDrop
  through zoom, rotation and hue. No style file changes. The driver is the
  Gielis superformula `r(θ) = (|cos(mθ/4)/a|^n2 + |sin(mθ/4)/b|^n3)^(-1/n1)`
  redrawn by the music: snares walk the lobe count `m` (upward through a
  buildup), a drop resets it to a bold low count, a section boundary re-rolls
  it, every change a crossfade between the frozen outgoing curve and the live
  one; harmonicity sets the sharpness `n1` and a kick pinches it; the spectral
  tilt, the hats and the PCM crest factor shape the lobes `n2`/`n3`; pan and
  width are the weights `a`/`b`; energy spins it, a section boundary reverses
  it, a snare turns it. It is fed the mono PCM block the renderer already
  receives (block RMS, peak, crest and zero-crossing rate, read only when a
  fresh block arrived) plus the feature frame. Eight taps around the curve,
  normalised to a mean radius of one, are the bipolar modulators; each feeds
  several parameters — zoom, rotation, sway, warp, ripple, morph, twist, the
  tiling that climbs through a buildup and releases on the drop, the
  kaleidoscope's fold count (the lobe count, when the user turned it on),
  fisheye, chromatic aberration, the scene clock, turbulence, particle size,
  density, trail length and warp, hue range and shift, fluid curl / splat
  radius / splat force / stirrer speed, flow strength, water flow / ripple /
  wave speed, the cymatics scale / ring / focus / swirl / flow. A presence
  gate on the level fades it all out in silence. Reduced motion scales the
  geometric part down. One new parameter, `formDrive` (Reactivity tab,
  default 0.7, 0 = off; wire index 134, preset key `formDrive`, animatable,
  rolled by the randomizer). Deterministic: the same audio gives the same
  shapes, so exported frames stay reproducible. Drift is deliberately left
  alone: `view()` and the composite both compute position as
  `drift × time`, so a drift that changes over time teleports the picture.
- **The musical structure now reaches the visuals.** The native analyser
  has always computed `tempoStability`, `barPhase`, `beatInBar`, `downbeat`,
  `downbeatConfidence`, `novelty`, `sectionBoundary`, `buildup`, `drop`,
  `arrival`, `harmonicity` and `warmup`, and the Kotlin `ReactiveAnalyzer`
  wrapper exposed them, but `AudioFeatures` never carried them and
  `FeatureFrameCodec` zero-filled them, so no scene ever saw a drop, a
  section, a bar or a buildup — every reaction could only be a level. They
  are carried now, live and in the offline snapshot. The analysis cache
  (`AnalysisCache`, format v2) does not store them; a timeline loaded from
  cache still has zeros there.
- **One-hop pulses no longer fall between display frames.** The analyser
  fires `beat`, `beatStrength`, `transient`, `kick`, `snare`, `hat`,
  `downbeat`, `sectionBoundary`, `drop` and `arrival` for exactly one 16 ms
  hop; the features travel through a conflating `StateFlow` and are read once
  per display frame, so at 30 fps (the thermal governor's paced rate) every
  other pulse was lost before any scene saw it. `AnalysisEngine` now holds
  each pulse for three hops. Consumers that must fire once per pulse already
  edge-detect (`live::Edge` in the fluid emitters); the rest take a
  max-envelope.
- **Fix: `lerpParams`/`blendParams` wrote out of bounds on every fade.**
  `SceneParams::lerpedFloats()` was a `std::array<FloatField, 98>` with 95
  initialisers; the three value-initialised entries held null
  pointers-to-member and every parameter fade or morph read and wrote
  through them. The count is now `kLerpedFloatCount`, documented as the
  thing to update when a lerped float is added.
- **Fix: `uSpike` never reached its peak.** The spike envelope was a
  one-pole chasing a target that was 1.0 for one frame, so it covered 13% of
  the gap and chased 0.0 again: it topped out near 0.13 at 60 fps against a
  documented "rises over ~120 ms". It is a linear attack over 120 ms now, then
  the same release.
- **Fix: the integer hashes in `lib_sdf3` ran at `mediump int`.** GLSL ES
  3.00 defaults fragment ints to mediump, which a GPU may honour at 16 bits;
  the 32-bit `uhash` family and `floatBitsToUint` collapsed there (an empty
  Nebula cloud). `precision highp int;` is declared where the hashes need it.
- **Fix: `geode_version()` said 1.7.0** while the app is 1.8.0.
- **Dead code removed.** Native units compiled into `libgeode.so` but
  included or called by nothing: `analysis/Envelope`, `MelBank`, `Mfcc`,
  `SpectralContrast`, `SpectralFlux`, `SpectralDescriptors`, `FrameGrid`,
  `AnalysisBranch`, `viz/FramePacer` (a C++ port shadowed by
  `FramePacer.kt`); `analysis/FrameLevels` is kept because the form driver
  reads it. Kotlin files referenced by nothing (checked against code, the
  manifest, DI, XML and tests): `audio/PcmTapSink.kt` (superseded by
  `PcmTap` in `:engine:audio-android`), `billing/AdPolicy.kt`,
  `billing/Entitlement.kt`, `playback/ShuffleModes.kt`,
  `publish/DescriptionMaker.kt`, `publish/ThumbnailMaker.kt`,
  `engine/audio-core/…/RingReader.kt` and `RingReadResult.kt`,
  `engine/scenes/…/FeatureRingBridge.kt`, and the `:engine:runtime` module
  (`EngineComposition`, `EngineLifetime`), which `:app` depended on and
  imported nothing from. `docs/visualizer-v2/GPU_RESOURCE_ABI.md` described
  the former `:engine:gl` module and is gone; the other V2 documents keep
  their content and no longer link a master plan that is not in the tree;
  `docs/AUDIO_CHAIN.md` names the tap that exists and the tests that do not;
  `docs/PARAM_MATRIX.md` no longer claims a generator that does not exist.
- **The Fluid tab has styles now** (Styles > Fluid, ids `fluid_ink`,
  `fluid_oilslick`, `fluid_neon`, `fluid_chrome`, `fluid_smoke`, `fluid_lava`,
  `fluid_marble`, `fluid_aurora`, alongside `fluid`, Curl Flow and Water).
  Every other family lists ten profiles over one engine; Fluid listed its
  three engines and nothing else. It now has the same shape as Silk and
  Cymatics: a `FluidStyle` table, mirrored in `StyleCatalog.cpp` and
  `VisualStyleCatalog.kt`, where each entry is the same solver under a
  different LOOK plus a few multipliers. The looks are eight material
  branches of `fluid_display_frag.glsl`, selected by a `LOOK` keyword
  `FluidLook` prepends next to SHADING/BLOOM/SUNRAYS, so the hot shader still
  never branches on a uniform: pigment on paper, a thin-film oil slick indexed
  by density and sheared by its gradient, glowing iso-contours, liquid chrome
  that reflects a studio off the density gradient, smoke, a blackbody lava
  ramp, veined marble and an aurora whose hue turns with time and height.
  The multipliers (curl, both dissipations, splat radius and force, bloom,
  and a hue offset on the emitters) sit ON the user's Customize values, never
  in place of them, so no Fluid control goes dead on any style. `fluid` keeps
  its id and its original look; built-in `fluid ·` presets match every style
  in the family, and five new ones (`fluid_ink · Sumi`, `fluid_chrome ·
  Mercury`, `fluid_neon · Pulse`, `fluid_lava · Magma`, `fluid_aurora ·
  Curtains`) name a styled look so applying one switches to it; the GLSL
  injection shaders reach every built fluid-solver scene rather than only the
  one named `fluid`; Journey, Emitters and the particle layer scope to the
  family.

- **The eight raymarched styles share a premium material and a sky**
  (`lib_dmt.glsl`, a new registered include). `lib_sdf3` is geometry only,
  so each marched style had shaded its own hit with its own diffuse-plus-
  fresnel block and put a flat palette ramp behind it. The reference material
  this family is built from - the visionary-art rendering of the DMT visual
  and the many "chrysanthemum" replications of it - is consistent about
  neither being flat. `dmtShade()` is the one material now: nested shells of
  hue on a banding coordinate, a thin-film sheen whose hue moves with
  incidence (0.2 of a turn - 0.45 was tried and painted rainbow rings on every
  sphere), a Fresnel rim with a different exponent per channel so the
  silhouette is dispersed rather than white, a reflected softbox band that
  reads as chrome, a treble-sharpened specular and a subsurface term for the
  thin parts. `dmtChrysanthemum()` is the sky: a kaleidoscopic fold of the
  ray direction with two orbit traps (knots and threads), turning at a tenth
  of a turn a minute, never black, evaluated in each style's camera frame so
  it sits behind the geometry from every angle. `kifs`, `noneuclid`,
  `morphogen`, `vanishing` and `curl_bloom` shade with it and every one of
  the eight has the mandala behind it; no fold set, march or Lipschitz
  argument changed for that.

  The library also carries the life of the frame, because the references are
  never one object turning: `dmtLife()` (a birth-hold-dissolve envelope on a
  per-body clock, eased at both ends so nothing pops), `dmtMorphBody()` (a
  closed ring sphere - gem - torus - box - octahedron, blended by mix() and so
  still 1-Lipschitz), `dmtSatellites()` (up to six such bodies on their own
  precessing orbits, spins and clocks, culled by a bounding ball) and
  `dmtTunnelPath()` / `dmtTunnelWarp()` / `dmtFlightRay()` (a flight path
  that bends in every direction, leaning toward the last spike's heading, and
  the space warp that bends a straight tube along it, bound stated).
  - *Rod Tunnel* rides the path, so the tube curves up, down and sideways
    ahead of the camera; each cell's rod buds, holds and dissolves on its own
    clock; the bead chain morphs from smooth rod to a string of pearls and
    back on uFormPhase; the chrysanthemum is what the tunnel is aimed at.
  - *Curl Bloom* gains the satellite bank around the stirred host - Detail
    buys population, three to six - each on the morph ring at its own offset,
    coloured by its seed and lit as it arrives and leaves. The bank sits in
    unstirred space, so only the host's term is divided by the warp bound.
  - *Morphogen* gains the same bank around the organism. Its ray cull grows
    to hold the orbits (SCENE_BOUND) while the body keeps its own clip; a
    satellite has no seam, so the metamorphic heat is zero on it.
  - *KIFS*, *Non-Euclid*, *Vanishing* and *Nebula* gain the bank too, by a
    different route. Each of those bounds its own march - an escape ball, a
    cull ball, a support sphere - so a body outside the bound would never be
    reached by folding it into map(), and each carries a Lipschitz division,
    a dissolve clip or a volume integral the bank would have to be special-
    cased through. So the bank is marched ON ITS OWN
    (`dmtMarchSatellites()`, a handful of length() calls per step) and the
    style takes whichever of its own hit and the bank's is nearer: occlusion
    comes out right by construction and nothing about the style's own march
    changes. KIFS orbits the bank outside its escape ball and lets the bank's
    hit cap the cathedral's march; Non-Euclid and Vanishing orbit it around
    their own centres; Nebula puts it inside the cloud, so its hit is the far
    end of the volume slab and the dye in front of a body is integrated while
    the dye behind it is not. `dmtSatelliteColor()` shades a bank hit the
    same way in all four.
  - *Nectar Flow* flies the same path (a volume march needs no correction for
    a curved camera) and the mandala sits on the flight axis behind the dye.

  One bug worth recording, because it is the shape of bug a bounding-volume
  early-out always has: `dmtSatellites()` returned the distance to a body's
  bounding ball when outside it, and that distance is exactly zero on the
  ball, which passes the march's hit test - every ray stopped on an invisible
  sphere and shaded the inside of it. The same failure `kifs_frag` documents
  for its escape ball. The bound now returns the ball distance plus the slack
  to the body's true extent, which is still a lower bound and is 0.34 r on
  the ball.

- **`tools/hostlink`**, a host link check for the native renderer. Every
  translation unit `core/CMakeLists.txt` lists under `viz/` and `util/` is
  compiled with the project's own flags and linked into one shared object
  with `--no-undefined` against Mesa's `libGLESv2`/`libEGL`; projectM's public
  headers come from the pinned submodule commit with the repo's render-fbo
  backport applied, and the only stubs are the extern "C" entry points of
  projectM, the asset manager and the logger, generated from `nm`. A changed
  constructor or a declaration with no definition fails here as it fails in
  the NDK link, with no SDK or device. It says nothing about clang, the
  NDK's libc++ or the audio and library trees.

- **`tools/glslcheck`**, a headless compile-and-render check that runs.
  `tools/shaderpreview` reads Kotlin scene files deleted in the C++ port and
  fails on its first line; it is marked stale. glslcheck parses the include
  registry out of `ShaderSource.cpp`, resolves includes the way the native
  core does, refuses to render a shader that declares a uniform its value
  table does not cover, and writes the frame as a PNG. Every fragment style
  and all 72 fluid display variants were compiled and rendered through it
  for this change. It is SwiftShader, so it is a lower bound on portability
  and not a device check; `docs/DEVICE_CHECKS.md` still applies.

- **Two more styles that put the fluid into three dimensions** (Styles >
  Shaders, ids `curl_bloom`, `nectar_flow`). Both are raymarched, so unlike the
  four flat styles below they DO spend the Detail budget and both join
  `MARCHED_SCENES`. *Curl Bloom* is one solid body that is continuously
  changing what it is - sphere into octahedron into torus into box and round to
  the sphere again - while the space it sits in is being stirred, so the
  surface is always folding through itself. The morph costs the march nothing:
  the four primitives sit on a closed ring and neighbouring pairs are blended
  with `mix()`, which is a convex combination of two 1-Lipschitz functions and
  so is itself 1-Lipschitz, meaning every intermediate shape is exactly as
  marchable as the two it lies between. *Nectar Flow* is a volume rather than a
  surface: luminous dye wrapped around a fold-and-scale skeleton, stirred by
  the same flow, with the camera flying through it. The fold decides where the
  light is and the flow decides how it moves, which is the pairing the style
  exists for - a fractal fold alone is rigid and obviously synthetic, an
  incompressible flow alone has no structure to show off.

  `lib_scene_motion` gains the 3D half of the fluid to support them.
  `curlVelocity()` differentiates a noise field, which costs four fbm
  evaluations - acceptable once per pixel and ruinous inside a march loop where
  `map()` runs up to 128 times per ray. So the 3D field is analytic instead:
  `abcFlow()`, the Arnold-Beltrami-Childress flow, a steady solution of the
  Euler equations whose every component depends only on the other two
  coordinates, making its divergence identically zero by construction rather
  than approximately zero, at a cost of six trig calls. `fluidWarp3()` folds it
  by applying it twice, and `fluidWarp3Lipschitz()` returns the bound a surface
  march must divide its distance estimate by - derived from the row-sum norm of
  the flow's Jacobian, which has two entries per row each at most 1, so one
  application is bounded by `1 + 2*amount*scale`. It is a bound and not the
  true norm, so the march takes shorter steps than it strictly must; that is
  the correct direction to be wrong in, because an overestimated step walks the
  ray through the surface and a distance march cannot recover from that.
  Nectar Flow needs none of it - a fixed-step volume loop has no estimate to
  overshoot, the same reasoning `nebula_frag` sets out for its own volume
  march - which is also why its domain can be folded as violently as it is.
  One built-in preset each (Stirred, Through It).

- **Four new fragment styles from the reference clips** (Styles > Shaders, ids
  `merkaba_grid`, `spiral_eye`, `blacklight_bloom`, `fractal_temple`).
  *Merkaba Grid* is a cathedral of sacred geometry: a triangulated web filling
  the depth, wire-frame geodesic spheres along the floor, star tetrahedra
  hanging in the field with a glint at every vertex, and a column of light up
  the middle that blooms where it leaves the hall. *Spiral Eye* winds a
  turbulent cloud into a logarithmic spiral and streaks it outward - the dye is
  sampled six times along the radial pull, which is a motion blur that costs no
  buffer - with a small iridescent mandala burning at the centre. *Blacklight
  Bloom* is a dense mirrored tapestry in violet and magenta, drawn three times a
  hair apart so every edge carries cyan and green fringes, with spiral discs and
  wire tetrahedra floating in front and a soap bubble refracting the tapestry
  through the middle. *Fractal Temple* closes a triangulated dome overhead, hangs
  a ring of neon lamps under it and runs a stepped causeway out to the horizon
  between two rows of filigree spires. Each pairs its own structure with the
  curl-advected dye and the drifting mote layer described below, so the fluid is
  part of the style rather than something the composite adds afterwards. All
  four are 2D perspective constructions, so none of them spends the Detail march
  budget. One built-in preset per style (Sanctum, Event Horizon, UV Ink,
  Causeway).

- **Audio response reworked across every fragment style: smooth, and a
  transient now means a change of state rather than a flash.** A fragment
  shader has no frame-to-frame state, so it cannot smooth anything itself;
  every style was therefore multiplying its structure straight by a raw band
  envelope that is free to move 0 -> 1 between two frames. The smoothing now
  lives in `ShaderScene::stepMotion` on the C++ side and arrives as uniforms,
  documented in the new `app/src/main/assets/shaders/lib_scene_motion.glsl`:

  - `uBassSmooth`, `uMidSmooth`, `uTrebleSmooth`, `uEnergySmooth` are the
    slew-limited companions to the raw envelopes - same range, same meaning,
    but an 8 Hz one-pole covers about 13% of the gap in a 60fps frame, so a
    band spiking 0 -> 1 in one frame moves these by 0.13 rather than by 1.
    `uSwell` is slower again: how loud this passage is, not what just happened
    in it.
  - `uSpike` replaces the `beatEnv * beatEnv * beatBump` idiom wherever that
    idiom drove brightness. It is the same accent with a ~120ms rise on it, so
    it physically cannot reach its peak inside one frame.
  - A transient no longer moves the picture, it moves a DESTINATION, and the
    value the style reads glides there over most of a second. There are three,
    and they are the three things a spike is allowed to mean: `uMoveDir` (a new
    travel direction - a bounded turn, never a reversal), `uSpawnSeed` with
    `uSpawnAge` (a new spawn, with an age to grow it in from nothing) and
    `uFormPhase` (a new fractal - the next plateau on a golden-ratio walk, so
    consecutive spikes always land visibly apart instead of dithering around
    one value). Spikes are rate-limited to one per 0.28s, so a busy drum line
    re-aims the picture a few times a second at most rather than once a frame.
  - `uFlowPhase` is an integrated travel clock: loudness sets its rate and
    never its sign. This fixes a defect the styles shared. `uTime * (base + k *
    energy)` does not speed a camera up, it teleports it - at t = 60s a rate
    moving 1.4 -> 3.6 slides the Rod Tunnel viewpoint 132 units down the tube
    in a single frame - and the same shape was spinning Neon Tiles' hall,
    Mandala Dome's domes, Chroma Orb's skin and Bead Vortex's strands.

  Converted: `vanishing`, `morphogen`, `nebula`, `noneuclid`, `kifs`,
  `orb_lattice`, `rod_tunnel`, `neon_tiles`, `chroma_orb`, `mandala_dome`,
  `bead_vortex`. Two behaviours were wrong rather than merely abrupt and are
  fixed with them: Orb Lattice stepped its kaleidoscope 8 -> 6 -> 4 as the beat
  envelope decayed, so it re-folded twice on the way down from every hit and
  held none of the three, and Mandala Dome fed raw `uTreble` into a glow term,
  which took the whole hall to white on the frame a cymbal landed. Both now sit
  on held plateaus and slew-limited signals. The description of the three
  styles below is superseded on this point: the beat no longer widens the
  fringes or flares the core on the frame it lands, and energy sets a rate
  rather than an angle.

  Every converted style also gained the fluid pair from the same library:
  `fluidWarp`, a divergence-free curl advection of the style's own domain, and
  `fluidMotes`, sparse particles riding that same field. Deliberately computed
  in-shader rather than read from `uFlow`: the composite pass already warps
  every style through the shared FlowField, so a second read of that texture
  here would apply one velocity field twice.

- **MilkDrop: the repeating logcat error, and the preset that did not come back
  from the background.** Both were the same failure seen from two ends.
  `ensureEngine()` runs once per frame from `draw()`, and its
  `projectm_create returned NULL` line sat outside the `reportedCreateFailure_`
  guard, so a single failed create wrote roughly sixty error lines a second for
  as long as the scene was on screen. It also never recovered, which is why the
  preset stayed gone: nothing backed the retry off and nothing re-armed the
  preset when a later attempt would have worked. A failed create now waits two
  seconds before the next attempt, logs once rather than once per frame, and
  clears its latch when it succeeds. `armLastPreset()` re-queues the preset the
  scene last had on screen AND clears the load debounce, so a rebuilt engine
  loads it on the next frame instead of having the 0.4s debounce swallow the
  restore. `onPresetSwitchFailed` writes a line only when the message has
  changed, and `init()` resets the debounce, the GL-error latch and the log
  dedupe along with the rest of the per-context state.

- **The Beam style is gone**, with its scene, its two shaders, its four
  parameters (`beamXy`, `beamWidth`, `beamIntensity`, `beamTail`), its Shape-tab
  section, its Styles tab, its `ParamScope` group and the woscope third-party
  notice that covered the shaders. The scene parameter frame is a POSITIONAL
  contract verified against the native library at startup, so both sides moved
  together: `SceneParamsCodec.FIELDS` and `SceneParams::fieldNames()` go from
  138 to 134 and the three `rippleOverlay*` fields move from slots 135-137 to
  131-133.

- **Three new fragment styles after the reference pictures** (Styles >
  Shaders, ids `chroma_orb`, `mandala_dome`, `bead_vortex`). The earlier
  attempts at the same three pictures (`orb_lattice`, `rod_tunnel`,
  `neon_tiles`) stay as they are; these are new styles beside them.
  *Chroma Orb* is a glass sphere skinned with a four-fold kaleidoscope of dot
  lattices, a ringed tunnel bored through its centre and the whole skin drawn
  three times at slightly different scales, so every dot has a green core with
  blue and orange lens fringes, over a dark green field of bokeh. *Mandala
  Dome* is a mirrored hall of eight-point stars, rings and framed tiles in
  violet, blue and magenta with green, cyan and red fringed outlines, wrapped
  over two domes bulging in from the sides and a patterned sphere in the
  middle. *Bead Vortex* looks down a tunnel walled with strands of segmented
  beads - crimson, olive and violet - winding into a white-hot core through a
  purple haze of drifting motes. All three are 2D sphere and tunnel mappings
  with faked lighting, so none spends the Detail march budget; bass breathes
  the patterns and swells the beads, the beat widens the fringes and flares
  the core, energy sets the spin, and each still moves in silence. One
  built-in preset per style (Lens, Cathedral, Corkscrew).

## v1.8.0 (code 32) - Native core, Studio timeline, player features

The engine's compute moved to C++ behind one `libgeode.so`, the Studio grew a
real timeline and multi-clip export, and the player gained Android Auto, a
widget, smart playlists and a native audio engine. The owner's first full
build of this tree (PR #9) compiled the native core, the NDK media and GLES
calls and the Media3 code; the three new fragment styles below have not yet
run on a device. This section folds the build ledger that tracked the work
(`docs/BUILD_STATUS.md`, now retired), newest phase first.

- **Finish (8).** README rewritten for the module tree, the native core and
  the submodule step, with the ledger's owner notes carried into a maintainer
  section; the stray `lines.txt` removed; stale paths in
  `THIRD_PARTY_NOTICES` and `docs/visualizer-v2/provenance.json` corrected.
- **Player (7).** `PlaybackService` is a `MediaLibraryService` with an
  Android Auto browse tree (tracks, albums, artists, playlists, favourites,
  recently played) built from the same stores the library screen reads;
  tapping a row in a folder queues the folder from that row. Smart playlists
  (rules over title, artist, album, folder, length, days since added, play
  count and favourite, all/any matching, optional limit) with an editor on
  the Playlists tab, and a Duplicates tab that groups same title, artist and
  length and deletes through `MediaStore.createDeleteRequest`. A Glance
  now-playing widget (artwork, title, artist, transport through a
  `MediaController` on the session). Tracks over twenty minutes remember
  their position (`BookmarkStore`, saved from the session poll, cleared when
  played to the end). A bit-perfect USB output toggle behind the native
  engine on Android 14+, through `AudioManager.setPreferredMixerAttributes`.
- **Video suite (6).** Editor project persistence with autosave and whole-
  project undo/redo; a multi-lane timeline on the Studio tab (trim, split,
  ripple, snapping, markers, tap-in, auto-cut from the analysed track);
  keyframe curve editor with hold/linear/ease/custom bezier, applied per
  frame at export. Multi-clip export through a Media3 `Composition` (the
  first media lane end to end plus the first audio lane), GL Transitions
  between clips compiled from `gl_transitions.json` into a `GlEffect` (the
  previous clip's last frame is captured and the transition runs over the
  next clip's opening), speed ramps from `clip.speed` keyframes as a
  `SpeedProvider`, and per-frame grade and rotation for keyframed clips.
  Captions: Text-lane clips become a time-varying `TextOverlay`, `.lrc`
  lyrics and SRT files import onto a text lane, and SRT exports. HEVC
  export in every encoder with a `MediaCodecList` check and H.264 fallback.
  `.cube` LUTs (3D and 1D) and per-channel gamma on clip edits through
  `SingleColorLut`.
- **Audio DSP and native player (5).** `core/audio/dsp`: ten RBJ peaking
  biquads on the ISO octave centres, a bass shelf, smoothed gain, headphone
  crossfeed and a lookahead limiter, inserted into the Media3 chain as
  `NativeDspProcessor`; `AudioFxController` drives it instead of the platform
  effects. TagLib (static, symbols hidden) behind `geode_tags_read/write`
  over a file descriptor: ReplayGain on playback (track/album, preamp, clip
  guard) and tag writing from the track editor behind a switch. A native
  player (`AMediaExtractor`/`AMediaCodec` decode, Hermite resampling, a
  lock-free mixer with gapless joins and crossfades, Oboe output) exposed as
  `NativePlayer : SimpleBasePlayer` behind a "Native audio engine" setting,
  default off; the analysis tap is fed from the native mix when it is on.
- **Renderer core (4).** Shaders moved to `app/src/main/assets/shaders/`
  and are resolved by the native `ShaderSource` with the same `//#include`
  rules. `core/viz` carries the GL capability probe and program-binary cache
  (same cache files as before), the frame graph (scene → trails → composite,
  with an explicit target FBO for export and the wallpaper), LFO/ADSR,
  visual safety and flash budget, frame pacing, thermal tiers, transitions,
  and every scene family: fragment styles, cymatics, fluid/curl-flow/water,
  the particle and simulation scenes, the compute path with its fragment
  fallback, and MilkDrop with projectM owned by the scene. The Kotlin scenes,
  passes and the `:engine:gl` module were deleted once each port completed;
  `VisualizerRenderer` and `OffscreenSceneRenderer` are adapters.
- **Analysis core (3).** Every producer in `engine/audio-core` ported to
  `core/analysis` on kissfft (windowing, spectrum, log bands, mel/MFCC,
  chroma, spectral flux and SuperFlux, descriptors, onsets, envelopes,
  whitening, tempo and its stability, beat grid, bars, drums, key,
  structure, stereo field, frame levels and grid, pulse replay); the Kotlin
  `ReactiveAnalyzer` is a thin wrapper over `geode_analysis_*`, JTransforms
  is gone, and the offline analyzer drives the same native code so cached
  timelines stay valid.
- **Reference looks (2).** Three fragment styles: `orb_lattice`,
  `rod_tunnel` (raymarched, on the kifs march budget) and `neon_tiles`, with
  built-in presets. Not yet run on a device.
- **Native skeleton (1).** projectM v4.1.7 as a git submodule built in-tree
  by the root `CMakeLists.txt` (16 KB page alignment, `-fvisibility=hidden`,
  LTO in release); `core/api/geode_api.h` as the only ABI JNI calls;
  `GeodeNative` in `:engine:audio-core`; the prebuilt `.so` blobs, vendored
  headers and the `native-libs.yml` workflow removed. Run
  `git submodule update --init --recursive` after pulling.

Older releases (v1.7.0 and earlier) are in the git history: `git log -- CHANGELOG.md`.
