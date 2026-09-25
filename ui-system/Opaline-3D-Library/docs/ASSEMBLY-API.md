# Reuse the library

## Geometry and material API

The geometry factory produces independent mesh groups with typed element IDs. A–F, J and N are physical geometry families; G, H, I, K and L describe effects, force fields, optical modules, interactions and transitions.

```js
import * as THREE from 'three';
import {createElement} from './src/geometry.js';
import {createMaterials, updateMaterials} from './src/materials.js';

const materials = createMaterials('tidal');
const button = createElement('A01', materials, {
  dimensions: [1.45, 0.65, 0.32]
});
scene.add(button);

// Run once per rendered frame with time in seconds.
updateMaterials(materials, elapsedSeconds);
```

Use the viewer's import map for the bundled `three` package and addons. When moving the source to another build system, preserve the Three.js version and the shader customizations. A plain `Material.clone()` can lose the custom shader callbacks; use the exported `cloneMaterial()` for independently animated material instances.

Available material selectors are `gel`, `blue`, `water`, `shell`, `pigment`, `film`, `nacre`, `stone`, `leaf` and `glow`. Themes are `tidal`, `opal`, `moss`, `obsidian`, `aurora` and `amber`.

## Composition format

`catalogue/compositions.json` is a plain array of 76 records. Each record names a generic UI role and provides reusable parts:

```json
{
  "id": "example",
  "name": "Empty panel",
  "parts": [
    {
      "id": "body",
      "element": "C01",
      "position": [0, 0, 0],
      "rotation": [0, 0, 0],
      "scale": [1, 1, 1],
      "dimensions": [2.8, 2, 0.28],
      "material": "shell"
    }
  ],
  "contentFrames": [
    {
      "id": "content",
      "owner": "body",
      "position": [0, 0, 0.18],
      "size": [2.44, 1.64],
      "role": "content",
      "renderedContent": null
    }
  ]
}
```

`dimensions` sets the part's requested local axis-aligned dimensions before its rotation. `scale` is an additional multiplier. Apply dimensions once: either pass `options.dimensions` to `createElement`, or normalize the returned group's bounding box yourself. Do not do both. `position` and `rotation` are relative to the composition root. The root can then be placed as a unit in the scene.

Changing a recipe's dimensions is a layout change, not a physically automatic material or solver rescale. Update optical thickness, solver particle spacing, collider sizes and mass as appropriate.

## Instantiate a whole recipe

```js
import {instantiateComposition} from './catalogue/instantiate-composition.js';

const recipes = await fetch('./catalogue/compositions.json').then(r => r.json());
const input = instantiateComposition(
  recipes.find(r => r.id === 'UI011'), materials,
  {bindMotion: true, onChange: event => console.log(event.partId, event.value)}
);
scene.add(input.root);

// This is an empty Object3D. Attach your actual text/input presentation here.
const textAnchor = input.contentFrames.get('input');
// Forward raycast hits to input.controllers.get('body').begin(hit.point), etc.
input.update(deltaSeconds, elapsedSeconds);
// Later, when removing this instance:
input.dispose();
```

The helper creates independent materials by default, keeping one part's contact glow from accidentally changing another part's uniforms. Pass `independentMaterials: false` only when deliberate material sharing is appropriate, and update the caller-owned materials yourself. `dispose()` releases generated geometry and owned materials, while retaining caller-supplied material ownership. The helper returns semantic `behaviorBindings` as data; it does not execute navigation, focus or text editing.

## Workbench inspection API

The running viewer exposes `window.OPALINE` with `select(id)`, `mode('world'|'specimen'|'composition')`, `theme(id)`, `frame()`, `render()`, `screenshot()`, `exportGLB()` and `getStats()`. It also exposes the scene, camera, renderer, controls, bus, catalogue, compositions, current selection and numerical specimen. This is useful for inspection; the independent factories are the reusable library API.

