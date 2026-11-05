# 05: Fosfora's GPU particle and simulation systems, compared with Geode

Fosfora checkout at commit 2e85d3b, called `F/` below. `F/P` = `F/crates/fosfora-app/src/gpu/particle/`, `F/S` = `F/assets/shaders/`. Geode is at HEAD 37761e8. The Graphify index is at 7ab67cb. None of the files it lacks are particle or simulation files: the diff is NoteBloom, Catalogue and Params. I read the files directly anyway.

**Graph check** (Graphify `tessie1993/fosforatess` at 2e85d3b):

- `frame_gain` (Rust) has exactly one caller, `ParticleSystem::update_composite_gain` (`system.rs:1901`). Its WGSL twin is `F/assets/shaders/lib/chronoflow.wgsl:119`.
- The graph lists only test callers of `ParticleSystem::dispatch` and none of `render`, because the call goes through an `Option` binding. Grep found the production calls at `F/crates/fosfora-app/src/gpu/pass_executor.rs:370` and `:436`.
- Graphify links no tests to `spatial_hash.rs`. Its GPU tests live in `system.rs` (`offsets_are_starts_and_the_query_reads_the_right_cells`, `:6588`).

## 1. How it works

### 1.1 Shape

There is one `ParticleSystem` per effect layer (`F/P/system.rs:79`, 6956 lines). It owns:

- the particle store;
- every optional producer: reaction-diffusion, the physarum trail field, shallow water, the obstacle fluid, the spatial hash, sort, compute raster, the sorted splat path, volumetric, Lattice, Helix and WBOIT;
- every source: image, video, webcam, model, splat, text and morph.

Each of these is an `Option<…>` field, and they are mutually exclusive by convention.

`PassExecutor::execute_profiled` runs one effect frame in this order (`F/crates/fosfora-app/src/gpu/pass_executor.rs:367-438`):

1. particle compute;
2. the effect's fragment passes, cleared each frame;
3. the particle render, drawn into the last pass's target with `LoadOp::Load`.

That last target is the feedback texture. Next frame's background therefore re-reads the particles, so trails come from the effect's own `k·prev` feedback rather than from a particle trail buffer. Bloom and post run on top (`F/docs/TECHNICAL.md:381`).

There are two layers of code:

- **Engine:** buffers, emission, the forces library `F/assets/shaders/lib/particle_lib.wgsl` (859 lines, prepended to every sim), hash, sort, raster and resolve.
- **Per effect:** a `.pfx` JSON `particles` block (`F/docs/TECHNICAL.md:282-339`) plus, in 33 of the 36 particle effects, a custom `*_sim.wgsl`. The sims and backgrounds total about 7.9k WGSL lines in `F/S/*_sim.wgsl`. A custom sim reads 16 `param(i)` slots (`particle_lib.wgsl` `param()`) and the 992-byte `ParticleUniforms`, whose size is pinned by a test (`F/P/types.rs:1200`). `F/docs/TECHNICAL.md:231` still says 128 bytes with 10 audio fields, which is stale.

### 1.2 Buffers and bandwidth

The particle store is SoA: four `vec4f` arrays, `pos_life`, `vel_size`, `color` and `flags`. That is 16 bytes each and 64 bytes per particle, and the whole set is ping-ponged (`system.rs:84-88`, `particle_lib.wgsl` `struct Particle`).

The point of the split is bandwidth. Consumers that only need positions, such as the spatial-hash count and scatter, bind only `pos_life`. Sort keygen binds only `vel_size`. They load 16 bytes instead of 64 (`F/docs/TECHNICAL.md:229`, `F/CHANGELOG.md:1444-1448`). Total memory is unchanged.

Other buffers:

- **Counter buffer:** four atomics, `[alive, dead, emit_used, aux_emit]` (`system.rs:551-563`).
- **Alive index list:** ping-ponged.
- **Dead index list:** CPU-filled with 0..N (`system.rs:586-594`). No shader reads `dead_indices` or `counters[1]` (grep of `F/S`), so this is vestigial.
- **Aux:** 16 bytes per particle (home xy, packed RGBA, gradient or sprite index), ×4 for morph's four targets (`system.rs:616-635`).
- **Indirect args:** 16 bytes.

At creation every buffer is GPU-cleared with `clear_buffer`. Without that, recycled VRAM held "alive" particles that blew out through additive feedback (`system.rs:514-545`).

Capacity is clamped three ways:

- to `max_storage_buffer_binding_size / 16`;
- to `max_compute_workgroups_per_dimension × 256` (`system.rs:488-508`);
- in `build.rs`, by the largest per-particle binding: the compute raster's `sorted_particles` at 9×4 bytes, or morph's aux at 64 bytes (`F/P/build.rs:17-26,50-62`).

