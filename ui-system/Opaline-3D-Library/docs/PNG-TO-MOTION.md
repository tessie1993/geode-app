# Generated artwork and live motion

The artwork supplies a material and composition target for the live kit. It is not a displacement, normal, roughness or physically calibrated depth map. Highlights, shadows and refractions baked into a PNG must not be treated as measured material data.

The environment direction is **abstract**: sculpted liquid volumes, suspended gel folds, mineral contours, soft gradients and large quiet regions for foreground controls. Do not replace this with a photograph of a lake, forest or mountain.

## Artwork-to-element map

| Artwork role | Live elements | Mechanical anatomy | Appropriate live response |
|---|---|---|---|
| Button pebble | A01, A03, A04, A22 | Closed soft body, stable content frame, rear mount | Local contact, elastic release, localized excitation |
| Input capsule | A05, C02, D14 | Shell, quiet text plane, independent focus conduit | Small local compression; focus light follows host focus |
| Text area | C01, C03, B23 | Thick panel, separate content, resize grip | Shell motion; resizing follows layout constraints |
| Menu panel | C03, C15, A05 | Parent panel, distinct row bodies, child hit regions | Panel lift and row-specific press |
| Bottom dock | D01, D02, D03, A03 | Shared support with independent child mounts | Support settles; child interaction stays local |
| Slider capsule | B01–B08 | Rail, thumb, collar or skirt, bounded axis | Thumb travel and local contact; host updates value |
| Toggle capsule | B09–B12 | Stops, cradle and moving selection body | Bounded travel to declared detents |
| Dial ring | B13–B18, C20 | Independent rotor and collar | Angular motion with a separate semantic angle |
| Tooltip shell | C05–C07 | Front content plane, attachment tail, mount | Anchor-relative lift, then damped settle |
| Modal shell | C01–C03, C15, C16 | Deep panel, front content, button bodies | Foreground lift with explicit background occlusion |
| Floating droplet | A09, E08–E10 | Closed volume or liquid particle domain | Gravity/drag for actual particles; authored geometry is a shape |
| Glow seed | A08, G01, G04, G07 | Emissive core and particle mass; optional tail | Shared wind acceleration; collision emitter can transfer impulse |
| Abstract liquid backdrop | E01–E07, J01–J04 | Near-field 3D surfaces plus distant backplate | Camera parallax on geometry, shared lighting, force-driven particles |
| Abstract mineral backdrop | J06–J11, J24 | Solid occluders, receivers and lights | Dynamic shadows and caustics; static background artwork stays static |

## Convert an artwork motif into a reusable element

1. Identify the silhouette and thickness. Choose or author real geometry with visible sides and an underside.
2. Separate shell, liquid, pigment, film and emission into independent material roles.
3. Reserve a stable content region. The generated picture remains text-free; the host adds content later.
4. Give the object a mount, collider and contact region. Do not use its rectangular image bounds as the physical collider.
5. Decide whether it needs an authored motion response or a numerical solver. A fixed-topology gel press and a fluid pinch-off are different problems.
6. Check the object from front, side, underside and a moving perspective view under changed lighting.
7. Compare at rest, at maximum contact, during interruption and after settling. A convincing still image does not prove a convincing interaction.

## How to use a PNG without breaking the shared world

- Use a backdrop as a distant visual plane with a limited camera angle. It will not generate correct new parallax when the viewpoint changes strongly.
- Keep major foreground forms as meshes; use their normals for shading and their geometry for contact.
- If an isolated component PNG has alpha, preserve the alpha. Do not interpret a checkerboard painted into RGB as transparency.
- Avoid stacking duplicate baked shadows beneath a 3D object that already casts a dynamic shadow.
- Do not add another baked shine layer over a physical transmission material. Let the shared light rig produce the changing highlights.
- Place colour motifs inside the 3D volume or material coordinates; avoid screen-locked noise that slides when the camera moves.

The actual artwork files and dimensions are listed in `catalogue/assets-manifest.json`. Only files that exist are indexed there. A theme or camera view is an artwork variant, not an additional physical component.

## Evidence limits

The PBF module is a 3D particle fluid with density constraints and pigment transport. It does not implement a high-resolution MPM/FLIP multiphase simulation, a complete capillary contact-angle model or an automatic foam-film topology system. Generated imagery can depict effects beyond what the current numerical modules reproduce. Those differences are recorded in the element status fields; the PNG appearance is never used as evidence that the corresponding solver is complete.
