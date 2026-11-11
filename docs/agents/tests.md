# Tests

One test file per line, in alphabetical order. Add a new test as its own bullet in its place.

## Where tests live

- JVM unit tests live in `app/src/test` and `engine/*/src/test`; CI runs every module's.
- Instrumented tests live in `app/src/androidTest`.
- `engine/scenes/src/test/` covers wire layouts, the saved-reactions codec, the catalogue, the params schema and the modulation-target table as Kotlin reads them, and the gyro look's on/off rule.
- `app/src/androidTest/.../ui/ReactivityTabTest.kt` is a Compose UI test of the reaction editor, run by the emulator job.
- `app/src/test/.../ui/StageIconRoomTest.kt` pins where the stage icons are drawn under each sheet height and the landscape panel.
- Put new pure tests in a JVM module where you can.
- Keep parsing that must be tested off-device out of `org.json`, which is a stub in local tests (see `render/ReactionRouteCodec`).
- Anything that needs a device goes into the PR as a short checklist.
- `tools/glslcheck` and `tools/hostlink` check shaders and the native link on the host.
- `tools/check_shaders.py` runs every fragment and vertex shader, and every fluid look, through the NDK's glslc (in `android.yml`).

## Native host tests (`core/tests/`, run by `native-host-tests.yml` on every PR)