The sim's group 0 has 13 bindings: one uniform and 12 storage buffers (`system.rs:680-700`). The device requests 16 storage buffers per stage and 5 bind groups (`F/crates/fosfora-app/src/gpu/context.rs:83-86`).

### 1.3 Passes per frame

These are the steps of `ParticleSystem::dispatch` (`system.rs:1966-2300`):

- Reset the counters with a `queue.write_buffer` of 16 zero bytes, then upload uniforms (`:1968-1971`).
- 0a. Gray-Scott reaction-diffusion, 1–32 steps per frame, scaled by beat and `sim_speed`. Its parameters are clamped under the saddle-node boundary `k < sqrt(f)/2 − f − 0.003` (`:1976-2040`).
- 0a2. Physarum trail-field diffuse and decay: 12 channels, decay held at 0.74–0.94 "so highways never become an absorbing state" (`:2043-2080`).
- 0a3. Shallow-water "virtual pipes" over the obstacle height field (`F/P/water.rs:1-10`, `:2082`).
- 0a4. Obstacle fluid: advect, divergence, 40 Jacobi iterations by default, project (`F/P/fluid.rs:1-12,40-50`, `:2088`).
- 0b. Spatial hash: count (atomics), then a single-workgroup Blelloch scan of up to 65,536 cells, a copy to a cursor, then scatter (`F/P/spatial_hash.rs:370-421`). The grid aims at about 16 particles per cell, clamped to 40–256 per side, or 8–64 cubed in 3D (`spatial_hash.rs:26-52`). Grid constants are patched into the sim source as `const` (`spatial_hash.rs:560-565`).
- 1. The sim, `workgroups = max_particles/256`. It always scans the full capacity, not only the live particles (`:2108-2138`).
- 1b. An optional bitonic sort on size, disabled above 65,536 particles. At 1M it would take 210 dispatches (`:1181`, `F/CHANGELOG.md:1516`).
- 2. `prepare_indirect`: a single-thread shader writes `[6, alive, 0, 0]` (`F/S/builtin/particle_prepare_indirect.wgsl`).
- 4. Exactly one of the following (`:2215-2289`):
  - Helix sweep;
  - Lattice CA steps;
  - volumetric scatter, resolve and march;
  - the splat counting sort;
  - the compute raster, tiled at 50K live particles or more (`F/P/compute_raster.rs:13,538`), else direct.
- 5. Copy the counters to staging for an async readback. The CPU's `alive_count` is at least one frame late and only chooses tiled or direct raster (`:2290-2340`). The draw path never waits for the CPU.

Render (`system.rs:2365`) picks one of: the raymarch, the sorted splats, the compute-raster resolve, WBOIT (two passes into RGBA16F accum and R8 reveal), or instanced billboards. The billboards use vertex pulling: six vertices per instance, `alive_indices[instance]`, and no vertex buffer (`F/S/builtin/particle_render.wgsl`). The fragment draws a Gaussian `exp(−2d²)` disc and discards below 0.01, or samples a sprite-atlas frame chosen from age over lifetime.

### 1.4 Emission

This happens in `update_uniforms` (`system.rs:1726-1749`):

- `emit_accumulator += rate·dt`;
- on `beat > 0.5`, add `burst_on_beat`;
- `emit_count` is the integer part, and the fraction carries to the next frame.

In the sim, each dead thread calls `emit_claim()` (`atomicAdd(counters[2])`) and spawns if `slot < emit_count`. Each live particle calls `mark_alive()`, which appends to the alive list (`F/S/builtin/particle_sim.wgsl`, `particle_lib.wgsl` `emit_claim` and `mark_alive`). Death happens when `age ≥ lifetime`.

Emitter shapes are point, ring, line, screen, disc, cone and image (`particle_sim.wgsl` `emit_particle`). Spawns carry per-particle variance in speed, life and size, plus inheritance of the emitter's velocity.

### 1.5 Forces

`apply_builtin_forces` (`particle_lib.wgsl`) applies, in order:

- gravity and wind;
- drag as `pow(drag, dt·60)`;
- FBM curl or turbulence noise, or legacy hash noise;
- point attraction;
- a vortex with smoothstep falloff;
- a pre-baked 64³ curl-noise 3D texture in RGBA16F (`F/P/flow_field.rs:6-9`).

The flow-field term is added without `dt`: `v += sample_flow_field(pos)`.

