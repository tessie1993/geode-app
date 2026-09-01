# shaderpreview

Renders the app's GPU styles headlessly, off-device, and **measures** them.

There are ~37 visual styles in this app and, without this, the only way to see
any of them is to build an APK and put it on a phone. That is slow enough that
shaders get changed blind, and "it looks wrong" comes back as a sentence
instead of a number. This turns a style into PNGs plus a table of luminance
statistics you can assert on.

It renders the app's **real** GLSL — the files in `engine/scenes/src/main/res/raw`
and `app/src/main/res/raw`, with their `//#include` directives resolved exactly
the way `GlUtil.kt` resolves them — driven by uniform values computed by a JS
mirror of the Kotlin scene that would upload them.

One family is a partial exception, named here rather than buried: SILK's two
`.glsl` files are **`SimPass` bodies**, not whole shaders. The app wraps each in
a generated header (`SimGlsl.kt`) and compiles the step twice over — as an ES
3.1 compute kernel where the device proved it has one, as an ES 3.0 fragment
step everywhere else. This harness is WebGL2, so it assembles the fragment half
through `lib/simglsl.mjs`, a mirror of that generator, and **cannot see the
compute path at all**. The body it renders is the app's; the wrapper around it
is a mirror that can drift.

**Read [What it cannot tell you](#what-it-cannot-tell-you) before you trust
anything it prints.** An over-trusted harness is worse than no harness.

## Running it

No install step, no `node_modules`, no network.

```sh
cd tools/shaderpreview

node preview.mjs --list

# Silk, eight frames of a beat track, PNGs + report.json into out/silk
node preview.mjs --scene silk --frames 8 --audio beat --out out/silk

# one of the fragment styles
node preview.mjs --scene shader --shader julia --frames 4 --audio tone

# the four field-sim families (SILK / LIFE / ACID / MYCO): the app's own
# step/show (or agent/deposit/blur/show) fragment shaders run as a generic
# multipass ping-pong pipeline. Give the sims frames to develop - warmup
# steps the state without paying for the present pass.
node preview.mjs --scene silk --style silk_web --warmup 88 --frames 2 --audio beat
node preview.mjs --scene myco --style myco_rivals --warmup 118 --frames 2 --field-stats

# a minute of steady music, sampled every 5 s, with fluid-field statistics
node preview.mjs --scene silk --frames 13 --every 300 --audio tone --field-stats

# what the style looks like on a GPU with no float render targets
node preview.mjs --scene silk --out out/silk

# five hours of wallpaper uptime, after a 10 s warm-up
node preview.mjs --scene shader --shader plasma --warmup 600 --frames 6 --clock-jump 3600

# the Layers blend algebra: with both layers identical, DIFFERENCE must go
# to exactly 0, MULTIPLY must darken and SCREEN must brighten
node preview.mjs --scene shader --shader plasma --composite --layer 1,4 --frames 1

# through the composite pass, which is where Zoom and Rotation live on a
# fluid-family style - without this you are looking at a frame the user never
# sees, and half its controls are applied nowhere in it
node preview.mjs --scene silk --composite --param zoom=3 --frames 1
```

### Flags

| flag | meaning |
| --- | --- |
| `--scene shader \| silk \| life \| acid \| myco` | which driver to use |
| `--shader <id>` | style id for `--scene shader` (see `--list`) |
| `--style <id>` | style id for the four field-sim families (default = the family's first style; see `--list`) |
| `--frames N` | frames to **capture** |
| `--every N` | simulate N frames per captured frame |
| `--warmup N` | simulate N frames before capturing anything |
| `--fps N` | simulated frame rate (default 60); sets `dt` |
| `--size N` / `--width` / `--height` | render size (default 480) |
| `--audio silence\|tone\|beat\|arc` | audio model |
| `--param name=value` | override one `SceneParams` field, repeatable |
| `--no-melt` | force `uHasMelt = 0` (the no-float-buffer fallback) |
| `--no-float-sim` | field-sim families only: pretend no float format is renderable, so every target falls to RGBA8 — the app's `FluidBuffers` byte-fallback path, which SwiftShader's always-present `EXT_color_buffer_float` otherwise hides |
| `--composite` | run the scene through `composite_frag` afterwards, as the app does |
| `--layer mix,mode` | force the Layers branch (`uStyle` 6) at that mix and `BlendMode` ordinal. Both layers are the SAME texture, so this checks the blend algebra, not a real two-scene composite |
| `--field-stats` | read back the fluid dye/velocity grids and report their statistics |
| `--clock-jump S` | advance the free-running clocks by S seconds per captured frame |
| `--seed N` | seed for the body RNG |
| `--out DIR` | write `frame_NNN.png` and `report.json` |
| `--json` | print the whole report as JSON |

Frames that are not captured skip the draw entirely (every simulation still
steps), which is what makes `--every 300` finish — the raymarch is ~99% of the
wall clock on software GL.

## How it stays honest

A preview harness that invents its own uniform values is a drawing of a
shader, not a preview of a style. A uniform the harness forgets defaults to
**zero** in GL, and a zeroed uniform is indistinguishable from a feature that
is switched off — which is exactly how a working feature gets reported as
dead. So nothing here is allowed to be implicit.

**Includes.** `lib/glsl.mjs` parses the `GlUtil.INCLUDES` map out of
`GlUtil.kt` rather than hardcoding a list, and applies the same anchored
`^[ \t]*//#include[ \t]+(\w+)[ \t]*$` pattern, one level, unknown include is a
hard error. Add a library to the app and this picks it up; rename one and this
fails the same way the app does.

**Uniforms — a three-way audit, run before every render.**

| set | source |
| --- | --- |
| A. what the shader **declares** | `parseUniforms` over the resolved GLSL (array lengths resolved through `#define`) |
| B. what the Kotlin **uploads** | `loc("uX")` / `glGetUniformLocation(program, "uX")` / `setUniform1f("uX")` scraped from the scene's `.kt` |
| C. what the harness **supplies** | the driver in `lib/scenes.mjs` |

- `A \ C` → **fatal**: the render would contain a silent zero.
- `B \ C` → **fatal**: the harness has drifted behind the app.
- `C \ B` → **fatal**: the harness is inventing an input the app never sends.
- `A \ B` → reported as a note: the *app* leaves it zero too, which is a
  finding about the app.
- declared-but-dead-stripped-by-the-linker is reported separately, so the
  audit does not cry wolf about a uniform the shader never reads.

When the audit fails, the tool prints the mismatch and **refuses to render**.
That is the point: a picture from a drifted harness is worse than no picture.

**Uniform values** are computed by JS ports of the Kotlin that owns them:
`lib/palette.mjs` (`FluidHue`),
`lib/emitters.mjs` (the `FluidEmitters` paths `MeltField` actually enables),
and `lib/scenes.mjs` (the two `draw()` methods). Every constant is
copied with the comment that justifies it, so a drift is visible in a diff.

**The melt is not modelled — it is run.** `page/harness.html` executes the
app's own `fluid_*.glsl` fragment shaders in the app's own pass order
(`FluidSim.step`: advect → velocity splats → curl → vorticity → divergence →
Jacobi → gradient → dye splats → dye advect) on RGBA16F/RG16F/R16F ping-pong
FBOs at the app's grid sizes. There is no re-implementation of the fluid to be
wrong about.

**Stand-ins are named.** Where the app binds something conditionally (the
FlowField texture, the cyclic-palette atlas), or where there is nothing here to
model at all (the `uTouch*` block - this harness has no pointer), the harness
supplies the neutral value the app itself sends in that state, and prints it as
a stand-in rather than pretending it is the real thing.

**The composite pass is the app's, and audited the same way.** With
`--composite` the scene draws into an RGBA8 target (`VisualizerRenderer`'s
`TargetFbo`, filter and wrap included) and `composite_frag` draws that to the
screen, driven by `lib/composite.mjs` mirroring the renderer's `cLoc()` block
and gated by `CompositeGrade.gateFor` for the scene's family. Its uniforms go
through the same three-way audit against `VisualizerRenderer.kt`. Only the
non-transition case is modelled - `uStyle = CUT`, one scene texture, both
gates from the active family; two live scenes would need two drivers, and the
gate algebra a transition exercises is pinned by `CompositeGradeTest`
instead.

## What it measures

Per captured frame, from a `readPixels` of the default framebuffer:

- `meanLuma`, `maxLuma` — Rec. 709 luminance of the **stored 8-bit values**
- `fracBlownOut` — fraction of pixels with R, G *and* B above 0.95
- `fracBlack` — fraction of pixels with luminance below 0.02
- `deltaMeanLuma` — change in `meanLuma` since the previous **rendered** frame
- `skippedUniforms` — anything the plan set that the linker had dropped
- `glError`

With `--field-stats`, also the dye and velocity grids read back as float:
`max`, `mean`, `meanMagnitude`, `fracAbove1`, `nonFinite`.

## What it cannot tell you

Take these seriously. Every one of them is a way to be confidently wrong.

1. **SwiftShader is not a phone GPU.** Rendering happens on ANGLE over
   SwiftShader's Vulkan backend. It is a correct, conservative software
   rasteriser, and that is the problem: it agrees with the spec, and phone
   drivers do not. It will not reproduce Mali's or Adreno's fast-math
   reassociation, their `pow`/`atan` approximations, their loop-unrolling
   limits, or their register-pressure cliffs.

2. **Precision qualifiers are effectively ignored.** SwiftShader computes
   `mediump` and `lowp` at full float32. Every bug in this codebase's history
   that came from a real `mediump`/`lowp` range limit — including the
   `precision highp sampler2D` fix in the fluid shaders, whose comment
   explains that GLSL ES defaults fragment `sampler2D` to `lowp` and that Mali
   honoured it — is **invisible here**. A shader that renders perfectly in
   this harness can still clamp and quantise to garbage on a real device.
   `--clock-jump` tests float precision at large `t` in *float32*; a driver
   that runs that expression at `mediump` will fail much sooner than this
   says.

3. **Float render targets are available here, and are not everywhere.**
   `EXT_color_buffer_float` is present under SwiftShader, so the melt always
   runs unless you pass `--no-melt`. On a device without renderable half-float
   the app takes the `uHasMelt = 0` path. Use `--no-melt` deliberately; do not
   assume the default run is what a given phone shows.

4. **The ES 3.1 compute tier is invisible here.** WebGL2 is ES 3.0: no
   compute shaders, no image load/store, nothing to fake either with. So for
   SILK - the one family routed through `SimPass` today - this tool measures
   the fragment ping-pong and says nothing whatever about the dispatch: not
   that it compiles, not that it matches, not that it is faster. `SimGlsl`
   generating both paths from one body and one encoding is what makes them the
   same simulation; that is an argument from construction, and this harness is
   not evidence for it.

5. **Driver-specific compile and link failures do not happen here.** A shader
   compiling in this harness says nothing about whether a device driver will
   accept it: uniform-array sizes, loop bounds, `MAX_FRAGMENT_UNIFORM_VECTORS`
   (4096 here, as low as 224 on real ES 3.0 hardware), and vendor-specific
   optimiser bugs all differ. This tool is a *lower* bound on portability.
   `docs/DEVICE_CHECKS.md` is still the authority.

Also, more mundanely but just as capable of misleading you:


6. **`--clock-jump` moves the clocks and nothing else.** It advances `uTime`
   (and the camera drift phase, and `uRotation`) without ageing the body bank
   or the fluid, because the app clamps its own `dt` to 1/15 s and a scene
   stepped at a 60-second `dt` is a state the app can never be in. Read a
   jumped run for precision behaviour only; the geometry in it is whatever
   the warm-up left there.

7. **The composite pass is off unless you ask for it.** Without
   `--composite` what you see is the scene's own output before the app grades
   it - which is what you want for debugging a scene, and is *not* what the
   user sees. On a fluid-family style that difference is not cosmetic:
   `CompositeGrade.gateFor(FLUID)` hands the composite the whole
   zoom/rotation/colour-grade block, so Zoom and Rotation are applied nowhere
   in the scene-only frame and reading one is a good way to conclude that a
   working control is dead. Even with `--composite`, transitions and the
   spliced gl-transition variants are not modelled.

8. **Luminance is measured on the stored 8-bit values**, not on display-linear
   light. It is a good relative measure and a poor absolute one; use it to
   compare runs, not to make a claim about perceived brightness.

9. **Nothing is ever touching the screen here.** The `uTouch*` uniforms are
   supplied at TouchField's untouched values (all zero, `uTouchGesture = 0`,
   `uTouchCount = 0`), because this harness has no pointer to model. That is a
   real state the app spends most of its life in, so the frames are honest -
   but anything a style draws only under a finger, and the 0.55 s release wake
   behind a lifted one, are invisible to this tool. Reading a black frame here
   as "the touch response is dead" is a way to be confidently wrong.

   This covers the field sims as well as the fragment styles now: silk deposits
   dye under a finger, life seeds its state there, acid draws into the feedback
   source, and myco reburies a slice of its colony at the fingertip. Each is
   behind `uTouchCount == 0` (render/scene/SceneTouch.kt), and none of it runs
   here.

10. **Five scene families are wired up**: the 28
    `ShaderScene` styles and the four field-sim families
    `SilkScene`, `LifeScene`, `AcidScene` and `MycoScene` (10 styles each,
    `--style`). The fluid family's own display passes, WATER, CYMATICS, BEAM
    and MILKDROP have their own uniform contracts and are not covered. Adding
    one means adding a driver to `lib/scenes.mjs` — and the audit will tell
    you when you have not finished.

    SILK's two programs are ASSEMBLED rather than loaded, per the note at
    the top of this file: `silk_step.glsl` and `silk_show.glsl` are SimPass
    bodies and `lib/simglsl.mjs` supplies the wrapper. Its state target is
    `FormatPolicy.advectedField` - RGBA16F filterable where the probe proves
    it, pre-scaled RGBA8 (scale 8) otherwise, which is the world
    `--no-float-sim` shows.

    The field-sim scenes run as a PASS LIST the driver emits per frame -
    named programs, named ping-pong targets (formats chosen by the same
    renderability probe as `FluidBuffers`, falling RGBA16F→RGBA8, RG16F→
    RGBA16F→RGBA8, RGBA32F→RGBA16F→RGBA8), the myco deposit as additive
    GL_POINTS into the trail's READ side, exactly as the Kotlin sequences
    its passes. Their sim passes step on every frame; only the present obeys
    `--every`/`--warmup`. Named stand-in: the PCM strike envelope runs on
    the audio MODEL's waveform rather than the app's raw PCM tap. (The acid
    family's wheel was a `uChroma` mirror on both sides until the app
    replaced it with `uSpokes`, the live band envelopes folded onto twelve
    spokes; the harness had not followed, so its audit was refusing every
    acid style. It follows now.)
    `LifeScene`'s 4-second liveness census is mirrored through a per-frame
    readback of the state's centre texel (float, quantized to the app's
    8-bit steps driver-side).
