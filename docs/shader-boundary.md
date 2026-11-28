# Shader / Uniform Boundary

## Purpose

Keep BeatRealms’ GLSL focused on pixel math while resolving UI, look, sound, camera, quality, and frame-invariant state on the CPU. The boundary is intentionally explicit so a future style can be added without reintroducing per-pixel UI policy or duplicate LUT work.

## Ownership rule

> If a value depends only on uniforms, UI state, sound state, camera state, quality state, or the clock, compute it once outside the fragment shader. If it depends on `vUv`, `gl_FragCoord`, a state texture, or per-pixel noise, keep it in GLSL.

## Layers

### UI/controller layer

Owns:

- Compose layout and sheet geometry.
- Insets, safe areas, blur rectangles, corner radius.
- User look, palette stack, quality, post-FX choices.
- Display/window size and orientation policy.

Output:

- Stable renderer input structs.
- Normalized regions and quality settings.
- No GLSL source generation for UI policy.

### Renderer/uniform layer

Owns:

- Frame descriptors and change detection.
- Palette packing and fixed color samples.
- Rotation cos/sin, drift, posterize levels, inverse gamma, aspect and pixelate terms.
- Field, Glass, Silk/relief, Life-step, UI-blur mask, and composite descriptors.
- Uniform location caching and memoized uploads.
- Compile-time feature specialization and pass selection.

Output:

- Small typed uniform blocks/arrays.
- Only active passes and textures.
- No raw per-frame `glUniform*` calls outside `UniformCache`.

### GLSL layer

Owns:

- Palette interpolation at pixel-dependent `t`.
- SDFs, noise, raymarching, advection, reaction-diffusion, refraction and shading.
- Texture sampling and per-pixel post-processing.
- Pixel-dependent UV transforms and color math.

Forbidden:

- Rebuilding frame descriptors per fragment.
- Reconstructing palette/LUT data already supplied as uniforms.
- Repeating fixed-offset palette/color/trig calculations per pixel.
- Fetching textures for disabled features.
- Deciding UI visibility/lifecycle policy.

## Current correct patterns

- `ShaderScene::uploadParams()` packs palette stops into `uPalStops`; `lib_palette.glsl::palStops(t)` interpolates per pixel. This is the correct LUT-to-uniform boundary.
- `CompositeDescriptor`, `CompositeTerms`, and `CompositePass` precompute rotation, camera, drift, grade, hue, lens, and feature terms on the CPU.
- `SceneViewTerms` and `SceneGradeTerms` resolve the shared `view()`/`grade()` frame constants once in `ShaderScene`; `UniformCache` uploads the packed vec4s only when they change.
- `UiBlurRegion` normalizes sheet bounds in Compose; `StageSurfaceController` converts corner radius to buffer pixels; `UiBlurPass` converts to GL coordinates.
- `Renderer::surfaceChanged` updates width/height, viewport, and render targets on resize; aspect/projection terms are recomputed when surface proportions change, not per fragment.
- Model/view/projection and rotation matrices are composed on the CPU and uploaded through `UniformCache`; matrix multiplication order remains explicit.
- Texture compression and extension support are device-specific. Query support before use and declare any required compressed format with `<supports-gl-texture>`; this refactor does not add texture-based LUTs.
- `FluidLook` specializes shader keywords and prepares bloom curves on the CPU.
- `UniformCache` and `UniformMemo` already provide location caching and unchanged-value suppression.

## Known violations to remove