Obstacle collision bisects the step four times to find the contact point, nudges the particle out by 0.002, and reflects in screen space so non-square viewports keep their angles. The modes are bounce, stick, flow (tangential) and contain (`particle_lib.wgsl` `apply_obstacle_collision`). Lifetime curves are 8-point LUTs, and colour gradients hold up to 8 hex colours.

### 1.6 Audio coupling

`update_audio` copies about 60 scalars per frame into the uniforms: 7 bands, rms, kick, onset, centroid, flux, beat, `beat_phase`, `bar_phase`, 13 MFCCs, 12 chroma values, HPSS, buildup and drop (one frame), stereo and per-band pan, BPM and `beat_strength` (`system.rs:1910-1963`). The CPU decides only the burst. Everything else is per-effect WGSL. For example:

- Murmur scales cohesion by `mfcc(1)` and mids, and disorder `eta` by bass (`F/S/murmur_sim.wgsl:175-180`).
- Flux scales flow by bass and mids, snaps on beat and kicks on onset (`F/S/flux_sim.wgsl:81-107`).
- Image scatter is a spring with `K=12` and damping `0.85^(dt·60)`. It is kicked on beat by `0.12·(1 + 0.5·kick)` (`F/S/builtin/image_scatter.wgsl:16-20,66-78`).

### 1.7 Quality tiers and budgets

Quality is a global multiplier: Low 0.25×, Medium 0.5×, High 1×, Ultra 2×, Max 4× (`F/crates/fosfora-app/src/settings.rs:30-66`). It scales `max_count` and `emit_rate` together "so fill time stays consistent", with a per-effect `max_scaled_count` cap (`build.rs:38-48`, `F/CHANGELOG.md:1257-1261`). Changing quality rebuilds the effect.

The built-in effects default to 1–2M particles. The `raster_stress` effect runs 10M (from the `.pfx` table). Other automatic limits:

- trails switch off above 500K (`system.rs:3569`);
- the Auto render mode chooses the compute raster when there are at least 100K particles, no sprites or trails, size ≤ 0.005 and no WBOIT (`system.rs:1234-1240`).

### 1.8 Compute raster

Each pixel has four atomic `i32` buffers with 4096 fixed-point scaling. The changelog's estimate is "about 1000 overlaps per pixel". The kernel has three tiers: one pixel at radius 1 px or less, bilinear 2×2 up to 1.5 px, and a Gaussian beyond that, capped at 8 px (`F/S/builtin/compute_raster_draw.wgsl:36-110`).

The tiled path bins particles into 16×16 tiles, prefix-sums the bins and scatters into a 9× buffer, then accumulates each tile in 6 KB of workgroup memory before plain stores (`F/S/builtin/compute_raster_tiled.wgsl:36-48`). The resolve is a fullscreen fragment pass that reads the SSBOs and applies a Reinhard tonemap in additive mode (`F/S/builtin/compute_raster_resolve.wgsl`).

### 1.9 The other subsystems

- **Spatial hash consumers:** Murmur takes K=7 topological neighbours from 9 cells, at most 24 per cell, starting at a random point in each cell (`F/S/murmur_sim.wgsl:26,186-240`). Symbiosis is particle life with an 8×8 force matrix in the uniforms and at most 48 per cell.
- **Splat (3DGS):** a 32-byte record with log-scales (`F/P/splat.rs:1-23`). It is sorted by a 16-bit counting sort in about four dispatches (`F/P/splat_sort.rs:8-13`).
- **Volumetric:** each particle atomically adds into a 64³ `u32` grid with a hashed fake Z, followed by a 3³ blur, a manual-trilinear r32float march and Beer-Lambert absorption (`F/crates/fosfora-app/src/gpu/volumetric.rs:1-37`, `F/S/builtin/volumetric_scatter.wgsl`).
- **Lattice:** a 3D cellular automaton, 32–256³ with 128³ the default, Moore or von Neumann, rule strings, EMA display and auto-reseed on stagnation. Its frame-stable step budget is in `F/crates/fosfora-app/src/gpu/lattice.rs:1-74,210-223`.
- **Helix:** a 32 Hz CPU ring of 256 audio slices, about 8 seconds, swept into a volume each frame (`F/crates/fosfora-app/src/gpu/helix.rs:1-48`).
- **Depth:** MiDaS v2.1-small, an ONNX model at 256², runs on a background thread over the webcam. Its output feeds the obstacle texture, not particle positions (`F/crates/fosfora-app/src/depth/estimator.rs:5-14`, `F/P/types.rs:769-776`, `F/crates/fosfora-app/src/app.rs:1545-1559`). The model is downloaded at runtime (`F/crates/fosfora-app/src/depth/model.rs:9`).
- **World path:** an XR-only world-space renderer (`F/P/world.rs:1-10`).