GLB export includes geometry and representable PBR materials. Custom runtime shaders, physical solver state and application semantics are not embedded in a GLB file. Keep the source library beside exported geometry when those behaviors matter.

## Physical contact preview

```js
import {MotionController} from './src/motion.js';

const contact = new MotionController(button, {
  id: 'A01',
  onChange(value) { /* pass a normalized value to your application */ }
});

// Supply a world-space hit from a camera raycast.
contact.begin(hit.point);
contact.drag(nextHit.point, pointerDelta);
contact.release();
contact.update(deltaSeconds, elapsedSeconds);
// On disposal, release contact and temporary mesh resources.
contact.dispose();
```

This controller demonstrates local mesh indentation, spring settling and control-part motion. It does not turn arbitrary geometry into a volumetric soft body. For numerical deformation use `SoftBody` and provide a tetrahedral domain.

## Numerical soft-body specimen

```js
import {SoftBody, makeTetGrid} from './src/physics/soft-body.js';

const domain = makeTetGrid({nx: 6, ny: 4, nz: 6, size: [1.4, .6, 1.1], shape: 'ellipsoid'});
const body = new SoftBody({
  positions: domain.positions,
  tetrahedra: domain.tetrahedra,
  gravity: [0, -9.81, 0],
  edgeCompliance: 2e-5,
  volumeCompliance: 1e-9,
  iterations: 12
});
const geometry = new THREE.BufferGeometry();
geometry.setAttribute('position', new THREE.BufferAttribute(body.positions, 3));
geometry.setIndex(new THREE.BufferAttribute(body.surfaceIndices, 1));
geometry.computeVertexNormals();
const softMesh = new THREE.Mesh(geometry, materials.gel);

// In a fixed-step simulation loop, with configured colliders and mounts:
body.step(1 / 120);
geometry.attributes.position.needsUpdate = true;
geometry.computeVertexNormals();
```

Pin chosen vertices for a mount; use `grab`, `moveGrab` and `releaseGrab` for persistent contact constraints. The example's free body falls under gravity unless the caller supplies support. A render mesh with a different vertex layout requires a real embedding or deformation transfer; assigning the solver's position array to an unrelated high-resolution mesh is invalid.

## Content and semantics

The recipe's `behaviorBindings` are declarative host contracts. For example, `select-date`, `open-menu`, `navigate`, `increment` and `resize` are not an implemented application state machine. The viewer's press/release interaction and the solver APIs are executable; semantic form editing and navigation belong to the next integration layer.

The host must supply keyboard focus, accessible names, screen-reader semantics, editable text/IME, validation, focus restoration, scrolling, route state and data binding. That host can reuse the 3D components while keeping actual app content separate. No Android adapter is provided in this package.

## Optical modules

`createVolumeMaterial()` ray-marches a bounded ellipsoidal or box participating medium with single scattering. Bind it to its mesh through `material.userData.bindVolume(mesh)`. The boundary is analytic; it is not extracted automatically from arbitrary shell geometry.

`RefractiveCaustics` in `src/optics.js` traces photons through supplied mesh interfaces and deposits receiver energy into a texture. Supply valid receiver UVs, update moving geometry before tracing and reset accumulation when the configuration changes. Its transport calculation is separate from the raster material renderer. `transport: "reflection"` traces supplied reflector meshes; `transport: "split"` supports reflected and transmitted branches. A supplied `PhotonVolume` accumulates actual photon segments into a bounded 3D voxel field; `createVolumeCausticsMaterial()` ray-marches the directional field with single scattering. Supply a depth texture when exact opaque camera-ray clipping is required. Read the material module documentation and per-ID status for spectral, angular, branch and scattering limits.

## Ownership and cleanup

Dispose geometry, uniquely owned materials, render targets and simulation bindings when a component is removed. Do not dispose a shared material until all users have detached. Release pointer ownership on cancel and blur. A reused recipe should receive an independent state object; reusing its immutable JSON is safe, reusing another instance's mutable solver arrays is not.
