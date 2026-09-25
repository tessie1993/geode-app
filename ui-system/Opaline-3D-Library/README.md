# Opaline 3D Library

A text-free construction kit for immersive interfaces: thick gel controls, liquid channels, clear shells, films, abstract backdrops, nature-derived forms and coordinated motion. It includes working 3D source, exported models, generated artwork and reusable UI assemblies. **No Android integration is included.**

## Open the library

From this extracted directory:

```sh
python3 -m http.server 8080
```

Open **http://localhost:8080** in a WebGL 2-capable browser. Three.js is bundled; no npm install or online asset fetch is needed. Use HTTP rather than opening `index.html` through `file://`.

Explore the world, select individual elements, switch material themes or open the generic UI compositions. Orbit the camera to inspect sides and undersides. Touch and drag eligible controls; the numerical specimens expose additional fluid, film, particle and soft-body behavior. The viewer can export a PNG or a GLB snapshot.

## Included

| Deliverable | Contents |
|---|---|
| 157 physical element entries | Authored 3D geometry and individual GLBs; 1,003 meshes and 3,470,080 triangles across the exports |
| 247-entry element catalogue | 157 geometry entries plus 90 particle, force-field, optical, contact and transition entries; per-entry implementation status |
| 76 generic UI compositions | Actions, text fields, text areas, menus, sheets, panels, docks, navigation, sliders, dials, pickers, loading and immersive assemblies |
| 24 generated PNG artworks | Six abstract backdrops and 18 isolated components with actual alpha transparency |
| Seven Cycles reference renders | Six individual components and one shared specimen; seven editable Blender scenes |
| Six coordinated themes | Tidal, Opal, Moss, Obsidian, Aurora and Amber |
| Executable physics | Tetrahedral XPBD gel, PBF fluid, film membranes/drainage, particles, force fields, cloth, rods and bilateral contact |
| Optical source | Physical surface materials, bounded volume, reflective/refractive photon caustics and single-scattering volumetric caustics |
| Spatial transitions | 18 reusable authored 3D choreography recipes, with explicit physical hooks and limits |

The original research scope is represented throughout the catalogue. A catalogue entry does **not** mean a separately validated physical solver. The inspector and `catalogue/elements.json` distinguish numerical modules, authored geometry/choreography and remaining specializations. Full foam topology, capillary wetting, arbitrary multiphase reconstruction and complete app semantics remain explicit gaps. See [implementation status](docs/IMPLEMENTATION-STATUS.md).

## Package map

| Path | Purpose |
|---|---|
| `index.html`, `styles.css`, `src/workbench.js` | Local interactive workbench |
| `catalogue/elements.json`, `families.json`, `coverage.json` | Element IDs, families, implementation evidence and coverage |
| `catalogue/compositions.json` | All 76 text-free assembly recipes |
| `catalogue/instantiate-composition.js` | Reusable composition factory, empty content anchors, optional contact controllers and cleanup |
| `catalogue/design-tokens.json` | Units, materials, themes, spacing, lighting and content conventions |
| `catalogue/artwork-manifest.json` | All 24 PNG dimensions, alpha statistics, roles and hashes |
| `catalogue/artwork-motion-map.json` | Generated artwork mapped to geometry, anatomy and motion |
| `catalogue/assets-manifest.json` | Actual asset inventory and checksums |
| `assets/artwork/` | Generated abstract backdrops and isolated component artwork |
| `assets/models/` | 157 GLBs, model metadata, bounds, ports and verification report |
| `assets/pathtraced/`, `assets/blender/` | Cycles renders, render metadata and editable source scenes |
| `src/geometry.js` | Parametric physical element factory |
| `src/materials.js`, `src/shaders/`, `src/optics.js` | Surface, volume and caustic implementation |
| `src/physics/` | Renderer-independent numerical modules and exact API/limitations |
| `src/motion.js`, `src/transitions.js` | Local interaction preview and reusable spatial transition engine |
| `tests/` | Numerical invariants and transition tests |
| `tools/` | Catalogue, geometry-export and reference-render production utilities |
| `vendor/three/` | Bundled Three.js dependency and its license |
| `docs/` | Assembly API, anatomy, artwork/motion guidance, handoff and implementation notes |

## Build with the parts

Use `createElement(id, materials, options)` for an individual physical part, or `instantiateComposition(recipe, materials, options)` for a complete reusable assembly. Content frames are empty anchors for application text, icons and data. The assets themselves contain no app labels or logos.

The generated PNGs define appearance and provide distant backplates. Interactive dimensional objects use the 3D geometry. Custom shaders, solver state and application behavior remain in source; a GLB export does not contain that entire runtime.

Read [assembly API](docs/ASSEMBLY-API.md), [component anatomy](docs/COMPONENT-ANATOMY.md), [PNG-to-motion mapping](docs/PNG-TO-MOTION.md) and [optical transport](docs/OPTICS.md) and [future app handoff](docs/AI-HANDOFF.md). The design direction uses abstract sculptural liquid/mineral environments with coherent perspective and lighting.

## Verification and rebuilding

```sh
npm install ./vendor/three --ignore-scripts --no-audit --no-fund
node --test tests/*.test.mjs
python3 tools/build-catalogue.py
```

The catalogue builder regenerates recipes, tokens and the actual-file asset index. Every composition part has been instantiated and checked for finite vertices and requested dimensions; all GLBs have been round-tripped through a loader. Numerical tests cover named solver invariants and scenes, rather than certifying every research acceptance target or any particular Android device's performance. Detailed evidence is retained in the verification reports and module documentation.

See [third-party notices and asset provenance](THIRD-PARTY-NOTICES.md). Included dependency notices remain with their files. User reference videos and reference images are not redistributed in this package; no blanket license over those inputs is asserted.
