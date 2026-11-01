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
  the registered names are parsed out of that file's `kIncludes`, the line
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
