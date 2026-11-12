# Data flow and threads

One fact per line, grouped by stage. Add a fact as a new bullet in its stage.

## 1. Source → the PCM ring

- ExoPlayer runs `MvzAudioProcessorChain` on its playback thread.
- The `PcmTap` there comes first, before any DSP.
- `NativeDspProcessor` sits after the tap and steps aside (inactive, not throwing) for formats its chain cannot run, such as 5.1 files; native decides which (`geode_dsp_runs`).
- A capture of another app uses one `AudioCapturePump` worker thread.
- The mic has no JVM thread: a native input stream (`core/audio/ingest/MicInput`, Oboe) writes its ring from the audio callback.
- The tap, or a capture of another app in its place, writes one native ring: `PlaybackSession.sampleRing`, a `SampleRing` handle on `core/audio/ingest/PcmRing`; stereo, what every sound engine hears.
- Mic and capture use one `AudioCapturePump` worker thread each.
- The tap, or a capture of another app in its place, writes one native ring: `PlaybackSession.sampleRing`, the process's player ring (`geode_pcm_ring_shared`, `core/audio/ingest/RingKinds`); stereo, what every sound engine hears.
- The player's and the mic's rings are made on first use and never freed, so a surface or writer that outlives its session never reaches a freed ring.
- The wallpaper's and dream's output-mix ring is made on the capture's first start and freed by `SoundSource.release` once its stage has closed.
- Each block is written with its own rate and converted to float inside the ring: Kotlin loops over no sample.
- The mic is a layer, never a switch: it writes its own ring (`PlaybackSession.micRing`, `geode_pcm_ring_set_layer`) and never pauses the player.
- Each renderer mixes the two rings at its reader (`core/audio/ingest/LayerMix`, `layer_mix_test.cpp`): the mic resampled onto the player's blocks, alone once the player has stopped for 0.3 s.
- MilkDrop's raw PCM is no second ring: each native renderer mixes the samples its engine heard this frame to mono (`core/viz/PcmHandoff`, in `Renderer::writeAudio`), so live, cast, wallpaper and dream all feed projectM.

## 2. Sound input

- `PlaybackSession.sampleRing` is published as `AudioBus.soundInput`.
- The rate travels with the samples, so there is no rate beside the ring.
- The tap writes a sample when the sink takes it, up to its buffer and the output's latency before it is heard.
- So behind the player's sink a renderer's cursor reads only up to the sample the speaker plays now (`core/sound/interp/SpeakerSync`).
- `SpeakerSync` reads the process's one playout clock, `PlaybackEngine.playout`, which `audio/PlayoutSink` reports the sink to.
- The mic, a capture and a cast read the newest sample instead.
- Cast's sound reads the player's ring through its own native cursor, resamples, holds and pads it to each frame's cut (`core/output/CastAudioPump`, `geode_cast_audio_*`); `CastStream` only hands each block to the encoders.
- There is no analysis thread: every renderer's own `core/sound` engine hears the ring on its GL thread through its own native cursor (`geode_viz_feed_ring`).
- The cursor resets the engine on a new epoch (a tap flush, a source switch).

## 3. Render

