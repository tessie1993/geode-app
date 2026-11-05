# 04 — Render pipeline and effect model: fosfora vs Geode

Fosfora @ `2e85d3b` (paths below are relative to `crates/fosfora-app/src/` unless they start with `assets/` or `docs/`). Geode at `37761e8a`, paths relative to the repo root. I checked call paths with Graphify (`tessie1993/fosforatess`) and with grep. Graphify misses one call: `PostProcessChain::limit_and_composite` is called at `gpu/postprocess.rs:305` **and** `:495`, but the graph only has the first.

---

## 1. How fosfora works

### 1.1 Who owns what

```
App (app.rs, god object)                         SceneRenderer (headless/scene_renderer.rs:24)
 ├─ GpuContext (gpu/context.rs:19)  device, queue, surface, PipelineCache (persisted), device_lost flag
 ├─ EffectLoader (effect/loader.rs)  Vec<PfxEffect>, WGSL lib preamble, UNIFORM_BLOCK (loader.rs:166)
 ├─ LayerStack (gpu/layer.rs:695)   Vec<Layer>, ≤ MAX_LAYERS
 │    └─ Layer (layer.rs:397)  name, ParamStore, blend_mode, opacity, displace_amount, enabled/locked/pinned,
 │         │                   postprocess: PostProcessDef, chain: Option<Box<LayerChain>>, modulation: LayerModulation
 │         └─ content: LayerContent (layer.rs:277) = Effect(EffectLayer) | Media(MediaLayer)
 │              EffectLayer (layer.rs:258): PassExecutor, UniformBuffer, ShaderUniforms, RateState, shader_error
 │                PassExecutor (gpu/pass_executor.rs:80): Vec<CompiledPass>, Option<ParticleSystem>,
 │                  backdrop view, flip_parity
 │                  CompiledPass: ShaderPipeline, PingPongTarget, [BindGroup;2], input_srcs, iterations
 ├─ Compositor (gpu/compositor.rs:37)  ping-pong accumulator + stable @backdrop target
 ├─ TramaSystem (trama/mod.rs)  TramaRegistry, TramaExecutor, master NodeGraph, modulation
 ├─ ChainTargets (gpu/chain_targets.rs:56)  one output RenderTarget per chain slot (+generation)
 ├─ TransitionRenderer (scene/transition.rs:9)  lazy crossfade snapshot
 └─ PostProcessChain (gpu/postprocess.rs:100)  bloom (quarter res), composite, blit, flash limiter state
```

GPU resource lifetimes. A `RenderTarget` (`gpu/render_target.rs:9`) gets a process-unique `id` from an atomic counter (`:17-28`). That id is the identity stamp: plans and bind groups notice a recreated texture by it, so nobody has to bump a counter by hand. Feedback targets are made with `new_cleared` and recleared on resize (`:153-246`), so the first frame never reads NaN garbage. Targets are recreated only on resize, on an effect load, or on a rebuild that `PfxDiff::needs_rebuild` asks for (`effect/format.rs:353`). Steady state allocates nothing, which the trama docs call invariant "I8" (`trama/exec/executor.rs:1-13`).

### 1.2 One frame

1. **`App::update`** (`app.rs:927`):
   - drains the input channels;
   - mirrors the audio features into `ShaderUniforms` (`gpu/uniforms.rs:260 mirror_audio_features`);
   - calls `frame_prep::prepare_effect_layers` (`gpu/frame_prep.rs:24`). For each effect layer it copies the global uniforms, packs `ParamStore` into `params[16]`, calls `rates.advance(params, dt)` (`:42`) and does the CPU-side particle work;
   - drains the background compile results (`app.rs:1661-1730`).
2. **`App::render`** (`app.rs:4413`):
   - sets the flash budget from settings and reduced motion (`:4419`);
   - syncs `ChainTargets` against the stack (`:4470`);
   - calls **`frame_graph::execute_and_composite`** (`gpu/frame_graph.rs:120`):
     - With no layers it returns the cleared accumulator.
     - With one opaque layer it takes the fast path (`:166`): `layer.execute` → its chain → no compositing.
     - Otherwise it walks the layers bottom first. A layer that samples `@backdrop` first gets the composite of the layers below it (`:215-225`), then `execute` → `run_layer_chain` (`:341`) → a `LayerComposite` with the *effective* (modulated) blend, opacity and displace values. `Compositor::composite_tapped` then blends them.
     - The master chain runs last, upstream of post.
   - an optional dissolve renders the retiring stack through a second compositor and crossfades (`app.rs:4513-4545`);
   - **`PostProcessChain::render`** (`gpu/postprocess.rs:285`): bloom extract, blur H, blur V, then `limit_and_composite` (`:540`). That runs the flash-limit compute pass, copies the gain buffer, and runs the post-composite into the display target;
   - blits to the window and the second output; NDI, Spout, Syphon, v4l2 and recording all capture from the **same** composite and gain (`app.rs:4600-4660`);
   - `layer.flip()` on every layer (`:4666`). Parity advances only here.
