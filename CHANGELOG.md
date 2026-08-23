# Geode — Changelog

All release notes, newest first, kept verbatim from the project README (the
history used to sit above the README's own introduction; it now lives here).
One block of entries appeared twice in the old file — the exact duplicates
were removed, nothing else was changed. Historical entries reference files as
they existed at the time: `todo.md` and the planning docs were retired once
their content shipped, and the original `docs/DEVICE_CHECKS.md` was lost —
a partial reconstruction, rebuilt from the references in these entries, is at
[docs/DEVICE_CHECKS.md](docs/DEVICE_CHECKS.md).

## Composite visual families and mineral UI (working tree)

- **Removed the Hyperspace style family.** All eleven looks (Original · Living
  Fractals, Polytope, Liquid Warp, Caduceus, Cortex, Reliquary, Foam, Dustskin,
  Plume, Resonant Wormhole and Moiré), the raymarched scene and its shader, the
  body simulation, the world-anchored "melt" medium, the Customize tab and its
  twenty-four `hyper*` parameters are gone. Saved presets and templates that
  named a Hyperspace scene fall back to the default style; their unknown JSON
  keys are ignored on read, so nothing else in a preset is lost.
- Cymatics keeps its original saved-preset IDs while exposing ten authored
  substyles through one shared renderer.
- The eight named crystal themes now draw different deterministic mineral
  structures instead of recolouring one generic glass/nebula texture.
- Design rationale, open-source references, the complete style catalogue and
  on-device validation targets are in [`docs/VISUAL_STYLE_RESEARCH.md`](docs/VISUAL_STYLE_RESEARCH.md).

## v1.7.0 (code 31) - Home rebuilt, other apps' audio, a player with a face, and the Export Studio

- **Home is about your listening now, not a list of titles.** It used to be
  two rows of text chips - the Library tab, with less information in it. The
  hero card is live: artwork, transport, progress and the actual spectrum of
  whatever is feeding the analyser, sampled on its own 20 Hz clock rather than
  collected as state (the analyser emits at the FFT hop rate, and recomposing
  a scrolling list that often stutters for a decoration). Below it: five quick
  actions, a week of REAL listening time, and shelves of artwork - Jump back
  in, Favourites, On repeat, Recently added, and your saved looks. A shelf IS
  the queue a tap starts, the same rule the Library and search already follow.
  `ArtworkCache` decodes sleeves down to 384 px off the main thread and caches
  misses as misses; tracks with no picture get a gradient derived from their
  uri, so they still look like themselves everywhere they appear.
- **Listening time is measured, not counted.** `HistoryStore` accumulates
  milliseconds actually played, per track and per day, so "this week" is a
  measurement rather than a tally of taps. Time is booked between poll ticks,
  so a seek is not charged as listening and a suspended process does not bank
  the hours it was asleep. The week strip scales to its own busiest day. The
  v1 history file (a bare JSON array) is still read, so no install loses its
  history.
- **Visualize whatever is playing** (Settings > Other apps' audio, or one tap
  on Home). Android 10's playback-capture API feeds the SAME ring buffer the
  player's tap and the microphone already share, so the FFT, the beat tracker,
  every scene, the exporter and the live wallpaper are unchanged and unaware
  of where the samples came from.
  - **Spotify will not be captured, and the app says so.** An app declares
    whether it may be captured and the system honours that; Spotify declares
    no. What makes it nasty is that a refusal is not an error - the recorder
    opens happily and is handed perfect digital silence, which looks exactly
    like a working visualizer with nothing to draw. So it is diagnosed
    explicitly, and needs both halves to agree: four seconds without a single
    non-zero sample AND a media session saying something is playing. Then it
    names the app and offers the microphone, which hears the same speaker.
    YouTube, podcast players and games allow capture and simply work.
  - A foreground service holds the projection and an ongoing notification for
    as long as it runs - the platform requires it, and it is the honest
    statement that this app can currently hear the device. Revoking capture
    from the system UI tears it all down.
  - Naming the foreign track is separate and optional: it reads the media
    session (structured metadata) rather than scraping notifications, and
    needs notification access the visuals do not.
  - Nothing captured is written to disk or sent anywhere.
- **The player grew a face.** Now / Lyrics / Queue, over the live canvas.
  - The **seek bar is the track's own loudness**, reduced from the per-frame
    RMS curve the offline analyser already computes for the visuals. Peak per
    bucket, not mean - a mean flattens a song into a grey ridge. Unanalysed
    tracks fall back to a plain bar. Dragging commits on release, because
    seeking per pointer event would re-prepare the decoder dozens of times.
  - **Timed lyrics.** An `.lrc` beside the track beats the file's own tags.
    Several timestamps on one line (how LRC writes a repeated chorus) become
    several lines; hundredths and thousandths are told apart by width. Tapping
    a line seeks to it. Nothing is fetched - the app has no network access.
  - **The queue is visible and editable**: jump, pull forward, remove. Buttons
    rather than drag, because this list sits on a full-screen gesture surface.
  - **Favourites** (a heart in the player, a shelf on Home, a star in the
    queue), **A-B repeat** painted into the waveform, **fades** on pause,
    resume and skip (0-6 s; explicitly not a crossfade, which one player
    cannot do), and a sleep timer that can **let the track finish**.
- **The Export Studio** (new bottom-nav tab). The exporter made a file and
  stopped, leaving "cut the good bit out" and "get it somewhere" to a second
  app. Studio lists what Geode has rendered (or opens any video), and does
  both: trim over a filmstrip, seven looks that write ordinary grade sliders,
  brightness/contrast/saturation/hue, speed, rotate, reframe to any of the six
  export ratios (cropping, not pillarboxing), mute, and a burnt-in caption.
  Built on Media3's Transformer - it knows when NOT to re-encode, and a
  trim-only edit becomes a lossless container rewrite. Every render is a NEW
  file in Movies/Geode; the original is never overwritten. "Send" is the
  system share sheet on purpose: the app holds no network permission and no
  API keys, and the sheet reaches YouTube, Instagram, TikTok and Drive with
  the account the phone is already signed in to.
- **The audio-quality panel is gone.** The LOSSLESS/LOSSY badge and its
  codec/bitrate card sat under the artist line on Now Playing, competing with
  the visuals for attention and reporting something you cannot act on.
- 45 new tests: LRC parsing and the line-at-position lookup, plays vs
  listening time, the per-day week, the v1 history file, favourites ordering
  across a reload, and the Studio's trim/speed/identity arithmetic.

## v1.6.0 (code 30) - The melt: Hyperspace poured into the fluid engine

- **The fluid engine now runs underneath the fractals.** HYPERSPACE gains a
  world-anchored `MeltField` - the same Stam solver the FLUID family uses, with
  velocity *and* dye - and the two are coupled in both directions. The
  raymarcher samples the velocity field at two orthogonal projections of every
  sample point and displaces it before evaluating any fractal, so the whole
  scene is stirred as one continuous medium: the geometry stretches, smears and
  pulls like taffy instead of turning as a rigid object. That is "Melt".
- **The loop closes: the bodies stir the fluid back.** Every living fractal
  drops a capsule of its own colour into the medium each frame, from where it
  was to where it is - so a body drifting across the room leaves a wake, and a
  body being born or dissolving blooms ink outward (it pushes 1.8x harder at
  both ends of its life). The music stirs it through the same `FluidEmitters`
  schedule the FLUID style runs, so a beat lands here exactly as it lands
  there.
- **You can mold it with your finger.** A drag on the visualizer is routed
  into the medium as a capsule of force and dye, so the fractals it crosses are
  pulled out of shape and stained in the same gesture.
- **World-anchored, not screen-anchored**, and that one decision is what lets
  all of the above agree with each other: sim space IS the world's xy plane. A
  screen-space field would have looked right until the camera moved, at which
  point the ink a body had shed would slide off it.
- **Three ways the dye is seen.** "Ink stain" lights the surfaces the medium
  has run over, lit by the same key light so it reads as wet rather than as a
  decal. "Liquid light" makes the dye a participating medium in its own right -
  integrated WITH extinction, so ink in front of a body dims it and the near
  side of a cloud is brighter than the far side. "Ridges" combs each surface
  across its local flow direction, hue and all, which is the single most
  recognisable mark in the reference paintings: every surface in them is
  combed, and the combing follows the current rather than the geometry.
- **Two numbers keep the raymarcher honest**, and both are pinned by tests
  rather than eyeballed. The displacement is hard-clamped to `MeltMath.reach`,
  which is *exactly* the amount every bounding sphere is inflated by on the CPU
  - a velocity spike that displaced further would push geometry outside the
  sphere the ray culled it with, and slice the body along a perfect circle. And
  the march step is relaxed by `MeltMath.stepRelaxation`, because warping the
  domain breaks the distance estimate's Lipschitz bound and a ray still taking
  the full estimate walks straight through thin geometry.
- Two rendering fixes the preview renders forced. The liquid medium is
  integrated with extinction rather than summed, because a plain sum has no
  upper bound and turned a well-inked region into one flat wash; and it is
  sampled at a per-pixel jittered offset along the ray, because sampling every
  pixel at the same depths quantized the integral into visible concentric
  shells that looked like contour lines drawn on the fog.
- Customize gains a **Melt** group on the Hyperspace tab: Melt, Ink stain,
  Liquid light, Ridges, Stir, Vorticity and Flow fade, all randomizable and
  lockable. A fourth built-in preset, `hyperspace · Molten`, turns the medium
  all the way up - fewer, larger bodies for it to pull on and a slow-clearing
  field so the ink builds into strata - and trims Detail to pay for it.
- On a GPU that cannot give the simulation half-float buffers the style still
  runs, as solid geometry: one uniform gates the whole medium, and "Melt" at 0
  is an exact no-op that does not even cost the texture reads.
- 9 new tests (`HyperspaceMeltTest`) pin the identity between the displacement
  ceiling and the sphere inflation, the monotonicity and bounds of both safety
  numbers, the world/sim mapping (including that every act's orbit spread lands
  inside the grid rather than in a handful of texels), and the splat clamps.

## v1.4.0 (code 28) - Hyperspace: a room of 3D fractals, each one alive

- **A new visualizer style, `hyperspace`** (Visuals > Styles > Hyperspace):
  the first raymarched style in the app. Every visible body is a
  distance-estimated 3D fractal, and there are up to eight of them at once,
  each with its OWN rotation, its own orbit, its own colour and its own life.
  They bud out of nothing on a hit, tumble past each other on unrelated
  clocks, and dissolve. That is the whole design - the visionary/DMT art it
  was built from is never one object turning, it is many things living next to
  each other.
- **Five fractals, one raymarcher.** GASKET is a sphere packing (the limit set
  of translations of a cell plus inversion in a sphere - clusters of spheres
  nested in spheres); TEMPLE is a kaleidoscopic IFS whose rotation between
  iterations turns a Menger sponge into terraced architecture; JEWEL is the
  box, whose negative scales fold it inside out into flat faceted panels;
  CORAL floors the inversion radius so the group stays connected and grows
  smooth branching tubes instead of a dust; BULB is `z -> z^n + c` taken in
  spherical coordinates, the round budded one. They share a loop because they
  share a shape - fold space, then invert or scale it, and keep the running
  scale factor so the estimate can be pulled back to world units. "Fractal"
  picks one or leaves the room Mixed.
- **A five-act story, walked over the track.** Threshold (a nearly empty
  field), Chrysanthemum (the mirrored symmetric fabric), Magic eye (the flat
  texture gains depth and the bodies separate out), Waiting room (a place,
  close and full), Breakthrough (everything at once, wide open). The act sets
  how many bodies live, how strong the background filigree is, where the
  camera sits, how fast everything turns and how much of the colour wheel is
  in play. On "Music" the track's own macro-dynamics decide: loud passages
  take the journey deeper over ~26s and quiet ones bring it back over ~44s,
  because coming back should take longer than going in. "Hold" pins one act
  and "Cycle" walks all five on a timer.
- **Eight distance-estimated fractals per pixel, on a phone.** Each body
  carries a bounding sphere; outside it the ray steps by the distance to the
  sphere and the estimator is never touched, so a ray iterates the one or two
  bodies it is actually near rather than all eight. The estimator is also
  intersected WITH that sphere - three of the five describe unbounded sets (a
  plane, a cylinder, a tiling), and without the clip a body streaked across
  the whole scene as a stripe. "Detail" moves march steps and iterations
  together as one frame-rate control.
- **Lit like the paintings, not like a render.** A fresnel rim for the neon
  outline every one of these images has, an orbit trap banding each body into
  nested shells of different hue, a translucency term so thin filigree lights
  up while thick cores stay dark, and a smooth aura taken from each body's
  bounding sphere rather than from the fractal inside it - the estimator jumps
  discontinuously at the sphere, and lighting the haze with it drew a hard
  ellipse around every body instead of a halo.
- **The background is the chrysanthemum**: an infinitely detailed filigree
  evaluated on the ray direction, so it has no edges and turns with the view.
  Two orbit traps, because the fabric has two scales - closest approach to the
  origin draws the knots, closest approach to an axis plane draws the threads
  between them. Summing the orbit instead washed the whole sky to a pastel
  fog, which is the difference between a filigree and a haze.
- **The camera stays outside every body.** A raymarcher started inside a
  folded distance estimator does not draw an interior - the estimate is only
  valid outside the set - so it draws a screen of stripes. The act asks for a
  distance and only gets it if it clears the widest body at that density, and
  a test walks every act at every density to prove it.
- **Visual safety, by construction.** Nothing here strobes: the fastest thing
  on screen is a body's own rotation at a fraction of a hertz, energy leans on
  the exposure by ±12%, and the one discontinuous change - the kaleidoscopic
  mirror, which cannot be interpolated without smearing the frame into streaks
  - can only happen when an act changes, no more than once every four seconds.
- Customize gains a **Hyperspace** tab (visible only on that style, like
  Cymatics and GLSL), grouped as Journey, Life, Look and Quality - seventeen
  controls, fifteen of them randomizable and lockable ("Detail" is a
  performance setting and "Journey" is a mode, so neither is rolled). Three
  built-in presets ship with it: `hyperspace · Breakthrough`,
  `hyperspace · Chrysanthemum` (the mirrored act, held) and
  `hyperspace · Coral garden` (one species, long lives, slow). The style joins
  the composite-graded family, so Hue shift, Brightness, Contrast, Gamma,
  Zoom, Rotation and the whole FX chain work on it as they do on the fluid
  styles.
- An idle drive walks the whole journey on its own over ~150s and keeps
  spawning bodies, so a paused app or a live wallpaper shows the whole story
  rather than parking on the empty opening act.
- 37 new tests (`HyperspaceMathTest`) pin the act table and its interpolation,
  all three journey modes and the dwell that stops a track hovering at the
  pivot from re-targeting the body count several times a second, the
  world-to-local rotation against the forward orientation it has to undo, the
  life envelope (including that retiring a body early does not step its fade),
  the bank's spawn/retire rules, the uniform packing, the camera's framing and
  orthonormality, and that its path does not close on itself inside ten
  minutes.
- Attribution: the five estimators are implemented from the published
  mathematics of the kaleidoscopic-IFS, Mandelbox, Apollonian/Kleinian and
  Mandelbulb constructions, and the five acts from published accounts of the
  DMT visual experience - see THIRD_PARTY_NOTICES. No source is copied. The
  in-app notices asset had drifted out of sync with the root file and is now
  a copy of it again.

## v1.3.0 (code 27) - Cymatics: the sound itself, fullscreen

- **A new visualizer style, `cymatics`** (Visuals > Styles > Cymatics): the
  standing-wave field of whatever is playing - a track, or the microphone on
  live input - evaluated PER PIXEL and filling the whole screen. Play a tone
  into a dish of water and the surface answers with a figure; this style is
  that figure, so the picture is a depiction of the sound rather than a look
  reacting to it. A pure tone draws one clean symmetric figure, a chord draws
  the superposition of its notes' figures, and silence draws almost nothing.
- **Two geometries off the same ringing modes.** "Water dish" runs
  circular-membrane modes - `J_m(beta*r)cos(m*a)`, concentric rings crossed by
  petals, the CymaScope look - and "Chladni plate" runs the square-plate
  formula's nodal lattice (`cos(n PI x)cos(m PI y) - cos(m PI x)cos(n PI y)`,
  the maths of `codenlighten/3D-Cymatics`). One mode table, one resonator bank
  and one pitch -> figure law feed both, so they can never disagree about what
  the music is doing.