11. **The sprite pass clears the target only when the echo is off.** With
    Trails on, the echo blit is the background exactly as `drawWithEcho()`
    sequences it — previous frame warped, decayed and redrawn under the new
    sprites, ping-ponged between two RGBA8 targets. `--param trails=false`
    previews the cleared, sprites-only path instead.

## Layout

```
preview.mjs             CLI: resolves shaders, runs the audit, drives the page
lib/cdp.mjs             Chromium launch + CDP over Node 22's built-in WebSocket
lib/glsl.mjs            GlUtil-equivalent include resolution; uniform declaration scan
lib/kotlin.mjs          uniform names scraped from Kotlin; the three-way audit
lib/audio.mjs           AudioFeatures models + the 64x2 uAudioTex rows
lib/palette.mjs        JS port of FluidHue
lib/emitters.mjs        JS port of the FluidEmitters paths MeltField enables
lib/scenes.mjs          per-frame uniform plans, mirroring the Kotlin draw()s,
                        plus the silk/life/acid/myco style tables (verbatim
                        VisualStyleCatalog.kt mirrors) and pass-list drivers
lib/simglsl.mjs         SimGlsl.kt's FRAGMENT path, mirrored: the header, state
                        sampler, decode helpers and main() the app generates
                        around a SimPass step or display BODY
lib/composite.mjs       the same, for VisualizerRenderer's composite_frag pass
page/harness.html       WebGL2: compile, upload, run the fluid passes and the
                        field-sim pass lists, measure
```

### Why CDP and not Playwright

`node -e "require('playwright')"` fails with `MODULE_NOT_FOUND` in this
environment, and `playwright install` is explicitly off the table. Node 22
ships a global `WebSocket` and `fetch`, which is all the DevTools Protocol
needs, so `lib/cdp.mjs` drives the browser in ~120 lines with no dependency at
all. A `--headless --screenshot` invocation was rejected because the harness
has to keep GL state (the fluid ping-pong grids) alive **across** frames and
read values back out, which one-shot screenshotting cannot do.

The Chromium binary comes from `/opt/pw-browsers/chromium`; override with
`MUSICVIZ_CHROMIUM`.
