# Data flow and threads

One fact per line, grouped by stage. Add a fact as a new bullet in its stage.

## 1. Source → the PCM ring

- ExoPlayer runs `MvzAudioProcessorChain` on its playback thread.
- The `PcmTap` there comes first, before any DSP.
- `NativeDspProcessor` sits after the tap and steps aside (inactive, not throwing) for formats its chain cannot run, such as 5.1 files; native decides which (`geode_dsp_runs`).
- A capture of another app is read on a native thread of its own (`core/audio/ingest/CapturePump`).
- The mic has no JVM thread: a native input stream (`core/audio/ingest/MicInput`, Oboe) writes its ring from the audio callback.
- The tap, or a capture of another app in its place, writes one native ring: `PlaybackSession.sampleRing`, the process's player ring (`geode_pcm_ring_shared`, `core/audio/ingest/RingKinds`); stereo, what every sound engine hears.
- The player's and the mic's rings are made on first use and never freed, so a surface or writer that outlives its session never reaches a freed ring.
- The wallpaper's and dream's output-mix ring is made on the capture's first start and freed by `SoundSource.release` once its stage has closed.
- Each block is written with its own rate and converted to float inside the ring: Kotlin loops over no sample.
- The mic is a layer, never a switch: it writes its own ring (`PlaybackSession.micRing`, `geode_pcm_ring_set_layer`) and never pauses the player.
- Each renderer mixes the two rings at its reader (`core/audio/ingest/LayerMix`, `layer_mix_test.cpp`): the mic resampled onto the player's blocks, alone once the player has stopped for 0.3 s.
- The raw PCM the scenes read is no second ring: each native renderer mixes the samples its engine heard this frame to mono (`core/viz/PcmHandoff`, in `Renderer::writeAudio`), so live, cast, wallpaper and dream all feed the scenes that read raw PCM.

## 2. Sound input

- `PlaybackSession.sampleRing` is the live ring while a session holds it (`IngestNative.pcmRingSetLive`); `AudioBus.heard` is the phone's output mix while on, else that ring.
- The rate travels with the samples, so there is no rate beside the ring.
- The tap writes a sample when the sink takes it, up to its buffer and the output's latency before it is heard.
- So behind the player's sink a renderer's cursor reads only up to the sample the speaker plays now (`core/sound/interp/SpeakerSync`).
- `SpeakerSync` reads the process's one playout clock, `PlaybackEngine.playout`, which `audio/PlayoutSink` reports the sink to.
- The mic, a capture and a cast read the newest sample instead.
- Cast's sound reads the player's ring through its own native cursor, resamples, holds and pads it to each frame's cut (`core/output/CastAudioPump`, `geode_cast_audio_*`); `CastStream` only hands each block to the encoders.
- Live, one analysis thread per ring (`core/sound/hop/AnalysisThread`) hears it through its own native cursor and publishes each hop (`core/sound/hop/SharedAnalysis.hpp`); an export's engine hears its track inline.
- The cursor resets the engine on a new epoch (a tap flush, a source switch).

## 3. Render