- **No object, no camera, no black surround.** It is one fullscreen fragment
  pass: the field continues past every edge of the screen, and detail comes
  from the modes the music excites - higher pitch, finer figure - plus a
  "Field scale" control for how much of the field is on screen at once. The
  modes keep MOVING: each carries its own phase, advancing at its own rate,
  and "Wave flow" turns the standing waves into travelling ones that march
  outward the way a driven dish sheds them.
- **The pitch -> figure law is the real one.** A stiff plate's modal
  frequencies rise with the SQUARE of the mode order (Kirchhoff plate theory,
  and the reason Chladni's own law is quadratic), so `wavenumber =
  sqrt(hz / fundamental)`: an octave up is 1.41x finer, not 2x, which is what
  lets a 40 Hz - 16 kHz spectrum land inside the enumerated modes instead of
  running off the field within three octaves. On the dish, each mode's rings
  sit at the zeros of J_m, placed by McMahon's expansion.
- **Bessel, cheaply but not wrongly.** The dish evaluates J_m per pixel from
  two terms of Hankel's asymptotic expansion. One term is not enough: the
  leading cosine alone puts J_4's first ring at 7.85 instead of 7.59, and the
  innermost rings are exactly the ones filling the middle of the screen. With
  both terms every ring lands within ~0.05 of the real function's zero, which
  `CymaticsMathTest` pins against a series expansion. J_0 peaks at the centre
  and every higher order vanishes there - getting that backwards punched a
  black hole through the middle of the dish, which is now a test.
- **Modes are resonators, not FFT bins.** Each of the ~100 modes has its own
  attack and a "Plate ring" decay, so a figure holds its shape through a note
  and dissolves after it rather than flickering with the frame rate. "Tonal
  focus" chooses what the field hears: raw band energy (bass-led, big slow
  shapes) or spectral peaks above the local mean (pitch-led, so a melody or a
  hi-hat can put a fine figure up at all).
- The band -> frequency map mirrors `FftProcessor.bandEdges` INCLUDING its bin
  quantization: at 44.1 kHz a bin is ~21.5 Hz, so the bottom ~20 log-spaced
  band edges round onto the same bin and get bumped apart. Band 12 covers
  280-323 Hz, not the 130 Hz the pure logarithm suggests, and a field that
  believed the logarithm answered every bass note an order too coarse.
- **Visual safety, by construction.** A real plate vibrates at the frequency
  it is driven at - hundreds of hertz, in the band the photosensitivity work
  exists to stay out of. The mode phases are that motion strobed down to
  0.12-1.6 Hz, well under the WCAG three flashes per second, and pinned there
  by a test.
- Customize gains a **Cymatics** tab (visible only on that style, like GLSL),
  grouped as Wave (geometry, Fundamental, Standing waves, Tonal focus, Plate
  ring), Field (scale, Wave flow, Field swirl) and Look (Fill, Nodal lines,
  Nodal glow, Iridescence, Caustic sheen). All thirteen are randomizable and
  lockable. Three built-in presets ship with it: `cymatics · CymaScope`
  (filigree over dark cells), `cymatics · Psychedelic` (the same field filled
  in and banded into iridescent rings) and `cymatics · Chladni plate`. The
  style joins the composite-graded family, so Hue shift, Brightness, Contrast,
  Gamma, Zoom, Rotation and the whole FX chain work on it exactly as they do
  on the fluid styles.
- 20 new tests (`CymaticsMathTest`) pin both field formulas, the Bessel
  approximation against a series expansion, the m-fold rotational symmetry of
  a dish mode, the band map, the resonator envelopes, per-mode phase advance,
  snapshot normalization and the safety band.
- Attribution: the nodal rendering approach (narrow Gaussian for the filigree,
  wide one for its halo) follows Naadara, the MIT licensed open cymatics
  laboratory - see THIRD_PARTY_NOTICES. No source is copied.

## v1.2.0 (code 26) - Visual safety (photosensitivity limits), and the pulse tracker stops carrying one track's beat grid into the next

- **Safe visuals** (Settings > Visual safety), the first photosensitivity
  controls in the app. An audit of every flashing path found no clamps
  anywhere in the tree, and three hazards reachable without doing anything
  unusual: `strobe` ran a **9 Hz** full-frame square wave at up to 85% depth
  with the rate HARD-CODED in composite_frag.glsl (so turning the slider down
  only ever gave a dimmer 9 Hz flicker, never a slower one); `flash` fires
  once per detected beat, so its rate is the track's and nothing capped it;
  and an LFO can drive `BRIGHTNESS`/`INTENSITY` at up to **30 Hz** - which the
  Randomize button can roll. 9-30 Hz is the band photosensitive-seizure
  guidance is about.
- Thresholds follow WCAG 2.3.1: three flashes per second, risk peaking around
  15-20 Hz. Rate, depth and inversion are separately adjustable, and Reduced
  motion is an independent switch because vestibular comfort and seizures are
  different problems.
- Rate is capped, not just depth. New `uStrobeHz` uniform (defaulting to the
  old literal 9.0) uploaded by BOTH the renderer and FxCompositor;
  `LfoEngine.tick` slows any LFO pointed at a luminance target; the analyzer's
  minimum beat gap is floored so the beat flash cannot outrun the limit; a
  hard CUT transition becomes a crossfade.
- `VisualSafety.apply` runs LAST, after LfoEngine/AdsrEngine, at the two-line
  idiom the live and export paths already shared. Clamping the stored params
  instead would be bypassed - the modulators push those values straight back
  up to their own ceilings afterwards. Both paths clamp identically, so an
  exported clip is as safe as the screen it was approved from.
- Turned OFF it is an EXACT no-op: the clamp returns the same object instance,
  not an equal copy, so saved presets and the export byte-parity tests are
  untouched. It ships off; whether it should default on is an onboarding
  question recorded in todo.md.
- **Beat tracking now restarts between tracks and on seeks.** AnalysisEngine
  holds one FeatureExtractor for the whole session, so a locked beat grid, the
  30 s rolling energy peak and the flux history all carried into the next
  track - the old grid then suppressed the new track's kicks as off-grid. On a
  128 -> 75 BPM change that was 1 beat in the first five seconds where a cold
  extractor finds 6, graded at 0.208 against 0.739: the visualizer started
  every song nearly deaf and faded in over ~15 s. It also broke export parity,
  since the offline replay always starts cold while live playback did not.
  The engine resets through a volatile flag consumed by its worker loop, not
  directly - the extractor is owned by that coroutine and player callbacks
  arrive on the main thread.
- **The beat-sensitivity replay no longer stalls.** It ran the tempo
  autocorrelation on every frame - O(lags x window), ~1.5 s for a four-minute
  track - on the path that runs at cached-track load, on every sensitivity
  slider settle and before every export. It now refreshes every eight frames
  (~128 ms) and reads a chronological copy of the flux window instead of two
  modulos per term: ~1460 ms -> ~61 ms. Every time constant that depended on
  the per-frame cadence is rescaled, so beat decisions are byte-identical
  across four fixtures x four settings.
- Docs corrected where they disagreed with the code: `beatStrength`'s real
  floor is 0.168, not the documented 0.35; ADSR attack and beat-edge splat
  firing are deliberately ungraded rather than moved to `beatImpulse`; and
  ExportGradeState's KDoc named the wrong field. Two tests that could not fail
  were tightened.
- Device checks 37-39. NOTE check 39's "Safe visuals off" comparisons
  deliberately produce fast full-screen flashing.

## v0.14.0 (code 24) - Customization actually works on every style: composite grading for the fluid family, palette maker, one randomizer
- Composite grading + geometry for the styles that grade NOTHING themselves
  (Fluid, Curl Flow, Water): composite_frag gains uPostZoom/uPostRotation/
  uPostSat/uPostBright/uPostContrast/uPostGamma/uPostHue behind a uPostGrade
  enable flag, uploaded only when the active scene is not a ShaderScene /
  ParticleSceneBase / ProjectMScene. Zoom, Rotation, Saturation, Brightness,
  Contrast, Gamma, Hue shift, Intensity, Colour cycle, Mirror and Invert were
  dead on all three fluid styles and now work, at the same slider response as
  everywhere else. Rotation and the colour cycle are integrated as SPEEDS on
  the composite's own clock (CompositeGrade, CPU-mirrored and tested).
- Export parity: FxCompositor uploads the same grading uniforms behind the
  same gate, so an exported fluid clip matches the live view — including the
  spin/cycle distance travelled, at 30 and 60 fps alike.
- Hue applied exactly ONCE on the fluid family: the SCENE owns palette
  identity (base hue + the palette's own span, via FluidHue.span, decided at
  emission time), the COMPOSITE owns rotation (Hue shift + cycle phase).
  Fluid/Water/Curl Flow previously folded the rotation in as well and turned
  the wheel twice per slider unit; Curl Flow and Water also dropped the span,
  so every palette looked the same in a different tint.
- Brightness/Intensity de-quadratised: Water and Curl Flow no longer multiply
  their own output by the exposure the composite grade already applies.
- Palettes: Cyan / Magenta / Yellow appended to PALETTES (append-only — saved
  presets index into it), plus a full gradient/palette maker: build a palette
  from base hue + span, audition it live, save it to a JSON-per-file library,
  and pick it from the same chip row as the built-ins. Backed by four
  override fields on SceneParams with a documented UNSET_OVERRIDE (-1f)
  sentinel, so a custom palette renders through the existing
  paletteBase/paletteRange path with no render-side change.
- Customize tab gating narrowed to what each style actually READS
  (isEmitterSceneId / isParticleLayerSceneId / isJourneySceneId / …): Water
  regained the emitter + quality sections it has always consumed, Curl Flow
  regained "Particle drag", and Fluid-only controls stopped appearing on
  styles that ignore them. Water's Catch radius and Hue shift are wired.
- Randomizer: ParamRandomizer is the single implementation (102 label-keyed
  entries covering the fluid/water/FlowField blocks it used to skip), with
  curated per-slider ranges so a roll is always reproducible by hand. Chip
  selectors (Palette, Palette 2, Particle shape, Beat pattern, Path) now
  render a lock chip, so every rolled param can be held — previously a roll
  always re-picked the user's palette. Two label collisions split: LFO
  "Depth" -> "LFO depth" (was sharing a lock with Water's Depth) and the
  all-styles overlay's "Ripple strength" -> "Ripple overlay strength".
- Beat sensitivity: sigma ceiling raised 4 -> 6, new "Minimum gap between
  beats" (the lever that actually tames slow tracks, since sigma is relative
  to the track's own flux history), plus Slow track / Default presets.
- Trail-warp decay bugfix: drawTrailWarp took the caller's remapped retention
  but computed uDecay from the raw Trail length slider, so switching Trail
  zoom or Trail warp on dropped Curl Flow below its stream floor. Now shares
  CurlFlowMath.warpDecay with the plain fade path (headless test).
- Cleanup: the orphaned full-screen CustomizeDialog() composable (no callers
  since the navigation refactor, and it had no Fluid tab) is deleted;
  ParamRandomizer no longer imports ui.PaletteStore — the palette-override
  sentinel rule moved onto SceneParams.withoutCustomPalette(), which both
  layers already depend on, removing the only render.scene -> ui edge.
- Docs: docs/PARAM_MATRIX.md rebuilt as a six-family × every-param matrix with
  file:line citations and an explicit gap list; DEVICE_CHECKS items 21-27
  merged and renumbered (fluid colour, water controls, composite grading,
  Curl Flow trails, export grading, beat sensitivity, randomize locks).
- Verify note: built in a container without the Android SDK (dl.google.com
  unreachable), so the gate is GitHub Actions: ktlintCheck +
  testDebugUnitTest + assembleDebug + lint. Run DEVICE_CHECKS items 21-27
  on-device before release.

## v0.13.1 (code 23) - Player settings, library metadata & search, WATER style + ripple overlay, glass UI, boot intro
- Player: PlayerPrefs store (shuffle/repeat now persisted and restored),
  playback speed 0.5-2x and pitch +/-6 st, skip silence, pause-on-unplug,
  keep-screen-on, auto-resume (prepares last track), sleep timer with 3 s
  fade; Equalizer/BassBoost/LoudnessEnhancer on the ExoPlayer session with
  device presets and per-band sliders, fully persisted, degrades gracefully
  where audiofx is unsupported.
- Audio quality readout: Now Playing shows a LOSSLESS / BIT-PERFECT / LOSSY
  badge with an expandable detail card (source codec/rate/depth/channels vs
  decoded output, bitrate, one-line verdict). The tap tee is read-only, so
  the readout reflects what the device actually receives.
- Library: track info editor (title/artist/album/genre/year/track#/comment)
  stored app-only in a versioned library.json v2 (legacy array migrates);
  overrides join into MediaStore rows; imports read full tags.
- Search: MediaStore query moved off the composition thread into the VM
  (fixes Library-tab jank); debounced multi-term search across device
  tracks, imported tracks (incl. album/genre), playlists and presets with
  grouped results and queue-append.
- Visuals: new WATER scene (wave-equation RippleSim height field, 2 GPU
  passes/frame, choreography/emitter-driven drops, refracted depth-graded
  pool, Blinn specular + fresnel, treble glints); ripple overlay applies
  water refraction + glint over ANY style including MilkDrop, with full
  export parity (FxCompositor/VideoExporter) and deterministic musical
  drops shared by live and export paths. GlUtil.resetFrameState now also
  unbinds sampler objects and pack/unpack PBOs (projectM leak hardening).
  New CPU mirrors RippleMath/RippleOverlayDrops + headless tests.
- Appearance: Settings restructured into five collapsible sections; glass
  chrome finally consumes the bar-opacity slider (nav bar, mini player,
  Now Playing chip/card, search overlay); 18 themes fleshed out to full
  derived schemes; corner style and player position (top/bottom) wired;
  accent intensity, background dim, compact mini-player, follow-system
  light/dark - all applying live. Boot animation: system splash hands off
  to a tap-skippable ripple intro (toggle in Appearance).
- Docs: full-app wireframe (docs/wireframe.html, 14 frames) for UI
  ideation; DEVICE_CHECKS items 16-20 cover the new visuals/appearance.
- Verify note: built in a container without the Android SDK (Google Maven
  unreachable); gate = kotlinc typecheck of the SDK-independent subset
  against android-all + 106 headless JUnit tests green. Run the standard
  ./gradlew gate + DEVICE_CHECKS items 16-20 on-device before release.

## v0.13.0 (code 21–22) - Fluid & particle REBUILD: spawn/catch journey choreography
- Note: r22 (transition-flash fix + system back handling) shipped under the
  same 0.13.0 version name.
- The fluid + particle stack is rebuilt around a progression engine
  (render/fluid/FluidChoreography.kt): up to 8 SPAWN points (dye splats
  fire there, particles are born there) and up to 4 CATCH points
  (attractors that pull particles in, capture them and recycle them back
  to a spawn point) now JOURNEY through the track instead of sitting on
  static patterns. Three nested time scales: song progress slides the
  layout through a journey arc (radius breathes, layout precesses, center
  rises), detected sections re-seat the pattern by golden-angle steps, and
  beats advance a phyllotaxis floret counter. Anchors chase targets with a
  rate-limited follow - progression glides, never teleports (proven
  headless: FluidChoreographyTest).
- Particle layer rebuilt with a full lifecycle (FluidParticles + MRT
  state): state A = pos+vel, state B = age/ttl/emitter/seed in a second
  render target. Every particle is born AT a spawn point (weighted,
  jittered), ages with fade-in/out, and recycles - by catch-point capture
  or ttl expiry - at the CURRENT spawn choreography, so the population
  physically migrates with the journey. Catch attraction is a softened
  inverse-square pull with a hard soft-cap (CPU mirror
  FluidMath.attractorForce, tested) so close passes swing, never explode.
- Emitters rebuilt anchored to the choreography: stirrers orbit the moving
  spawn anchors, beat splats fire from them (all four patterns), catch
  points emit inward suction splats so the dye visibly drains into the
  same wells that capture particles. Fixes rolled in: stale-stirrer
  velocity kick on re-enable, priority-ordered splat budget (beats first),
  dt-scaled sparkle EMA (was frame-rate dependent), sparkle finally wired
  to a param (fluidSparkle).
- Curl Flow rides the same choreography (spawn/catch + curl field
  compose: swirl AND convergence), with a linear-filtered 96-res field
  (was blocky NEAREST 64), cached uniforms, and wall-clock respawn hashing
  (quiet passages no longer freeze recycling).
- Track-position plumbing: AudioFeatures gains progress/sectionIndex/
  sectionCount; live path enriches from the player position + cached
  section analysis (PlayerViewModel.enrichFeatures), export path uses the
  deterministic FeatureTimeline.progressionAt - live and export agree
  exactly (FluidLifecycleMathTest).
- Customize > Fluid gains the "Journey" section (path family chips:
  Orbit/Lissajous/Rose/Bloom/Drift, spawn points, progression amount,
  catch points/pull/radius, particle life), shown for FLUID and CURLFLOW.
  New LFO/ADSR targets: Catch pull, Catch radius. Randomizer rolls the
  journey (progression amount deliberately excluded). Presets: six fluid
  variants retuned onto distinct journeys + new "fluid - Journey"
  showcase + first "curlflow - Streams" built-in. All 8 new SceneParams
  fields persist through PresetStore + param-fade + preset morphing.
- Core sim fixes: pre-first-resize splat queue no longer accumulates into
  a burst; a force-shader compile error is no longer masked by a
  successful dye compile; dye advection samples velocity through a LINEAR
  sampler object (blocky back-trace staircase at high dye res gone).
  Point sprites are resolution-compensated (same preset reads the same at
  1080p and 1440p+).
- Verify note: this round was built in a container without the Android
  SDK; gate = kotlinc typecheck of analysis/render/export/PresetStore
  against android-all + the full headless JUnit fluid suite (37 green).
  Run the standard ./gradlew gate + docs/DEVICE_CHECKS.md item 13 before
  release.

## v0.12.7 (code 20) - LOWP SAMPLER root cause (device symptoms finally explained)
- SECOND ROOT (numerically proven via a CPU reference run of the exact
  shader pipeline): vorticity confinement injects energy faster than
  dissipation removes it - with realistic beat splats the reference run
  reaches max|v|~38 by 5s, dye near-black by ~10s and max|v|~830 with the
  screen fully black by ~30s. fluid_gradient_frag (the frame's final
  velocity write, shared by the FLUID scene AND the FlowField service) now
  applies a terminal-speed soft cap: v *= 12/max(12,|v|) - exact identity
  below 12 sim units/s (6 screen heights/s, beyond any aesthetic need),
  magnitude-pinned with direction preserved above it. This also bounds the
  fluidWarp warp offset at the source. CPU mirror
  FluidMath.terminalSpeedCap + tests lock the property.
- FLUID particles also get the gentle stochastic respawn (0.08/s) so
  tracer coverage never decays over long sessions.
- ROOT CAUSE, all three device symptoms: GLSL ES 3.00 predeclares fragment
  sampler2D as LOWP (range [-2,2), ~8 fraction bits) and EVERY fluid sim
  shader left its samplers undeclared. On GPUs that honor sampler precision
  (Mali family especially) every velocity (+-36), pressure and HDR dye read
  was clamped and quantised: injection rounded toward zero so decay won
  ("a few pixels then black"), the projection chaos flashed, and the
  FlowField's garbage field made the composite warp thrash the whole screen
  ("flashes colours"). composite_frag already documented this exact hazard
  for its own uFlow sampler; the 18 sim/look/particle shaders now all
  declare `precision highp sampler2D;`. Headless validators can't catch
  this - it's runtime hardware behavior - which is why every gate was green
  while devices failed. glReadPixels bypasses samplers, which is why the
  dye-liveness probe reported a healthy sim under a black screen.
- fluidWarp bound: emitter velocities legitimately reach ~36 but the 0.015
  UV scale was tuned for +-6 - the warp could displace fetches by half the
  screen per frame. The field is now soft-limited (v * 6/(6+|v|), CPU
  mirror + FluidFlowLimitTest) so max warp is ~0.09 UV: a fluid bend, never
  full-screen flashing.
- Curl Flow "no new origin points": the one-time seed only ever wrapped and
  slowly filamented. The particle update kernel now stochastically respawns
  particles at fresh hashed positions (uRespawn expected-rate/s; CurlFlow
  runs 0.25/s ~ 4 s full turnover, FLUID keeps 0 = unchanged look) -
  continuous new origins, clustering can never accumulate, works on 16F
  fallback state too.
- Curl Flow strobe: beat response now lives in the field kick only; the
  brightness pulse eased (0.85 + 0.35*beatEnv, was 0.7 + 0.8) so amp +
  brightness + size no longer compound into a flash on busy tracks.
- Verify gate green: ktlint, 63/63 unit tests, assembleDebug, lint.

## v0.12.6 (code 19) - GL state contract + live customization persistence
- RENDERING ROOT FIX: nothing in the pipeline ever reset scissor, stencil,
  color/depth/stencil write masks or the blend EQUATION - and the native
  libprojectM render runs an arbitrary preset pipeline free to leave any of
  them dirty. A leaked scissor rect silently clips every subsequent FBO pass
  (all ~30 fluid grid passes included -> "fluid dead after visiting
  MilkDrop"), a MIN/MAX blend equation corrupts every blended draw, with
  zero GL errors. New GlUtil.resetFrameState() enforces the contract at the
  top of every frame, immediately after the native projectM render, and
  defensively at the start of FluidScene.draw.
- CUSTOMIZATION ROOT FIX: the live viz state was never persisted - every
  app restart reset the selected style and ALL Customize sliders to
  defaults (only explicit presets survived). The live state (scene id,
  every SceneParams field, attack/decay reactivity) now saves to prefs on
  every change via the preset JSON serializer (full roundtrip coverage) and
  restores at startup. New Robolectric regression test proves a fresh
  ViewModel restores scene + sliders + reactivity.
- CACHING FIXES: projectM post-pass looked up every uniform location every
  frame (13 glGetUniformLocation calls/frame); the fade + trail-warp
  programs did the same. All now cached per program link like the composite
  pass already was.
- Verify gate green: ktlint, 61/61 unit tests, assembleDebug, lint.

## v0.12.5 (code 18) - fluid "goes black almost straight away" ROOT CAUSE fix
- ROOT CAUSE (verified against the upstream MIT sim's source): the vorticity
  confinement force omitted the h (= dx) factor of the GPU Gems ch.38 form
  f = eps * h * (N x omega). Our curl pass carries a halfRdx (1/2dx ~ 64-142)
  scale, so the applied force was 64-142x stronger than the 0..50
  curlStrength range was tuned for. Chain: velocity exploded to the +-1000
  clamp within ~2 frames -> divergence = halfRdx * (+-4000 diffs) ~ 284k
  OVERFLOWED the R16F half-float grid (max 65504) to Inf -> Jacobi produced
  Inf pressure -> gradient computed Inf - Inf = NaN into velocity -> dye
  advected through NaN and rendered black; the 0.8x pressure warm start
  recycled the NaN forever. fluid_vorticity_frag now multiplies by uDx,
  restoring the upstream tuning (eps*h*omega collapses to eps*0.5*velDiff,
  the exact dimensionless force the reference uses at CURL=30).
- Hardening so no transient can ever latch black again: advection (velocity
  AND dye run through it every frame) and the pressure clear pass sanitize
  NaN/Inf texels to zero (self-healing); divergence output clamped to the
  half-float-safe +-60000; gradient-subtract output clamped +-1000 like the
  vorticity pass (it was the one unbounded velocity write).
- New headless gate: FluidVorticityMathTest + FluidMath.confinementDeltaV
  CPU mirror assert the confinement is non-explosive at default curl on
  every quality tier, grid-resolution independent, and equal to the
  upstream tuning scale - this test fails on the pre-fix formulation.
- Expected on device: vivid multicolor ink continuously injected by the
  stirrers/beat splats, swirling in smooth vortices over black, glowing
  (bloom + sunrays), never emptying - the upstream reference look.

## v0.12.4 (code 17) - fluid crash/flicker hardening round (full-section debug)
- FIX (crash): every fluid shader compile is now guarded - FluidSim base
  programs, FluidLook's 8 display variants + bloom/sunrays chain,
  FluidParticles (which also compiles MID-DRAW on tier changes),
  CurlFlowScene's field program and FlowField's readback copy. A
  driver-rejected shader now degrades that layer (look chain -> plain dye
  display; particles/style -> unavailable + onShaderError message) instead
  of throwing an uncaught ShaderCompileException on the GL thread, which
  killed the whole app - even for users who never opened Fluid, since all
  scenes init at surface creation.
- FIX (crash, memory): copy-preserving grid reallocation was double-
  allocating (all new grids created while every old grid was still alive).
  Now staged - pressure freed first (its copy was only a Jacobi warm
  start), then velocity and dye each copied and released one at a time -
  roughly halving peak GPU memory on Ultra-tier changes / rotation.
- FIX (silent black): the half-float renderability probe failing (ok=false)
  left Fluid permanently black with no feedback. FluidScene now reports
  "unavailable on this GPU" through onShaderError; every FBO is
  completeness-checked at creation, and a failed allocation disables the
  sim (with message) instead of rendering into broken attachments.
- FIX (flicker): automatic quality downgrades no longer recreate the
  particle layer - create() reseeded every particle to random positions,
  a full-screen pop right when the device was already struggling. Only an
  explicit user tier change reallocates particles. PerformanceMonitor.reset
  also zeroes its sample window now.
- FIX (flicker, low fps): emitters ticked on raw frame dt while the sim
  clamped to 1/60 s - below 60 fps capsule spacing outran the fluid and
  splats degenerated into disconnected flickering stamps. One shared dt
  (clamped to 1/30 s, raised from 1/60 so 30-60 fps devices keep real-time
  fluid speed) now feeds emitters, sim and particles in both FluidScene
  and FlowField.
- FIX (latent crash): Spectrum-arc beat pattern threw
  IllegalArgumentException (coerceIn(0,-1)) on an empty bands array;
  guarded. queueSplat also drops splats while the sim is unavailable
  instead of growing the pending list unboundedly.
- Verify gate green: ktlintCheck, 57/57 unit tests, assembleDebug, lint.

## v0.12.3 (code 16) - fluid bug-analysis round (device report: flashing / not working)
- ANALYSIS TOOLING: glslangValidator now in the build environment; every
  fluid shader PROGRAM link-validated headlessly - all 25, including all
  8 runtime-assembled display keyword variants and the particle pairs:
  CLEAN. Shaders ruled out as the black-screen cause.
- FIX (certain, "style not working at all"): the Curl Flow style was
  listed in the picker but NEVER constructed in the renderer - selecting
  it silently did nothing. Now built live and in the export factory. If
  you tapped the second entry under the Fluid tab, this was your bug.
- FIX (certain, "flashing"): FluidSim's copy-preserving grid reallocation
  rebinds framebuffer + viewport but runs OUTSIDE the scene's draw
  snapshot when triggered by resize - after every rotation or quality
  change the engine's next pass rendered into a fluid grid. allocGrids
  now saves/restores both.
- Hardened: display #define injection now lands after the #version line
  wherever it is (GLSL ES ordering rule) instead of assuming line 1.
- STILL NEEDED FROM DEVICE if fluid remains black: `adb logcat -s
  FluidSim` - the scene logs the format probe, "first frames stepped
  clean"/glError codes, and "dye liveness: <max>" at t=1.5s. Those three
  lines conclusively split sim-dead vs display-dead vs never-selected.

## v0.12.2 (code 15) - organic motion round 1: echo-trails + curl-noise scene
- From an organic-motion research report, since retired: by the time anyone
  went back to it, it was telling readers that no native GLES/Kotlin fluid
  port existed, which `render/fluid/` had disproved several rounds earlier.
  The two sources it drew on are cited where the work landed instead - Bridson
  2007 in `res/raw/curl_field_frag.glsl`, the MilkDrop feedback warp in
  `res/raw/trail_warp_frag.glsl`. Joint round with a parallel session - their
  drop audited and completed here.
- NEW scene "curlflow": particles ride the curl of a time-morphing FBM
  potential (divergence-free per Bridson 2007 - streams swirl, never
  clump). Tiny 64-res field texture feeds the existing GPU particle layer,
  so it costs a fraction of the full fluid sim. Mids drive field morph
  speed, treble adds fine turbulence, beats kick amplitude/brightness with
  impulse + exponential release; cosine-palette coloring; Speed/Turbulence/
  Audio drive/Particle size/Palette all bite.
- Echo-trails everywhere: the trail pass can now zoom and sine-warp the
  previous frame (trailZoom/trailWarp in Customize) - the MilkDrop-style
  liquid echo/tunnel feel on every scene, live AND in exports.
- BUG caught by the reflection roundtrip gate: the new trailZoom/trailWarp
  params were missing from preset JSON (saves would silently drop them) -
  serializer fixed, test green. Divergence-free property of the new field
  proven headless via a CPU mirror of the exact shader math.
- ON-DEVICE: curlflow shows silky non-clumping streams that whirl faster
  in busy mids; trailZoom small positive = endless-tunnel echo on any
  scene; save/load a preset with trail warp set and confirm it survives.

## v0.12.2 (code 15) - library roots + analysis cache (old-P3 complete)
- Media folder ROOTS: Library > Folders now keeps your chosen folders as
  persistent library roots (SAF, permission persisted) with Add folder,
  per-root remove, and a Rescan that re-walks every root, tag-reads new
  files, and merges into the library (dedupe by uri). The VLC-style
  "point it at your music folders once" flow.
- Analysis CACHE: every full track analysis is now saved to a compact
  binary store (all 10 feature fields per frame; bands/waveform
  int16-quantized, versioned header, LRU-capped) and reused - repeat
  visualizer prep and ESPECIALLY exports skip the whole offline analysis
  phase on cache hits. Settings shows cache entries/size with Clear.
  Corrupt or version-mismatched files are dropped and re-analyzed.
- Round audited from a parallel-session drop: all three analyze paths
  verified cached, cache roundtrip covered by unit test, full gate green.
- ON-DEVICE: add a folder root, drop a new file in it later, Rescan picks
  it up; export the same track twice - the second export must skip the
  analysis phase (progress jumps straight to rendering); Settings cache
  row counts up and Clear empties it.

## v0.12.1 (code 14) - baseline audit + six fluid launch variants
- Adopted the externally built v0.12.0 (F5 Fluid tab, F6 adaptive quality,
  F7 FlowField) as the local baseline and AUDITED it here: full gate run
  (ktlint after a param-comment style fix, every unit test including the
  reflection roundtrip now covering all 35 fluid/flow params, assemble,
  lint); targeted sweep of this project's known bug classes - cross-stage
  uniform precision (clean in all fluid shaders), lowp samplers on
  position/flow textures (composite uFlow already highp with a strength
  gate + zero-texture default so non-fluid scenes are untouched),
  FlowField GL release completeness (all 5 objects freed), and the
  spec rule that the fluid scene's own field is reused rather than
  running two sims. versionCode collision with the old code-13 resolved
  (now 14 / 0.12.1).
- NEW - six fluid launch variants in the preset browser: Inkdrop (calm
  center drops, long aging ink), Vortex (ring-pattern spin-up with
  low-drag particle streaks), Spectrum (spectrum-arc splats + bass pump +
  sunrays), Nebula (dye hidden - pure particle field riding the invisible
  flow), Lava (slow heavy blobs, max chromatic aging, hot bloom), Storm
  (everything on, never clears). Each is a strongly different starting
  point for customizing.
- ON-DEVICE: flip through the six variants (each should feel immediately
  distinct); Nebula must show particles with no ink; verify the Fluid tab
  sliders bite on top of a variant and Save produces a working preset.

## v0.12.0 - fluid phases F4-F7 complete (look chain, Fluid tab, adaptive quality, FlowField)
- F4 look chain: the FLUID scene now renders through the full
  Pavel-style look stack - soft-knee HDR bloom through a mip up/down chain
  with additive (ONE,ONE) upsample, 16-step screen-space sunrays marched to
  the scene-FBO centre, pseudo-normal embossed shading, and noise dither -
  composited by #define keyword display variants (SHADING/BLOOM/SUNRAYS,
  program cache keyed by flag set; no uniform branching).
- F5 Customize > Fluid tab: 35 new SceneParams fields (quality/solver,
  character, emitters, particles, look, audio routing, FlowField), all with
  locks + Randomize ranges, preset JSON roundtrip (reflection test covers
  them automatically), settings-fade lerp, and six new LFO/ADSR routing
  targets: Fluid curl, splat radius, splat force, glow, fade, Flow strength.
- F6 adaptive quality: five tiers (Ultra..Min) set sim grid, dye grid,
  particle count and Jacobi iterations together; a Welford rolling-FPS
  monitor with 2.5 s hysteresis downgrades on sustained deficit only
  (single stalls discarded), latches (never auto-upgrades mid-session),
  and reallocates at frame boundaries with copy-preserving grids.
- F7 FlowField: a shared velocity-only fluid sim (64 grid, 12 iterations)
  now drives EVERY style - the new universal fluidWarp composite slot bends
  particles, shaders and MilkDrop output (and exports, via FxCompositor
  parity + an export-context FlowField), particle scenes can ride the field
  through a 16x16 CPU readback, and shader scenes (plus the GLSL editor)
  get the field as uFlow/uFlowStrength. When the FLUID scene is active its
  own field is reused - the service never runs twice.
- F7 extension points: the fluid force and dye injection passes are now
  user-editable GLSL (Fluid tab > Injection shaders) with the built-in
  capsule splat as the starting template, audio uniforms (uDt, uDx, uTime,
  uBass, uMid, uTreble, uEnergy, uBeat), last-good-program fallback on
  compile errors, and export parity.
- Fluid fixes: dye no longer advects at 1/4 speed (back-trace now scales by
  the velocity grid's texel size, not the source's); surface resizes
  preserve the simulation through a copy-resize instead of wiping it (and
  same-size resume no longer resets anything); particle drag is
  frame-rate-independent (per-1/60s factor); the particle pass restores the
  blend function it changes.

## v0.11.1 - launch crash fixed, app-wide automated test pass
- FIXED the immediate crash on launch. Root cause: the ViewModel's main loop
  starts in an init block on Dispatchers.Main.immediate, which executes
  synchronously during construction until its first delay - and the v0.11
  preset-lock guard inside applyIntelligence() read a field declared ~400
  lines BELOW that init block, so on a real device it was still null at
  launch (NullPointerException). The construction-order-critical fields now
  live above the init block, with a comment explaining why they must stay
  there. (Robolectric's deferred looper hid this, which is why it only
  crashed on-device.)
- New on-JVM app test suite (Robolectric + Compose UI tests) that actually
  launches MainActivity and walks the app: every bottom-nav destination,
  every Visuals tab including MilkDrop, the Customize hub (sliders +
  Randomize), Library's permission screen, Settings. Plus behavior tests:
  randomizer respects locks, lock toggling, Auto tri-state wiring
  (off/random/intelligent), preset-lock blocking random switching, preset
  save/delete + folder placement + name sanitizing, play-history ordering.
  29 tests, all green.
- Crash capture: any future uncaught exception writes its full stack trace
  to a file; on next launch the app shows a "Previous crash captured"
  dialog with a Copy button so the exact trace can be reported.
- Restored the Now Playing gestures and collapse button (lost in a
  concurrent edit): swipe down = collapse, swipe up = queue, swipe L/R =
  prev/next preset, double-tap edges = seek, plus seekBy/quick-preset APIs.

## v0.11.0 - navigation v2: full app-shell restructure
The app moved to two layers, like a proper music player - a persistent shell
with a docked mini-player, and a fullscreen visualizer expanded out of it. The
navigation doc this was planned from is gone; `ui/AppShell.kt` is the
structure now:
- App shell: bottom nav (Home / Library / Visuals / Settings) with a
  persistent mini-player docked above it; tap to expand into the fullscreen
  visualizer (Now Playing), collapse arrow or swipe down to return.
- Home: resume card, Recently played / Most played rows (new play-history
  store), shuffle-all.
- Library: the whole device via MediaStore - Tracks, Albums, Artists,
  Playlists, and device Folders tabs, with runtime permission, drill-in
  play/shuffle, and per-track menu (play next / add to queue / add to list).
  Google Drive appears in Folders as coming-soon.
- Visuals hub: Presets (folder tree with add-folder, save-into-folder,
  remove), Styles (Particles / Shaders / MilkDrop - Load .milk and Textures
  live in the MilkDrop tab), Customize (same tabs as the sheet, with
  per-param locks and Randomize), Textures.
- Now Playing gestures: swipe down = collapse, swipe up = queue, swipe
  left/right = prev/next preset, double-tap screen edges = seek +/-10 s.
  New chips: Lock (keep current preset) and Auto (off / random /
  intelligent tri-state).
- Search overlay from Home/Library: tracks + visual presets.
- Settings destination: theme, bar opacity, player position, beat-threshold
  slider, export dialog.
Built on v0.10.0's flicker fix, MilkDrop tab groundwork, preset remove,
persisted param locks + randomizer, and beat-triggered ADSR envelopes
(live + export).

## v0.10.0 - feature round 1 from the to-do list
- Flicker fixed: beat detection now needs a stronger onset (2.5 sigma, longer
  refractory) and treble gets dedicated smoothing so hi-hats stop strobing the
  visuals; new Settings > Analysis "Beat threshold" slider to tune it.
- MilkDrop overhaul: Styles is now tabbed (Particles / Shaders / MilkDrop);
  Load .milk and Textures moved into the MilkDrop tab; the low-quality bundled
  .milk presets were removed (old copies are cleaned up on first run) - the
  tab lists your own imported/saved .milk files.
- Preset remove button for user presets in the browser.
- Randomizer: one tap randomizes every unlocked Customize parameter; each
  slider has a "lock" toggle to pin values you want kept.
- ADSR envelope: beat-triggered attack/decay/sustain/release assignable to any
  LFO target (Customize > FX > Envelope), persisted, and reproduced in exports.
- Planning docs added under docs/. All three have since been retired: the
  navigation plan and the feature to-do were overtaken by the code (nearly
  everything the to-do listed shipped, and a stale to-do reads as work still
  outstanding), and the wireframe survives in the form anyone actually opens,
  docs/wireframe.html.

## v0.3.4
- Freeze after loading presets, mitigations: removed
  preserveEGLContextOnPause (foreign edit; preserved-context resume after
  the file picker is a known device-specific GL hang source - our
  context-loss discipline with automatic preset restore covers resume);
  projectm_set_texture_search_paths now only fires when the preset's
  directory changes (each call purges projectM's texture cache and repeated
  purges caused stalls); full GL state restore after projectM renders.
- Customize panel restructured: Style is its own tab with a vertical
  scrolling scene list (suggested scene marked with a star); Behavior tab now
  contains Attack/Decay reactivity sliders and the manual/suggest/auto
  intelligence mode buttons; main overlay slimmed to a quick bar
  (Customize, milkdrop preset buttons, saved presets) + transport.

## v0.9.6 - P0 verification + P1 milkdrop tab & preset architecture
- P0: reflection roundtrip test guards all SceneParams fields through preset
  JSON forever (any dropped field fails CI); param x scene-family matrix
  documented (docs/PARAM_MATRIX.md) and its dead combos fixed - Pulse now
  works on particle scenes (beat size swell) and Endless Zoom works on all
  five particle scenes (Swarm/Fountain outward flow, Orbit radius drift).
  Flicker fix confirmed in: beat threshold 2.5 sigma (user slider in
  Settings > Analysis), longer refractory, extra treble smoothing.
  Built-in milkdrop presets removed; "Preset >" cycles your own .milk files.
  On-device checklist: docs/DEVICE_CHECKS.md.
- P1: Style browser has Particles | Shaders | MilkDrop tabs with Load .milk
  and Textures moved into the MilkDrop tab; user presets are deletable;
  preset folders (add/rename/move) form a browsable tree; NEW Settings >
  Paths lets you choose a preset folder - every save mirrors the JSON and
  paired .milk there so your own sorting shows in any file manager.

## v0.9.6 - P0 verification, P1 preset architecture, P2 modulation
Working through todo.md (P0-P2), merged with a parallel session's drops:

P0 Verify & stabilize:
- New reflection roundtrip test: every SceneParams field is perturbed,
  serialized, parsed, and compared - presets can never silently drop a
  parameter again (serializers moved to an internal companion for
  testability; 30 tests green).
- docs/PARAM_MATRIX.md: all 49 params traced across shader / particle /
  milkdrop with their mechanism; dead combos fixed - Pulse now works on
  particles (beat size swell) and Endless Zoom works on all five particle
  scenes (outward flow; Orbit drifts radii with respawn). Remaining
  exceptions documented with reasons. docs/DEVICE_CHECKS.md lists the
  on-device checks that can't be verified headless.
- Flicker fix confirmed in: beat threshold 2.5 sigma (user slider in
  Settings > Analysis), ~333 ms refractory, extra treble smoothing.
- Built-in milkdrop presets removed; "Preset >" cycles your own files.

P1 Milkdrop tab & preset architecture:
- Style sheet now tabs Particles | Shaders | MilkDrop, with Load .milk and
  Textures moved into the MilkDrop tab; delete button on user presets;
  preset folder tree (add / rename / move).
- NEW Settings > Paths: choose a preset folder (system picker) - every
  save mirrors the JSON and paired .milk there so your own sorting is
  visible in any file manager.

P2 Modulation:
- 2x ADSR envelopes exactly per spec: BEAT triggers attack (optional
  retrigger), band ENERGY holds sustain (with hysteresis; sustain can
  track energy), release when energy drops. Multi-target: params or LFO
  rate/depth. Runs in the live renderer AND exports. Editors in
  Customize > Mod; persisted.
- Per-parameter locks + "Randomize (skips locked)" in Customize.

## Fluid round 3 (code 12) — F3 GPU particle layer
- Up-to-65k (256^2 default) inertial particles ride the fluid: state lives
  in an RGBA16F ping-pong texture (xy pos in sim space, zw velocity), one
  fullscreen kernel advances every particle, and a static texel-coord VBO
  drives a GL_POINTS render whose vertex stage fetches state (no CPU per
  frame). Drag inertia v += (flow - v) * drag (0.5) turns tracer dots into
  streaming light trails; edge wrap keeps the field populated; GPU-hash
  seeding, reseeded on aspect change.
- Speed-colored soft sprites drawn additively over the dye; existing knobs
  drive them now: Particle size scales the points, Density their
  brightness, palette base + hue range their speed-gradient colors.
- Cross-stage precision discipline from the round-2 crash applied to the
  new render program (explicit highp varyings/uniforms).
- Tests: drag-inertia contract (geometric convergence; drag=1 is the pure
  tracer) alongside the existing fluid math suite. Also integrated a
  parallel session's GLSL-tab groundwork (uFlow-ready helper text).
- ON-DEVICE: trails streak along the flow (not dots); rotation reseeds
  cleanly; style-cycling still leaks nothing; fps holds at 128/512+65k.

## Fluid round 3 (code 12) — F3 GPU particle layer
- Inertial GPU particles riding the fluid: state lives in a ping-ponged
  texture (xy = position in sim space, zw = velocity), one fullscreen
  kernel advances every particle per frame (v += (flow - v) * drag,
  p += v * dt, wrap at the domain edge), and a static VBO of texel
  coordinates drives a GL_POINTS render whose vertex stage fetches state -
  no CPU involvement, no per-frame uploads. Drag inertia (default in the
  streaks-not-dots range) is what turns tracer dots into streaming light
  trails; particles draw additively over the dye.
- Correctness work this round: highp qualifiers on the state/velocity
  samplers (lowp default sampling can quantise positions on some GPUs -
  same driver-strictness family as the round-2 link crash); grid scale dx
  now derived from the allocated grid (2/gridHeight) so alpha = -dx^2 and
  all finite differences are correct at ANY sim resolution - the spec 6.4
  invariant that resolution changes quality, not look (the fixed 2/32
  from round 1 was a constant-factor error, absorbed visually then, wrong
  once resolution varies). RGBA32F probed for particle state with 16F
  fallback so positions don't visibly cluster.
- Tests: state-texture side math (smallest covering square).
- ON-DEVICE: particles visible as moving streaks over the ink; drag feel
  (streaks curve through vortices rather than sitting on them); no
  clustering/stepping in particle positions (if stepping appears the 32F
  probe result in the format log line is the first thing to check);
  style-cycling still leak-free with the extra state buffers.

## Fluid round 2 (code 11) — device crash fix + F2 audio emitters
- FIX (device crash report): GL link failure "uInvRes precision does not
  match" on strict drivers - the shared vertex shader declares uInvRes as
  highp while the divergence/pressure/gradient fragments redeclared it
  under mediump default precision. Cross-stage uniforms now use explicit
  highp; swept all fluid shaders - uInvRes was the only cross-stage
  declaration, so this was the whole crash class.
- F2 emitters: the four-emitter audio system this round was specified from:
  BeatSplat (center / ring-vortex / random / spectrumArc patterns),
  BandStirrer x2 orbiting on band energy, TrebleSparkle on treble
  transients, BassPump
  (gated, off by default) - all as capsule splats with velocity blending;
  self-contained beat/bass envelopes shaped like the app ADSR.
- Continuous modulation: mids raise curl, quiet passages fade the canvas
  (drops leave lasting ink), beat envelope pulses splat radius; chromatic
  aging spreads per-channel dye decay so fading ink drifts in hue.
  Existing Color customize controls (palette, hue range, cycle) drive the
  dye; Speed drives stirrers, Turbulence scales curl.
- Tests: FluidEmittersTest - beats produce exactly beatSplats requests,
  stirrer paths are frame-continuous, spectrumArc orders left->right by
  band, beat envelope attacks/releases.
- ON-DEVICE: re-run the round-1 checklist on this build (the code-10 APK
  crashed at init on strict drivers before the probe line could help),
  then: beat splats land on beats; quiet/loud fade behavior audible in
  the ink; ring pattern spins up a visible vortex.

## Fluid round 1 (code 10) — FLUID scene: core Stam sim (phases F0+F1)
- New visual style FLUID: GPU stable-fluids sim (a GLES3 port of Stam's
  stable fluids): half-float renderability probe with fallback cascade, sim-space
  coordinates (aspect handled once), boundary sampling in every pass
  (Neumann pressure / free-slip velocity), grid-scale-correct Jacobi
  (alpha = -dx^2) so resolution changes quality not look, manual-bilerp
  advection, vorticity confinement, capsule (swept-segment) splats with
  velocity blending instead of impulses.
- Registered in Styles under a new Fluid tab; P1 debug emitters = two
  band-driven orbiting stirrers; display = dye with soft HDR rolloff
  (full bloom/sunrays/shading chain comes in the Look phase).
- Licensing per spec: MIT headers on ported shaders + THIRD_PARTY_NOTICES;
  GPL reference used as clean-room principles only.
- Tests: FluidMathTest (aspect resolution mapping, CPU Jacobi projection
  reduces divergence >90%, capsule segDist degenerate guard). todo.md now
  carries the remaining fluid phases F2-F7.
- ON-DEVICE checklist for this round: format-probe logcat line; 5-min soak;
  rotation keeps ink; pause freezes sim; style-cycling leaks nothing.

## v0.9.9 (code 9) - P3/P4 round: AIFF everywhere, key badges, playlists, morphing
- AIFF support completed across ALL decode paths: playback already used the
  Media3 AiffExtractor (parallel work, audited); analysis and export now
  read AIFF too via a new streaming PCM reader (COMM/SSND, 80-bit extended
  sample rate, 8/16/24/32-bit, >2ch folds to stereo) since the platform
  extractor can't - so AIFF files analyze, visualize, and export.
- Musical key detection (chromagram + Krumhansl-Schmuckler, parallel work
  audited): key + BPM show as "artist · Cm · 124 BPM" badges on analyzed
  library rows.
- Playlists: rename (dialog) and track reorder (expand a playlist, move
  rows up/down).
- Preset morphing: applying a preset now interpolates parameters over N
  beats of the detected BPM (Settings slider, 0-16 beats, default 4;
  0 snaps like before).

## v0.9.8 - real track titles/artists + bottom inset fix
- Track rows and the player showed bare document numbers for many files.
  Titles now resolve like a real media player: embedded tags first
  (MediaMetadataRetriever TITLE/ARTIST), then the provider display name,
  then the path - and every queue item is built with MediaMetadata so the
  playing title/artist is correct everywhere (Now Playing, mini-player,
  library rows, search). Artist shows as a secondary line; existing
  library entries with number-titles are repaired once on startup.
- The Now Playing control card no longer sinks under the system
  navigation bar (navigationBarsPadding + lifted bottom margin); the top
  row respects the status bar.
- Removed the orphaned MusicLibraryDialog left from the old navigation.

## v0.9.7 - one navigation everywhere: new shell is the only UI
- The new navigation (Home / Library / Visuals / Settings shell with the
  mini-player) is now the ONLY navigation. Previously it showed on fresh
  boot but expanding the player brought back the old TopPlayBar + QuickBar
  chrome - that entire legacy layer is deleted (old VisualizerScreen
  chrome, QuickBar, TopPlayBar, TransportRow, Queue/Texture/PresetSave
  dialogs, BrowserDialog; 692 -> 203 lines).
- Now Playing is a clean fullscreen canvas in the shell's design language:
  collapse chip + title on top, one Material3 card with seek, transport
  (shuffle/prev/play/next/repeat), a Visuals shortcut that jumps to the
  hub, and the Auto mode toggle (off/random/smart). Tap the canvas to
  hide/show controls; Back collapses.
- Root-cause fix for "reverts to old behavior": all renderer bindings
  (scene, params, LFO/ADSR configs, transitions, preset/shader apply,
  features/PCM) moved from the expanded screen to the app shell
  (VisualizerEngineBindings), so visual changes made anywhere apply
  regardless of which screen is open.

## v0.9.6 - todo.md P0-P2: verified pipeline, preset folder, ADSR + randomizer
- P0 verify: new reflection roundtrip test guards every SceneParams field
  through preset JSON (fails loudly if a field is ever dropped again);
  docs/PARAM_MATRIX.md traces all params x scene families; fixes from that
  audit: Pulse now works on particle scenes (beat size swell) and Endless
  zoom works on all five particle scenes; docs/DEVICE_CHECKS.md lists the
  on-device GL checks. Beat threshold slider + treble smoothing confirmed
  in (flicker fix), built-in milkdrop presets fully removed.
- P1: Style sheet has Particles | Shaders | MilkDrop tabs with Load .milk
  and Textures moved into the MilkDrop tab; preset delete button; preset
  folder tree (add/rename/move); NEW: Settings > Paths > "Choose preset
  folder" - saves mirror the preset JSON (and .milk) into your chosen
  folder so your own sorting shows in any file manager.
- P2: 2x ADSR envelopes - beat triggers attack, band energy holds sustain
  (hysteresis + optional energy-tracking sustain level), release when the
  energy drops; every time/band/threshold adjustable; multiple targets per
  envelope (params or LFO rate/depth); runs live AND in exports.
  Per-parameter locks + "Randomize (skips locked)" in Customize.

## v0.9.5 - full debug-audit fix round (16 items)
Applied the complete 2026-07-23 debug audit, every P0/P1/P2 item:

Rendering (P0):
- Supersampled FBOs were destroyed on the first frame (onDrawFrame re-ensured
  them at screen size), defeating the zoom AA fix and cropping/off-centering
  visuals on devices where the supersample factor > 1. All ensure/viewport
  sites now use the render resolution consistently.
- Geometry/color FX (warp, ripple, kaleido, tile, twist, pixelate, bloom,
  posterize) were applied TWICE on all 20 shader scenes - in-shader and again
  in the composite pass (Tile 3 produced a 9x9 grid, kaleido folded twice).
  The composite now passes neutral values for shader scenes and real values
  for particle + milkdrop scenes, matching the mirror/invert guard pattern.
- Invert was double-applied on particle scenes (per-particle AND composite),
  cancelling itself out. The composite pass is now the single owner.
- Band gains were squared on shader scenes (applied in gainAdjusted() and
  again inside ShaderScene.update). One owner now: gainAdjusted().

Export parity (P1) - renders now match the live view:
- Milkdrop exports load the active .milk preset instead of projectM's idle
  default, and stay audio-reactive via the timeline waveform.
- Custom GLSL edits are used in exports (previously silently reverted).
- LFO automations run during export: a dedicated LfoEngine ticks per frame
  and the compositor takes per-frame params instead of a construction-time
  snapshot.
- Band gains apply in exports for all scene types.
- Particle trails render in exports: the export FBO fades (like live FBO A)
  instead of clearing when trails are on.

Smaller fixes (P2):
- GLSL editor reopens with your edited source, not the stock shader.
- Morph-adjacent params that were dead on particle/milkdrop scenes now work
  via the composite pass: drift X/Y (wrapping scroll), sway, shake, flash,
  temperature, solarize.
- Custom-shader apply no longer races scene switches (VizApply carries its
  sceneId; the pending-shader slot is now a queue so rapid edits all land).
- Multichannel (5.1/7.1) audio is downmixed to the AAC encoder layout, fixing
  garbled/wrong-speed export audio; the EOS flush no longer stalls 10 ms per
  iteration at the export tail.
- Endless-zoom phase wrap no longer pops (triangle-wave exponent in the
  milkdrop post shader); LFO random (S&H) samples once per cycle as labeled.
- Uniform locations are cached per program link (composite ~30, shader ~45,
  particle ~9 lookups/frame eliminated).
- Transcode phase honors Cancel; cancel no longer reports as an error.
- Export "succeeds with empty file" guarded (muxer must have started).
- 4K x ultrawide exports clamp to the 4096 px AVC hardware limit, scaled to
  preserve aspect.
- Random mode can't pick MilkDrop when the native library is unavailable.
- Track import metadata queries moved off the main thread.
- Preset names containing the built-in separator are sanitized so user
  presets stay deletable; export success text distinguishes chosen-folder
  destinations; launcher now has a proper adaptive icon; versionCode/Name
  track the internal version (0.9.5 / 5).

## v0.9.4 - exports now include the FX/geometry chain
- Fixed a significant export bug: the exporter drew each scene straight to the
  encoder surface and skipped the composite pass entirely, so exported videos
  were missing the whole screen-space FX and geometry chain (chromatic
  aberration, vignette, scanlines, grain, glitch, fisheye, strobe, bloom,
  posterize, warp, ripple, kaleidoscope, tile, twist, mirror, invert). Worst
  hit were particle scenes, whose shape/color customizations are composite-only
  (see v0.9.3). Exports now render each scene into an offscreen FBO and
  composite it through the same FX shader the live view uses, so what you
  export matches what you see.

## v0.9.3 - universal composite FX so particle scenes honor all params
- Empirical per-parameter audit found 21 Customize parameters had no effect on
  particle scenes (their vertex pipeline only forwards 9 uniforms). Geometric
  (warp, ripple, kaleidoscope, symmetry, pixelate, tile, twist, mirror) and
  color (bloom, posterize, invert) effects are now applied universally in the
  composite shader, so every scene type honors them. Shader scenes keep doing
  mirror/invert in-shader (guarded against double-application).
- GUI: the full transport row (shuffle / previous / play-pause / next / repeat)
  is grouped together in the top bar with elapsed/total time; the bottom row is
  secondary actions only.

## v0.9.2 - preset rework, milk save, export destination, zoom AA, UI border
- Reworked built-in presets: 12 strongly-differentiated looks (Chill, Punchy,
  Hypno, Vivid, Retro, Glitch, Dream, Warp, Prism, Noir, Strobe, Deep) x every
  scene, each exercising a distinct mix of motion / shape / color / screen-FX
  params so presets are visibly different (the old four were too alike). All
  57 params round-trip through save/load (v0.9.1 loader fix).
- Milkdrop presets now save as real .milk files: saving a preset while on the
  milkdrop scene copies the active .milk into your preset folder (tracked at
  every load site) alongside the customization bundle.
- Export destination: new "Render to chosen folder..." button opens the system
  file picker (SAF) so you choose exactly where the .mp4 is written, instead of
  it always going to Movies/Geode.
- Zoom pixelation fixed: scenes now render into 1.4x supersampled framebuffers
  (eased down on large screens) and downsample at composite time, so the Zoom
  customization has real detail to magnify instead of blowing up screen texels.
- Milkdrop "Use texture" preset reworked to a minimal, transpiler-safe comp
  shader (single sampler, no per-pixel branching) since projectM's HLSL->GLSL
  translation is fragile with complex comp shaders.
- UI: a themed outer border now frames the whole app, tinted from each theme's
  accent color so it fits all themes.

## v0.9.1 - deep audit: every param wired, presets load everything
- Full wiring audit of every Customize parameter through UI -> ViewModel ->
  renderer -> shader. Three real defects found and fixed:
  - Morph did nothing on 17 of 20 shader scenes (uniform uploaded but never
    used). It now blends the coordinate plane toward a polar remap in the
    shared prelude - a smooth geometric metamorphosis on every scene.
  - Bass/Mid/Treble gain sliders did nothing: the gains are now applied to
    the band levels in the shader pipeline.
  - Loading a preset silently dropped every parameter added since v0.6
    (warp, kaleidoscope, dual palette, bloom, all FX, drift, tile, twist,
    band gains, and more): the loader only reconstructed the original v0.6
    field list. It now covers all 57 parameters, generated from the actual
    SceneParams defaults so it cannot drift again.
- Audited and integrated the parallel FX/LFO drop: screen-space FX chain
  (chromatic aberration, vignette, scanlines, grain, glitch, fisheye,
  strobe) applied at composite time so it covers shader, particle AND
  milkdrop scenes; 3 chainable LFOs modulating any parameter; parameter
  fade; player position (top/bottom) setting; export fps choice. Verified
  each layer end-to-end; all transitions (cut/fade/melt/slide/zoom) intact.
- GUI audit: every dialog state has exactly one render site; top bar and
  overlay respect system insets; all tabs reachable.

## v0.9 - new scenes, built-in presets, texture "Use", true export length, beat sync
- 7 new visual styles: starfield (warp-speed stars), waves (layered
  oscilloscope ribbons), hexgrid (pulsing hex cells), spiral (log-spiral
  tunnel), aurora (flowing curtains), solar (band-driven flares) shader
  scenes plus orbits (band-driven orbital rings) particle scene - 25 scenes
  + milkdrop total. All support the full Customize panel, transitions,
  presets and export.
- Built-in presets for every visual style: four curated looks (Chill /
  Punchy / Hypno / Vivid) x every particle and shader scene (~100 presets),
  listed beside your saved presets in the Styles browser and usable in the
  visual playlist and Random mode. Built-ins can't be deleted.
- Random mode: switch styles/presets on an interval or on strong musical
  moments, with source filters and per-switch color shuffle.
- Milkdrop textures are now actually usable: each imported texture has a
  "Use" button that generates an audio-reactive display preset referencing
  it (MilkDrop presets only show textures they name - previously imports
  loaded but nothing referenced them). Imported names are made shader-safe,
  and .gif (never scanned by projectM) is no longer accepted.
- Musically intelligent visuals: a BPM-locked beat-phase clock drives the
  Beat pulse so it lands on the actual beat instead of free-running;
  detected-beat resync keeps it locked through tempo drift.
- Exports are now exactly as long as the music: video frame count derives
  from the measured duration of the actual transcoded audio (not the
  analysis timeline), and the analyzer's timing drift was fixed (~4%
  shortfall previously cut exports short).
- Bug-fix batch: preset saves persist edited custom shaders; AUTO mode
  crash fixed (ExoPlayer touched off-main); long-export OOM fixed (AAC
  streams to a temp file); cancelled exports no longer publish truncated
  files; BPM detection de-biased from doubled tempos; projectM PCM cursor
  no longer skips samples; scene switches clear stale trails; float-PCM
  decoder output handled; playlist analysis progresses past failed tracks;
  persistable-permission crash guarded.

## v0.8 - fixes: textures, background reset, title, lighter icons
- Top bar: play/prev/next icons are now white (lighter) for contrast over the
  visuals, and the title field shows the current song, falling back to the
  file name when the track has no embedded title metadata.
- Removed the duplicate play/pause/prev/next from the bottom control row - the
  primary transport now lives only in the top bar. The bottom row keeps
  shuffle and repeat (which aren't in the top bar) plus the secondary actions.
- Bug fix - imported milkdrop textures now actually load: adding textures
  re-parses the current preset (bypassing the load debounce) so projectM
  re-binds the texture search path. The texture picker also accepts .tga/.dds
  (classic Milkdrop formats), not just standard images.
- Bug fix - visuals no longer reset when the app is backgrounded: when Android
  destroys the EGL context, the renderer now restores the current scene
  params, any edited custom shader, and the last milkdrop preset on surface
  recreation instead of falling back to defaults.

## v0.7 - milkdrop textures, bigger customize panel, single seek bar
- Milkdrop "Add texture": a Textures button (in the milkdrop control row)
  opens a manager to import image files into the shared texture folder that
  projectM already searches. Many MilkDrop presets reference external image
  textures by filename (the classic Milkdrop texture pack, plus per-preset
  images); importing them here lets those presets render correctly instead of
  falling back to noise/black. The current preset reloads automatically after
  import so textures apply immediately.
- Customize panel greatly expanded, now four tabs (Motion / Shape / Behavior
  / Color) plus GLSL:
  - New Shape tab: domain warp, ripple, morph, kaleidoscope with selectable
    fold count (2-12), pixelate, posterize, and particle-scene shape
    (Dot / Ring / Star / Square / Spark) + particle size.
  - Motion gained sway and beat-pulse.
  - Color gained a second palette with a blend slider, five extra palettes
    (Candy, Forest, Aurora, Sunset, Ice - ten total), bloom, and duotone.
  All shader-driven params flow through the shared prelude, so every shader
  scene inherits them; particle shapes/size drive the particle scenes.
- Removed the duplicate seek bar: the progress slider now lives only in the
  top play bar (it had been shown twice).

## v0.6 - top play bar, settings menu, themes
- Top play bar: a translucent card pinned to the top of the screen with the
  track title, standard transport icons (previous / play-pause / next) and a
  seek slider - always available while controls are visible, like a normal
  music player. The gear button on it opens Settings.
- Settings menu (gear) now leads with a Theme picker and keeps the full
  export section (quality, aspect ratio, share/Drive) below it.
- Six selectable app themes (Midnight, Neon, Sunset, Forest, Mono, Light),
  persisted across launches; the choice recolors all control surfaces,
  dialogs, sliders and accents via the Material 3 color scheme.

## v0.5 - music library, playlists, settings & export upgrades
- Music library browser (Library button): import individual audio files or a
  whole folder (SAF tree), with persisted read permission. Imported tracks
  list their cached BPM + duration once analyzed and persist across launches
  (library.json), so you can play them again later.
- Playlist maker: create named playlists, add tracks from the library,
  reorder with up/down controls, remove, delete, and play from any position.
  "Analyze" batch-analyzes every track in a playlist in the background and
  caches the results into the library.
- Transport is now icon-based (shuffle / previous / play-pause / next /
  repeat) like a standard player, with active-state tinting.
- Settings menu (gear icon) now hosts Export. Export gained a quality tier
  (720p / 1080p / 4K, bitrate scales per tier, 4K auto-falls-back if the
  device encoder refuses it) and six aspect ratios (16:9, 9:16, 1:1, 4:5,
  4:3, 21:9) computed to even encoder dimensions. Finished exports offer
  "Upload to Drive / Share" via the system share sheet (Drive appears when
  installed).

## v0.4 - customization for milkdrop, playlists, transitions
- FIX freeze after loading .milk presets: preset import and built-in asset
  copying now run on a background thread with one-time caching and tap
  debouncing (the old code re-copied every asset on the main thread per tap).
- FIX customizations now affect milkdrop: projectM renders into an
  offscreen FBO texture (projectm_opengl_render_frame_fbo) and a post-process
  pass applies zoom, rotation, endless zoom, mirror, hue/saturation/
  brightness/contrast/gamma/invert/intensity; Beat response maps to projectM
  beat sensitivity. (Speed cannot retime projectM's internal clock.)
- Color tab gains Contrast and Gamma faders across all scene types.
- Attack/Decay reactivity sliders and the Manual/Suggest/Auto intelligence
  chips moved into Customize > Behavior.
- Music player: shuffle, repeat (off/all/one), previous/next transport, and
  a tap-to-jump Playback queue dialog.
- Style/preset browser: pick a style first, then its presets - user-saved
  presets are grouped under their style; milkdrop lists built-in and
  imported .milk files. Anything can be added to the visual playlist.
- Visual preset playlist with auto-advance (fixed interval or Intelligent -
  switches on strong musical moments with a minimum dwell and a 2x-interval
  fallback) and transition styles between scenes: cut, fade, melt
  (dual-FBO compositor; melt drips the outgoing frame away by luminance).
  Same-scene preset changes currently hard-cut.

## Bugfix log (v0.3.3)
- Milkdrop lifecycle aligned with working reference integrations (official
  projectM examples-android and a ProjectM-4 Android TV app): the engine is
  now created lazily on the GL thread with the REAL surface size (never at
  1x1 followed by set_window_size), glViewport is forced to the surface size
  both before and after every projectm_opengl_render_frame (projectM depends
  on and mutates it), the last loaded preset is re-applied automatically when
  the engine is recreated after GL context loss, and an engine-init failure
  now shows an on-screen error instead of a silent black screen.
- Workspace placed under git (baseline commit) after repeated foreign file
  modifications were found; use git status to detect tampering.

## Bugfix log (v0.3.2)
- Milkdrop preset loading root cause found and fixed: opening the system
  file picker paused the GLSurfaceView, destroying the GL context; on
  return every scene was rebuilt and the queued .milk path landed on a
  discarded ProjectMScene instance - the preset deterministically never
  loaded, and any loaded preset (and custom GLSL edits) silently reverted
  on every app switch. Fixes: preserveEGLContextOnPause=true, and durable
  renderer-level state (milk preset path + custom shader sources) re-applied
  after genuine context loss. Engine verified working end-to-end via a
  headless Mesa/GLES smoke test (tools/smoke.c) that loads real BeatDrop
  presets and checks rendered pixels. Errors now display in the main
  overlay, including preset import failures.

## Bugfix log (v0.3.2)
- Milkdrop root cause found: libprojectM had been built from the *master*
  branch, which carries an experimental GL bootstrap (GLResolver/GladLoader
  "strict context gate") not present in any release - the resulting .so had
  no GLES linkage at all and projectm_create could fail on-device, leaving
  the scene black and preset loads with no engine to load into. Rebuilt from
  the stable v4.1.7 release tag: OpenGL ES: ON, direct libGLESv3.so linkage
  (verified with readelf), JNI relinked against release headers.
- Bundled three self-authored starter presets (assets/presets/*.milk,
  MilkDrop-1 syntax, no licensing constraints) and a "Preset >" button that
  cycles them - instant on-device verification without hunting for files.
  Pin the projectM checkout to a release tag in any future rebuild
  (tools/build-projectm.md updated).

## Bugfix log (v0.3.1)
- Export: after end-of-stream the encoder drain no longer aborts on the
  first TRY_AGAIN (was truncating the video); cleanup in finally is
  exception-safe so real errors are reported instead of "Failed to stop
  the muxer"; encoder falls back to 30 fps / 8 Mbps if the device rejects
  1080p60; MediaStore writes use IS_PENDING + Movies/Geode on Q+;
  export errors now include the exception class.
- Milkdrop: presets load with a hard cut (smooth blending is fragile on
  GLES), the preset is locked so projectM can't auto-switch away, texture
  search paths are set to the preset's folder + ./textures, load/render
  errors surface in the UI and logcat (tag projectM-jni), and PCM is fed
  as fresh samples via a ring cursor instead of overlapping windows.

## Bugfix log (v0.2)
- Controls no longer overlap system bars: enableEdgeToEdge + safeDrawing
  insets on the overlay; GL canvas stays truly fullscreen.
- Export fixed for MP3/OGG/FLAC sources: MP4 cannot carry those audio
  codecs, so audio is now transcoded to AAC-LC (192 kbps) before muxing,
  and both tracks are registered before MediaMuxer.start().
- Analyze crash fixed: offline analysis is now streaming (constant memory
  regardless of track length) and failures are caught as Throwable.

---

## Appendix: pre-split README body (historical)

The sections below are the original README introduction that this changelog
used to sit on top of, kept verbatim for the record. Several of them describe
the app as it stood around v0.3 and the MVP phases; the current
[README.md](README.md) supersedes them.

# Geode — Android Music Visualizer (MVP)

Native Android app: pick a local audio file, it plays with a real-time
GL ES 3.0 particle visualization driven by FFT data. The roadmap this section
was written against - offline analysis and scene intelligence, shader scenes,
projectM, deterministic export - has all since shipped; the changelog above is
the record, and the plan document it lived in is gone.

## Build
- `./gradlew assembleDebug` — build APK
- `./gradlew testDebugUnitTest` — unit tests (FFT, smoothing, ring buffer)
- `./gradlew lint ktlintCheck` — lint
- `./gradlew installDebug` — install on a connected device

Requires JDK 17+, Android SDK 36. `local.properties` must point at your SDK.

On a machine with no SDK — a Claude Code cloud session, a fresh container —
`tools/setup-android-sdk.sh` installs the packages `compileSdk` asks for and
writes `local.properties` for you. Every package comes from `dl.google.com`,
which the "Trusted" network policy does not allow and `maven.google.com`
redirects to, so a cloud environment needs **Full** network access or
**Custom** with `dl.google.com` and `maven.google.com` allowed. Without that,
Gradle cannot resolve the Android plugin either, and the script says so rather
than failing mid-download.

## Architecture (one-way: ui -> audio/analysis/render)
- `audio/` — ExoPlayer wiring: `TapRenderersFactory` overrides `buildAudioSink`
  to insert a `TeeAudioProcessor`; `PcmTapSink` copies PCM (16-bit or float,
  any channel count) off the playback thread into `PcmRingBuffer` (lock-free,
  mono-downmixed). No RECORD_AUDIO permission, no `android.media.audiofx.Visualizer`.
- `analysis/` — `AnalysisEngine` worker on Dispatchers.Default: Hann window ->
  2048-pt real FFT (JTransforms) -> 64 log-spaced bands -> dB normalize ->
  asymmetric attack/decay smoothing -> `StateFlow<FloatArray>`.
- `render/` — `VisualizerRenderer` (GL ES 3.0): 2,500 point-sprite particles,
  one draw call, additive glow blend. All GL resources recreated in
  `onSurfaceCreated`; bands handed over via a volatile snapshot (no locks on
  the GL thread). Shaders live in `res/raw/*.glsl`.
- `ui/` — Compose: SAF picker (`ACTION_OPEN_DOCUMENT` + persisted URI
  permission), play/pause/seek overlay, `LifecycleResumeEffect` forwards
  onResume/onPause to the GLSurfaceView.

## Status
Phases MVP + A + B + C + D implemented and verified: build, 16/16 unit
tests, lint + ktlint all green.

- Phase A: OfflineAnalyzer (MediaExtractor/MediaCodec decode), FeatureExtractor
  (spectral flux onsets, beat pulses, autocorrelation BPM, centroid),
  FeatureTimeline with novelty-based section detection, SceneSuggester with
  three intelligence modes (manual / suggest / auto), reactivity sliders
  (attack/decay) in the UI.
- Phase B: Scene interface; NebulaScene + BurstScene (particles), Julia +
  Tunnel ShaderScenes (Shadertoy-style: uTime/uResolution/uBass/uMid/uTreble/
  uEnergy/uBeat + 64x2 uAudioTex bands/waveform texture); in-app GLSL editor
  with runtime compile, inline errors and last-working fallback; JSON presets.
- Phase C: deterministic offline export - scene re-rendered frame-exact at
  60 fps from the feature timeline into an H.264 encoder surface (EGL +
  eglPresentationTimeANDROID), original audio track muxed alongside, 1080p
  16:9 or 9:16, saved via MediaStore, cancellable with progress.
- Phase D: multi-file queue (OpenMultipleDocuments) with prev/next, audio
  focus handling, ambient mode (tap canvas to hide controls), keep-screen-on.

## MilkDrop / projectM backend
libprojectM 4 is integrated as a fifth scene ("milkdrop"), built for
arm64-v8a with the Android NDK (GLES3). The scene renders projectM's idle
preset by default; tap "Load .milk" to import any MilkDrop/BeatDrop preset
file via SAF. Live PCM is fed from the existing TeeAudioProcessor ring
buffer. libprojectM is LGPL-2.1 and dynamically linked (lib/arm64-v8a/
libprojectM-4.so); a thin JNI wrapper (libprojectmjni.so) exposes create/
resize/addPCM/render/loadPreset/beatSensitivity. Rebuild instructions: see
tools/build-projectm.md. Note: the APK ships arm64-v8a only.

## Scenes (v0.3)
18 scenes total. Particles: nebula, bursts, swarm (firefly attractor),
fountain. Fragment shaders (all share one prelude with the audio texture +
param uniforms): julia, tunnel, mandel (pairs with Endless zoom), kaleido,
plasma, bars, ring (circular spectrum), scope (oscilloscope), liss
(vectorscope), warp (starfield), grid (synthwave horizon), voronoi,
metaballs, ripples. Plus milkdrop (projectM, arm64).

## Customize panel (v0.3)
Tabbed: Motion (speed, zoom, rotation, endless zoom + dive speed),
Behavior (audio drive, beat response, turbulence, density, mirror,
trails + trail length), Color (5 palettes, hue shift/range, saturation,
brightness, intensity, color cycle + speed, invert), and GLSL (raw editor,
shader scenes only). All params apply to every scene type uniformly, are
saved in presets, and carry into exports. Trails are implemented as a
fade-quad instead of a clear (particle scenes).

## Customization (v0.2)
Every scene has a "Customize" panel with live sliders: Speed, Zoom, Color,
Intensity. They apply uniformly - particle motion/size/hue, shader uniforms
(uSpeed/uZoom/uColorShift/uIntensity), and projectM beat sensitivity - and
are saved in presets. Shader scenes additionally expose an "Advanced" tab
containing the raw GLSL editor (compile-on-apply, inline errors, safe
fallback), following the ISF pattern of sliders-first, code-optional.
