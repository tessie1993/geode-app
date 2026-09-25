# Material and optical implementation

`opaline.js` contains the actual shader strings compiled by `materials.js`, not pseudocode or unused mirrors. All procedural cloud fields use three-dimensional material coordinates. Rendered vertices and normals come from the current 3D mesh. This module never replaces a dimensional element with an image plane.

## Distinct matter

`createMaterials(theme)` exposes `gel`, `blue`, `water`, `shell`, `pigment`, `film`, `nacre`, `stone`, `leaf`, and `glow`. Themes are `tidal`, `opal`, `moss`, `obsidian`, `aurora`, and `amber`.

- Gel/hydrogel: rough dielectric, absorption, thickness, coated shoulders and 3D cloudy density field.
- Water: clear dielectric with water IOR and longer absorption distance.
- Shell: independent clear wall, low roughness and wall-scale optical thickness.
- Pigment: denser absorbing material with domain-warped 3D flow coordinates.
- Film: thin dielectric with angle-dependent physical iridescence; separate from dense gel.
- Nacre: softly rough surface and thin-layer interference.
- Stone: mineral body and a separate PBR wet clearcoat layer.
- Leaf: thin, double-sided, tinted transmission.
- Glow: emitting geometry, physically occluded; bloom belongs to the shared renderer.

`cloneMaterial(source)` preserves shader hooks and creates independent interaction uniforms. Plain Three `Material.clone()` does not copy `onBeforeCompile`. Uniform field coordinates remain attached to material space as CPU geometry deforms. Visual pigment-coordinate flow is not claimed as conserved fluid advection; a density simulation must supply actual pigment state when conservation is needed.

## Participating medium

`createVolumeMaterial(options)` integrates a 3D density field, Beer–Lambert extinction and Henyey–Greenstein single scattering over 96 view steps with eight shadow/extinction samples toward the local light per step. Use a box mesh with the declared half-extents, then call `material.userData.bindVolume(mesh)`. The density can be bounded by an ellipsoid or box. Place it inside the shell as a separate volume, not on the shell surface. `lightPosition` is expressed in that volume's local coordinates.

This is a real volume integral but it is not diffusion-based multiple scattering. It does not automatically infer arbitrary mesh interiors or external object shadows. Standard transparent raster composition also cannot recursively refract arbitrary overlapping volumes. Its extinction units are local-space units; scale and density must be authored consistently.

## Geometric caustics

`RefractiveCaustics` in `../optics.js` emits deterministic quasi-Monte-Carlo photons through actual current mesh triangles. Transport includes Snell refraction, exact dielectric Fresnel energy loss, three-band dispersion, Beer–Lambert absorption, a nested-medium stack and total internal reflection. The first valid receiver hit deposits energy into its UV map. Pixel energy is divided by the receiving triangle's world-area/UV-area Jacobian. Static scenes accumulate; geometry, material, light and transform changes reset accumulation.

The receiver must have nonoverlapping valid UVs; the refractors must be closed and consistently outward wound. Receiver curvature is supported through triangle intersections. Supply opaque blockers through `occluders`; the solver does not automatically discover a whole scene. The default 768-photon update is one progressive batch, not a claimed converged image. Accumulate many batches for final evidence. Moving geometry cannot reuse static accumulation without ghosting, so its convergence costs more. Float texture rendering requires WebGL2 support for the relevant floating-point formats.

This solver is intentionally CPU-expensive. It now supports optional energy-weighted reflected split branches and an actual photon-volume grid with single-scattering beam display; see `../../docs/OPTICS.md`. It does not perform multiple scattering, bidirectional light transport, rough microfacet direction sampling, external area-light sampling or complete global illumination. Its surface map adds the receiver's Lambertian irradiance response. It is not a scrolling texture and does not derive light patterns from the screen.

For a simulated BubbleFilm surface, `enableFilmThickness(material)` enables the per-vertex `filmThickness` attribute in metres. That physically evolved thickness replaces the procedural film thickness in the actual thin-film interference evaluation. Bind only when the geometry supplies the attribute.

## PBR limits

Three's raster `MeshPhysicalMaterial` provides transmission, absorption, thin-film iridescence and dispersion, but its screen-buffer transmission is not a recursive nested dielectric ray tracer. Surface cloud coloring is not a substitute for the separate volume integrator. The specimen can combine the modules, while these limits remain explicit. No Android implementation or hardware performance claim is included.

Primary documentation: https://threejs.org/docs/pages/MeshPhysicalMaterial.html · https://threejs.org/docs/pages/Raycaster.html · https://threejs.org/docs/pages/ShaderMaterial.html
