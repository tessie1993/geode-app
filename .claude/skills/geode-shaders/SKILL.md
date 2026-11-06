---
name: geode-shaders
description: Use when writing or fixing a Geode GLSL style or pass in app/src/main/assets/shaders - the lib_* includes, uniforms, knobs, families, fluid looks - and to compile-check or render one without a device.
---

# Geode shaders

Shaders are GLSL ES 3.00 (`#version 300 es`) in `app/src/main/assets/shaders/`. They are not Shadertoy shaders: the general `shader-art-coding` skill is fine for technique (SDFs, noise, raymarching), but its entry point, uniforms and includes are not ours. Follow this file for those.

## House rules

- Shared code comes in by `//#include lib_x`. The allow-list is `core/viz/ShaderIncludes.inc` (compiled into `core/viz/ShaderSource.cpp`, read by `check_shaders.py` and `glslcheck`): a new `lib_*` file must be added there.
- Every style hears sound through `lib_sound` (`uSound`, `uSoundTex`, `sndChroma`). `lib_scene_uniforms` pastes it in, together with `lib_zoom` and the six knobs `uKnob[6]` (k0 PULSE … k5 SPACE; exactly 0.5 while Note & key reactions are off).
- 3D styles read the camera through `lib_camera` (`core/viz/CameraRig`). They dive by flying the rig, never by zooming UV.
- 2D styles get endless zoom from `lib_zoom` (two layers an octave apart). In Liquid mode `LiquidPass` replaces it.
- Families share `lib_<family>` (`arbor`, `bulb`, `chrys`, `lucid`, `dive`, `glass`, `nebula`, `terra`, `truchet`, `foam`); the neon set shares `lib_neon`; simulation scenes present through `lib_relief`. The catalogue of ids, kinds and families is `core/viz/Catalogue.cpp`.
- Time rule: `uTime` is the speed-integrated clock (`ShaderScene` adds `speed * dt` each frame), so a shader never multiplies it by `uSpeed` again: that squares Speed and makes every Speed change (slider, LFO, macro, route) jump the picture. Write `uTime * 0.3`, not `uTime * uSpeed * 0.3`. The same goes for every accumulated clock: for a Speed-scaled flow read `uFlowTime`, not `uFlowPhase * uSpeed`; for Speed-scaled band clocks read `uSoundTimeSped`, not `SND_*_TIME * uSpeed` (all integrated in `core/viz/SpeedClock.hpp`). `tools/check_shaders.py` fails on any statement that puts one of these clocks with `uSpeed`.
- Photosensitivity: every frame goes through `safety::apply` (`core/viz/VisualSafety`). Never add a path around it.

## Check without a device

```sh
python3 tools/check_shaders.py "$(command -v glslc)"     # every shader and fluid look; CI's compile gate
cd tools/glslcheck && node check.mjs --shader <id> --out out/<id>.png   # render one frame, then look at it
```

`check_shaders.py` bumps `300 es` to `310 es` for SPIR-V, so it catches syntax, types and undeclared names, not ES 3.00-only limits; `glslcheck` compiles under the ES 3.00 grammar and shows the pixels. Look at the PNG before calling a visual change done. CI's visual gate (`--stills`, judged by `core/tests/visual_gate.cpp`) fails a style whose stills are mostly black, blown, or flash enough to engage the FlashLimiter; fix the style, or baseline it with a reason in `core/tests/visual_gate_baseline.txt` (thresholds: `tools/glslcheck/README.md`, "Stills gate"). Thumbnails (`assets/thumbs/<id>.webp`) are rendered by the manual `render-thumbs.yml` workflow, never drawn by hand.
