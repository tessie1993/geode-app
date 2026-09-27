# Path-traced material and geometry references

`tools/render_cycles.py` is the offline quality route for the actual exported 3D components. It imports GLB meshes, replaces the preview materials with native Blender shader node graphs, and renders a perspective camera in Cycles. It is separate from the browser's interactive renderer.

## What is built

- Distinct gel, coloured gel, water, shell, pigment, film, nacre, stone, leaf and glow materials.
- Actual dielectric reflection and refraction at the mesh boundaries, with high transmission path depth.
- Heterogeneous internal volume scattering and absorption for gel and pigment. Density and pigment variation are evaluated in three-dimensional object space.
- Thin-film interference for the film and nacre shader families through Blender's Principled shader.
- An air substrate for bubble materials, with the water-like film represented by a thin optical boundary. This avoids accidentally rendering a closed bubble as a solid sphere of water. The nanometre film is represented by the optical model rather than an artificially thick wall mesh.
- Actual area emitters, soft shadows, coloured reflections and multi-bounce illumination.
- Transparent, 16-bit RGBA component renders, preserving a reusable cutout outside the object silhouette.
- A shared abstract specimen scene with satin geometry and broad sculptural contours. Its background contains no photography, lake photograph, landscape plate or photographic environment map.
- Editable, compressed `.blend` scenes with the imported geometry, shader graphs, camera and lighting.

The material parameters are an art direction for the Opaline set. They are not laboratory measurements of a manufactured material. Blender's RGB rendering is not a full spectral renderer. Thin-film interference does not imply full spectral dispersion, and the script makes no such claim.

## Reproduce the renders

Use Blender 4.5 or later. Run from the library root:

```bash
blender --background --python tools/render_cycles.py -- \
  --ids A01 B01 C03 C10 E08 F01 \
  --views threequarter --theme tidal --size 1024 --samples 128

blender --background --python tools/render_cycles.py -- \
  --specimen --theme tidal --size 1600 --samples 256
```

Render every GLB present, with three distinct perspective views:

```bash
blender --background --python tools/render_cycles.py -- \
  --all --views front threequarter underside --size 1536 --samples 512
```

The default CPU configuration uses eight render threads. Adjust with `--threads`. On a compatible machine select `--device OPTIX`, `CUDA`, `HIP`, `METAL` or `ONEAPI` to render with that GPU backend; the script reports an error if the requested device is unavailable. `--no-blend` writes just renders and metadata. The six shared themes are `tidal`, `opal`, `moss`, `obsidian`, `aurora` and `amber`.

Outputs go into `assets/pathtraced/` and `assets/blender/`. Every successful render gets a JSON sidecar recording the actual Blender version, resolution, maximum samples, adaptive sampling setting, time, transparency and source geometry. Those sidecars are the record of what has actually been rendered. A command's ability to render every model does not mean that all models or all material variants have already been path traced.

`assets/pathtraced/render-index.json` collects completed renders, verifies their PNG header dimensions against the metadata, and links their editable scenes and source models. It records native bit depth and file size as well.

## Quality settings and interpretation

| Matter | Interface IOR | Surface roughness | Transmission | Interior treatment |
|---|---:|---:|---:|---|
| Dense gel | 1.39 | 0.16 | 0.96 | Spatially varying scattering and coloured absorption |
| Coloured gel | 1.40 | 0.14 | 0.97 | Reduced scatter with stronger visible pigment |
| Clear shell | 1.46 | 0.045 | 1.0 | Low-density scatter and mild absorption |
| Water | 1.333 | 0.022 | 1.0 | Very low scatter with depth-dependent tint |
| Pigment | 1.37 | 0.20 | 0.80 | Dense participating colour field |
| Film | 1.0 air substrate / 1.333 film | 0.018 | 1.0 | Air-filled envelope plus 140–540 nm optical film model |
| Nacre | 1.52 | 0.23 | 0.48 | Scattering body with thin-film colour |
| Wet stone | 1.48 | 0.46 | 0 | Opaque substrate with a smooth surface coat |
| Leaf | 1.35 | 0.40 | 0.12 | Tinted surface with restrained transmission |
| Glow | 1.37 | 0.16 | 0.65 | Scattering body plus modest emission |

Default settings are 128 maximum samples with adaptive sampling at 0.008, up to 18 total bounces, 16 transmission bounces and 6 volume bounces. Denoising is enabled and stated in each sidecar. For final stills, increase sample counts and inspect fine gel scatter and caustic regions for convergence rather than relying only on denoising.

The scene enables Cycles reflective/refractive light paths, with shadow-caustic flags on relevant objects and lights. These features permit caustic light transport; they are not a guarantee of clean, complete, spectral or arbitrarily deep nested-volume caustics. Refractive caustics can require substantial sampling. The specimen makes the geometry and light transport inspectable in the saved `.blend` file.

## Geometry, controls and motion

The source GLBs retain separate moving parts and names. Slider thumbs, cradles, shells and liquids are not flattened into a single PNG. The offline images show one authored state, while the live motion modules operate on the meshes. The material volume uses local object coordinates so internal patterns follow each object when its transform changes.

The renderer does not claim to bake a fluid solver, soft-body simulation, film drainage or particle trajectory into each GLB or PNG. To render an animated solver result, export its changing mesh/particle state into Blender and keep the same named family assignments. An image alone cannot supply parallax, deformable geometry, collision response or fluid simulation.

PNG alpha removes the space outside a component. Lighting and refraction seen inside the rendered object come from the authored render environment; compositing that PNG over a new background cannot make it refract the new background. Use the meshes and physical materials for that interaction.

GLTF's +Z control face becomes Blender's -Y face after the standard Y-up to Z-up conversion. The asset camera exposes thickness from the front, three-quarter and underside directions. The specimen rotates the controls to show their front surfaces while their bodies, undersides and shadows occupy a shared three-dimensional space.

## Primary documentation

- [Blender Principled BSDF](https://docs.blender.org/manual/en/4.5/render/shader_nodes/shader/principled.html)
- [Cycles light paths](https://docs.blender.org/manual/en/4.5/render/cycles/render_settings/light_paths.html)
- [Volume absorption](https://docs.blender.org/manual/en/4.5/render/shader_nodes/shader/volume_absorption.html)
- [Volume scattering](https://docs.blender.org/manual/en/4.5/render/shader_nodes/shader/volume_scatter.html)
- [Cycles caustic object settings](https://docs.blender.org/manual/en/4.5/render/cycles/object_settings/object_data.html)

These sources explain the shader and integrator capabilities. Actual delivered outputs and their settings are recorded in the adjacent JSON sidecars.