### 1.10 Measured claims

All the performance claims are anecdotal entries in the changelog. `F/bench/` scores only audio, and `F/docs/BENCHMARKS.md` has no GPU numbers. The claims:

- Tide: about 214K particles above 100 FPS with trails (`F/CHANGELOG.md:916`).
- Polycephalum: about 59 FPS with 500K agents (`:943`).
- Splat: 800K at 1080p went from 314 to 721 FPS by fitting the quad to the splat (`:880`).
- Tiled raster: "~100× faster atomics" (`:1270`).
- Frame-rate spread, which is measured: Cascade's brightness spread across 30, 60 and 120 fps fell from 47.7% to 6% (`:493-494`), and a later fix took another effect from 59% to 1% (`:501`).

The RTX hint is `system.rs` "~1 in 6 on the RTX".

## 2. Contracts and invariants

1. **Ping-pong side.** The sim reads `current` and writes `1−current`. The render, compute raster and volumetric read `output_idx = 1−current`. `last_output` is stored absolutely, so render order does not matter (`system.rs:89-94,2207,2216`).
2. **The draw path never waits on a CPU readback.** Instance count comes from GPU indirect args. The CPU's alive count is a hint that may be stale (`:2278,2290`).
3. **Counters are zeroed every dispatch.** `aux_emit` (Cleave's second budget) depends on it (`:1968`).
4. **Bind-group layouts never change at runtime.** Group 2 always has a trail buffer, a 16-byte dummy when trails are off, because a sim referencing it otherwise failed validation every frame (#1921, `system.rs:188-202`). Empty group 3 is bound to keep group 4 contiguous (`:2122-2125`).
5. **Slot meaning is per sim.** `pos_life.z` is an angle only in the built-in sim with spin. Other sims keep a size, depth or generation there, so the renderer gates spin (`types.rs:437-441`, `particle_render.wgsl` spin). Species live in `flags.z` (`particle_lib.wgsl` "Species convention"). The world layout is "documented, not enforced".
6. **Frame-rate invariance, bit-exact at 60 fps.**
   - drag is `pow(drag, dt·60)`;
   - the trail ring advances on a fixed 60 Hz clock and backfills skipped slots, clamped to one full ring (`system.rs:1846-1873`, `particle_lib.wgsl` `trail_write`);
   - the additive composite gain is `frame_gain = (1 − kⁿ)/(1 − k)` with `n = dt·60` clamped to 2, returning the input unchanged when `n == 1` (`types.rs:1051-1078`);
   - the gain applies to additive output only. Alpha blending's steady state couples source and destination, so no source scale fixes it (`compute_raster_resolve.wgsl` header);
   - the frame `dt` is clamped to 50 ms (`app.rs:946`).
7. **Each `.pfx` that composites over feedback declares its retention `k`** (`CompositeDecay`). A test parses every `*_bg.wgsl` and fails if the declaration and the shader disagree (`types.rs:1000-1003`).
8. **Device limits are clamps, not crashes** (`system.rs:488`, `build.rs:50`).
9. **Integer hashing for anything indexed by particle.** The fract-sin hash collapses beyond about 1e4, and f32 ULP makes `hash(s)` and `hash(s+1)` identical at seed 3.5e7 (`particle_lib.wgsl` `uhash` comment). The fix for the "immortal emitter" is `F/CHANGELOG.md:811`.

## 3. Excellent vs weak

### Excellent

- **A GPU-driven lifecycle.** The alive list, atomic emission and indirect draw keep the CPU at O(1) per frame.
- **SoA laid out by who reads what.** Each consumer binds only the arrays it needs.
- **Steady-state brightness reasoning.** `frame_gain`, the 60 Hz ring, the bit-exact 60 fps passthrough, and the "a/(1−k)" note in the authoring tips (`F/docs/TECHNICAL.md:241`) make brightness and trail length frame-rate independent. The results are measured (§1.10).
- **Hard-won numeric lessons written next to the code**: hash precision, the obstacle's aspect-correct reflection, and log-scale splat covariance (`splat.rs:11-23`).
- **The best algorithm for each job**: a counting sort, not bitonic, for 3DGS; single-workgroup Blelloch scans; tile shared memory for dense raster.
- **Pluggable producers.** Particle scatter, Lattice and Helix are three density producers sharing one ray marcher (`volumetric.rs:15-19`).
- **GPU tests on a real device**: validation of sim dispatch, trail-zero, spatial-hash cell offsets and world projection (`system.rs:5762-6956`).
- **Quality scales count and emit rate together**, so fill time stays constant.

### Weak

- **A god struct.** About 150 fields, with exclusive modes as `if let` chains in both `dispatch` (`:2215-2289`) and `render`.
- **Dead code bound every frame.** `dead_indices` and `dead_count` are uploaded and bound but never read.
- **Cost scales with capacity, not population.** The sim and the compute-raster draw dispatch `max_particles` threads (`:2110`, `cr.dispatch_draw(…, max_particles)`), even though live counts are known on the GPU.
- **Draw order is nondeterministic.** `mark_alive` appends in atomic-race order, so alpha-blended billboard effects (Etch, Pegboard: `"blend":"alpha"`, billboard) composite in a different order each frame.
- **Known defects kept for the look.** The banded `rand_vec2` stays because "the artefact is load-bearing" (`particle_lib.wgsl` `rand_vec2` comment). Builtin flow-field force skips `dt`.
- **Brightness is not compensated for count.** Ultra simply gets brighter, unlike Geode's `particleExposure`.
- **Docs drift**: the 128-byte uniform claim. Performance claims are not reproducible: there is no GPU benchmark.
- **Two looks for one effect.** The compute raster's Reinhard resolve and the billboard's raw additive path look different, and the path is chosen by particle count (Auto).

## 4. Geode today

### Particles exist in exactly two places, both fragment and texture-state

**`fluid::Particles`** (`core/viz/fluid/FluidParticles.{hpp,cpp}`) is used by `FluidScene` and `CurlFlowScene`.

- **State:** a `DoubleMrt` of two RGBA32F textures (RGBA16F if that is all there is): A = (pos, vel) and B = (age, ttl, emitter, seed). That is 32 bytes per particle per side (`FluidParticles.cpp:16-25`, `fluid_particle_update_frag.glsl:11`).
- **Update:** one fullscreen pass over a `side×side` target with `discardContents` before drawing, which suits tile-based GPUs (`FluidParticles.cpp:194-199`). The kernel:
  - takes a midpoint RK2 step through the solver's velocity texture;
  - applies inverse-square catch points with a soft cap;
  - recycles the particle at the current spawn choreography;
  - adds an analytic psrdnoise curl stir in calm water.

  It samples the noise twice per particle because of the midpoint rule (`fluid_particle_update_frag.glsl:51-140`).
- **Render:** a static VBO of texel coordinates. Either `GL_POINTS` with vertex-texture fetch, clamped to 1–12 px with a sub-pixel dimming hold, or instanced four-vertex motion-blur streaks where the tier allows (`fluid_particle_vert.glsl`, `FluidParticles.cpp:164-168`). Blending is `ONE, ONE`.
- **Population:** every particle is always alive. There is no emission budget, no burst, and no alive list.

**`MycoScene`** runs physarum agents in a 160–208² state texture (`core/viz/scenes/StyleCatalog.cpp:103-114`). They deposit into a 384² trail through a `GL_POINTS` additive scatter (`MycoScene.cpp:150`, `MycoScene.hpp:35`). This is the ES 3.0 equivalent of Fosfora's atomic deposit.

### Budgets (`core/viz/fluid/FluidQuality.hpp:24-30`)

| Tier | Velocity grid (simRes) | Dye grid (dyeRes) | Tracers (side² from particleSide) | Pressure passes | MacCormack | Curl Flow tracers |
|---|---|---|---|---|---|---|
| Ultra | 288 | 1152 | 1024² ≈ 1.05M | 28 | yes | 640² |
| High | 224 | 896 | 768² | 24 | yes | 512² |
| Medium | 128 | 512 | 512² | 20 | yes | 320² |
| Low | 96 | 384 | 320² | 16 | no | 224² |
| Min | 64 | 256 | 160² ≈ 25.6K | 12 | no | 160² |

`FluidSceneBase::autoQualityTick` adds downgrade steps from `PerformanceMonitor` (`FluidSceneBase.cpp:59-69`, `ThermalGovernor.hpp:15-40`). However, `FluidScene::onApplyQualityTier` re-creates particles only when `userChanged` (`FluidScene.cpp:41`). An automatic downgrade therefore shrinks the grids but keeps the tracer count.

`particleExposure(count, ref) = (ref/count)^0.75` (`FluidMath.cpp:122-125`) is applied in `CurlFlowScene.cpp:122` only, not in `FluidScene`.

### The simulation scenes and their class structure

- `FluidSceneBase : Scene` (`FluidSceneBase.hpp:17`) holds the clock, idle synthesis, the quality ladder, choreography and the GL state guard. Three scenes derive from it:
  - **FluidScene** owns a `FluidSim`, a `Look`, `Particles` and `Emitters` (`FluidScene.hpp:16-60`). `FluidSim` is stable fluids on half-float grids: MacCormack advection, batched splats, vorticity and buoyancy, and a multigrid V-cycle pressure solve with a budget plan (`FluidSim.hpp` header comment; maths in `FluidMath`, tested in `core/tests/fluid_math_test.cpp`, which has tests such as `multigridBeatsJacobiAtEveryBudget` and `particleExposureKeepsLightSane`).
  - **CurlFlowScene**: tracers carried by a curl-noise field.
  - **WaterScene**: a ripple height field with ink.
- The rest derive from `Scene` directly:
  - **SilkScene** (320², `SilkScene.hpp:44`) and **LifeScene** (Lenia and Gray-Scott, 288², `LifeScene.hpp:45`) step through `sim::SimPass` (`core/viz/compute/SimPass.hpp`). One `stepBody` is built as an ES 3.1 compute image pass where proven, otherwise as a fragment ping-pong; `SimSpec` and the `Build` variant decide which.
  - **AcidScene**: an RGBA8 feedback loop at 540².
  - **MycoScene**.
  - **CymaticsScene**: an analytic plate drawn in the fragment shader.

The simulation scenes present their fields through `lib_relief`. `LiquidPass` (`core/viz/liquid/`) advects the whole frame by `FlowField`.

### Audio coupling

The CPU folds the sound frame into a few dt-scaled drives that the kernels read as uniforms (`SilkScene.hpp:20-25`, `LifeScene.hpp:20-26`, `FluidMath` `Drives::particleStir`). Kernels never see the raw frame.

### GL tiers

`GlTier` is either `compute-es31` or `baseline-es30` (`core/viz/GlCaps.hpp:84-94`). The prober reads only the compute and fragment storage-block limits, not the vertex storage-block limit or the shared-memory size (`core/viz/GlProber.cpp:675-677`). The compute work-group target is 64 (`core/viz/Compute.hpp:23`). No SSBO is used anywhere: grep for `GL_SHADER_STORAGE_BUFFER` in `core/viz` finds nothing.

### Trails

`TrailPass` corrects retention for `dt`: `1 − (k·0.97)^(dt·60)` (`core/viz/TrailPass.cpp:31-33`). It does not correct the source gain, which is Fosfora's #2349 bug class.

## 5. Recommendations for Geode

### What mobile GLES allows

- **Compute is ES 3.1 only.** The spec floors are 4 storage blocks per compute stage, 0 in the vertex and fragment stages, 128 work-group invocations and 16 KB of shared memory.
- **Atomics.** Buffer atomics are core in ES 3.1. Image atomics need `OES_shader_image_atomic` until ES 3.2.
- **Indirect draws.** `glDrawArraysIndirect` and `glDispatchComputeIndirect` are core in ES 3.1.
- **Blending.** On tile-based GPUs (Mali, Adreno, PowerVR), additive point blending happens on-chip and is nearly free. Writing to an SSBO framebuffer takes the work off the chip and costs DRAM bandwidth, which costs battery.
- **ES 3.0** has no scatter except rasterized points. Transform feedback exists but cannot compact output, so texture ping-pong stays the base path.
- **Heat.** Thermal tiers cap fps (`ThermalTierInfo.fpsCap`), so frame-rate invariance matters more on a phone than on a desktop.

| # | What | Files | Behaviour change | Test | Cost | Priority |
|---|---|---|---|---|---|---|
| R1 | **Correct the trail source gain for frame rate.** Port `frame_gain(gain60, keep60, dt)` with the n clamp of 2 and the bit-exact n==1 passthrough, and scale each scene's additive draw brightness by it wherever `TrailPass` retention applies | new pure `core/viz/FrameGain.hpp`, used in `TrailPass.cpp:31-40` and passed into `Particles::draw` brightness and the dye display | Under the thermal fps cap (30 fps) trails and tracers stop dimming to about half; 60 fps output is unchanged | `viz_math_test`: steady state `a·g/(1−k^n)` equals the 60 fps value within 1% at dt 1/30, 1/60 and 1/120; exact at 1/60 | S | P0 |
| R2 | **Let auto-downgrade shrink tracers without reallocating.** Keep the state texture; update a scissored row band and `glDrawArrays(…, 0, rows·side)`, so the population is a prefix of the texture | `FluidScene.cpp:36-45`, `CurlFlowScene.cpp:41-48`, `FluidParticles.{hpp,cpp}` (add `activeRows`) | A hot phone sheds tracer fill and fragment work too, not only grid work; no state is lost | `fluid_math_test`: `activeRowsFor(tier, appliedSide)` never grows on auto, matches the tier's count to within one row, and `particleExposure` follows the active count | S | P0 |
| R3 | **Apply `particleExposure` in `FluidScene`**, as `CurlFlowScene` does, using the active count from R2 | `FluidScene.cpp:128-131` | Ultra and Min look equally bright | Extend `particleExposureKeepsLightSane` | S | P1 |
| R4 | **An emission ring for beat bursts on ES 3.0.** A row cursor; an accumulator carries `rate·dt` plus a burst per *counted* beat (a `beatCount` delta, not a `beat > 0.5` level); a scissored respawn draw re-seeds the band `[cursor, cursor+n)` at the current spawn | pure `fluid::math::emissionBand` in `FluidMath`, `FluidParticles::step` (second respawn draw), `fluid_particle_update_frag.glsl` (`uForceRespawn`) | Tracers burst from the emitters on the beat, as Fosfora's `burst_on_beat` does (`system.rs:1736-1749`); idle stays identical | `fluid_math_test`: fraction carried across frames, wrap at the end of the texture, a burst never exceeds the population, identical at 30 and 60 fps | M | P1 |
| R5 | **Integer hashing in respawn.** Replace `hash(vUv·k + uTime)` with lowbias32 over `uint(gl_FragCoord.xy)` and a frame counter (`uint` ops are core in ES 3.0) | `fluid_particle_update_frag.glsl:62-72`, `myco_agent_frag.glsl` seeding | No banding of spawn jitter at large `uTime`; a small look change, needs an A/B | Mirror the hash in `FluidMath`; chi-square over 1M texel indices at t = 0 and 250 s; `tools/glslcheck` renders the seed | S | P1 |
| R6 | **Bake the calm-water stir.** It evaluates analytic psrdnoise twice per tracer; sample a pre-baked tileable curl texture instead, 64² × 16 layers, `GL_TEXTURE_3D`, z scrolled by `stirTime`, as Fosfora's 64³ `FlowFieldTexture` does (`flow_field.rs:6-30`) | `FluidParticles`, new `core/viz/fluid/CurlBake.{hpp,cpp}` (CPU bake at init), update shader | ALU saved per tracer at 1M (Ultra); visually the same at the low and mid tiers | A host test that the bake is divergence-free (finite-difference div < ε); glslcheck before and after | S | P2 |
| R7 | **Probe what a compute particle tier would need**: `GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS`, `GL_MAX_SHADER_STORAGE_BLOCK_SIZE`, `GL_MAX_COMPUTE_SHARED_MEMORY_SIZE`, `GL_MAX_COMPUTE_WORK_GROUP_COUNT`; derive `vertexPulling` and `indirectDraw` capabilities | `GlCaps.hpp:44-75`, `GlProber.cpp:675-680`, `capability_cache` (bump `kSchemaVersion`) | None yet; a cache change re-probes once | Existing capability-cache encode/decode round trip | S | P1 |
| R8 | **A compute particle store (ES 3.1, proven only).** Two SSBOs per side in Geode's existing 32-byte A/B layout (≤4 storage blocks per stage, so no 13-buffer bind group); an atomic alive counter and alive list; `glDrawArraysIndirect`; vertex pulling only where R7 proves vertex SSBOs, else an `imageStore` of the state into the existing textures so the current VTF draw is reused; work groups of 64. Reuse SimPass's one-body/two-harness idea so the update GLSL is shared with the fragment path | new `core/viz/particles/` (`ParticleStore`, `ParticlePass` with a `std::variant` Compute/Texture like `SimPass::Build`), `FluidParticles` becomes a client | Variable populations, emission by budget, and a base for R9 on compute phones; ES 3.0 phones unchanged. The gain is features, not speed: tile-based blending of points is already cheap | Host mirror of the emission and alive-list maths; a `docs/DEVICE_CHECKS.md` entry: same frame on compute vs texture, the alive count matches, thermal over 10 min | L | P2 |
| R9 | **Neighbours through grid fields, not a spatial hash.** Splat density and mean velocity of tracers into a low-res RGBA16F grid with additive `GL_POINTS` (the Myco deposit pattern, `MycoScene.cpp:150`), then steer by the field's gradient (separation), mean velocity (alignment) and blurred density (cohesion). Murmuration-like flocking on ES 3.0; an exact hash (count, scan, scatter) only on R8's tier | `fluid/FluidParticles` (an optional deposit pass), new kernel, `FluidMath` steer maths | A flocking look on every phone | `fluid_math_test`: steering from a synthetic field (a uniform field gives zero separation, a gradient gives a push away) | M | P2 |
| R10 | **Image decomposition with a spring home.** Tracers keep a home (texel → cover-art pixel and colour) in a third RGBA16F aux texture; a spring of `K≈12`, damping `0.85^(dt·60)` and a velocity cap; scatter impulse on a counted beat (`image_scatter.wgsl:16-20,66-78`). Cover art is local, so it suits an offline app | `FluidParticles`, a new choreography mode, a Kotlin cover-art bitmap upload through the existing palette path | A new look: album art that dissolves on drops and reforms | Host: settle time of the spring-damper under 0.6 s at 30, 60 and 120 fps; no overshoot past the cap | M | P3 (product call) |
| R11 | **Write the per-sim state layout down as a contract** (A/B lanes, Myco agent lanes) in one header beside the kernels, and assert in host tests that any new kernel's lane names match. This is Fosfora's spin lesson (`types.rs:437`) | `FluidParticles.hpp`, `MycoScene.hpp` | None | A `static_assert` or host test on a lane table | S | P3 |

## 6. Do not copy

- **The 13-binding storage-buffer layout and 256-thread work groups.** They exceed the ES 3.1 floors of 4 storage blocks per stage and 128 invocations (`system.rs:680-700`, `context.rs:83-86`).
- **The atomic-SSBO compute rasterizer as a default.** It needs four `i32` buffers per pixel: about 41 MB per frame cleared, written and read at 1080×2400. Its resolve reads SSBOs in the fragment stage, where the ES floor is 0. Image atomics need an extension. On tile-based GPUs the desktop reasons for it (2×2 quad waste, a rasterizer bottleneck) mostly disappear, and the bandwidth is paid in battery. At most, try it on a proven Mali or Adreno at half resolution as a device experiment.
- **Particle counts of 1–2M as default, Max 4×, and the 10M stress effect.** Geode's ladder of 25K to 1M with exposure compensation is the right range for a phone.
- **The bitonic depth sort** (`system.rs:1181`), **WBOIT** (blending RGBA16F MRT needs `EXT_float_blend` on ES 3.x), **3DGS splats** (GB-scale loads), the **XR world path**, and the **webcam, video and model particle and obstacle sources**.
- **MiDaS depth.** Its runtime download (`depth/model.rs:9`) breaks the offline, no-INTERNET rule, and it needs the camera.
- **Lattice and Helix volumes at 128–256³ r32float** with a full-screen ray march: 8–64 MB of volume and a heavy fragment cost per pixel. If ever wanted, use 64³ in the compute tier, in an optional pass that the thermal tier drops.
- **The god-struct composition** (`ParticleSystem` with about 150 `Option` fields and if-chains). Geode's direction is variants and capability interfaces (`docs/research/2026-11-05-fosfora-structure-plan.md` F8, step 5).
- **Keeping a known-bad hash because it looks good.** Port the look on purpose instead.
- **Streaming about 60 raw audio scalars into every kernel** (`system.rs:1910-1963`). Geode's CPU-side fold into dt-scaled drives is cheaper in uniforms, testable on the host, and keeps one-frame triggers from being lost.
- **The vestigial dead-index buffer and dispatching for capacity instead of population.**

**Summary.** Fosfora's particle core is a GPU-driven lifecycle: four SoA `vec4` buffers, 64 bytes per particle, ping-ponged, with atomic emission slots, an alive list and an indirect draw. Around it sit optional producers: reaction-diffusion, physarum, water, an obstacle fluid, a spatial hash, sort, compute raster, splats, volumetric, Lattice and Helix. Most of its effects are per-effect WGSL over one 992-byte uniform block. Its best idea is frame-rate discipline: `frame_gain`, the 60 Hz trail ring and the dt clamp, with measured results. The weak points are the 7k-line god struct, unused dead lists, dispatching for capacity, nondeterministic alpha draw order, and GPU performance claims that are anecdotal only. Geode already has a sound ES 3.0 design: texture-state tracers with RK2, points or streaks, a five-tier ladder up to 1M, a dual-path `SimPass` and multigrid fluids. Its gaps are trail source gain not corrected for frame rate, auto-downgrade that never sheds tracers, and no emission or burst model. Recommendations R1–R5 are small and safe. A compute particle store (R8) and field-based flocking (R9) are the larger, ES 3.1-aware steps. The atomic rasterizer, WBOIT, splats, depth and the multi-million budgets should not be ported.
