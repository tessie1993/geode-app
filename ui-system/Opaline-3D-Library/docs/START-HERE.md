# Opaline 3D Library

A reusable, text-free kit of opaline gel, clear liquid, films, mineral forms, controls, containers and abstract environments. The art direction is sculptural and immersive: translucent volumes, visible thickness, perspective, softly tinted interiors and coherent light. Backdrops are abstract liquid/mineral spaces, not photographs or scenic landscape wallpapers.

The catalogue contains 240 research targets plus seven newly authored nature-derived elements (247 entries). They are mapped to authored geometry, working numerical modules, optical components or explicit implementation gaps. **The number 240 does not mean 240 fully simulated and individually validated behaviors.** Read `catalogue/elements.json` or `IMPLEMENTATION-STATUS.md` for the distinction.

The kit also includes 76 generic UI compositions, six coordinated material themes, shader source, reusable simulation code, generated artwork and assembly contracts. Labels, icons and application data remain separate from the visual assets. A menu shell can become any app's menu; an input shell can surround any app's editable text.

## Open the local viewer

From the extracted library directory:

```sh
python3 -m http.server 8080
```

Open `http://localhost:8080`. The bundled Three.js dependency allows local use without an npm install. Use an HTTP server: opening the HTML with `file://` does not provide the module and fetch behavior the viewer needs.

The viewer is a component workbench. It demonstrates 3D geometry, materials and interaction specimens; it is not a finished application's navigation or form engine. UI compositions assemble the actual elements, and their semantic events are supplied as adapter contracts.

## Choose the right deliverable

| Need | Location |
|---|---|
| Browse element IDs and precise implementation status | `catalogue/elements.json` |
| Build menus, inputs, panels, docks and other generic UI | `catalogue/compositions.json` |
| Shared spacing, units, content and theme conventions | `catalogue/design-tokens.json` |
| Actual file inventory with size and SHA-256 | `catalogue/assets-manifest.json` |
| Reusable physical geometry | `src/geometry.js` |
| PBR surfaces and bounded participating volume | `src/materials.js`, `src/shaders/` |
| Reflective, refractive and volumetric caustics | `src/optics.js` |
| Numerical soft body, fluid, film, particles, cloth and rods | `src/physics/` |
| Preview contact and control interactions | `src/motion.js` |
| How PNG appearance maps to live 3D behavior | `PNG-TO-MOTION.md` |
| How to construct and adapt the kit | `ASSEMBLY-API.md`, `COMPONENT-ANATOMY.md` |
| Limits and remaining target behaviors | `IMPLEMENTATION-STATUS.md` |

No Android integration is included. This package establishes reusable assets, rendering code, numerical modules and composition data before a platform adapter is built.

## Rebuild the catalogue index

```sh
python3 tools/build-catalogue.py
```

This recreates the 76 compositions, tokens and actual-file manifest. It does not invent artwork entries for missing files and does not promote an unimplemented target to a finished component. `catalogue/elements.json` preserves the reviewed implementation status.

## Use the style consistently

Keep a shared camera and light rig. Give gel, water, film and solid support different material responses. Place content on a stable plane attached to the deforming object; allow the shell to move without making text wobble. Use empty space and smooth material gradients to keep the interface calm. The background and foreground should share colour temperature and reflected light.

PNG artwork is useful for appearance, pre-rendered distant planes and concept comparison. Interactive controls use 3D geometry. Painting a PNG on a rectangle does not create the sides, changing reflections, perspective, collisions or local deformation required by this style.
