# MuzViz Studio Pro

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
Visuals driven by the microphone instead of a track.
_Avoid_: Mic mode

**Capture**:
Visuals driven by another app's audio (Android playback capture).
_Avoid_: External audio, visualize other apps (UI label only)

## Watching

**Audio features**:
What the analysis hears in one moment of audio: levels, bands, onsets, beat, bar, key, section.
_Avoid_: FFT data, spectrum (the spectrum is one feature)

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
The fly-through style family: one camera diving along a path (straight, serpent, helix, accordion, orbit) through repeating lattices, jewels, mushrooms, gyroscopes and mandalas, switching its own cosine palettes on drops.

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
One of the six Customize knobs (Energy, Speed, Complexity, Mood, Space, Chaos), -1..1 with "as styled" at the centre, laid over every style by `core/viz/Macros`. It never writes a slider; it shapes the look, the sound tuning, the camera's music moves and a few routes of its own.
_Avoid_: Meta control, master knob

**Macro preset**:
A named set of the six knob positions: the built-in starters (Calm, Groove, Rave, Dreamy, Wide, Glitch) or the user's own (saving is Pro). Separate from look presets.

**Event recipe**:
A named bundle of reaction routes offered in the Events tab ("Kick punch", "Drop burst"), turned on and off as one.
_Avoid_: Trigger preset

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
The multi-lane timeline editor where a video is cut.
_Avoid_: Editor (the code name for its model), timeline (one part of it)

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

**Take**:
A recording of a live performance: every scene and param change, timed, replayable as keyframes.
_Avoid_: Recording, session

**Template**:
A shareable recipe for a video (look, layout, text slots, export settings) that fills itself in from any track.
_Avoid_: Video template (code name), format

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
The part of the sound engine that hears music rather than sound: tempo and beat (Rhythm), key, chords and scale (Harmony), and sections, build-ups and drops (Form). See `docs/research/musical-layer.md`.

**Beat phase / bar phase / phrase**:
Where the music is within its beat, its 4-beat bar and its 8-bar phrase, 0 at the start, 1 at the end. A downbeat is the first beat of a bar.
_Avoid_: Beat clock (the hit clock is a different thing)

**Section**:
A stretch of the song that sounds of a piece (verse, chorus, breakdown). A section change fires on a downbeat, one bar after the boundary; a returning section keeps its number.
_Avoid_: Scene (a scene is a visual), part

**Section state**:
The song part a section is read as: intro, verse, build, drop, breakdown or outro.

**Build-up / drop**:
A build-up is a passage getting louder, brighter and busier with the bass pulled out; the drop is the bass coming back after it.

**Feeling**:
What the music feels like now: valence (sad … happy), energy and tension, and which of the nine GEMS feelings (wonder, transcendence, tenderness, nostalgia, peace, power, joy, tension, sadness) leads. Three quarters from the scale (measured), a quarter from the key (Schubart's tradition). Never shown as a word. See `docs/research/auto-director.md`.
_Avoid_: Mood label, emotion name

**Auto-director**:
Settings › Behaviour's switch that lets the feeling choose a look at each section, drop or key change: palette, macro knobs, camera moves and what reacts to what, on every style. Pro; free gets the first minute of each song.
_Avoid_: Auto visuals (that switches styles on a timer)

**Look**:
One auto-director decision: a palette, a behaviour and a set of routes, glided in over a bar. Never the same look twice in a row of six.

**Note & key reactions**:
Settings › Behaviour's switch that lets notes, chords, key and mood move every style inside the auto-director's look: a colour per note, a bloom per note onset, leaps swing and steps glide, tension bends and resolution smooths, a chord change shifts the fold count, drums weighted by how percussive the track is. Free and on by default; Customize › Advanced › Note & key (Pro) tunes it per style. See `docs/research/note-key-response.md`.
_Avoid_: Note mode, melody mode

**Bloom (note bloom)**:
The paradoxical fractal flower one note grows over the picture (petals that hold the flower again), fading as the note ends. Four looks (flower, glow, ripple, burst), the style's own unless Customize › Look › Note blooms picks one. Not the `bloom` glow parameter.

**Personality (note layer)**:
Note & key reactions' character for a play: how many blooms at once, how fast, how big, how strongly the notes tint the picture, how fast the blooms travel, how far they tilt. Rolled at random per play and again at every section and drop.
_Avoid_: Flower effect, note flash

**Percussiveness**:
How much of the track is drums, measured as it plays (hit and onset rate, noise, against pitch confidence and key clarity), 0 tonal … 1 percussive. It scales how much the drums move the picture.
_Avoid_: Drum mode, genre

**Style knobs**:
The six per-style controls every style may read (`uKnob`): PULSE, TEXTURE, FLOW, TONE, BUILD, SPACE. Driven by Note & key reactions, else 0.5. Not Customize › Macros.
_Avoid_: Macros (those are the user's six)

**Scale mood**:
The brightness (dark Locrian … bright Lydian) and tension of the scale the music is in, from the musical intelligence database (`docs/musical-intelligence`).
_Avoid_: Emotion (it is a design reading, not a measurement)

## Code

**Engine**:
Code that turns sound into numbers or numbers into pixels: DSP, analysis, feature maths, reactions, scene parameter maths, camera, randomizers and the scene and style metadata. It lives in C++ (`core/`). See ADR 0003. Not the `:engine:*` Gradle modules, which still hold both engine and shell Kotlin.
_Avoid_: Core logic, backend

**Shell**:
The Kotlin around the engine: JNI declarations, Android lifecycle, Media3 and MediaCodec wiring, UI, stores, permissions, file IO, product rules (Pro) and display labels.
_Avoid_: Wrapper, glue layer
