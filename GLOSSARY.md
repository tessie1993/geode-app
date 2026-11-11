# BeatRealms 3D

A music player that draws reactive visuals from the music it plays, and a studio that turns those visuals into videos. One context: listening, watching and making are three views of the same track.

## Listening

**Track**:
One audio file in the user's library, from MediaStore or a granted folder.
_Avoid_: Song, file, media item

**Library**:
Every track the app can see, plus the user's playlists, smart playlists and favourites.
_Avoid_: Collection

**Queue**:
The ordered list of tracks that plays next.
_Avoid_: Playlist (a playlist is saved; the queue is the current run)

**Smart playlist**:
A playlist defined by rules over track facts (artist, length, play count…) rather than by a fixed list.

**Live input**:
Visuals driven by the microphone: layered over the track while one plays, alone while none does.
_Avoid_: Mic mode

**Capture**:
Visuals driven by another app's audio (Android playback capture).
_Avoid_: External audio, visualize other apps (UI label only)

## Watching

**Audio features**:
What the analysis hears in one moment of audio: levels, bands, onsets, beat, bar, key, section.
_Avoid_: FFT data, spectrum (the spectrum is one feature)

**Loudness (loudnessM / loudnessS)**:
The music's absolute volume, measured as fosfora does: BS.1770 K-weighted LUFS over 400 ms (momentary) and 3 s (short-term), -60 LUFS = 0 … 0 LUFS = 1 on every song. Never relative to the song's own average (that is what the bass, mid and treble ratios are). `core/sound/LufsMeter`.
_Avoid_: Level (the sound frame's `level` is the RMS in dBFS, also absolute, but not K-weighted)

**Silence gate**:
The one flag every detector reads: opens when the momentary loudness reaches -55 LUFS, closes after 250 ms under -60, starts closed. Not the wallpaper's rest rule (`Silence`), which is about when to draw.

**Busy**:
How dense the music is, 0..1, on fixed scales: onsets a second (8 = 1), kicks and snares a second, and how sure the beat is.

**Drive**:
How much the music is, 0..1: short-term loudness × (0.35 + 0.65 · busy), eased. The magnitude of every audio-driven motion is its gain (`sound/DriveGain`: 0.15 at no drive, 1 at 0.7 and up), so an ambient pad moves the picture a little and a loud dense track fully. The relative ratios only shape the pulse inside it.
_Avoid_: Audio drive (the user's Customize amount, which scales the reach of the music further)

**Feature timeline**:
The audio features of a whole track, computed ahead of time, so an export is frame-exact.
_Avoid_: Analysis cache (that is where it is stored)

**Scene**:
One kind of visual the engine can draw (a fluid, cymatics, MilkDrop…), named by a scene id.
_Avoid_: Visualizer, effect, mode

**Style**:
A named look within a scene family, picked from the style catalogue.
_Avoid_: Variant, skin

**Style family**:
Twelve fragment-shader styles built on one shared GLSL library (`lib_<family>`), each with its own geometry and its own way of hearing the music. The first is the basic one and is free; the rest are Pro. The families are Arbor, Bulb, Chrysanthemum, Lucid, Dive, Glass, Nebula, Terrain, Truchet and Sphere Foam.
_Avoid_: Pack, collection, scene family (that is the renderer, such as Cymatics or Fluid)

**Arbor**:
The fractal-tree style family: branching folded distance fields, flown through or orbited.

**Bulb**:
The Mandelbulb style family: ray-marched power-N fractals and their Julia relatives.

**Chrysanthemum**:
The petal style family: polar petal fields and mandalas that bloom out of a glowing core.

**Lucid**:
The domain-warp style family: a noise field sampled through itself several levels deep, often folded into a kaleidoscope.

**Dive**:
In Customize, the category of the endless zoom (its switch, Dive speed and the tunnel's shape). Also the fly-through style family: one camera diving along a path (straight, serpent, helix, accordion, orbit) through repeating lattices, jewels, mushrooms, gyroscopes and mandalas, switching its own cosine palettes on drops.

**Glass**:
The refraction style family: ray-traced glass shapes, dispersion, thin-film bubbles and caustic light.

**Nebula**:
The space style family: layered dust, spiral galaxies and gas volumes flown through. Not the loose `nebula` style, which predates it.

**Terrain**:
The landscape style family: ray-marched heightfields (ridges, dunes, canyons, islands, moonscapes) flown over or framed.

**Truchet**:
The tiling style family: randomly turned tiles whose arcs and lines join into continuous paths, rewired by onsets.

**Sphere Foam**:
The sphere-packing style family (`foam_*`, `lib_foam`): falls down a curving bore through, or orbits a ball of, a 3D Apollonian foam of spheres inside spheres, one material per style. Not Fractal Tunnel, the loose style ported from the same PR #7 shader, which keeps the rig's own flight.

**Scene params**:
The full set of tunable values a scene is drawn with at one moment.
_Avoid_: Settings, knobs

**Customize tabs**:
The pages the scene params are edited in, simple first: Macros, Look (what every style shares), the style's own page (titled with its name), Events, then Advanced. The engine still rolls and resets by its own groups (Shape, Colour, Motion, Effects, Sound response, Scene, Fluid, Cymatics).

**Advanced section**:
One card in Customize › Advanced (Sound response, Reactions, Layers, Camera, Transitions, Gradient maker, Flow field & water ripples, Shader code), opening its own page. All Pro.

**Wave**:
One of the three modulation slots: an LFO that runs steadily or follows a sound, moving one control.
_Avoid_: Slot, LFO (in the UI)

**Keep when shuffling**:
The per-control lock that holds a control still when Randomize rolls its page.
_Avoid_: Lock (that word belongs to Pro)

**Reaction route**:
One line of the reaction matrix: a sound (band, level or event) moves one scene param, by following, stepping, spinning or jumping.
_Avoid_: Binding, mapping

**Jump**:
A route mode: each hit holds a new random value for its target (sample and hold), seeded so an export draws the same ones.

**Toggle**:
A route mode: each hit flips its target between rest and the full amount, such as a palette swap per phrase.

**Phrase**:
Eight bars of music, or fewer when the analyser hears a section change. Phrase beat A fires on the beat in even phrases and phrase beat B in odd ones, so a style can answer the beat one way, then another.
_Avoid_: Section (that is the analyser's structure boundary)

**Style DNA**:
The reaction routes a style plays until the user edits them, generated from the style id and a seed.

**Macro**:
One knob of the macro bank (`core/viz/MacroBank`): a name, a value -1..1 with "as styled" at the centre, and up to twelve links, each a scene param and how far a full turn up and down moves it, as a fraction of its range. Global, laid over every style after the auto-director and before the LFOs; it adds to the slider's value and never writes it. Four built in (Glow, Heat, Bend, Flow); the user's own come with the knob editor.
_Avoid_: Meta control, master knob

**Event recipe**:
A named bundle of reaction routes offered in the Events tab ("Kick punch", "Snare crack"), turned on and off as one.
_Avoid_: Trigger preset

**Event**:
A moment the engine reacts to: a kick, a downbeat, a drop, a build starting, a key change, a style switch. It has a strength (0..1) and sometimes a value (a section id, a key). Raised once a frame from the sound frame or by the engine; the kinds are one table (`core/signal/events/EventKinds.inc`).
_Avoid_: Trigger (that is the sound frame's field an event comes from), moment (the Music page's groups of sources)

**Event route**:
A reaction route whose source is an event: on every Nth matching event, it does one thing to one param (set it, nudge it, glide it, step it, toggle it or pulse it). Event recipes are bundles of these. A pulse on a brightness-like param is slowed and limited to three a second.

**Param phase**:
What one param's value is doing: at rest, gliding to a new value, held under a finger, on trial (a locked value the user is trying, which springs back), or driven by a timeline. A finger outranks the engine, and a timeline outranks both while it drives (`core/signal/params/ParamState.hpp`).
_Avoid_: Param mode, lock state

**Modulation**:
LFOs and ADSR envelopes that move scene params over time or on hits.

**Layer**:
A second scene drawn over the main one with a mix amount and blend mode.
_Avoid_: Overlay (overlay is a Studio lane)

**Transition**:
How one scene or clip hands over to the next.

**Auto visuals**:
The engine choosing and switching scenes for you, on musical events.
_Avoid_: Auto mode, autopilot

**Preset**:
A saved scene id plus its scene params (and optionally a custom shader or a MilkDrop preset), shareable as a link or file.
_Avoid_: Profile, config

**Milk preset**:
A MilkDrop `.milk` file, drawn by the MilkDrop scene; a milk pack is a zip of them.

**Theme pack**:
The app's own look: palette, motion, material, sounds and per-component surface art, named after a stone.
_Avoid_: Theme (alone), skin

## Making

**Studio**:
Where videos are made and kept: Render (one tap, a batch or a loop) and My videos (every render and recording). It has no editor: the owner dropped video editing from the launch (#227).
_Avoid_: Editor (the code name for the project model), timeline

**Project**:
A Studio folder: one edit (lanes, clips, markers, keyframes) that the render path draws and no screen edits, its own export settings and every video rendered from it. A **Loop** project draws a short seamless loop and repeats it to a long length.
_Avoid_: Untitled, document, session

**Destination**:
Where a video is meant to go (TikTok, Shorts, Feed, Square, YouTube, Master, Custom). Each has export defaults in Settings › Export, copied into a new project.
_Avoid_: Platform preset (the code's starting values)

**Lane**:
One horizontal track of the Studio timeline, holding one kind of content: visual, media, text, overlay or audio.

**Clip**:
A span on a lane: what plays, from where, for how long.
_Avoid_: Segment, item

**Studio clip**:
A video file from the device offered for use as a clip.

**Keyframe**:
A value pinned at a time on a curve, for a scene param or a clip param.

**Marker**:
A named instant on the timeline, often tapped in live to the beat.

**Recording**:
A live video of the stage and what it hears, made by Record (a hold on the visual, Pro) until the user stops it, then saved like a render and listed in My videos.
_Avoid_: Take (the replayed performance it replaced), session

**Template**:
A project minus its audio: lanes, looks, text, overlays and export settings, fitted to whatever track a new project is made over. A **Batch** is one template over many tracks, one project each.
_Avoid_: Video template (the older look-level format, replaced in wave 1), format

**Export**:
Rendering a video file from a track, a template or a Studio project.
_Avoid_: Render (the GPU part of an export), save

**Loop render**:
An export that draws one short seamless loop and repeats it to fill a long track.

**Chapter markers**:
Section boundaries written into an export so players can jump between them.

## Surfaces

**Wallpaper**:
The visualizer as the home-screen live wallpaper.

**Dream**:
The visualizer as the screensaver (Android Daydream).
_Avoid_: Screensaver (UI label only)

**Widget**:
The home-screen now-playing widget.

**Musical layer**:
The part of the sound engine that hears music rather than sound: tempo and beat (Rhythm), key, chords and scale (Harmony), and sections, build-ups and drops (Form).

**Beat phase / bar phase / phrase**:
Where the music is within its beat, its 4-beat bar and its 8-bar phrase, 0 at the start, 1 at the end. A downbeat is the first beat of a bar.
_Avoid_: Beat clock (the hit clock is a different thing)

**Section**:
A stretch of the song that sounds of a piece (verse, chorus, breakdown). A section change fires on a downbeat, one bar after the boundary; a returning section keeps its number.
_Avoid_: Scene (a scene is a visual), part

**Track boundary**:
The player telling the engine a new track (or the same one again) has begun, with the new play's seed. The musical layer starts over and a section change fires at once, at full strength, even when the track follows gapless or crossfaded. A pause holds the song; a seek starts it fresh. The mic and other apps' audio have no player, so 2 s of silence stands in for a boundary there.

**Fade gain**:
The player's fades (pause, skip and sleep fades times the crossfade), 0..1, told to the engine because the tap sits before the volume and never hears them. A reaction source; 1 while the mic or another app's audio is the source, and always 1 in an export.

**Section state**:
The song part a section is read as: intro, verse, build, drop, breakdown or outro.

**Build-up / drop**:
A build-up is a passage getting louder, brighter and busier with the bass pulled out; the drop is the bass coming back after it.

**Feeling**:
What the music feels like now: valence (sad … happy), energy and tension, and which of the nine GEMS feelings (wonder, transcendence, tenderness, nostalgia, peace, power, joy, tension, sadness) leads. Three quarters from the scale (measured), a quarter from the key (Schubart's tradition). Never shown as a word.
_Avoid_: Mood label, emotion name

**Auto-director**:
Settings › Behaviour's switch that lets the feeling choose a director look at each section, drop or key change: its behaviour axes (offsets on the scene params), camera moves and the motion dials. Never a colour and never a route: the palette is the style's and the user's, and what reacts to what is the reaction matrix alone. Pro; free gets the first minute of each song.
_Avoid_: Auto visuals (that switches styles on a timer)

**Look**:
Everything the user set for the visual: the style, the base values of its scene params, the reaction routes, the layer, the transition, custom shader code and the MilkDrop preset. Saved, undone and gated as one, and a preset is a named look. What moves it this frame (sound, LFOs, the auto-director, touch) is modulation, never the look itself. ADR 0006 and `docs/architecture/kotlin-target.md`.
_Avoid_: State, settings, the look for one auto-director decision (that is a director look)

**Director look**:
One auto-director decision: a behaviour (axes, camera moves, motion dials), glided in over a bar and then held still. It modulates the user's look; it never changes it, and never its colours.
_Avoid_: Look (alone)

**Director reach**:
How far the auto-director goes past its director look, one native setting (Visuals › Settings, under Auto). Look only: the director look alone. Shuffle: at a new section, a drop or a long stretch it also rolls some Customize categories with the randomizer, sliders gliding, toggles flipping on the moment. Shuffle and modulate: it also runs slow waves and sound followers of its own on sliders. Both are offsets over the user's look, seeded by the play, and keep off kept controls, the style's unreached controls, a control the user moved since the last moment and the hand mask's moments. `core/viz/DirectorReach`.
_Avoid_: Auto level, auto-randomize, shuffle mode

**Look snapshot**:
Everything the user set that decides what a renderer draws (style, params, LFOs, envelopes, their own reactions, sound tuning, reduced motion, transition, background), as one value every surface draws: live, wallpaper, dream, Cast, export. An export pins one when it starts. `render/look/LookSnapshot`.
_Avoid_: Live state, look (that is the auto-director's)

**Note & key reactions**:
Settings › Behaviour's switch that reads notes, chords, key and mood into the six style knobs each style spends on its own geometry (drums weighted by how percussive the track is). It writes no scene param and no colour. Free and on by default; Customize › Advanced › Note & key (Pro) tunes it per style. See `docs/research/note-key-response.md`.
_Avoid_: Note mode, melody mode

**Bloom (note bloom)**:
Removed. The fractal flower each note once grew over every style was dropped by the owner; presets that still carry `bloomLook` or `noteBloom` load and ignore them. "Bloom" now means only the `bloom` glow parameter and the fluid glow.

**Personality (note layer)**:
Removed with the notes' tint: the layer's character is no longer rolled per play or section.
_Avoid_: Flower effect, note flash

**Percussiveness**:
How much of the track is drums, measured as it plays (hit and onset rate, noise, against pitch confidence and key clarity), 0 tonal … 1 percussive. It scales how much the drums move the picture.
_Avoid_: Drum mode, genre

**Style knobs**:
The six per-style controls every style may read (`uKnob`): PULSE, TEXTURE, FLOW, TONE, BUILD, SPACE. Driven by Note & key reactions, else 0.5. Not the macro bank's knobs.
_Avoid_: Macros (those are the user's bank)

**Scale mood**:
The brightness (dark Locrian … bright Lydian) and tension of the scale the music is in, from the musical intelligence database (`docs/musical-intelligence`).
_Avoid_: Emotion (it is a design reading, not a measurement)

## Code

**Engine**:
Code that turns sound into numbers or numbers into pixels: DSP, analysis, feature maths, reactions, scene parameter maths, camera, randomizers and the scene and style metadata. It lives in C++ (`core/`). See ADR 0003. Not the `:engine:*` Gradle modules, which still hold both engine and shell Kotlin.
_Avoid_: Core logic, backend

**Playout clock**:
The engine's answer to "which ring sample is the speaker playing now": the sink's position (output and Bluetooth latency included) mapped onto the ring, plus the user's A/V offset. Live surfaces read the player's ring up to it, never past it (`core/sound/interp/PlayoutClock`, `SpeakerSync`; `audio/PlayoutSink` reports the sink to it). The mic, another app's audio and Cast have no sink in between and read the newest sample.
_Avoid_: Presentation clock, sink clock (the deleted Kotlin ones)

**Shell**:
The Kotlin around the engine: JNI declarations, Android lifecycle, Media3 and MediaCodec wiring, UI, stores, permissions, file IO, product rules (Pro) and display labels.
_Avoid_: Wrapper, glue layer

**Scene params resolver**:
The engine's per-frame chain that turns the requested scene params into the ones a scene is drawn with: the param fade, then the auto-director, Note & key reactions, the director's axes, the macro bank, LFOs, envelopes, reaction routes, motion field, and the safety clamp last. It knows nothing of GL. It reads the sound frame, dt, the style on screen and the engine settings, and also returns the director's style moments and camera moves. Device policy (thermal tier, post-FX, Liquid's hot policy) and the camera rig stay in the renderer. It lives at `core/signal/Resolver`, the seam ADR 0006's later steps rebuild behind (#344).
_Avoid_: Look resolver (the look is what the user set; the resolver turns it and this frame's modulation into the scene params drawn), param pipeline

**Engine settings**:
The process-wide values the scene params resolver reads: the macro bank, the auto-director's amount, seed, reach and keep set, the Note & key switch, the hand mask, reduced motion and the note entropy. The renderer latches one copy per frame. An export pins its own copy at prepare, built natively from what Kotlin last forwarded plus the entitlement (Pro, trial or free; native applies what it means for the auto-director), with the note entropy at 0 so a re-render is the same film. A live edit never reaches a render.
_Avoid_: Globals, settings (alone), scene params

**Gain ramp**:
A change of playback level (ReplayGain, a pause, resume or skip fade, the sleep fade, the crossfade) made on the samples in `core/audio/dsp/GainStage`: linear in dB, counted in frames, starting and ending on the sample it was meant for. The shell only names a target and a length. After the tap, so the visuals never hear it.
_Avoid_: Volume fade, player volume (`player.volume` is only Cast's mute)