3. **`PassExecutor::execute_profiled`** (`pass_executor.rs:357`):
   - uploads the 400 B uniform block once;
   - dispatches particles;
   - runs each pass's draws. Every draw is a fullscreen triangle with `LoadOp::Clear`. An iterated feedback pass alternates targets in the encoder so its *last* draw lands in `targets[flip_parity]` (`:394-399`);
   - renders the particles last with `LoadOp::Load`.

**Headless reuse.** `SceneRenderer::render_frame` (`headless/scene_renderer.rs:612`) and `step` (`:690`) call the same three functions: `mirror_audio_features`, `prepare_effect_layers` and `execute_and_composite`. Graphify confirms three callers of `execute_and_composite`: `App::render`, `frame_tapped` and `SceneRenderer::render_frame`. The headers say why the code was pulled out: copies "began to drift" (`frame_graph.rs:1-8`), and "skipping any piece produces frames that render without error and are silently wrong" (`frame_prep.rs:1-9`). Headless runs no trama (`None`) and plays Dissolve as Cut. It records these as warnings instead of skipping them silently (`scene_renderer.rs:12-14`).

### 1.3 The effect model (`.pfx`)

`PfxEffect` (`effect/format.rs:219`) holds:
- name, author, description;
- `shader`, or `passes: Vec<PassDef>`;
- `inputs: Vec<ParamDef>`, up to 16 typed params (Float, Color, Bool, Point2D) packed in declaration order into `array<vec4f,4>`;
- `rates`;
- `postprocess: Option<PostProcessDef>`;
- `particles`;
- `category`, `alpha` and `loop_mode` (`LoopMode::PhaseLocked` means "pure function of the uniform block", `:20-33`).

`PassDef` (`:53`) is `name, shader, scale, inputs[], prev_inputs[], iterations, feedback`. `PassExecutor::new` (`pass_executor.rs:97`) resolves it in two phases:
- `inputs` name **earlier** passes (current frame) or the special inputs `@backdrop` and `@particles.velocity`. A forward reference is a hard error (`:118-150`).
- `prev_inputs` name **any** feedback pass, for its previous frame. These are the edges that cut the solver cycle velocity → divergence → pressure → velocity (`:156-185`).
- `iterations > 1` ping-pongs a pass's own target for Jacobi solves.
- `scale` sizes each pass's target (Fluvid runs at 0.25, 0.5 and 1.0, with 16, 32 and 64 iterations; `assets/effects/fluvid.pfx`).

