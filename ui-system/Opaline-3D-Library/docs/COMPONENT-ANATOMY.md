# Component anatomy and assembly rules

The shared coordinate system is right-handed, metres for scene authoring, +Y up, +Z toward the user and Euler XYZ rotations in radians. These are authored scene units; the future app adapter must define its screen-to-world scale explicitly.

Every visible interactive component has several separate roles. They can share a transform but should not be collapsed into a single textured plane.

| Role | Responsibility | Should move with |
|---|---|---|
| Render shell | Silhouette, underside, sidewall, surface normals | Geometric or solver deformation |
| Interior | Pigment, clouding, liquid or bounded participating medium | Its own material or solver state |
| Mechanical support | Mount, cradle, collar, socket or hinge | Parent assembly constraints |
| Content frame | Empty attachment for text, icon, image or data | Stable local front plane |
| Touch region | Input ownership and hit geometry | Current visible object |
| Collider | Numerical contact approximation or actual boundary | Solver state |
| Light receiver | Surface that receives transmitted or reflected energy | Receiver geometry |
| Event port | Semantic events and physical impulses | Host event routing |

## Geometry before effects

Visible thickness is geometry. A slab should retain an underside when the camera passes below it. A frame has a real opening. A slider has separate thumb, rail and mount. An input shell preserves a quiet front area. The geometry factory contains authored mesh assemblies for the A–F, J and N families; liquid and film shapes in those families are initial shapes, not proof that their target fluid/topology behavior is solved.

Do not infer optical thickness from alpha. PBR transmission has separate IOR, thickness and absorption. When dimensions change, revisit optical thickness as well as vertex scale. Strong nonuniform scaling changes the apparent material path length and the intended mechanical response.

## Stable content

`contentFrames` in each composition are empty. They supply a front-facing transform, size, padding, role and owner. The host can attach native text, a canvas texture, a vector icon or another UI surface. The kit itself does not bake labels, invented placeholder words, app branding or logos into the artwork.

A content frame is defined in composition-local space. Its `owner` identifies the element it follows. Convert it to the owner's local coordinate system when parenting dynamically. Its `position` is not automatically a mesh vertex index. Layout must keep content above the highest intended front surface and inside the quiet area.

For form fields, use a real host editable surface for caret, selection, composition input and accessibility. The visual shell may depress and glow; the insertion point should remain stable. `secure-editable-text` is a semantic role, not a security implementation.

## Input ownership

1. Hit-test the nearest eligible interactive part through the current perspective camera.
2. Establish one contact owner for each pointer.
3. Apply force at the actual local contact point and retain the contact identity through dragging.
4. Release or cancel the contact on pointer-up, cancellation, blur and component disposal.
5. Emit a semantic value through the host adapter after its own bounds and validation rules.

Decorative background forms, glow seeds and mist should not capture application input. A shared force field may influence them without making them focusable controls.

The viewer's `MotionController` is a deterministic local-deformation preview. The separate XPBD `SoftBody` is a numerical elastic model. They are different tools: a geometric press preview does not by itself enforce tetrahedral volume conservation or contact mechanics.

## Shared-world connections

| Connection | Contract |
|---|---|
| `mount` | Position/orientation attachment; host owns detach policy |
| `contentFrame` | Readable content anchor independent of soft vertices |
| `touchRegion` | Hit target and pointer ownership |
| `collider` | Explicit physical boundary and material pair |
| `wetBoundary` | Target liquid/solid boundary; full wetting is not implemented |
| `fluidIn`, `fluidOut` | Particle transfer API with equal particle mass; no implicit portal |
| `fieldRegion` | Spatial force field bounds and sample function |
| `lightReceiver` | Valid UV receiver geometry for the caustics module |
| `eventOut` | Host semantic event or physical impulse payload |

An entry's listed connections describe the planned assembly interface. Presence in the catalogue does not mean every connection is automatically active in the viewer. See each item's implementation record and bind only the APIs actually available.

## Perspective and depth

Use a perspective camera for product scenes. Place panels and decorations at genuinely different depths; keep enough clearance to avoid interpenetrating translucent surfaces. Shadow and caustic receivers must exist as geometry. Use abstract sculptural environment forms near the viewer, with PNG backplates limited to distant visual context. Camera movement should reveal sides and produce coherent parallax in the geometry.

Avoid accidental intersection of nested transparent shells. Raster transmission cannot resolve arbitrary recursive refraction between any number of objects. The photon caustics module has its own interface tracing, which does not turn the screen renderer into a path tracer.
