# Handoff for a future application adapter

Use this library as a coordinated set of 3D parts. Read the target application's actual screens, navigation and state before selecting compositions. Keep the library's text-free visual assets separate from application content and semantics. This package does not yet integrate with Android or any application repository.

## Source of truth

1. `catalogue/elements.json`: actual element IDs, implementation status, source entrypoints and limits.
2. `catalogue/compositions.json`: generic UI assemblies and empty content frames.
3. `catalogue/design-tokens.json`: coordinate, material, spacing and content conventions.
4. `catalogue/artwork-motion-map.json`: generated artwork mapped to live geometry and behavior.
5. `docs/IMPLEMENTATION-STATUS.md`: what is numerical, authored and still a target gap.
6. Source APIs and their local documentation: geometry, materials, physics, optics and transitions.

`referenceTarget` describes the desired reference behavior. It must never override the actual `implementation` field when making a feature claim. A detailed name such as “bubble coalescence” does not mean a topology solver is complete.

## Mapping application roles

| Application role | Library assembly family |
|---|---|
| Action/toolbar affordance | UI001–UI005 |
| Choice, toggle or filter | UI006–UI010 |
| Editable content | UI011–UI018 |
| Continuous control | UI019–UI026 |
| Context, dropdown or action menu | UI027–UI033 |
| Navigation | UI034–UI038, UI062 |
| Card, dialog, sheet or tooltip | UI039–UI046 |
| Progress, loading or status | UI047–UI054 |
| Lists, grids, media and grouped content | UI055–UI061 |
| Pickers | UI063–UI066 |
| Split, scroll, resize or reorder | UI067–UI070 |
| Immersive scene composition | UI071 onward |

Map semantic role first, then adjust dimensions and content-frame padding. Do not turn a semantic form input into an image-only button. Do not make text part of a deforming shader if the input must remain readable, selectable or accessible.

## Required adapter responsibilities

The host creates stable semantic state, keyboard/IME editing, accessibility roles, routes, focus order, validation, list virtualization and selection behavior. It maps those states into bounded visual control coordinates and events. One shared physical clock, force-field set and light rig should govern connected 3D elements.

Input value changes should occur when the application accepts the input, independently of visual spring settling. A user should not need to wait for a droplet to finish a flourish before a button action runs. Readable content stays attached to a stable front frame while the shell deforms.

When a component leaves the scene, release contacts and pointer ownership, then dispose instance-owned GPU and numerical resources. Respect a reduced-motion setting by providing settled views and deliberate transitions without changing the semantic interaction.

## Preserve the intended appearance

The environment is abstract and spatial. Use sculptural liquid or mineral masses, gradients, thin mist and restrained luminous particles. Give foreground parts visible sidewalls, underside shading, real overlap and coherent perspective. Use the generated artwork as an appearance target, and the 3D geometry as the live interactive structure.

Keep gel, clear shell, water, film and stone different. The soft controls need dense optical depth; a uniformly transparent pane does not reproduce that. Use one shared illumination scheme so every part appears to inhabit the same place.

For the latest motion language, a moving pearl can stretch in its travel direction and recover at its stop; a liquid fill has an advancing meniscus; an emerging notification separates into body, draining connection, drops, water contact and delayed emission. Some of these sequences are authored choreography and must not be represented as conserved fluid simulation unless that solver binding is actually present.

## Review a future adapter

Inspect important components from the front, side and underside. Change lighting, resize the layout, cancel input during motion, use simultaneous contacts and confirm the application state stays correct. Check scene coupling and camera parallax rather than judging only a still screenshot. Record device performance after the adapter exists; this source package makes no mobile frame-rate claim.