- `absolute_level_test.cpp`: the sound engine's absolute loudness (`core/sound/LufsMeter`, the drive in `SoundEngine`).
- `auto_timer_test.cpp`: Auto's timer (interval plus spread, never sooner) and the `.milk` progress duration.
- `bit_perfect_test.cpp`: while the platform takes the AudioTrack bit-perfect, the playback DSP is pure pass-through.
- `cache_sentinels_test.cpp`: `core/viz/CacheSentinels`, the program-binary cache's crash sentinel.
- `cast_audio_test.cpp`: `core/viz/frame/CastAudio`, Cast's audio pump, as counts.
- `cast_clock_test.cpp`: Cast's frame clock: steady slots, drop not burst, monotonic PTS, the audio cut.
- `catalogue_test.cpp`: the catalogue against the old Kotlin tables, and its text against `core/tests/catalogue.txt`.
- `core/tests/catalogue.txt` is the copy JVM tests read through the `geode.catalogue` property; regenerate it with `catalogue_test --write`.
- `clip_media_test.cpp`: the Studio clip maths (`ClipGrade`, `CubeLut`) and the export's PCM conversions (`PcmConvert`, `PcmTiming`).
- `config_schema_test.cpp`: the config params schema (`core/viz/ConfigTable.inc`).
- `customize_test.cpp`: the Customize panel's map (`core/viz/CustomizeMap`), its text against `core/tests/customize.txt` (`--write` regenerates it).
- `director_reach_test.cpp`: the director's reach: seeded, kept controls, scope, ranges, glides at 30/60/120 fps, offsets over the base, Look bit-identical, the export pin, no tempo.
- `director_test.cpp`: the emotion table, the live feeling, the auto-director's decisions and its axes: no colour param touched, no route, held still between decisions, no tempo assumed.
- `dsp_handoff_test.cpp`: the playback DSP's format hand-off.
- `edge_glow_pass_test.cpp`: `frame::EdgeGlowPass`'s lifetime and what one wallpaper frame of it leaves in the target.
- `edge_glow_test.cpp`: the wallpaper edge glow's options, envelope and mask.
- `encoder_ladder_test.cpp`: the encoder fallback ladder: rungs, floor, bit rate, unknown ceiling.
- `export_passes_test.cpp`: the export post-passes' maths: watermark tiling, seam weights, transition progress.
- `feedback_motion_test.cpp`: `core/viz/liquid/FeedbackMotion`: each term against MilkDrop's formula, 30/60/120 fps, silence, the fold.
- `fluid_math_test.cpp`: the fluid engine's solve plan, MacCormack, heat and tracer maths, against CPU references of the passes.
- `gain_ramp_test.cpp`: the playback DSP's gain ramps, ReplayGain on the boundary, crossfade, sleep fade.
- `gltf_import_test.cpp`: glTF import: every limit, the fit and its counts, refusals and the low-detail copy.
- `gltf_import_test.cpp` also covers the textures: caps, mips, ASTC and ETC2 decoded back, the memory fit and drops, on .glb, PNG and KTX2 fixtures the test builds itself.
- `gyro_look_test.cpp`: phone tilt to camera look.
- `hls_segments_test.cpp`: Cast's HLS timing: media-time EXTINF, TARGETDURATION, the playlist, the fragment rewrite.
- `layer_mix_test.cpp`: `core/audio/ingest/LayerMix`, the mic layered over the player's ring, mixed at the reader.
- `layer_state_test.cpp`: the process-wide Customize layer (`core/viz/LayerState`).
- `lfo_random_test.cpp`: the random wave shapes of `core/viz/Lfo`: a value per slot and cycle from the engine's seed, the same at 30/60/120 fps, a beat-timed wave on the measured tempo only.
- `lfo_random_test.cpp` also covers `core/viz/WaveRandomizer`, the roll of a set of waves.
- `library_search_test.cpp`: voice "play X".
- `liquid_test.cpp`: `LiquidMath`; `tools/glslcheck --liquid <id>` renders the pass.
- `loop_seam_test.cpp`: a loop render's seam driver in `core/viz/frame/LoopSeam`: stash or present, PTS, the 192 MB GPU budget; `LoopRender` only presents.
- `loudness_test.cpp`: export DSP, the BS.1770 meter.
- `macro_bank_test.cpp`: `core/viz/MacroBank`: the user's knobs as weighted offsets on scene params, their edits and text, the process-wide bank, C ABI `geode_macro_bank_*`.
- `mod_targets_test.cpp`: `core/viz/ModTargets`: reach, what each picker may move, and its text against `core/tests/mod_targets.txt`, read through `geode.modTargets`; `--write` regenerates it.
- `musical_test.cpp`: the sound engine's musical layer.
- `note_response_test.cpp`: Note & key reactions.
- `offline_step_test.cpp`: an export's frame grid and audio cut, drift-free over 3 h, 29.97 fps.
- `output_fifo_test.cpp`: the Oboe output's FIFO: odd chunks, underruns, the position at 44.1/48/96 kHz over pause, flush, a new stream and stop, the volume ramp, a flush under a running consumer, two threads.
- `output_test.cpp`: the offline render's planning (`core/output`: `FrameClock`, `LoopPlan`, `EncodePlan`, `Chapters`) and its C ABI.
- `pacer_test.cpp`: `core/viz/frame/Pacer`, the frame clock every surface shares.
- `params_schema_test.cpp`: the params schema: names, slots, defaults in range, get/set, the wire, fades, the LFO clamps, and its text against `core/tests/params_schema.txt`.
- `core/tests/params_schema.txt` is the copy JVM tests read through the `geode.paramsSchema` property; regenerate it with `params_schema_test --write`.
- `params_test.cpp`: the Customize randomizer and Reset tab of `core/viz/ParamRandomizer`, its palette and camera path rolls over the whole schema range; Kotlin's `ParamRandomizer` only calls it.
- `pcm_handoff_test.cpp`: MilkDrop's raw-PCM hand-off (`core/viz/PcmHandoff`).
- `pcm_ring_test.cpp`: the PCM ingest ring: random chunk sizes and rate changes mid-stream, every sample once, in order, with its rate, also with a writer thread; the old `SoundCursorTest` cases, the encodings, the C ABI.
- `playout_test.cpp`: the playout clock: 1× and 1.5×, Bluetooth latency, pause, skipped silence, seek, A/V offset, the same sample at 30/60/120 fps.
- `presets_test.cpp`: the bundled looks, pinned to the Kotlin literals they replaced.
- `push_versions_test.cpp`: `core/viz/frame/PushVersions`, which pushed inputs a live frame still owes native.
- `quality_test.cpp`: `core/viz/frame/Quality`, Settings › Performance's tiers and the first-run pick.
- `reaction_resolve_test.cpp`: the renderer's reaction resolve against the old Kotlin path, and when it re-resolves.
- `record_plan_test.cpp`: a take's maximum lengths, the stop and the file size estimate under the muxer's target.
- `renderer_cadence_test.cpp`: `core/viz/frame/Cadence`, what a live surface's frame owes the shell.
- `resampler_test.cpp`: export DSP, the resampler.
- `resolver_test.cpp`: the scene params resolver as a chain: order, reduced motion once, settings pinned against the globals, determinism, safety last.
- `resolver_test.cpp` also covers the bank's offset, an export's pinned bank, and the director leaving the bank alone.
- `resolver_test.cpp` also pins the owner's rule: with Auto and Note & key on every colour param is the user's and, with no route and the Motion dials at 0, no param hears the sound.
- `scene_residency_test.cpp`: `core/viz/SceneResidency`, which built scenes stay on the GPU.
- `shader_overrides_test.cpp`: the process-wide shader overrides (`core/viz/ShaderOverrides`).
- `sound_rate_test.cpp`: the sound engine at 30 and 120 fps equal to 60 at every shared sample, on a synthetic song and real tracks.
- `sound_source_test.cpp`: the wallpaper's ring-or-capture choice.
- `sound_test.cpp`: the sound engine.
- `speaker_sync_test.cpp`: a reader held to the speaker: the start, pause, seek, gapless, 192 kHz over Bluetooth, the mic and Cast, and another app's audio in the player's ring with the player paused.
- `studio_files_test.cpp`: takes moved out of project folders (#384).
- `style_reads_test.cpp`: shader truth: each shader style's camera rig (`catalogue::fliesCamera`, which Customize's Camera framing and feel follow) and its march against the `uCamBasis` and `uSteps` its assembled shader reads.
- `style_schedule_test.cpp`: an export segment's style schedule, which drops the styles the entitlement locks.
- `surface_motion_test.cpp`: the wallpaper's and dream's run rule under reduced motion, the still frame's settle, and each surface's fps choice.
- `surface_run_test.cpp`: the wallpaper's and dream's run rule and the trial's end, and the stage's (`core/viz/StageRun`).
- `thermal_test.cpp`: the process's one thermal state (`core/viz/frame/Thermal`): the fps-first ladder, the thermal cap on the pacer, every renderer reading one tier.
- `timeline_test.cpp`: keyframe evaluation and clip speed ramps (`core/signal/timeline`) and their C ABI.
- `touch_transform_test.cpp`: the two-finger pinch and twist of `core/viz/TouchTransform`, clamped to the schema ranges.
- `transition_library_test.cpp`: `core/viz/TransitionLibrary`, the GL Transitions library parse.
- `transition_programs_test.cpp`: the base composite program and its LRU of spliced GL Transitions variants.
- `underlay_test.cpp`: the background image's crop rect, blur, dim and clamps (#651).
- `viz_math_test.cpp`: reaction matrix, style DNA, camera math and the endless zoom (its seamless tunnel, no hit moving it, the 8-measured-beat follow, equal travel at 30/60/120 fps).
- `wallpaper_window_test.cpp`: the wallpaper's on-screen window and fps pick.
- `world_genome_test.cpp`: Morph World's genome and morph driver (`core/viz/world`).

