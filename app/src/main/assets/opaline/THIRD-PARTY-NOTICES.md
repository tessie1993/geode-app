# Third-party notices and asset provenance

## Three.js

This package bundles **Three.js 0.186.1** under `vendor/three/`, including the addon modules used by the workbench and production tools. Three.js is distributed under the MIT License. The complete upstream notice is retained in [`vendor/three/LICENSE`](vendor/three/LICENSE), including the upstream copyright attribution.

The custom renderer/material code uses Three.js's physical material and shader infrastructure. Retain the upstream license and notices when redistributing the included dependency.

## Generated artwork

The 24 PNG artworks in `assets/artwork/` were generated for this deliverable from the user's visual direction: six abstract liquid/mineral backdrops and 18 isolated interface or nature-derived components. Their generation instructions are recorded in [`assets/artwork/generation-prompts.json`](assets/artwork/generation-prompts.json). Dimensions, actual alpha statistics, roles and checksums are recorded in `catalogue/artwork-manifest.json`.

The generated PNGs are artwork and appearance references. They are not measurements of depth, normals, optical thickness or physical solver state. No app labels or logos were requested in these assets.

## Blender/Cycles reference renders

Blender's Cycles renderer was used as a production tool to render six individual authored library components and one shared specimen scene. These seven PNG outputs are in `assets/pathtraced/`; the seven editable source scenes are in `assets/blender/`. Per-render metadata, the render index, and `docs/PATH-TRACED-REFERENCE.md` record the production settings and scope.

The Blender executable is not bundled. The supplied Python production script and `.blend` scene files describe the library's original authored geometry and scene construction. A render produced with Blender is distinct from the image-generation artwork listed above.

## Mathematical methods and reference inputs

Numerical modules were authored for this package from the cited mathematical methods. Method citations and implementation limits are included in `src/physics/README.md`. Referencing a method does not mean its authors' solver source was incorporated.

The user's reference images and videos informed the visual and motion direction. Those reference media are not redistributed in this package. This notice does not assert ownership or grant a blanket license over user-supplied or external reference inputs, and it does not choose a public license for the user's overall project.