Bind groups are built for **both parities** ahead of time (`build_bind_groups`, `:645`). A pass reads `P.targets[g]` for a current-frame input of a feedback pass, `targets[0]` for a non-feedback pass, and `targets[1-g]` for a prev input. The parity is global, so cross-pass reads stay aligned (#1481, `:80-95`).

**Shader ABI.** The loader prepends a 400-byte `PhosphorUniforms` struct to every shader: time, dt, resolution, ~70 audio scalars, `params`, `mfcc`, `chroma` and the reserved tails. It also prepends textures: prev_frame and three audio textures at bindings 0-6, then `inputN` at `7+2i` (`effect/loader.rs:166-296`), plus the helpers `param(i)`, `feedback(uv)`, `spectrum(x)` and so on. Injection is decided by parsing with comments stripped (`:319-362`), after a bug where a *comment* mentioning the struct suppressed injection (#1855).

**Rates** (`effect/rates.rs`). The engine keeps an f64 running integral of each named param (`value·dt`, or `dt/value` for a period). It writes the integral into the slots *after* the packed params, and refuses duplicates and overflow past 16 slots (`layout`, `:92`; `advance`, `:160`). The module doc (`:1-36`) records the measurement behind it: at a 300 s clock, a 0.01 nudge to `u.time * speed` cost 884× to 12,000× more than at a fresh clock.

**Hot reload.**
- `shader/hot_reload.rs` routes changed files by location, then extension (`:21-25`). It drops events whose mtime and size stamp has not moved, because inotify OPEN events made every *read* look like a change (`:82-87`). A watcher that cannot be created is a status-bar note, not a crash (`:95-99`).
- `.pfx` edits go through `PfxDiff` (`format.rs:312-355`). Changed passes or particles mean a full rebuild that keeps params with a matching name and type (`app.rs:1943-1957`). Changed inputs or post settings are merged incrementally.
- WGSL edits compile on the `ShaderCompiler` thread (`gpu/shader_compiler.rs`) and are swapped in by `swap_pass_pipeline` (`pass_executor.rs:616`). On an error the old pipeline keeps running and `shader_error` is set.

### 1.4 The node graph (trama)

`NodeKind` (`trama/node.rs:60`) is `Source | Effect | Feedback | ChainInput | Anchor | Output`. `NodeGraph` (`trama/graph.rs`) holds some invariants by construction:
- exactly one Output;
- `connect` refuses cycles (`:304`);
- one wire per input pin;
- every edit bumps `version`.

`topo_order` (`:450`) is Kahn's algorithm, and it **does not count wires into a Feedback node**. That is the one-frame delay (I9), and it is what lets `mix → transform → feedback → mix` exist. `live_set` (`:496`) keeps only the nodes that reach Output.

`TramaExecutor::execute` (`exec/executor.rs:440`) rebuilds the `ExecPlan` (`:198`) only when its key moves. The key is graph version, registry generation, previews on, chain-input generation and output-target generation. `build_plan` (`:656`):
- resolves bypass and Anchor aliasing at plan time, so neither runs a pass;
- gives the final producer the caller-owned output target and every other node a pooled target (`exec/textures.rs`, keyed by width, height and format, reset at each plan);
- keeps feedback pairs **outside** the plan, keyed by `ChainNode`, so a rewire elsewhere never clears an echo;
- puts the copy steps at the end of the frame;
- gives each chain a disjoint uniform-arena region (`chain_slot_base`), because every `write_buffer` in a frame is staged before any pass runs.

A frame is N uniform writes plus N draws. Failure rules:
- a failed plan build keeps the last good plan and stamps the keys, so it does not retry every frame (`:482-510`);
- a missing effect clears its node to magenta (`:97-130`);
- an unwired Output is deliberate black (`:600-617`);
- an inactive layer chain passes the layer through untouched (`frame_graph.rs:325-340`).

Trama effect files carry their manifest in the shader itself: `/*! trama { name, id, kind, inputs, params, rates, integers } */` (`trama/effect.rs:36-60`; `assets/trama/effects/hue_drift.wgsl:1-13`). They are validated by naga before a pipeline is created (I5). A bad file is skipped and listed in `errors`.

### 1.5 Compositing, post, safety

- **Compositor.** It blits the first layer, then composites each later layer against the accumulator, which it samples as a texture so the displacement modes can read it at an arbitrary uv. It has 20 blend modes, with HDR-safe Screen, Overlay and Exclusion (`assets/shaders/builtin/composite.wgsl:18-24`, fixing #93). Only Normal is a true premultiplied over (`docs/alpha.md`, "Known limitations"). INV-A is "premultiplied alpha everywhere internal". `resolve_output_alpha` is shared by screen, sinks and headless (`frame_graph.rs:37`).
- **Flash limiter** (`assets/shaders/builtin/flash_limit.wgsl`):
  - One 16×16 workgroup samples `post_color()` on a 64×64 grid, so it measures exactly what is shown, and builds 16 tiles × {relative luminance, BT.1702 red} = 32 tracks.
  - Each track holds a falling/rising state, its extreme and the times of the last four rises.
  - When a track has spent the budget, the frame's gain is set so the next rise peaks at 90 % of the threshold step (`:129-148`). Falls are never touched.
  - A new budget resets the tracks. A clock that runs backwards (the hourly `u.time` wrap, or a restarted render) **shifts the rises back** instead of forgetting them (`:117-126`).
  - The budget is 3/s Standard and 1/s Strict, also under OS reduced motion (`postprocess.rs:730-732`).
  - It also runs when post-processing is off, with neutral post params (`:299-306`, `:741`).
- **Cost control.** Per-pass `scale`, quarter-res bloom, `ParticleQuality` (`settings.rs:30`), dt clamped to 50 ms, thumbnails at a fixed size refreshed round-robin every 3rd frame (`executor.rs:72`). Profiler scopes cost nothing when the `profiling` feature is off (`gpu/profiler.rs:1-8`). There is **no** thermal or frame-time governor (ARCHITECTURE-NOTES, "D tiers": "no governor").

## 2. Contracts and invariants (fosfora)

1. **One frame function** for live, dissolve, headless and thumbnails (`frame_graph.rs`), and one per-frame prep (`frame_prep.rs`).
2. **Global parity.** Every feedback target flips in lockstep. Consumers bind by parity, never by a pass's own `current` (`pass_executor.rs:80-95`; `executor.rs:263-268`). The parity advances only once per frame, so a second execute in one frame (dissolve) re-renders with identical state.
3. **Delay edges are the only recursion.** Current-frame inputs form a DAG, and previous-frame inputs may point anywhere.
4. **Plans are pure functions of their keys.** Texture identity is part of the key, and steady state does zero allocations (I8).
5. **Last good wins.** A failed compile or plan keeps the previous pipeline or plan. Missing things are made *visible* (magenta), and unwired things are deliberate black, never stale.
6. **Feedback is born cleared.** No pass reads uninitialised memory.
7. **Base vs effective values.** Bindings write `LayerModulation` and `PostProcessModulation` overlays. Saves and the UI read the base value, the render reads the effective one (`layer.rs:368-394`; `format.rs:161-200`).
8. **Rates are integrated by the engine.** A shader never writes `time * modulatable`.
9. **Photosensitivity is measured on the output** and applied once, before every sink.
10. **Validate before pipeline.** naga for trama, wgpu error scopes for everything (`gpu/pipeline.rs:34-44`). Every shipped shader is compiled by a test through the *production* preamble (`loader.rs:147-156`; `pass_executor/tests.rs:1960`).

## 3. Excellent vs weak

**Excellent**
- Extracting `frame_graph` and `frame_prep` with headers that say *why*, plus a headless parity probe.
- Global-parity ping-pong with two-parity prebuilt bindings: it is correct for cross-pass and iterated reads, and covered by GPU probes (`pass_executor/tests.rs:352 passgraph_cross_pass_and_parity`, `:601`, `:692`).
- `prev_inputs` and `iterations`. They give a declarative fluid solver with no engine code (`fluvid.pfx`).
- Target ids as identity (`render_target.rs:17-28`), and plan keys that include the *texture identities*.
- A flash limiter that measures the output, survives clock wraps and is shared by every sink.
- Rates as a manifest field, plus a GPU probe that every rate param is integrated (`pass_executor/tests.rs:2504`) and a test that pins the strobe premise (`gpu/mod.rs:60-120`).
- Hot-reload hygiene: stamp de-dup, a degraded watcher is non-fatal, `PfxDiff` picks rebuild vs merge.
- Errors that show up on screen (magenta) and in the UI, without stopping the frame.

**Weak**
- `frame_prep.rs:55-125` keys behaviour that belongs to one effect (helix, splat, symbiosis) off param *slot numbers* (`p[0]*6+2` = species) inside the generic loop. This is a hidden per-effect ABI.
- The uniform block is one 400 B all-features struct copied per pass and per node (the arena stride is 448 B), with "reserved, 0.0 until the detector lands" tails. It is ABI churn by design.
- Missing context structs (`#[allow(clippy::too_many_arguments)]` on `PassExecutor::new`, `resize`, `execute_and_composite` and `build_plan`).
- The `@backdrop` input re-composites the layers below it for each consumer (`frame_graph.rs:215-225`).
- The trama pool gives every step its own target, with no lifetime aliasing.
- The docs are stale: TECHNICAL.md says "13 modes" and "74 features" (ARCHITECTURE-NOTES "Corrections").
- GPU tests are `#[ignore]` and only run on a developer box ("GPU renders stay dev-run").
- The blocking `recompile_pass` is left in as dead code (`pass_executor.rs:585`).
- There is no thermal or frame-time tiering.

## 4. Geode today

```
Renderer (core/viz/Renderer.hpp:49; ~150 members, owns everything by value)
 ├─ ShaderSource assets_ ── ProgramBinaryCache ── ProgramLoader ── ProgramStager (+ ScenePrep, pure)
 ├─ SceneRegistry (if-chain create, SceneRegistry.cpp:28-51) ── scenes_: vector<pair<id, unique_ptr<Scene>>>
 │     Scene (Scene.hpp:20): 8 pure virtuals + 19 hooks with no-op bodies
 │       ├─ ShaderScene (UniformCache, SoundUniforms, MotionField; shaderTime_ = ∫speed·dt)
 │       ├─ MilkdropScene (projectM → own FBO, pm_post_frag)
 │       └─ FluidScene/Silk/Life/Acid/Myco/Cymatics/Water/CurlFlow (SimPass: compute or fragment ping-pong)
 ├─ modulation: Director, NoteResponse, LfoEngine, AdsrEngine, Macros, ReactionEngine ×2, MotionField, safety::
 ├─ sound_: SoundEngine (stepped once per frame in beginFrame)
 ├─ Framebuffer fboA_ (scene), fboB_ (layer OR outgoing), bornFbo_     [RGBA8]
 ├─ TrailPass ─┐  CompositePass (one uber fragment pass: transition LRU-4 spliced programs, layer blend ×8,
 ├─ LiquidPass ┤                 grade, ripple, underlay, tint, dither)
 ├─ NoteBloomPass, UiBlurPass ─┘  no common interface
 ├─ CameraRig, TouchField, Overlays (RippleSim, FlowField)
 └─ ThermalGovernor (4 tiers: renderScale, optionalPasses, fpsCap) + user scale/march/postFx
```

Facts:
- **Frame order is hard-coded** (`RendererFrame.cpp:408-434`): `beginFrame` → `applyRenderScale` (tier or user change) → `resolveActiveScene` → `resolveParams` (the 10-stage modulation chain, `:120-226`) → fluid injection → `resolveLayerScene` → `stepOverlays` → `stepCamera` → `drawSecondaryTargets` (layer **or** outgoing into fboB) → `drawSceneTarget` → `drawLiquid` (may `drawBorn`) → `composite` (+ bloom) → `drawUiBlur`.
- **The "run a scene" protocol is copied four times**: `setParams → deliverPcm → update(gainAdjusted) → draw` at `RendererFrame.cpp:268-275`, `:279-284`, `:296-300` and `:548-552`.
- **The hooks are used unevenly.** `acceptPcm` is overridden by 7 scenes and `setTouchField` by 6. `setFlow`, `setPaletteLut`, `setCamera`, `drawsCamera`, `setFragmentSource` and `prepare` are ShaderScene only. The four `*Milk*` hooks are Milkdrop only. `isFluid`, `velocityTexture` and `setInjectionShaders` are Fluid only. `isWater` and `queueTouchStroke` are Water only, and `trailRetention` is CurlFlow only (grep of `override` in `core/viz/scenes/*.hpp`).
- **Uniform upload.** `ShaderScene::uploadParams`, `uploadMotion`, `uploadTouch` and `uploadCamera` (`scenes/ShaderScene.cpp:122-246`) make about 80 `glUniform*` calls per draw. Each goes through `set1f(const char*)` → `UniformCache::loc(const std::string&)` (`Program.cpp:10`), so every call builds a `std::string` and does a hash lookup. On top of that, each scene runs `SoundUniforms::upload` (`SoundUniforms.cpp:40`), which resamples three rows and does one `glTexSubImage2D` *per scene per draw*. With a layer, an outgoing scene or a born scene, that runs 2–3 times a frame.
- **The F11 rate bug is live.** `ShaderScene::update` already integrates time by speed (`shaderTime_ += p.speed*dt`, `ShaderScene.cpp:76`), and `MotionField` modulates speed every frame (`o.speed = p.speed*(0.7+0.5*energyRel)`, `MotionField.cpp:186`; `LfoTarget::Speed`, `Lfo.cpp:57`). Yet **43 shader files** compute `uTime * uSpeed`: all 12 `arbor_*`, all 12 `chrys_*`, all 12 `lucid_*`, and `lib_arbor`, `lib_bulb`, `lib_dive`, `lib_glass`, `lib_nebula`, `lib_terra`, `lib_truchet` (for example `arbor_bolt_frag.glsl:59`; `lib_arbor.glsl:13` documents it). The phase is therefore `uSpeed·∫uSpeed` and jumps by Δspeed × uTime. uTime wraps at 7100 s, so late in a play a tiny loudness-driven speed change moves the picture by seconds of animation in one frame. That is exactly the strobe fosfora measured.
- **Flash safety is parameter-only, and the budget does nothing.** `composite()` calls `flashBudget_.gainFor(timeSeconds_, safety::flashImpulse(fx.flash, 0.0f))` (`RendererFrame.cpp:336`). `flashImpulse` returns `flash * beatImpulse * depth` (`VisualSafety.hpp:24`), so the impulse is always 0 and the gain always 1. `FlashBudget::gainFor` also *resets* when time goes backwards (`VisualSafety.cpp:79`), which is the wrap behaviour fosfora explicitly rejects. Nothing measures the pixels. MilkDrop presets (third-party content), Difference/Add layers (clamped only by `safety::layerMix`) and any style's own beat-driven brightness are not limited.
- **Already ahead of fosfora:**
  - staged, budgeted program builds with `KHR_parallel_shader_compile` (`ScenePrep.hpp`, `ProgramStager.hpp`), and a program binary cache;
  - a thermal and frame-time governor with tiers (`ThermalGovernor.hpp`);
  - compute-or-fragment `SimPass` chosen by `GlProfile` (`compute/SimPass.hpp`);
  - tile-aware `glInvalidateFramebuffer` (`Framebuffer.cpp:47-53`, `TrailPass.cpp:52-57`) and clear-on-bind;
  - one opaque uber composite;
  - live and export share `Renderer::render`.
- **The include allow-list** (`ShaderSource.cpp` `kIncludes`) and glslcheck's uniform table are hand mirrors of the uniform upload (`tools/glslcheck/README.md`: "The table mirrors `ShaderScene::uploadParams`…").

## 5. Recommendations for Geode

### R1 — Integrate the 43 `uTime * uSpeed` sites (priority P0, cost S)
- **What.** In `ShaderScene::update`, also integrate `speedPhase_ += p.speed * p.speed * dt` (f64, wrapped like `shaderTime_`). Upload it as `uSpeedTime` and replace `uTime * uSpeed` with `uSpeedTime` in the 43 files.
- **Behaviour.** At a speed held constant since the scene started, `∫s²dt = s·(s·T)`, so the look is identical. Under modulation the per-frame step is bounded by `s²·dt` and no longer jumps by Δs·T.
- **Lint.** Add a check to `tools/check_shaders.py` that refuses `uTime\s*\*\s*\(?u(Speed|Warp|Morph|Twist|Sway|Turbulence|Intensity|Knob)`. Fosfora's "phase-locked source lint" is the precedent.
- **Test.** In `core/tests/viz_math_test.cpp`, port fosfora's `accumulated_orbit_stays_smooth_when_the_rate_is_modulated` (`gpu/mod.rs:60-120`): 10 min at a steady speed, then beat-swept speed. Assert the worst per-frame step is at most `dt·max²` for the integral, and above 1 for the old form. Add `glslcheck --shader arbor_grove` at speed 1 to show identical pixels.
- **Constraints.** None. Free on the GPU.

### R2 — Measure flashes on the output, on GLES 3.0 without compute (P1, M)
**The pure part.** `core/viz/FlashLimiter.hpp`:
```cpp
struct FlashTracks {                              // 16 tiles × {luma, red}; a pure port of flash_limit.wgsl
    struct Track { bool rising; float extreme = 1e9f; std::array<float,4> rises{-1e9f,-1e9f,-1e9f,-1e9f}; int slot = 0; };
    std::array<Track, 32> t; float lastT = -1.0f, budget = 0.0f;
    float step(const std::array<float,32>& metric, float now, float budget);   // returns gain; carries rises on wrap
};
```

**The GL part.** `FlashLimitPass`, in GLES 3.0 terms. This is fosfora's trick of measuring the composite before it is shown, using fragments instead of compute:
1. Run `CompositePass::draw` a first time into a 64×64 RGBA8 FBO, with the same `Inputs`. That is 4,096 fragments, which costs very little.
2. A 32×1 "track" fragment pass reads 16 texels per tile with `texelFetch`, keeps its state in an RGBA16F ping-pong of 32×3 texels, and writes the gain into texel (0,0). Half-float render targets come from `GlProfile.formats` (FluidBuffers already probes this). If they are missing, read the 64×64 back through a PBO one or two frames late and step `FlashTracks` on the CPU. The fallback admits the first over-budget rise late but still bounds the count.
3. The real composite reads `uFlashGain` with `texelFetch` (no CPU stall) and multiplies the colour.

**Placement and settings.** Run it before `NoteBloomPass`, whose blooms are budgeted by NoteResponse. Run it in the offscreen export as well, so video equals screen. The budget is 3/s, or 1/s under `setReducedMotion`, like `FLASH_BUDGET_STRICT`.

**Also fix.** Fix `RendererFrame.cpp:336` (delete the dead `FlashBudget` or feed it), and the wrap reset at `VisualSafety.cpp:79`.

**Tests.**
- `viz_math_test`: a 10 Hz 0↔0.5 luminance square wave is held to ≤3 rises per rolling second after gain; a 2 Hz wave passes with gain 1; a clock wrap at t = 7100 keeps the rises; red-only flashes count.
- `glslcheck --flash` renders a synthetic strobe through the GLSL track pass and compares the gains with the C++ sequence.

**Constraints.** It costs one extra small composite and one FBO switch. On tile GPUs, clear the 64×64 FBO on bind and invalidate it after use. No compute is needed, so it works on GLES 3.0 Mali-T and Adreno 3xx.

### R3 — Slim `Scene` with capability interfaces (step 5) (P1, M)
```cpp
struct FrameIn { const SceneParams& p; const GeodeSoundFrame& sound; float dt, time; };   // sound already gain-trimmed
class Scene {
public:
    virtual ~Scene() = default;
    virtual const std::string& id() const = 0;
    virtual SceneFamily family() const = 0;
    virtual void init() = 0;  virtual void prepare() {}
    virtual void resize(int w, int h) = 0;
    virtual void step(const FrameIn& f) = 0;          // was setParams + update
    virtual void draw(const FrameIn& f) = 0;
    virtual void release() = 0;
    // capabilities: one accessor each, nullptr = not supported (no -frtti dependency)
    virtual PcmConsumer*     pcm()      { return nullptr; }   // 7 scenes
    virtual TouchReceiver*   touch()    { return nullptr; }   // 6
    virtual ShaderStyle*     shader()   { return nullptr; }   // flow, LUT, camera, drawsCamera, fragment source
    virtual MilkdropControl* milkdrop() { return nullptr; }   // 4 milk hooks
    virtual FlowSource*      fluid()    { return nullptr; }   // velocityTexture, injection shaders
    virtual WaterSurface*    water()    { return nullptr; }   // queueTouchStroke
    virtual TrailKeeper*     trail()    { return nullptr; }   // trailRetention (CurlFlow)
};
```
- **Shared call path.** Add `Renderer::runScene(Scene&, const SceneParams&, float dt, Framebuffer& into)` and use it at the four copies (`RendererFrame.cpp:268`, `:279`, `:296`, `:548`).
- **Registry.** `SceneRegistry::create` becomes a `constexpr` table of `{catalogue::Kind, factory}`.
- **Behaviour.** None.
- **Tests.** Host build via `tools/hostlink`, `viz_math_test` unchanged, emulator journeys. Add one `static_assert` per scene type that its capabilities match the catalogue kind, for example `Kind::Water ⇒ water() != nullptr`, checked in a host test that builds every scene with a null GL loader.

### R4 — `Pass` plus a pure `FramePlan` (step 6) (P1, M→L)
Fosfora's real lesson is the **keyed plan with no allocation in steady state**, not a general DAG. Geode's passes are fixed and few, so keep the plan table-driven:
```cpp
enum class Res : uint8_t { SceneA, SceneB, Born, Liquid, FlashMini, Target, Count };
enum class PassId : uint8_t { Secondary, Scene, Trail, Born, Liquid, FlashLimit, Composite, NoteBloom, UiBlur, Count };
struct FrameFlags { bool layer, outgoing, trail, liquid, born, flashLimit, bloomLive, uiBlur, offscreen; };   // from FrameState
struct FramePlan {
    struct Step { PassId pass; std::array<Res,3> reads; Res write; bool clearOnBind; Res invalidateAfter; };
    std::array<Step, size_t(PassId::Count)> steps; uint8_t count; uint32_t key;
};
FramePlan planFrame(const FrameFlags& f);          // pure, constexpr-friendly, host-tested
class Pass {
public:
    virtual ~Pass() = default;
    virtual PassId id() const = 0;
    virtual bool create(const PassBuild& b, std::string* error) = 0;   // ShaderSource, cache, fadeVert, GlProfile
    virtual void run(const FrameState& s, const PassIo& io) = 0;      // io: input textures, output fbo, w, h
    virtual void release() = 0;  virtual void forget() = 0;           // forget = context lost, no GL calls
};
```
- **How it runs.** `Renderer::render` becomes `beginFrame → resolve → step sims → plan = cache.get(flagsOf(state)) → for step: bind/clear → pass.run → invalidate`. The plan is cached by `key` (fosfora's `ExecPlan` key), so a frame allocates nothing.
- **Composite stays one uber pass.** On tile GPUs every extra full-screen pass is a full tile load and store, so `CompositePass` must not be split.
- **Profiling.** Every `run` gets an `EXT_disjoint_timer_query` scope when available, as fosfora's `ProfilerHandle` does.
- **Behaviour.** None.
- **Tests.**
  - Host: enumerate all 2⁹ flag sets and assert that Composite runs exactly once; nothing reads a `Res` before it is written in the frame (Liquid's own feedback is internal to `LiquidPass`); UiBlur is never in an offscreen plan; Born only appears with Liquid; every transient `Res` is invalidated after its last reader; FlashLimit precedes Composite.
  - Device: the export thumbnails (`StyleThumbnailRenderTest`) are byte-equal before and after.

### R5 — One `std140` block per scene instead of ~80 `glUniform` calls (P2, M)
- **Declaration.** In `lib_scene_uniforms.glsl`, declare `layout(std140) uniform GeodeScene { float uTime; vec2 uResolution; … float uKnob[6]; … };` with **no instance name**. Block members are then in global scope, so all 227 fragment sources stay source-compatible.
- **C++ side.** Add `struct alignas(16) SceneBlock` with `static_assert(offsetof(...))` per std140 member. ShaderScene fills it in `step` and uploads it with one `glBufferSubData` per draw.
- **Sound.** A second block, `GeodeSound { vec4 uSound[kVec4s]; vec4 uSoundEvents; vec2 uSoundCounts; float uSoundDrive; }`. Upload `uSoundTex` once per *renderer* frame and share it, instead of once per scene per draw, wherever the gain trims are 1.
- **Fallback.** If `glGetUniformBlockIndex(program, "GeodeScene") == GL_INVALID_INDEX` (a user custom shader that declares its own uniforms), keep the legacy `set1f` path. Change `UniformCache::loc` to take `const char*` with a heterogeneous lookup, which removes the per-call `std::string`.
- **Tests.** `check_shaders.py` (glslc compiles the blocks); a host `static_assert` table; glslcheck learns the block; device GPU frame-time A/B in `docs/DEVICE_CHECKS.md`.
- **Constraints.** GLES 3.0 guarantees 12 fragment blocks of 16 KB or more. std140 pads `float[6]` to 96 B, which is fine. Measure on Mali-G52 and Adreno 610: the win is CPU on the GL thread, not GPU.

### R6 — Per-style manifests in the shader (F10) (P2, cost L because of the UI)
Put a header comment in each `_frag.glsl`, the way trama does, in a line format parsable without JSON (the core has no JSON library, and `org.json` is a stub in JVM tests):
```glsl
//! style arbor_bolt family=arbor kind=shader loop=free
//! control branch  float 0.5 0 1   -> uStyle[0]
//! control spark   float 0.3 0 1   -> uStyle[1]   rate
//! uses uMorph uWarp uKnob          (generated: do not edit)
```
- **Controls.** `uStyle` is a `vec4[2]` in the `GeodeScene` block, packed in declaration order, like fosfora's `params[16]` and `param(i)`. `rate` marks a control integrated in C++ (fosfora `RateState`, f64).
- **Generated `uses`.** The `uses` line is generated in CI by `check_shaders.py` from glslc/SPIR-V reflection of the *active* uniforms. That replaces the hand-kept `ParamScope` relevance for "no dead controls". ShaderScene already infers `drawsCamera_` from `loc("uCamBasis") >= 0` (`ShaderScene.cpp:276`), and this generalises it.
- **Other wins.** `catalogue.txt` gains the rows. glslcheck's uniform table and the `kIncludes` list can be generated from the same scan. `loop=phase_locked` could mark styles that are safe for `LoopRender`.
- **Behaviour.** Customize shows the style's own controls. This is the owner's open question 3.
- **Persistence.** Presets keep controls by name and type, as fosfora restores params on a rebuild (`app.rs:1943-1957`).
- **Tests.** `catalogue_test` against the generated rows; a generator check in CI; a preset codec test for name and type matching.

### R7 — A declarative multipass for styles (P3, L)
Only if styles start needing feedback without C++. Adopt fosfora's `PassDef` subset `{name, shader, scale, inputs, prev_inputs, iterations, feedback}` with **global parity** and clear-on-create. It would replace one-off hooks such as `trailRetention`.
- **GLES costs.** Each pass is an FBO switch, so cap a style at 4 passes and 16 iterations, and scale iterations by `ThermalTier`.
- **Formats.** RGBA8 unless a pass asks for `half`, gated on the format probe.
- **Tests.** Ports of `passgraph_cross_pass_and_parity` and `passgraph_prev_input_reads_previous_frame` on glslcheck.

### R8 — Layers: take the modulation split, not the stack (P3, S)
Keep one layer plus the underlay. On a phone, N full scene draws cost far more than they are worth. Take `LayerModulation`'s base-vs-effective split for `layerMix`, `blend` and the underlay amount, so routes never overwrite saved values. That is plan step 7.

### R9 — Make the frame prep GL-free (P2, M; enables the step 1 and step 7 goldens)
Extract `resolveParams` and its engines (`RendererFrame.cpp:120-226`) into a `FrameResolver` with no GL members. A host golden pins SceneParams for N frames of synthetic sound at a fixed dt. This is the `frame_prep.rs` lesson: unprobed per-frame prep drifts silently.

## 6. Do not copy

- **HDR `Rgba16Float` everywhere** (`context.rs:206`). On tile GPUs it doubles bandwidth. Keep RGBA8 scene targets, with half float only for feedback and liquid, as now.
- **The parity `[BindGroup;2]` machinery.** GL binds textures per draw, so parity is just an index into two texture ids.
- **A background compile thread** (`shader_compiler.rs`). GL needs a shared EGL context, and `ProgramStager` with `KHR_parallel_shader_compile` is the better fit for Android.
- **The 400 B all-features uniform ABI** with reserved tails. `lib_sound` plus a schema (plan step 3) is already better.
- **trama's user-authored DAG, Kahn's planner, egui canvas and 20 blend or displacement modes.** A phone is not a patch bay (`MOD_MATRIX_UX.md`).
- **The `@backdrop` re-composite per consumer, and stringly special inputs (`"@…"`).**
- **The compute-shader limiter as written.** GLES 3.0 devices have no compute; port it as fragment passes (R2).
- **The `frame_prep.rs:88-120` pattern** of effect-specific meanings for generic param slots.
- **The `App` god object**, and signatures with nine or more arguments: pass context structs instead.