## Journeys, UI tour, thumbnails, store shots

- Journeys: `app/src/androidTest/.../journey/` drives `MainActivity` end to end with AndroidX Test (Compose test + Espresso, Truth) under Orchestrator with `clearPackageData`.
- Seed a journey past onboarding with the `Onboarded` rule.
- `@Ignore` a regression whose fix is unmerged, with its QA number.
- UI tour: `journey/UiTourTest` is skipped unless `-e uitour 1`; look per `uitourConfig=portrait|landscape|font`.
- `UiTourTest` walks every screen, sheet and visual gesture under gesture navigation and writes numbered screenshots plus `ui-tour-report.md`/`.json` (expected vs observed, system-inset collisions).
- `ui-tour.yml` (manual) runs all three looks on one emulator and uploads them as artifact `ui-tour-<run>`.
- Style thumbnails are `app/src/main/assets/thumbs/<styleId>.webp` (~237 × ~10 KB ≈ 2.4 MB of APK), rendered, not drawn.
- No thumbnails are committed yet; only the widget reads them, Styles and Presets are text.
- The manual `render-thumbs.yml` runs `app/src/androidTest/.../thumbs/StyleThumbnailRenderTest` (skipped unless `-e thumbs 1`; `thumbsOnly=<ids>` for a subset) on the emulator against a synthetic groove and opens a `bot/style-thumbs-<run>` PR.
- The manual `store-assets.yml` runs `app/src/androidTest/.../store/` (`StoreShotsTest`, `StoreReelTest`, `StoreFramesTest`; skipped unless `-e storeshots|storereel|storeframes 1`) on one emulator cut to phone and tablet screens with `wm size`.
- `tools/store_assets_collect.py` checks every store picture against Play's sizes before the artifact `store-assets-<run>` is uploaded.
