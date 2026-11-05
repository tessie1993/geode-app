# 06: Modulation and control (params, bindings, LFOs, node modulation, presets, scenes, persistence)

Fosfora @ `2e85d3b`, paths under `crates/fosfora-app/src/` unless noted. Geode @ `37761e8a`.

## 1. How it works in fosfora

### 1.1 Params: the base value and this frame's overlay are separate

- `ParamDef` is a tagged enum: `Float{min,max,default}`, `Color`, `Bool`, `Point2D` (`params/types.rs:3-26`). `ParamValue::lerp` snaps a Bool at t = 0.5 and keeps `self` when the types differ (`types.rs:89-106`).
- `ParamStore` (`params/store.rs:5-23`) keeps `values` (the base: what the user set, what a preset saves, what `changed` tracks) apart from `modulated: HashMap<String, Option<ParamValue>>` (this frame's overlay). `set_modulated` never touches `changed` (`:98-109`). `clear_modulation` sets each slot to `None` and keeps the key, so a param bound on every frame allocates nothing (`:113-117`). `effective()` = overlay, else base (`:131-133`). `pack_to_buffer` packs the effective values into `[f32;16]` (`:170-186`). Loading or merging defs drops stale overlays (`:35-74`).
- Each frame (`app.rs:1326-1342`): `binding_bus.evaluate(...)`, then `layer_stack.clear_modulation()` and a reset of `master_postprocess_modulation`, then `apply_outputs`. A binding that is removed or goes quiet gives its param back to the base on the same frame. The headless renderer runs the same sequence: `evaluate_offline` → `clear_modulation` → `apply_outputs` (`headless/scene_renderer.rs:721-724`). The Graphify index of `tessie1993/fosforatess` has no call edge for `apply_outputs`, so this was confirmed by grep. Graphify does confirm that `modulate_param`'s only non-test caller is `apply_param_binding`, and that `resolve_node`'s only non-test caller is `TramaSystem::update` (`trama/mod.rs:169`). The tests state the contract: modulation renders but is not an edit (`bindings/apply.rs:319-339`), and a user drag under a running binding moves only the base (`store.rs:294-305`).
- UX contract (`docs/TUTORIALS.md:995`): "A bound control keeps the value you set … a small triangle under it shows where the binding has it … the preset saves your value, not the binding's."

### 1.2 The binding bus

- `Binding{id "b_NNN", name, enabled, scope Preset|Global, source: String, target: BindingTarget, transforms: Vec<TransformDef>}` (`bindings/types.rs:307-317`).
- **Sources** are dotted strings: `audio.kick`, `audio.band.0`, `audio.mel.N`, `lfo.saw.beat.90`, `midi.<dev>.cc.<ch>.<cc>`, `osc./path`, `ws.<field>`. They are collected each frame into `SourceSnapshot = HashMap<Cow<'static,str>, (f32, SourceRaw)>`, reused across frames so the steady state does not allocate (`sources.rs:13-23, 73-182`; `bus.rs:139-143`). Indexed key tables are built once (`sources.rs:28-42`). Derived sources such as `audio.key_hue` ease toward 0.5 as the key confidence falls (`sources.rs:57-62`).
- **LFO sources** (`bindings/lfo.rs:1-21`): `lfo.{shape}.{division}[.{deg}]`. Each is a pure function of the beat clock (`beat_index + beat_phase`), with no state: `value()` at `:128-141`. Sample and hold hashes the cycle number (`:146-150`), so an offline replay draws the same levels. The source key *is* the spec. The base keys are built once, and offset variants are published only while something is bound to them (`:175-200`). The curve function `wave()` is shared with trama (`trama/modulation.rs:403-425`).
- **Transforms** (`types.rs:319-347`, `transforms.rs:5-74`): Remap, Smooth{factor}, Invert, Quantize, Deadzone, Curve{name}, Gate, Scale, Offset, Clamp. They run in order. `CURVE_TYPES` sits next to `apply_curve`, and older curve names alias onto the arms they duplicate (`transforms.rs:76-100`).
- **Targets**: `BindingTarget` enum (`types.rs:68-100`): `Unset`, `Param{layer,effect,param}`, `LegacyParam{effect,param}`, `Layer{layer,field}`, `PostFx`, `Particle`, `Uniform`, `SceneTransport`, `GlobalMasterOpacity`, `Unknown(String)`. The leaf enums come from one `leaf_enum!` macro (wire name, `ALL`, `parse`; `target_fields.rs:14-50`). On disk a target is the same dotted string as before (`Display`/`FromStr`, `types.rs:202-305`). Parsing never fails: anything it does not recognise, including a known family with a misspelled leaf such as `uniform.kik`, becomes `Unknown` and is kept verbatim (`:238-276`), with a warning at load (`persistence.rs:77-87`).
- **Evaluate** (`bus.rs:432-509`): learn mode first. Then, for each enabled binding: skip it if its source is missing or not finite (`:470-477`), run the transform chain on a per-binding `BindingRuntime` (`types.rs:350-374`), then latch the rising edge: `rising = out > 0.5 && !prev_above`. The latch freezes while the source is absent, so a missing source can suppress a trigger but never fire a spurious one. The result is `BindingOutput{binding index, value, rising}`: an index into the binding list, not a cloned target (`types.rs:381-389`).
- **Apply** (`bus.rs:402-426` → `apply.rs:29-118`): one exhaustive match, shared by the live app and the headless renderer. `Param` → `modulate_param`, which writes `min + (max-min)·clamp(v)` as the overlay, an **absolute** value (`apply.rs:253-267`). Layer, post-FX and particle fields each scale the normalized value into their own unit (`apply.rs:120-160`). `SceneTransport` pushes an action only on `rising` (`:94-98`), and `main.rs:2475-2490` drains it.
- **Layer reorder and delete** (`bus.rs:225-268`): `remap_layer_targets(Fn(old)->Option<new>)`. It uses `BindingTarget::layer()` and `with_layer()`, both spelled out per variant with no catch-all, so a new variant that carries a layer is a compile error until it is handled (`types.rs:102-146`). Deleting a layer drops its bindings. Changing a layer's effect drops only that layer's preset-scoped bindings (`bus.rs:530-552`). Merging a preset sidecar re-ids any colliding `b_NNN` (`:560-579`).
- **Templates** (`templates.rs:14-50`): placeholders `{layer}`, `{effect}`, `{param: a|b|*}`. Each resolves to the first named param the effect actually has; `*` takes the next free one. Role lists (punch, colour, motion, scale) and a skip list (`trail_decay`, `mode` …) apply only to the `*` fallback.

### 1.3 Node-graph modulation (trama)

- One slot per Float param: `Modulation{source: Oscillator(Osc)|Audio(AudioFeature), amount -1..1, mode Add|Multiply|Replace, smoothing}` (`trama/modulation.rs:23-150`). The runtime `ModState` (phase, rng, held, drift, smoothed, packed slot, resolved) is never serialized (`:152-175`).
- `resolve_node` (`:219-264`) combines the signal with the base, then clamps, then smooths:
  - **Add** is `base + s·amount·span`.
  - **Multiply** is `base·(1+s·amount)`.
  - **Replace** crossfades toward `min + span·u` by `|amount|`.
  - The clamp is to the param's range.
  - The smoothing is a dt-correct one-pole, τ = smoothing²·2 s. The first resolve snaps rather than slewing in from an unrelated value.
- Oscillators run in Hz or `BeatSync(BeatDiv)`. A synced oscillator freezes while the tempo is unlocked (`:115-123`, `:356-398`). Its RNG is seeded by FNV(param) ^ node id, never by the wall clock (`:445-452`).
- **Once per frame, never per execute** (`trama/mod.rs:151-176`): `resolve_node` and `integrate` run in `update`. The executor only reads `apply_resolved` (`modulation.rs:266-275`), so the dissolve's second execute cannot advance an oscillator twice.
- **Rates** (`effect/rates.rs:1-37`; `modulation.rs:277-317`): a param declared as a rate gets `phase += value·dt`, using the *resolved* value, so a change in speed moves the picture by Δ·dt and never by Δ·uptime. A period param integrates `dt/value`. `scripts/audit_pfx_rates.py` blocks the `u.time * speed` pattern in pre-commit (`docs/TUTORIALS.md:274`).

### 1.4 Scenes, cues and transitions

- `SceneSet{version, name, cues, loop_mode, advance_mode Manual|Timer|BeatSync{beats_per_cue}}`. `SceneCue{preset_name, transition Cut|Dissolve|ParamMorph, transition_secs, hold_secs, label, param_overrides: Vec<HashMap>, transition_beats}`. Every field is `#[serde(default)]` (`scene/types.rs`).
- `cueing.rs:33-91`: overrides are applied by layer **position** and skip locked layers. `MorphSnapshot{params per layer, opacities}`. `apply_morph` lerps only the params present on both sides and **writes the base** (`store.set`, then `changed=false`), so bindings still lay their overlay on top. In `app.rs`, evaluate runs at `:1326` and the morph at `:1463-1480`.
- `switch.rs`: a staged switch goes Decoding → AwaitCapture (at most 3 frames) → Ready (`:42-128`). `dissolves_frame` returns true for Dissolve always, false for Cut, and for Morph only when some `LayerKey` changed (`:146-152`). `keep_changed_layers_still` empties the morph for layers whose effect changed, because "a param that two effects both name means something else in each" (`:156-165`).
- Timeline BeatSync counts `feed_beat(uniforms.beat > 0.5)` rising edges, or Link or the MIDI clock (`app.rs:1440-1462`). `transition_beats` is converted with the denormalized period (`timeline.rs:485-551`).

### 1.5 Persistence and forward compatibility

- Bindings: `global-bindings.json` plus a `presets/<name>.bindings.json` sidecar, `BindingsFile{version:1, bindings}` (`persistence.rs:7-50`). `version` is written and never read (ARCHITECTURE-NOTES.md "I/O + config"). The save is a plain `std::fs::write` (`persistence.rs:115-138`). A parse failure returns `Vec::new()` (`:52-71`). Migrations happen on load and are rewritten on the next save: `osc./phosphor/`→`osc./fosfora/` (`:92-106`), effect renames through `current_effect_name` (`types.rs:249-259`), and the first-run import of the old MIDI/OSC maps (`migration.rs:6-103`). `LegacyParam` is upgraded to `Param` against the preset's layers when the preset loads (`apply.rs:274-294`).
- Trama documents (`trama/ser.rs:1-46, 87-131, 276-431`) are the best part:
  - The document types are deliberately **not** the runtime types.
  - The version written is the **oldest build that can read the file** (`BASE_VERSION`=1, `ANCHOR_VERSION`=2 only when an Anchor is present). A file that uses nothing new still opens in older builds.
  - `migrate` lifts one version per match arm. `TooNew` is a typed error.
  - `restore` repairs what it can and reports it in `Restored::notes`: a missing effect becomes a placeholder that keeps its pins, values and mods; a mod whose param is gone is reported (`:362`); a wire to a lost pin is reported. Nothing is dropped silently.
  - A golden test pins the spelling of the config enums.
- Presets: `Preset{layers, active_layer, postprocess, volumetric, master_chain}` with no version, relying on `serde(default)` throughout (`preset/store.rs:39-226`). `to_json_checked` re-parses the JSON before writing, which catches NaN/Inf (`:396-402`). The write is `paths::write_atomic`, a tmp file plus a rename (`paths.rs:113-121`).
- Settings: `serde_json::from_str(..).unwrap_or_default()` and a non-atomic write (`settings.rs:321-336`).
- Inbound: bounded queues that drop the **oldest** message, so the end of a fader drag survives a render stall (`inbound.rs:1-60`).

## 2. Contracts worth naming

1. **The base is sacred.** Only a user edit, a preset load, a cue override or a morph writes the base. Modulation is an overlay that is rebuilt from nothing every frame and is never saved, never dirties anything and never lands in a snapshot (`store.rs:5-23`; `cueing.rs:72-91`).
2. **Each stateful modulator advances exactly once per frame** in `update`, and render paths only read the result (`trama/mod.rs:160-176`).
3. **Triggers are edges, not levels.** The level is computed after the transforms, and the latch freezes when the source is missing (`types.rs:356-361`).
4. **Ids are not positions.** A target names its layer explicitly, and moving or deleting a layer is a remap through one exhaustive function. Binding ids are re-issued when they collide on merge.
5. **Unknown input survives a round trip** for targets (`Unknown(String)`), for trama nodes (placeholders) and for renamed effects. It is reported, not dropped.
6. **A saved file advertises the oldest reader it needs** (`ser.rs:36-46`).
7. **Rates are integrals** (`rates.rs`).

## 3. Excellent and weak

**Excellent**
- The base/overlay split with a per-frame clear. It fixed three bugs at once: presets reading dirty forever, a `.pfx` autosave of modulated values, and sliders that jumped (`apply.rs:248-252`).
- Targets are typed once at load and dispatched by an exhaustive match. A typo cannot drive nothing silently (`target_fields.rs:1-10`).
- `layer()` and `with_layer()` have no catch-all, so a half-fix cannot compile (`types.rs:133-136`).
- Trama's document discipline (`ser.rs`), and `to_json_checked`.
- The LFO is stateless, a function of the beat clock, so export and replay are trivial (`lfo.rs:19-21`).
- Smoothing is dt-correct, and the seeds are deterministic (`modulation.rs:252-262, 443-452`).
- Morph vs dissolve is decided by "does this layer show the same thing", and layers that changed hold still (`switch.rs:143-165`).
- One apply function serves both live and headless (`apply.rs:1-7`).

**Weak**
- **Last writer wins, absolute.** The bus writes `min+(max-min)·v` and overwrites the slot (`apply.rs:259-266`, `store.rs:103-108`; `Layer` likewise at `:69`). Two bindings on one param do not add up; the one later in the list wins. The base is shown but ignored while a binding runs. Trama's Add, Multiply and Replace live in a *second* system with other semantics, and the two share only `wave()`.
- `Smooth{factor}` is per frame, so it depends on the frame rate. One `smooth_state` per binding is shared by every Smooth transform in its chain (`transforms.rs:28-32`, `types.rs:352`).
- The edge threshold is a single 0.5 with no hysteresis (`bus.rs:495`). The timeline's BeatSync reads a per-frame `uniforms.beat > 0.5` edge, so at a low frame rate a one-frame beat can be lost (`app.rs:1454-1458`).
- `version` fields are written and never read. A `TransformDef` or `TransitionType` the build does not know fails the **whole file**, because serde tagged enums have no fallback. `load_from_path` then returns `[]`, and `remove_binding` calls `save_global` immediately (`bus.rs:212-223`), so a newer file opened in an older build can be overwritten with less. Bindings and settings are written without the atomic rename that presets get.
- Layer index as identity forces remap code everywhere, and the cue `param_overrides` are positional and not remapped (`cueing.rs:23-25`).
- A string-hashed snapshot of about 300 keys on every frame.

## 4. Geode today (facts)

### 4.1 The frame (`core/viz/RendererFrame.cpp:120-213`)

```
requested ──(paramFadeSec / morph one-pole, k = dt/fade, :131-132)──► displayedParams_
GeodeSoundFrame ─┬─► Director.step (emotion; commits counted, :140-146)
                 ├─► NoteResponse.step (:150-166)
                 ├─► AdsrEngine.tick ──EnvBand[6]──► lfoOffsets → LFO rate/depth, same frame (:167-168)
                 ├─► LfoEngine.tick ──ModSource[9] followers / oscillator──► 3 values (:169)
                 ├─► ReactSignals ──ReactSource[117]──► ReactionEngine ×4:
                 │        reactions_ (12: DNA | Events | user, hand:: mask)
                 │        macroReactions_ (7, macros::routes)
                 │        Director outgoing_ + incoming_ (Director.hpp:125-126)
                 └─► MotionField.step (reads the frame itself)

displayed ─director.apply─► notes.apply ─macros::apply(kDrives[29], ptr-to-member lo/hi)─►
  lfo.apply ─► AdsrEngine::apply ─► reactions_.apply ─► macroReactions_.apply ─►
  director.applyRoutes (incoming(outgoing(p)), Director.cpp:491-494) ─► MotionField.apply ─►
  safety::apply ─► lastFinalParams_ ─► grade::integrateRotation / integrateCyclePhase (:207-208)
```

The six modulators are Director, NoteResponse, LFO, ADSR, Macros (knobs plus routes) and Reactions, plus MotionField. There are three source enums: `ModSource` (9, `Lfo.hpp:10`), `EnvBand` (6, `Adsr.hpp`) and `ReactSource` (117, `Reactions.hpp`). There is one target enum, `LfoTarget` (about 57, including 6 chain slots and `Retired25`; `Lfo.hpp:14-24`), applied by `applyLfoTarget` (`Lfo.cpp:54-108`). The Macros, MotionField and Director stages write their fields directly, not through `LfoTarget`.

### 4.2 Facts that matter for the matrix

- **No base/modulation split.** Every stage takes a `SceneParams` by value, adds to it and **clamps it**: `applyLfoTarget` clamps per target with hand-written scales (`v*1.5` rotation, `v*25` fluidCurl …; `Lfo.cpp:57-104`); Reactions apply one route at a time through the same function (`Reactions.cpp:369-379`); MotionField has its own clamps (`MotionField.cpp:178-193`). So two opposing pushes saturate in an order-dependent way: clamp(clamp(b+a)+c) ≠ clamp(b+a+c). The final value is kept for transitions (`lastFinalParams_`, `RendererFrame.cpp:100, 206`) and is never saved. That part is already right.
- **Ranges live in at least five places.** `applyLfoTarget`, `macros::kDrives` (`Macros.cpp:20-28`), MotionField, and Kotlin (`CustomizeTabs.kt:495`, `CustomizePanel.kt:402`, `editor/AnimatableParams.kt:151`; e.g. speed 0.05..4 in all of them). `catalogue::Range` holds only `kMarchDetail`, `kTouchZoom` and `kTouchRotation` (`Catalogue.hpp:58-77`). **MOD_MATRIX_PLAN §2's "params from `fieldNames()` plus the catalogue's ranges" assumes a table that does not exist yet.**
- **Plan D6 misstates today's order.** It reads "director → LFO/ADSR → macros → Reactions". The code applies macros *before* the LFO, and the ADSR after the LFO (`RendererFrame.cpp:180-181`). MOD_MATRIX_UX §1 has it right.
- **Triggers are better than fosfora's.** Reactions Step, Jump and Toggle arm at 0.5 and re-arm only below 0.3 (`Reactions.cpp:332-356`). Clock sources are read by their delta (`:267-270`). `StyleSchedule` counts `beatCount`, so it never drops a beat (`StyleSchedule.hpp`).
- **Chaining is order-based.** "A slot may only steer a LATER slot" (`Lfo.cpp:139-141`). The ADSR feeds the LFO rate and depth in the same frame. Plan D4 (read the previous frame) replaces both.
- **The LFO follower is not dt-correct.** It uses `followed += Δ·clamp(dt/τ)` (`Lfo.cpp:187-191`). Reactions use `onePoleK` (`Reactions.cpp:365`).
- **Rates.** `ShaderScene::update` already integrates speed (`shaderTime_ += p.speed·dt`, `ShaderScene.cpp:76`), but **43 shader files then compute `uTime * uSpeed`** again (for example `lucid_lattice_frag.glsl:67`, which also has `uFlowPhase * uSpeed`; `lib_arbor`, `lib_bulb`, `lib_glass`, `lib_nebula`, `lib_terra`, `lib_truchet`, `lib_dive`, and the lucid, chrys and arbor styles). Every change in speed therefore moves the picture by Δspeed × ∫speed, with `uTime` wrapping at 7100 s (`ShaderScene.hpp:42`). MotionField rewrites speed on every frame (`speed·(0.7+0.5·energyRel)`, `MotionField.cpp:186`). This is fosfora's #2984 bug, by arithmetic; no device check yet. The structure plan's F11 says "Not checked yet". It is now checked.
- **Determinism.** `noteSeed` mixes in `std::random_device` per renderer (`RendererFrame.cpp:110-118`), and I found no pin for offscreen renders. An export therefore rolls a different note personality from the live play (to verify).
- **Persistence.**
  - `ReactionRouteCodec` (`reactions/2`, pipe-separated, enum names) returns `null` for a version it does not know, and drops any line whose source or target it does not know (`ReactionRouteCodec.kt:44-60`). The caller then falls back to the DNA, and the next save writes the shorter list.
  - `LfoStore` (org.json, names) turns an unknown target into `NONE` (`LfoStore.kt:127-133`).
  - Presets are written field by field in hand-written JSON, with no version and with the defaults repeated in `fromJson` (`PresetStore.kt:136-400`). The write is atomic (`AtomicWrite.text`, `:127`).
  - Names, not ordinals, are saved everywhere. That is good. Ordinals are only the JNI wire.

## 5. Recommendations for Geode

The jacks of `MOD_MATRIX_PLAN.md` become the four types of the structure plan's step 7. Fosfora sharpens them in three ways:
- the overlay is *summed*, not last-writer-wins;
- a field is clamped *once*;
- unknown saved input survives a round trip.

### R1. Fix the double speed (F11, now confirmed). Priority P0, cost S
- **Files:** the 43 shaders and `lib_*` that compute `uTime * uSpeed` or `uFlowPhase * uSpeed`; `ShaderScene.cpp:76`; `tools/check_shaders.py`.
- **Change:** use `uTime` alone (it is already ∫speed), and integrate `uFlowPhase` with speed natively. If the owner likes the speed² feel, integrate `speed²·dt` natively instead of multiplying in GLSL. Add a check that rejects `uTime\s*\*\s*uSpeed`, like fosfora's `audit_pfx_rates.py`.
- **Behaviour:** speed responds linearly, and macro, LFO and MotionField changes stop strobing the picture forward. Thumbnails need re-rendering.
- **Test:** a `check_shaders.py` rule; a host test that `shaderTime_` advances by exactly `speed·dt`; `glslcheck` renders at speed 1 against main.

### R2. `ParamTable`: one generated range table, the E0 prerequisite. Priority P0, cost M
- **File:** `core/modulation/ParamTable.inc`, an X-macro with one row per `SceneParams` field: `X(zoom, 0.3f, 3.0f, 1.0f, Unit::Times, Lerp::Linear, Flags::Modulable)`. Flags: `Luminance` (replaces `isLuminanceTarget`), `Rate`, `Angle` (wraps instead of clamping, like `colorShift`), `Discrete`, `NotFaded`.
- **Generates:** `fieldNames()` (`ParamsFields.cpp`), `lerpedFloats()`, the `applyLfoTarget` clamps, the `kDrives` lo/hi, and rows in `geode_catalogue_text`, so Kotlin's sliders read min and max from native and stop repeating them.
- **Behaviour:** none (equal ranges). Where today's copies disagree, the table picks one value and the PR says so.
- **Test:** `params_test.cpp` (each table row against the old `applyLfoTarget` clamp and `kDrives` bound), `catalogue_test --write`, and a JVM test that each Kotlin slider's range equals the table's.

### R3. One accumulator, one clamp: the bus's core. Priority P1, cost M
```cpp
// core/modulation/Bus.hpp
struct ModFrame {                       // rebuilt from zero every frame (fosfora clear_modulation)
    std::array<float, kFieldCount> add{};   // normalized: share of the field's span
    std::array<float, kFieldCount> mul{};   // log2 factor, for MotionField-style scaling
    std::bitset<kFieldCount> touched;
    void push(FieldId f, float v)  { add[f] += v; touched.set(f); }
};
SceneParams resolve(const SceneParams& base, const ModFrame& m);  // clamp(base·2^mul + add·span) once
```
- **Ordering rule** (replaces D6):
  1. *Look stages* write the **base**: param fade → `Director::apply` → `NoteResponse::apply` → `macros::apply`. They are decisions about the look, and fosfora's morph also writes the base (`cueing.rs:82`).
  2. *Modulators* only `push`: LFO, ADSR, the four `ReactionEngine`s and the matrix cables. Order within this stage no longer matters, because addition commutes.
  3. `resolve`, which clamps once.
  4. `MotionField::apply`.
  5. `safety::apply`, always last.
  6. Rate integration (`grade::integrate*`, `ShaderScene` phases) on the final values.
- **Files:** `RendererFrame.cpp:167-200`; `ReactionEngine::apply` and `LfoEngine::apply` gain `push(ModFrame&)` beside their current `apply` (kept for tests); `applyLfoTarget` becomes a `LfoTarget → FieldId + span` table (R6).
- **Behaviour:** this changes only where routes saturate together: opposing pushes cancel instead of the first one clipping. The luminance attack and release floors stay per route (`Reactions.cpp:360-363`).
- **Test:** `viz_math_test`: at `bloom` = 1, routes +0.6 and −0.6 resolve to 1 (today 0.4); a shuffled route order gives a bit-identical result; with no routes enabled, the frame equals today's.

### R4. Sources: one `SourceTable`, double-buffered. Priority P1, cost M
```cpp
enum class SourceId : uint16_t;          // generated: FeatureSchema (step 3) ++ modulator outputs ++ params
struct SourceTable {
    std::array<float, kSourceCount> now{}, prev{};   // prev = last frame's, for modulator and param jacks (D4)
    void beginFrame() { prev = now; }
};
```
- `ReactSignals` fills the sound rows. LFOs, ADSRs, macros and matrix modulators fill their own rows after they tick. Param rows take `lastFinalParams_`.
- **Retiring the side enums:** `ModSource::Bass` becomes `sound.bassAtt` × 0.5 plus a Smooth(0.02 s, 0.16 s) transform, the LFO's current follower (`Lfo.cpp:126-133`). `EnvBand` becomes a gate jack on the same rows. Both enums stay as decode aliases only (R7).
- **The rule fosfora's lesson adds:** a cable whose source is a modulator, or a param, reads `prev`; sound, macros and tempo read `now`. This retires "only a later slot" (`Lfo.cpp:139-141`) and the ADSR's same-frame offsets.
- **Behaviour:** chained LFOs lag by one frame (16 ms).
- **Test:** `modmatrix_test`: LFO1 → LFO2.rate → LFO1.depth terminates and replays bit-identically live and at 1/fps.

### R5. Transforms with their own state. Priority P1, cost S
```cpp
enum class Xf : uint8_t { Remap, Smooth, Curve, Invert, Quantize, Gate, Scale, Offset, Clamp, Slew };
struct Transform { Xf kind; float a = 0, b = 1, c = 0, d = 1; };   // meaning per kind
struct XfState  { float y = 0; bool primed = false; bool high = false; };   // one per transform
```
- Smooth and Slew are dt-correct: `1-exp(-dt/τ)`, with rise and fall in seconds, never a per-frame factor.
- Gate is two thresholds (hi, lo), keeping Geode's 0.5/0.3 hysteresis.
- Every transform owns an `XfState`, which avoids fosfora's shared `smooth_state`.
- `ReactRoute` maps without loss: `curve` → Curve, attack and release → Smooth(rise, fall), `mode` stays as the integrator after the chain.
- **Test:** Smooth over 1 s reaches the same value at 30 and at 120 fps within 1e-4; two Smooths in one chain hold independent state.

### R6. Targets and routes. Priority P1, cost M
```cpp
struct TargetId { enum Kind : uint8_t { Field, ModParam, Action, Unknown } kind; uint16_t index; };
struct Route {
    SourceId src; TargetId dst; ReactMode mode; float amount;      // −1..1 of dst span (attenuverter)
    std::array<Transform, 4> xf; uint8_t nxf;
    enum Origin : uint8_t { User, Dna, Event, Macro, Director, Cable } origin;   // for the hand mask + UI
};
class Bus {                                        // core/modulation/Bus.{hpp,cpp}
public:
    void evaluate(const SourceTable&, float dt, ModFrame&);   // once per frame, GL thread
    std::span<const Route> routes() const;
private:
    std::vector<Route> routes_; std::vector<std::array<XfState,4>> state_; std::vector<float> acc_;
};
```
- The four `ReactionEngine`s become four route banks in one `Bus`, tagged with an `Origin` so `hand::` and the director's crossfade still work. Director crossfading becomes per-bank gain, not two engines in series (`Director.cpp:491-494`).
- `Action` targets cover fosfora's `SceneTransport`: next or previous style, Surprise, next palette. They fire on a *count* edge, `floor(acc)` changed, never on a level, and leave through a counter like `geode_viz_take_director_style_moments`, rate-limited by `safety`.
- **Test:** `Bus` with the DNA bank alone matches today's `ReactionEngine` output frame for frame on a recorded `GeodeSoundFrame` sequence, using `note_response_test`-style dumps.

### R7. Saved routes and presets: migrate, carry unknowns, and keep old builds able to read new files. Priority P1, cost M
- **Codec** (`ModPatchCodec.kt`, `:engine:scenes`):
  - Plain text with a header line `modpatch/1`, then one route per line: `src=sound.kickEnv dst=param.camPush mode=FOLLOW amt=0.4 xf=smooth:0.015:0.25`. Fields are key=value, so new keys can be appended without changing the version.
  - An unknown line or key is kept **verbatim** and written back on save (fosfora's `Unknown(String)`). An unknown jack id gives a route that is kept but inert, and the Routes tab shows it as "unavailable". It is never dropped.
  - The header names the **oldest version that can read the file** (`ser.rs:36-46`). A newer header is read as far as possible, and the patch is marked read-only rather than returned as `null`. This fixes `ReactionRouteCodec.kt:44-46`.
  - Saving goes through encode → decode → equal → `AtomicWrite` (fosfora's `to_json_checked`).
- **Migration from `reactions/2` and `LfoStore`:** a static table from each `LfoTarget` name to a field id plus its span. The names differ from the field names: `FLUID_RADIUS`→`fluidSplatRadius`, `FLUID_GLOW`→`fluidBloomIntensity`, `FLUID_FADE`→`fluidDensityDissipation`; `RETIRED25` is inert. Each amount is rescaled to keep the delta the same: `amt_new = amt_old · scale_old / span`, where `scale_old` is the multiplier in `Lfo.cpp:57-104`. `ModSource` and `EnvBand` names map to source ids plus transforms (R4). The new codec dual-reads; the old keys stay until the first save, so rolling back keeps working. The `Backup` allowlist carries both sets of keys during the transition.
- **Presets:** add `"v": 1` and generate `toJson` and `fromJson` from `ParamTable` (R2) using its defaults. Unknown keys are kept in an `extra` map and re-emitted.
- **Test:** a JVM golden test: every `LfoTarget` × {−1, 0.3, 1} in a `reactions/2` line decodes to a cable whose resolved delta equals today's within 1e-5 before the clamp; an unknown line survives decode → encode verbatim; a `modpatch/9` header loads read-only.

### R8. Determinism. Priority P2, cost S
- Seed each route's and modulator's random draws from FNV(patch id, route index), like fosfora's `seed_for`.
- Let offscreen renders pin `noteSeed` from the take or project, replacing `std::random_device` (`RendererFrame.cpp:110-118`).
- **Test:** an offscreen render of 300 frames twice is bit-identical; live at dt = 1/fps equals offscreen.

### R9. Style morph rules from `switch.rs`. Priority P2, cost S
- Replace the one-pole `k = dt/fade` (`RendererFrame.cpp:131-132`) with fosfora's linear progress between captured endpoints. It ends exactly on time, and an export frame n equals live frame n.
- When the style changes family, drop the lerp for fields outside the new style's `ParamScope`: they mean nothing there. This is `keep_changed_layers_still`.
- **Test:** `viz_math_test`: a fade of 1.0 s reaches `requested` at t = 1.0 to within 1e-6 at 30 and at 60 fps.

### R10. The UI contract. Priority P2, cost S, folded into E2
- Sliders always show the **base**, with a ring or triangle for `final − base` from the 30 Hz snapshot (fosfora's triangle, Vital's ring).
- A modulated value never marks a preset dirty. Geode already saves no final values; keep it that way when the matrix lands (`lastFinalParams_` must stay unsaved).

## 6. Do not copy

- **Absolute, last-writer-wins overlays** (`apply.rs:259-266`). Geode's D3 summing is the better contract. Keep it, and clamp once.
- **Two modulation systems with different semantics** (the bus and trama). Geode should end with one `Bus`; LFO, ADSR and macros are modulators that feed it.
- **String-keyed per-frame snapshots** (`sources.rs:23`). Strings belong in the saved form only; at runtime use ordinal arrays.
- **Per-frame smoothing factors and shared transform state** (`transforms.rs:28-32`).
- **A single 0.5 edge without hysteresis, and timeline beats read from a per-frame level** (`bus.rs:495`, `app.rs:1457`). Geode's hysteresis and `beatCount` counters are stronger.
- **`version` fields nobody reads; tagged enums without a fallback, which fail a whole file; a save that runs immediately after a failed load; non-atomic writes** (`persistence.rs:52-71, 115-138`; `settings.rs:321-336`).
- **Positions as identity** (layer indexes in targets and in `param_overrides`). If Geode grows layers (structure plan "later"), give them stable ids from day one.
- **Guessing params by name in templates** (`templates.rs:51-75`). Geode's `ReactionDna` personalities, `ParamScope` and `EventRecipe` already pick targets by role.
- **MIDI, OSC and WebSocket learn and sources.** They are outside this report. MIDI arrives through plan E4, offline.