- The native stage thread (`core/stage`, one per surface) draws each paced vsync through `LiveClient`: `geode_viz_frame` (`core/viz/frame/LiveFrame`) and the `core/viz` renderer.
- Built scenes stay resident only while `core/viz/SceneResidency.hpp` allows: the ones drawn plus a few recent spares, none under memory pressure (`onTrimMemory`).
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
- DNA draws from the reaction sources (`ReactSource` in `core/viz/Reactions`): one per `GeodeSoundFrame` value, then sources `ReactSignals` derives from the frame and the director's feeling.
- The derived sources are the bar counts (hold, pulse, flip on every 1, 2, 4 bars and every phrase, from Rhythm's downbeats only), the melody contour, note and presence, the degree accent, valence, felt tension and the tension's rise.
- The derived sources also include the felt energy, the key's character (`KEY_SHARPNESS`), the kind of feeling (the leading GEMS kind, held until another clearly leads) and five kinds' shares (wonder, peace, power, joy, sadness).
- The derived sources also include the song part as six states (`IN_INTRO` … `IN_OUTRO`, from Form's `sectionState`).
- `SECTION_RETURN` fires when Form names a new section with the number of one heard before.
- `SECTION_PROGRESS` runs 0..1 through a section against how long the song's sections have run so far (16 bars until one has ended), holding at 1 past it.
- `CHORD_MOTION` is how fast `chordHue` moves round the circle of fifths, eased over 1.5 s; a quarter of the circle a second reads 1.
- The resolver hands the reaction matrix the Emotion state the director heard (`ReactionEngine::tick`'s `feeling`).
- A renderer is handed the user's saved routes (`geode_viz_set_reactions`), reads the hand mask itself before each render, and resolves what plays from those, the style and `reactSeed`.
- The export's renderer (`core/output/ExportFrame`) resolves reactions the same way, so exports react like the screen.
- Every style's groove ends on one route from the transient detector (`Transient` or `TransientEnv`).
- Every style's groove starts on the drums: the kick side hears `KICK`, `KICK_2` or `KICK_ENV`, the snare side `SNARE`, `SNARE_2`, `SNARE_ENV` or `HAT` (`viz_math_test` pins it per catalogue style).
- No pulse personality turns the picture: the kick or snare side moves a target every style reads, never a rotation.
- After the groove every style's DNA has four musical routes: the bars, the tune, harmony and form, feel and timbre (`viz_math_test` pins each).
- No default route's continuous source is the absolute loudness (`Level`, `Loudness`, `Vol`, `VolAtt`, `Intensity`), nor the felt energy, which carries it.
- The DNA's harmony-and-form layer is harmony for half the styles (chords, their motion, tension, key, scale tension) and form for the other half (sections, their return and progress, the song part, builds, drops, novelty).
- The DNA's feel layer can draw the key's character, the kind of feeling or its wonder, beside the mood, the felt tension and the scale's brightness.
- The groove fills at most `StyleDna::kGrooveRoutes` (8, plus one for a zoom personality's beat zoom); the DNA at most `kMaxRoutes` (13 of the matrix's 16).
- A DNA already at its cap gives the transient route the slot of its last-drawn layer.
- The export's renderer (`core/output/ExportFrame`) resolves reactions the same way, so exports react like the screen.
- `StageView` feeds the camera's look while the stage says it listens (`geode_stage_gyro_listens`): the game rotation vector and the display rotation.
- `core/viz/GyroLook` reads them as the screen's own azimuth and pitch.
- The ring (`AudioBus.heard`, handed to the stage with `geode_stage_set_sound`; no Kotlin array) is analysed by `AnalysisThread` on its own thread, every 10 ms of samples; `Renderer::beginFrame` only reads the published hop (`readSound`, `core/viz/RendererFrame.cpp`).
- The ring (`AudioBus.soundInput`, handed to the stage with `geode_stage_set_sound`; no Kotlin array) is analysed by `AnalysisThread` on its own thread, every hop of samples (fftSize / overlap, `sound/AnalysisSettings`); `Renderer::beginFrame` only reads the published hop (`readSound`, `core/viz/RendererFrame.cpp`).
- So every value, and the feeling the director reads from `SoundEngine::emotion`, depends on the samples, not the frame rate.
- Styles read the sound through `lib_sound` (`uSound`, `uSoundTex`, `sndChroma`).
- Auto is one mode: each new section or drop the director commits a look at is counted natively (`geode_viz_take_director_style_moments`).
- `Stage.onDirectorStyleMoment`, polled on the main thread, hands the moment to `AutoVisualsController.onDirectorMoment`, which steps the visual playlist or draws a new style from Auto's sources.
- While Customize is open Auto holds every change (a menu rule), and its timers start over on close.
- Auto's timer is native (`core/viz/AutoTimer`, `geode_auto_timer_*`), polled by `AutoVisualsController`.
- Auto's wait is the interval plus a random spread per look (a quarter of it, 10 s at most).
- The wallpaper and dream read the same ring (`IngestNative.pcmRingLive()`).

## 4. Export

- `VideoExporter` and `LoopRender` run on Default inside `ExportRun.scope`.
- They draw one `LookSnapshot` pinned when the render starts (`ExportController.Host.look`) to the native export job (`core/output/ExportFrame`).
- The export job pins its engine settings once at prepare (`geode_viz_pin_engine_settings`, #350): the macro bank, the hand mask and the director.
- So live edits never reach a render in progress.
- Its own sound engine hears the track frame by frame (`core/audio/ingest/TrackPump`, decoded natively, stepped at 1/fps), so export equals live.
- Before frame 0 the engine hears a pre-roll (`core/output/PreRoll`): up to 8 s before the range, or the track's opening and then a track boundary, so the first frame is as warm as live playing on.
- A track the engine cannot hear fails the render.
- An export's style switches would reach it as a `StyleSchedule` (`geode_viz_set_style_schedule`, timed by `core/viz/StyleSchedule`), but nothing sets one since the Studio project render went.
- Frames go through `EncoderSurface` (EGL on the export thread) into MediaCodec and MediaMuxer.
- Exports write MediaStore rows with `IS_PENDING` set.
- The one-tap Render (`ExportController.startExport`) is one visual over the song or a part of it, straight through `VideoExporter` under `ExportRun` + `ExportService`.
- The Studio project render, its plan, Transformer composition and clip effects went with video editing (#999 F7).

## 5. Persistence

- `PlayerSession.storeScope` is `Dispatchers.IO.limitedParallelism(1)`.
