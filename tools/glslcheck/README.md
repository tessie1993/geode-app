# glslcheck

Compiles and renders the app's shader assets headlessly, and writes the
frame to a PNG so the result can be looked at rather than trusted.

It answers two questions per shader - does the GLSL the native core would
assemble compile under an ES 3.00 grammar, and does one frame of it come out
non-black - and nothing more. `tools/check_shaders.py` (run by CI with the
NDK's glslc) is the compile gate; this is the look at the pixels.

```sh
cd tools/glslcheck
node check.mjs --list                          # the fragment style ids, from core/tests/catalogue.txt
node check.mjs --shader kifs --out out/kifs.png
node check.mjs --all --size 96                 # every fragment style, compile + frame stats
node check.mjs --fluid-display --out out/fluid # every SHADING/BLOOM/SUNRAYS x LOOK variant
node check.mjs --shader curl_bloom --time 61   # a different point on the clocks
node check.mjs --shader bulb_prism --size 270 --height 480 --out out/p.png  # phone-shaped frame
```

Exit status: 0 when every job compiled and rendered, 1 when any failed, 2
when the run could not start (a bad flag, an unknown style, a uniform with no
value).

## How it stays honest

- The style ids are the `SHADER`-kind `style|` lines of
  `core/tests/catalogue.txt`, the text table of `core/viz/Catalogue.cpp`
  (`geode_catalogue_text`). `catalogue_test` checks that file against the
  catalogue, so `--all` covers what the app ships: the loose styles and every
  style family.
- Includes are resolved the way `core/viz/ShaderSource.cpp` resolves them:
  the registered names are read from `core/viz/ShaderIncludes.inc`, the
  allow-list that file compiles in (`kIncludes`), the line
  pattern is the same anchored `//#include <name>`, nested includes are
  resolved too (`lib_sound` inside `lib_scene_uniforms`), and an unknown or
  circular name is a hard error.
- Every uniform the resolved GLSL declares must have a value in the table in
  `check.mjs` or the run refuses to render. The table mirrors
  `ShaderScene::uploadParams` / `uploadMotion` / `uploadTouch` and
  `SoundUniforms::upload` (`uSound`, `uSoundTex`, `uSoundDrive`,
  `uSoundEvents`, `uSoundCounts`) with `SceneParams` defaults and a
  mid-loudness sound frame. A uniform the harness forgot defaults to zero in
  GL, and a zeroed feature is indistinguishable from a broken one.
- `dropped[...]` in a report line lists uniforms the linker removed because
  the shader never reads them. That is a finding about the shader, not an
  error.
- Textures are named stand-ins (`sound`, `palette`, `dye`, ...), RGBA8 where
  the app uses float formats, so `uSoundTex`'s waveform row reads 0..1 rather
  than -1..1.

## Stills gate

`--stills <file.tsv>` is the input of CI's visual quality gate (#470, the
`visual-stills` and `visual-gate` jobs in `.github/workflows/android.yml`):

```sh
node check.mjs --stills out/stills.tsv --out out/stills       # every style; --shard 2/4 for a quarter
g++ -std=c++20 -O1 -Icore core/tests/visual_gate.cpp -o visual_gate
./visual_gate core/tests/visual_gate_baseline.txt tools/glslcheck/out/stills.tsv
```

Every style plays the sound engine's own output, not a hand-made groove:
`out/sound_frames.tsv` (write it first with `sound_rate_test --write`, or point
`GEODE_SOUND_FRAMES` at one), each `GeodeSoundFrame` scalar at 60 fps
over `sound_rate_test`'s 20 s song (verse, build, a drop with four on the
floor and sixteenth hats, silence, verse). Its columns are matched to
`lib_sound`'s `SND_*` names, so the two cannot drift silently. The held
events, counts, Speed clocks, beat and bar oscillators and camera travel
follow the song too; `uSoundTex` is a stand-in that scales with the volume.

Per style it writes four stills at 96 px (song seconds 4, 9.5, 12, 18.5) and
the run from 9.5 s to 12 s (the end of the build into the drop) at 32 px and
60 fps, each frame boxed to a 16 × 16 grid. `visual_gate` judges them:

| Check | A still or run fails when | The style fails when |
|---|---|---|
| black | more than 92 % of pixels under display luma 0.02 | half its stills or more |
| blown | more than 25 % of pixels with R, G and B all past 0.95 | half its stills or more |
| render | the style does not compile, link or draw | once |

Why these numbers: a frame with 92 % black pixels has nothing on it (Morph
World's darkest world was 13 %, thin-line styles sit near 90 %). One dark or
bright still is a moment in the music; half the song is a broken style. There
is no flash check: the app's output flash limiter was removed, and the gate's
flash rule, which was that limiter's code linked in, went with it.

Styles that fail today are listed with a reason in
`core/tests/visual_gate_baseline.txt`; only a failure not listed there fails
CI. `./visual_gate --write <baseline> <tsv>...` rewrites the list on purpose.
An entry that passes again is reported as stale; remove it by hand. The
still PNGs are in the `visual-stills-*` artifacts: look at them before
baselining a style.

Not covered: scenes glslcheck cannot draw (MilkDrop presets, the simulation
and compute scenes); the fluid looks and Liquid mode, which have their own
modes (`--fluid-display`, `--liquid`) but not the gate yet; the endless zoom
stage (`--dive <id> [--speed <octaves/s>] [--frames <n>]`: the style, then
`dive_frag.glsl` over it, n frames across one octave of travel); and frame time,
which only a device measures (`docs/DEVICE_CHECKS.md` item 43).

## What it cannot tell you

Rendering is ANGLE over SwiftShader, which ignores precision qualifiers,
tolerates uniform counts real ES 3.0 drivers reject, and is not a Mali or an
Adreno. This is a lower bound on portability, not a device check.
`docs/DEVICE_CHECKS.md` is still the authority.

The browser driver is `cdp.mjs`, a dependency-free Chrome DevTools Protocol
client (Node 22's global `WebSocket` and `fetch`). The Chromium binary comes
from `/opt/pw-browsers/chromium`; override with `MUSICVIZ_CHROMIUM` (on
Windows, point it at an installed `chrome.exe`). Rendered output goes under
`out/`, which is ignored.
