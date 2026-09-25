# Motion-reference review

The three user-supplied clips were inspected at 1 fps: 10 + 10 + 8 frames. Original files and extracted pixels remain local under `.reference/videos/`; no reference footage is redistributed. `frames/index.csv` in each clip maps every frame to source time. Measurements use the video-to-ui Pillow tools. Video pixels have no known Android dp scale, so component dimensions below are visual estimates, not device measurements.

## Observed system and screen inventory

- Clip 1, frames 001–010 (0–9 s): a single player composition. A large refractive orb over transport changes internal color. The play button compresses, a luminous release separates upward, then the seek channel gains a visibly liquid interior (frames 003–006). Best full frame: 007. Surface has depth, reflection, front light and underside shadow. Approximate hero diameter: ~340 video px; primary button ~170 px. Region palette at frame 004: neutral reflections #898E96/#A7AAB1, violet shoulder #985D9B. These are compressed rendered pixels, not constant material base colors.
- Clip 2, frames 001–010 (0–9 s): one floating playlist sculpture. Seven opalescent pebble volumes tilt in a staggered sequence and return to a column (frames 003–005 and 007–009). Best rest frame: 010. No text or interactive data is shown; labels and list semantics must be supplied by the app. Measured frame 001 region palette includes #D1C8B7, #B2A998 and #EAE6DE. Shadows and reflected floor contact convey weight.
- Clip 3, frames 001–005 (0–4 s): Library over a cyan/lavender liquid field. White native-looking text, floating controls and a persistent bottom transport. Best Library frame: 002. Full-frame dominant pixels: #90A3B8, #8192A8, #A6B2CA, #6C7E9A. The colors come from the full 3D environment rather than isolated UI fills.
- Clip 3, frames 006–008 (5–7 s): Browser moves into Now Playing. A droplet becomes the hero above a thick, continuous channel and raised play control. Best player frame: 008. Sequence is authored and gives no numerical fluid-conservation evidence. Text stays visually stable while the material forms move.

## Applied design direction

- Keep the supplied Opaline library's Tidal material family and abstract environment; use the videos for volume, camera, light and contact response. The first two videos contain a photographed lake; the library supplies authored abstract backplates and live geometry. No stock landscape is invented.
- Expose real mesh shoulders and sidewalls in `OpalineComponents.kt` instead of covering the WebGL surfaces with opaque fills (clip 1 frames 003–007; clip 3 frames 002 and 008). Maintain a fully readable fallback when WebGL is loading or unavailable.
- Make play and album ring visually dominant in the music layout; keep queue, lyrics and browsing explicit native actions (clip 1 frames 001–010).
- Register library rows as dimensional C02 parts and retain a stable inset text area (clip 2 frames 001–010; clip 3 frames 001–004).
- Default the renderer to high quality for flagship hardware, with perspective, shared highlights/shadows, real mesh deformation and bounded numerical motion. Adapt workload only under sustained pressure (all clips).
- Preserve reduced motion, interruption/cancel behavior, keyboard accessibility and immediate state changes. Artistic motion must never delay playback or route changes.

These refinements are part of the already-authorized app redesign. No additional approval pause is needed for the supplied references.
