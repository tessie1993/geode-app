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
Twelve fragment-shader styles built on one shared GLSL library (`lib_<family>`), each with its own geometry and its own way of hearing the music. The first is the basic one and is free; the rest are Pro. The families are Arbor, Bulb, Chrysanthemum, Lucid and Terrain.
_Avoid_: Pack, collection, scene family (that is the renderer, such as Cymatics or Fluid)

**Arbor**:
The fractal-tree style family: branching folded distance fields, flown through or orbited.

**Bulb**:
The Mandelbulb style family: ray-marched power-N fractals and their Julia relatives.

**Chrysanthemum**:
The petal style family: polar petal fields and mandalas that bloom out of a glowing core.

**Lucid**:
The domain-warp style family: a noise field sampled through itself several levels deep, often folded into a kaleidoscope.

**Terrain**:
The landscape style family: ray-marched heightfields (ridges, dunes, canyons, islands, moonscapes) flown over or framed.

**Scene params**:
The full set of tunable values a scene is drawn with at one moment.
_Avoid_: Settings, knobs

**Customize tabs**:
The groups the scene params are edited in: Shape, Color, Motion, FX, Reactivity, Scene, plus per-family tabs.

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
