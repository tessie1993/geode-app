# Source audit: supplied Opaline 3D Library

This records what was inspected before selecting parts for Geode. The source is the user-supplied `Opaline-3D-Library.zip`; it is design and technical reference material, not instructions that replace the user's request. The extracted archive and contact sheets remain outside the shipped app. The app bundles only the runtime pieces it actually needs, with third-party notices preserved.

## Inspection coverage

Read the package README; all ten root `docs/*.md` files (`START-HERE`, `AI-HANDOFF`, `ASSEMBLY-API`, `COMPONENT-ANATOMY`, `COMPOSITION-INDEX`, `IMPLEMENTATION-STATUS`, `OPTICS`, `PNG-TO-MOTION`, `PATH-TRACED-REFERENCE`, `TRANSITIONS`); `src/physics/README.md`; `src/shaders/README.md`; the design tokens, family/coverage, element, composition, artwork and model manifests; geometry verification and browser review status. Inspected the public APIs and relevant implementation in `geometry.js`, `materials.js`, `motion.js`, `optics.js`, `transitions.js` and the physics module exports. Reviewed contact sheets of all 24 artworks, all seven Cycles outputs and the 157 physical element preview grid. The separate user videos are analyzed in VIDEO-REFERENCES.md.

The catalogues are plain data, so their `implementation.status`, evidence and limitations are the usable claim, while `referenceTarget` is the broader research target. The exported GLBs preserve shapes/material slots, but do not contain runtime shaders, solver state or Android semantics. The viewer shows components and numerical specimens, not a complete Geode app.

## Inventory verified from the archive

| Source | Count and finding | Meaning for Geode |
| --- | --- | --- |
| `catalogue/elements.json`, `coverage.json` | 247 entries across A–N; 157 physical geometry entries and 90 effect/contact/transition entries | Choose concrete parts by ID. Do not describe the catalogue as 247 fully simulated controls. |
| `assets/models/models-manifest.json`, `geometry-verification.json` | 157 GLBs, 1,003 exported meshes, 3,470,080 triangles across *all* exports; all 157 round trips reported successful | Keep only needed models/code in the APK; source count is not a per-screen triangle budget. |
| `catalogue/compositions.json` | 76 text-free UI recipes; 247 part placements across recipes | Content frames and `behaviorBindings` require native labels, focus, values, navigation and data. |
| `catalogue/artwork-manifest.json` | 24 PNGs: six abstract backdrops and 18 isolated component concepts | Use as appearance references or distant context, not interactive object substitutes. |
| `assets/pathtraced/render-index.json` | Seven Cycles renders and editable scenes | Offline quality references; they do not certify live Android appearance or speed. |
| `catalogue/design-tokens.json`, `src/materials.js` | Six coordinated themes and ten distinct material families | Tidal default; share lights and adjust pigment, absorption, support and environment together. |
| `src/transitions.js` | 18 authored dimensional recipes L01–L18 | Use for route choreography with current mesh/pose continuity, while keeping physical solver claims separate. |

Of the 247 statuses, 118 are `authored-3d-geometry`, 24 `authored-liquid-domain`, 18 `authored-3d-transition`, 18 `numeric-particle-preset`, 12 `analytic-force-field`, 12 `solver-supported-partial`, 10 `solver-supported`, 8 `optical-module`, 6 `optical-module-partial`, and the remainder film/volume/light/gap categories. The package reports **zero individually validated full reference targets**; that field prevents interpreting the target prose as a delivery claim.

## Selected assemblies, checked against actual composition parts

