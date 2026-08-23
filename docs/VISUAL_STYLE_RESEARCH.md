# Composite visual families and crystal material UI

Status: first implementation slice complete in this working tree.

The collection is implemented as **profiles over the existing authored engines**, not as twenty copied renderers. This keeps audio analysis, lifecycle, export, visual-safety handling and GPU resource ownership shared while each style selects a shader branch and biases the family controls.

## Implemented visual collection

### Cymatics

The original `cymatics` ID also remains unchanged. Ten additional resonator profiles are registered:

1. Chladni Sand — plate nodes rendered as granular deposits.
2. Drumhead — circular breathing membrane with an illuminated rim.
3. Harmonograph — two precessing samples of the same modal field.
4. Faraday — subharmonic buckling and cell-like surface motion.
5. Harmonic Shell — polar unwrapping, pearl relief and spiral structure.
6. Caustic Sheet — refracted coordinates and concentrated light folds.
7. Levitator — mirrored pressure nodes and suspended bead accents.
8. Standing Chamber — perspective-compressed nodal architecture.
9. Rosensweig Spikes — ferrofluid-like peak shaping.
10. Kundt Tube — long-axis standing waves inside a glass-like tube.

The variants reuse the existing resonator bank, normalized modal field and audio mapping, so Cymatics controls keep the same meaning throughout the family.

## Crystal UI material system

The old UI used one general glass/nebula treatment with different accent colors. The new material layer passes the selected `AppTheme` through a CompositionLocal and procedurally draws a different mineral structure on both the global backdrop and every crystal panel:

- Lapis: calcite seams and irregular pyrite flecks.
- Malachite: curved concentric green bands.
- Clear Quartz: internal fractures and faint prismatic planes.
- Rose Quartz: cloudy translucence and aligned fibrous veils.
- Sugilite: granular dark matrix and purple veins.
- Amethyst: angular light/dark growth zoning.
- Kyanite: directional bladed striations.
- Onyx: restrained parallel banding and polished depth.

These are deterministic vector/procedural marks. No crystal photographs or generated texture assets are shipped, which keeps the UI sharp at every resolution and avoids a generic image-overlay appearance.

Material cues were checked against gemological and museum references rather than generated-art conventions: [GIA on cloudy fibrous rose quartz](https://www.gia.edu/rose-quartz-quality-factors), [GIA on angular amethyst zoning](https://www.gia.edu/amethyst-description), [GIA on lapis lazuli's lazurite/calcite/pyrite aggregate](https://www.gia.edu/lapis-lazuli-description-v1), [the British Museum's description of banded malachite](https://www.britishmuseum.org/collection/term/x11179), [USGS imagery of bladed kyanite](https://www.usgs.gov/media/images/kyanite-specimen), [CAMEO on parallel-banded onyx](https://cameo.mfa.org/wiki/Onyx), and [GIA research on purple sugilite](https://www.gia.edu/gems-gemology/summer-1987-sugilite-wessels-shigley).

## Open-source research

The implementation is original except for code already credited by the project. The following open-source projects were studied as architecture or technique references:

- [`projectM-visualizer/projectm`](https://github.com/projectM-visualizer/projectm) — LGPL-2.1. Relevant for a reusable library/preset architecture, PCM analysis and rendering presets to OpenGL. MusicViz already integrates projectM and retains its notices.
- [`PavelDoGreat/WebGL-Fluid-Simulation`](https://github.com/PavelDoGreat/WebGL-Fluid-Simulation) — MIT. Relevant for GPU advection, pressure projection, vorticity and dye feedback. MusicViz's existing fluid port is already attributed in `THIRD_PARTY_NOTICES`.
- [`jberg/butterchurn`](https://github.com/jberg/butterchurn) — MIT. Relevant for keeping the renderer generic while data-driven presets select equations and parameters.
- [`karlstav/cava`](https://github.com/karlstav/cava) — MIT. Relevant for perceptually responsive spectrum smoothing and for exposing analysis as reusable core data rather than tying it to one renderer.
- [`flutomax/ChladniPlate2`](https://github.com/flutomax/ChladniPlate2) — public source and useful as a conceptual reference for combining waveform amplitudes, harmonic ratios and phases into a normalized level map. Its license should be reviewed directly before adapting any source; no code from it is included here.
- [`VCHackett/naadara`](https://github.com/VCHackett/naadara) — MIT technique reference already documented in `THIRD_PARTY_NOTICES` for the narrow/wide Gaussian nodal rendering approach. No source is copied.

## Further creative backlog

The profile system makes these additions possible without another renderer fork:

- Resonant Wormhole v2: drive portal mode numbers directly from the Cymatics resonator bank.
- Cymatics × Fluid: nodal lines inject dye while velocity transports the pattern.
- Cymatics × Particles: grains physically migrate toward the zero-level set.
- Crystal-aware visuals: optionally borrow the active UI mineral's banding or inclusions as a material modifier, without forcing the visual palette to match the UI.
- A/B morphing: interpolate two profiles in the same family by blending profile values and evaluating both coordinate transforms during transitions.
- Performance tiers: preserve the style identity on low tier by reducing secondary samples and material accents before reducing the core modal/raymarch quality.

## Validation targets

Before release on-device:

1. Compile both GLSL shaders on at least one Adreno and one Mali device.
2. Scrub all ten variants while audio is playing and while silent.
3. Verify export creates the same selected family profile as live playback.
4. Check that saved presets retain the new IDs after process death.
5. Test crystal panel text contrast in all eight named themes and with White Font enabled.
6. Profile Plume, Cortex, Faraday and Rosensweig first; they contain the heaviest added shader work.
