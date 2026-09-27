# What is implemented

The package is an executable geometry, material, simulation and composition library. It includes **157 authored physical element entries**, 76 text-free UI recipes, six themes and a 247-entry catalogue mapping the whole research scope to source implementations and explicit gaps. All physical entries are exported as GLB assets with bounds, materials, ports and checksums in `assets/models/models-manifest.json`.

The 247 catalogue entries are not 247 separately validated physical solvers. The catalogue keeps the original requested behavior under `referenceTarget`, while `implementation` describes the actual delivered level. The workbench displays that implementation summary. Recolouring one component, exporting it twice or placing it in two recipes does not increase the physical component count.

## Coverage by family

| Family | Entries | Delivered representation | Important distinction |
|---|---:|---|---|
| A Gel bodies | 24 | Distinct 3D closed-form bodies and assemblies; physical materials; local contact preview | Automatic tetrahedral embedding of every render mesh is not included |
| B Precision controls | 24 | Separate tracks, thumbs, collars, rings, supports and dimensional controls | Viewer mechanics and host semantic values are distinct layers |
| C Containers and surfaces | 24 | Panels, speech shells, trays, reservoirs, frames, sheets and wells | A fluid port or hinge label does not automatically solve fluid pressure or joints |
| D Spatial assemblies | 18 | Docks, paths, hubs, connections and mount geometries | Host owns attachment state and complete physical constraint graphs |
| E Liquid structures | 24 | Authored 3D initial liquid shapes/domains; reusable numerical PBF fluid | Selected viewer fluid studies use a shared bounded fluid domain, not a solver fitted to each authored shape |
| F Films and bubbles | 12 | Authored shapes plus numerical closed films, gas-volume constraints, conserved drainage, flattened pair caps and optical thickness; F07 is an XPBD cloth specimen | No shared-wall foam topology, Plateau network, coalescence or rupture solver |
| G Particles and floaters | 18 | Distinct numerical particle presets with forces, drag, collisions, spin, trails and selected links/adhesion | Aerodynamic torque, phase change and automatic pinch-off classification are not implemented |
| H Force fields | 12 | Callable 3D wind, shear, vortex, attractor, repulsor, current and buoyancy fields | Correlated wake/turbulence fields are prescribed; they are not solved Navier–Stokes wakes |
| I Light and optics | 18 | Real PBR materials, single-scattering volume and refractive caustics, with mapped specimens for the scope | Some requested specializations remain visual studies; see the table below |
| J Environment components | 24 | 3D water/mineral/botanical pieces, density-domain boxes and actual scene lights | Backdrop direction is abstract; distant PNGs do not have arbitrary camera parallax |
| K Touch and collision | 24 | Reusable numerical soft-body/contact operations, particle coupling and explicit authored special cases | Shared viewer specimens are not proof of each complete target sequence |
| L Spatial transitions | 18 | Authored 3D choreography and transition bindings | Authored neck/sheet shapes are not conserved fluid topology changes |
| N Nature-derived elements | 7 | Seed, compartmented leaf tray, dew lens, tendril, glass fern, luminous spore and hollow water-root panel | Separate roles and dimensional parts; force, fluid and contact bindings remain explicit |

## Numerical modules

- **Soft body:** tetrahedral edge and volume XPBD constraints, local grabs, pins, release/cancel, supported rigid collider primitives and fixed-rate stepping.
- **Fluid:** 3D Position Based Fluids density constraints, particle collision boundaries, velocity update, vorticity, viscosity, pigment diffusion and explicit equal-mass particle transfer.
- **Film:** closed triangular membranes with edge/area tension, enclosed gas-volume constraints, conserved surface-liquid mass and gravity-driven thickness drainage; pair contact flattens separate caps. F08/F09 deliberately accelerate drainage by 100,000× to make transport visible.
- **Particles:** 3D positions, velocity, orientation, angular velocity, drag, force fields, collisions, trails, optional links and threshold adhesion.
- **Cloth and rod:** stretch/shear/bend constraints for fabric-like surfaces and stretch/bend chains. No general rod torsion or self-collision claim.
- **Coupling:** mass-weighted bilateral particle contact and momentum impulses between systems. This is staggered coupling, not a monolithic pressure/FEM solver.

These are executable numerical algorithms. Their implementation does not establish high-resolution convergence, stability for all extreme parameters, GPU throughput or production device performance. Solver tests check named invariants and scenarios; they do not certify every catalogue target.

## Optical modules and gaps

| Scope | Actual implementation |
|---|---|
| Surface transmission and absorption | Three.js physical dielectric response with distinct IOR, thickness, absorption and dispersion |
| Cloudy material interiors | 3D material-coordinate fields; the separate bounded volume shader is used for actual density integration |
| Participating medium | 96 view samples and eight light attenuation samples, Beer–Lambert extinction and Henyey–Greenstein single scattering |
| Refractive receiver caustics | Progressive CPU photons through current triangles, Snell/Fresnel/TIR, RGB dispersion, nested-medium stack, optional reflected/transmitted splitting and UV receiver deposition |
| Film appearance | Angle-dependent thin-film interference, accepting procedural thickness or live numerical film-thickness vertex data |
| Reflective caustics (I02) | Actual reflector mesh paths, reflectance/Fresnel energy and receiver deposition; ideal specular transport |
| Volumetric caustics (I04) | Actual voxel photon-segment deposition and six-direction, 128-step single-scattering view integration; finite angular/spectral quadrature |
| Strain/flow emission (I07/I08) | Authored contact/flow appearance; no automatic measured-strain or conserved-pigment-to-emission coupling |
| Wetting optics (I17) | Wet surface layers exist; no dynamic wetting/contact-angle law |
| Nested screen transmission | Raster screen-buffer transmission does not support arbitrary recursive dielectric rendering |

The caustic tracer and raster renderer are separate systems. A nested-medium stack in the photon tracer does not make every screen pixel a recursive path-traced refraction result. A rendered PNG is artwork, not evidence of solver correctness.

## Interaction and app integration

UI recipes include buttons, switches, text fields, text areas, search, secure-entry shells, tags, date/time controls, sliders, dials, menus, sheets, tooltips, cards, lists, grids, navigation, loading, feedback, layout handles and immersive component assemblies. They instantiate real 3D parts.

Recipe `behaviorBindings` specify host semantics such as navigation, focus, value commit and selection. They do not implement an application's route model, text editing/IME, accessibility tree or data validation. No Android integration is included.

The ordinary contact controller produces geometric local deformation and spring motion. The numerical XPBD specimen is separate. Every part does not silently switch to a full tetrahedral simulation just because it uses a gel material.

## Unsolved physical specializations

The main remaining specializations are scientific-grade multiphase/free-surface reconstruction; shared-wall gas-cell topology; bubble coalescence/pinch/rupture; capillary wetting and dewetting; continuum viscoelastic creep; arbitrary soft-surface continuous collision; full buoyant torque; external-geometry volume shadows and multiple scattering; multiple-scattering caustic/global transport; and complete host UI semantics. These are explicitly represented as partial or gap states in `catalogue/elements.json`.

The source and exported assets can be reused now. The gap states are retained to prevent a future implementation from mistaking an authored image, mesh or animation for a physical solver it does not contain.