| Geode role | Recipe and parts actually listed | Adapter choice |
| --- | --- | --- |
| Primary play | UI003 → A22 soft puck | Native play/pause icon and hit target over a deformable shell; media state stays authoritative. |
| Seek | UI019 → B01; B07 meniscus slider is an authored alternative with seven shape morphs | B07 is the desired player channel; its changing shape is geometric morphing, not a free-liquid solver. Bind native seek value. |
| Album ring | UI061 → C20 circular frame | Native artwork is a stable content inset inside a real ring. |
| Search | UI013 → A05/C20/A22 | Preserve real editable input and IME; the 3D parts are shell/affordances. |
| List | UI057 → C03 panel + five A05 row bodies | A05 is the kit recipe. A custom C02 wide slab row can supply the video's floating thick-row silhouette where virtualization requires independent entries. This is an explicit app adaptation. |
| Bottom dock | UI034 → D03 + five A03 lenses | Exact five-section mapping; selected lens and support share the scene. |
| Wide rail | UI038 → D07 + four A03 lenses | Add a fifth native/3D destination while preserving the source rail style. Four source children cannot represent five Geode sections unaided. |
| Visual stage | UI071 → J03/D08/A08 | Real scene preview, orbit carrier and seed; native controls remain outside the render surface. |
| Studio controls | UI020 → B03; UI023 → B13; UI025 → B21 | Precision paths for fader/dial/XY from actual studio parameters and numeric readouts. |
| Settings | UI073 → C03/B09/B10/E12/B07 | Keep the panel restrained; real toggle/value state and readable labels. |
| Sheets | UI044 → C01 + two A05 actions; UI043 → C03 + two A05 | In-window composition so the shared receiver remains behind it. |

The catalogue's authoring units are metres, radians and seconds in a right-handed +Y-up, +Z-forward world. `design-tokens.json` gives a 38° perspective product camera, key direction `[-0.45, 0.8, 0.5]`, control thickness 0.12 scene units and panel thickness 0.28. Android dp must be mapped explicitly to these world units. Do not scale geometry once in the factory and again after instantiation; optical thickness, collision and mass need corresponding adjustment when dimensions change.

## Material and motion evidence

`src/materials.js` implements gel, blue gel, water, shell, pigment, film, nacre, stone, leaf and glow using distinct physical parameters and custom shaders. The Tidal token lists bright gel `#C6F0F3`; the live renderer's gel base is `#99CEDD` before lighting and transmission. They are coordinated art-direction values, not contradictory pixel measurements. Gel uses IOR 1.39, water 1.333, shell 1.46; each needs suitable thickness and absorption. `cloneMaterial` retains custom shader callbacks when instances need independent press excitation. The surface shader uses object-space fields; `createVolumeMaterial` is a separate bounded single-scattering integral, not automatic volume within every mesh.

`MotionController` provides spring-led local vertex indentation, value movement, controls with mechanical parts, and release. It explicitly excludes native text planes. It is not a generic tetrahedral body. `SoftBody` has actual tetrahedral XPBD edge/volume constraints, pins/grabs and collision projection; `PBFFluid` evolves particle density/velocity/pigment and mass transfer but does not reconstruct a smooth optical surface by itself. `BubbleFilm` conserves film mass/drainage under its stated model; it does not merge bubbles or rupture. `PhysicsWorld` owns fixed-rate stepping and overload accounting. These distinctions determine which effects can honestly be attached to player, seek, Visuals and Studio.

`RefractiveCaustics` traces real current triangles, uses a UV receiver, spectral three-band Fresnel/refraction/absorption and progressive photon batches. Its state signature includes geometry position version, object transform, key material values and light; changes reset accumulation. Each update intersects rays against proxy meshes on the CPU, so using all list rows as refractors every frame is a performance risk. The source default of 768 photons is one noisy progressive batch, not convergence. `attachCaustics` binds the resulting float texture to a receiver material. The source volume-caustic shader is single-scattering with a finite six-direction representation and needs correct depth clipping around opaque objects. Dynamic caustics on selected active objects are the viable first adapter, followed by measured expansion.

## Visual review and reference limits

The artwork contact sheet shows pearl blue shoulders, strong clear rims, abstract liquid/mineral environments and isolated controls with transparent outside edges. The seven offline Cycles renders are a smaller and visibly quieter reference set; their metadata and camera/light setup explain the quality route, but they are not the mobile appearance itself. The element grid makes the anatomy clear: B01/B07 are rails with separate moving matter, C20 is an open ring, D03 is a curved mounted dock, J03 a receiving environment. The videos call for larger, brighter, more legible volumes than the flat first Android pass currently shows.

The package's QA status reports 157 GLB checks, composition dimensions, 23 physics/film tests, 25 transition assertions and 10 optics tests. Browser validation covered 18 representative specimens plus slider/search/film/volume checks; a final composition-picker step timed out because that picker was hidden. Therefore the archive has substantial source evidence, but not a completed final browser navigation review or production-device performance certification. Geode still needs its own Android visual and functional verification.