| Area | Current per-fragment work | Target owner |
|---|---|---|
| `lib_field3d.glsl::fieldSetup()` | Surface/form/flow/chaos, octaves, sound swell, relief, folds, stride, LOD, tone/glow | `FieldFrameTerms` |
| `lib_glass.glsl` and Glass style mains | Setup, ear/listen/look/light, fixed palette colors, IOR/spread/bounces/cut/rotation | `GlassFrameTerms` |
| `silk_show.glsl` | Fixed palette colors, melody trig, relief tilt/light/look, fold constants, sparkle | `SilkPresentTerms` |
| `life_step_frag.glsl` | Resolution/aspect/Gray-Scott scales, ring weights, growth denominators, angle-invariant normalization | `LifeStepTerms` |
| `ui_blur_mask_frag.glsl` | Rect center/half-size/clamped corner | `UiBlurMaskTerms` |
| `fluid_splat_batch_frag.glsl` | Segment inverse length/radius/mode per splat per pixel | CPU-packed splat descriptors |
| `fluid_vorticity_frag.glsl` | Temperature fetch when buoyancy is zero | Feature guard/variant |
| `silk_show.glsl` | Final simulation sample fetched twice | Relief returns final sample |
| `composite_frag.glsl` | Duplicate hue dot; unused initial post-FX in slide/zoom | Local CSE/branch cleanup |

## Descriptor design

Descriptors are plain C++ structs with explicit layouts and equality operators. They are resolved on the stage/render thread, uploaded through `UniformCache`, and replaced rather than indefinitely expanded.

Initial descriptors:

- `SceneViewTerms`: aspect, pixelate scale, kaleidoscope segment, morph fade, rotation matrix, drift phase, twist exponent, warp amplitude.
- `SceneGradeTerms`: posterize levels, inverse gamma, temperature shift, brightness product.
- `UiBlurMaskTerms`: rect center, half-size, clamped corner radius.
- `LifeStepTerms`: texel, aspect, kernel scales/weights, growth denominators, normalization.
- `FieldFrameTerms`: all uniform-only `fieldSetup()` outputs.
- `SilkPresentTerms`: fixed colors, melody terms, relief frame terms, fold constants, sparkle.
- `GlassFrameTerms`: setup/listen/look/light/fixed colors and style constants.

Uniform budgets must be measured. When a descriptor replaces old uniforms, remove the old path after all readers migrate.

## Upload API

`UniformCache` is the only per-frame upload API.

- Add missing `Uniform` IDs for Life, Silk, UI blur, field, and Glass.
- Scalar, vector, array, sampler, and matrix setters all go through the cache.
- Remove `ShaderScene::set1f()` raw driver calls.
- Keep raw `glUniform*` only inside cache implementation.
- Use compile-time keywords for expensive optional features where a uniform branch would still execute both paths on mobile GPUs.

## Incremental migration order

1. Behavior-preserving local cleanup: duplicate dot, unused post-FX, zero-buoyancy temperature fetch, Life normalization. MacCormack keeps its bilerp because sampling `uForward` added a texture fetch and changed half-float rounding.
2. `UiBlurMaskTerms` and `UniformCache` conversion.
3. Consolidate Life/Silk/UI-blur uniform IDs.
4. Shared `SceneViewTerms` and `SceneGradeTerms` (done for `lib_scene_uniforms.glsl` and `lib_scene_grade.glsl`).
5. `LifeStepTerms` and `FieldFrameTerms`.
6. `SilkPresentTerms` and relief final-sample contract.
7. Fluid splat packing and advection specializations.
8. `GlassFrameTerms`, then migrate Glass styles individually.
9. Delete obsolete uniforms/helpers after every reader is migrated.

## Validation

- `tools/check_shaders.py <glslc>` in CI.
- `tools/glslcheck/check.mjs` where applicable.
- `core/tests/uniform_memo_test.cpp`.
- `core/tests/style_reads_test.cpp`.
- `core/tests/liquid_pass_test.cpp`.
- Pixel-diff or golden validation for Life, Silk, Glass, fluid, composite, and UI blur.
- Export/live parity checks.
- AGI Frame Profiler review of ALU, texture, branch, register, and precision counters on a physical device.

## References

- `kb://android/agi/frame-trace/shader-performance`
- `kb://android/games/optimize/optimization-tips`
- `kb://android/develop/ui/views/graphics/opengl/draw`
- `kb://android/develop/ui/views/graphics/opengl/motion`
- `kb://android/develop/ui/views/graphics/opengl/projection`
- `kb://android/develop/ui/views/graphics/opengl/about-opengl`
- `kb://android/games/agdk/configure-graphics`