- The native stage thread (`core/stage`, one per surface) draws each paced vsync through `LiveClient`: `geode_viz_frame` (`core/viz/frame/LiveFrame`) and the `core/viz` renderer.
- Built scenes stay resident only while `core/viz/SceneResidency.hpp` allows: the ones drawn plus a few recent spares, fewer as the phone heats.
- A spare is resized when drawn again.
- A new scene is built across frames, never in one (`core/viz/ScenePrep.hpp`, `ProgramStager`), while the old scene keeps drawing.
- `ProgramStager` runs a bounded slice of program compiles per frame, polled through `GL_KHR_parallel_shader_compile` where the driver has it.
- So a main-thread call that waits on the GL thread never waits out a whole build.
- An export (`geode_viz_set_offscreen`) builds each scene whole on its first frame.
- A scene whose program fails, or whose compile finishes nothing for 30 s (`ScenePrep::GiveUp`), is dropped and remembered until a new context or shader edit (`ScenePrep::fail`).
- Its error goes to `lastError` and the old scene keeps drawing.
- Reaction routes are the user's (`LfoStore`) or the style's DNA.
- Customize › Music's hand mask sits beside the user's routes: the user's routes play only on the moments they set, the live DNA on the rest.
- The hand mask is `hand::` in `core/viz/Reactions`; `mergeHand` and the Music page's edits are in `core/viz/ReactionDna`; `LfoStore.pushHand` pushes it process-wide (`geode_hand_set`).
- The hand mask to push on load is native's (`geode_mod_hand_load`, `core/api/geode_modulation_api.cpp`): 0 with no routes, `HAND_ALL` for routes saved without a mask, -1 for unreadable routes, which `LfoStore` then deletes.
- The style's DNA is built natively by `core/viz/ReactionDna`: a personality for each side of the groove plus layers, each drawing its source from the sound frame.
- DNA draws from the 121 reaction sources (`ReactSource` in `core/viz/Reactions`), one per `GeodeSoundFrame` value.
- A renderer is handed the user's saved routes (`geode_viz_set_reactions`), reads the hand mask itself before each render, and resolves what plays from those, the style and `reactSeed`.
- The export's renderer (`core/output/ExportFrame`) resolves reactions the same way, so exports react like the screen.
- DNA draws from the 121 `ReactSource`s, one per `GeodeSoundFrame` value.
- Every style's DNA ends on one route from the transient detector (`Transient` or `TransientEnv`), drawn last so the routes before it keep their draws.
- A DNA already at its cap gives the transient route the slot of its last-drawn layer.
- DNA is fetched through `BuildStyleReactionsUseCase` (`geode_style_reactions`) and resolved on the GL thread by `StyleReactionsCache`.
- The export's renderer (`core/output/ExportFrame`) resolves reactions the same way, so exports react like the screen.
- `StageView` feeds the camera's look while the stage says it listens (`geode_stage_gyro_listens`): the game rotation vector and the display rotation.
- `core/viz/GyroLook` reads them as the screen's own azimuth and pitch.
- Each frame the GL thread has the native renderer take the ring's new PCM (`AudioBus.soundInput`, handed to the stage with `geode_stage_set_sound`; no Kotlin array) into its own `core/sound` engine.
- The engine analyses every 10 ms of samples inside `write`; `beginFrame` only reads.
- So every value, and the feeling the director and Note & key read from `SoundEngine::emotion`, depends on the samples, not the frame rate.
- Styles read the sound through `lib_sound` (`uSound`, `uSoundTex`, `sndChroma`).
- Auto is one mode: each new section or drop the director commits a look at is counted natively (`geode_viz_take_director_style_moments`).
- `Stage.onDirectorStyleMoment`, polled on the main thread, hands the moment to `AutoVisualsController.onDirectorMoment`, which steps the visual playlist or draws a new style from Auto's sources.
- While Customize is open Auto holds every change (MilkDrop's menu rule), and its timers start over on close.
- Auto's timer is native (`core/viz/AutoTimer`, `geode_auto_timer_*`), polled by `AutoVisualsController`.
- Auto's wait is the interval plus a random spread per look (a quarter of it, 10 s at most).
- A `.milk` preset's `progress` runs 0→1 over that wait (`core/viz/MilkProgress`).
- With Auto off or in an export, `progress` runs over projectM's 30 s from the preset's load, then holds at 1.
- The wallpaper and dream read the same `AudioBus.soundInput`.

## 4. Export

- `VideoExporter` and `LoopRender` run on Default inside `ExportRun.scope`.
- They draw one `LookSnapshot` pinned when the render starts (`ExportController.Host.look`) through `export/ExportScene`, which hands it to the native export job (`core/output/ExportFrame`).
- The export job pins its engine settings once at prepare (`geode_viz_pin_engine_settings`, #350): the macro bank, the Note & key switch, the hand mask and the director, no note entropy.
- So live edits never reach a render in progress.
- Its own sound engine hears the track frame by frame (`core/audio/ingest/TrackPump`, decoded natively, stepped at 1/fps), so export equals live.
- Before frame 0 the engine hears a pre-roll (`core/output/PreRoll`): up to 8 s before the range, or the track's opening and then a track boundary, so the first frame is as warm as live playing on.
- A track the engine cannot hear fails the render.
- An export's style switches would reach it as a `StyleSchedule` (`geode_viz_set_style_schedule`, timed by `core/viz/StyleSchedule`), but nothing sets one since the Studio project render went.
- Frames go through `EncoderSurface` (EGL on the export thread) into MediaCodec and MediaMuxer.
- Exports write MediaStore rows with `IS_PENDING` set.
- The one-tap Render (`ExportController.startExport`) is one visual over the song or a part of it, straight through `VideoExporter` under `ExportRun` + `ExportService`.
- The Studio project render, its plan, Transformer composition and clip effects went with video editing (ADR 0007, #999 F7).

## 5. Persistence

- `PlayerSession.storeScope` is `Dispatchers.IO.limitedParallelism(1)`.
