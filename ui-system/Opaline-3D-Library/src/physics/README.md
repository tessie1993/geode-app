# Opaline 3D physics

Dependency-free ES modules for renderer-independent three-dimensional simulation. Coordinates are right-handed, **+Y is up**, positions are metres, time is seconds, and velocities are metres per second. No Android integration is included.

These are working numerical implementations with persistent positions, velocities and material state. They are not sprite animations. They are also not a claim that every specialized phenomenon in the research catalogue has been solved: the exact boundaries of each system are below.

## Implemented systems

| Export | Numerical construction | Useful parts |
|---|---|---|
| `SoftBody` | Tetrahedral volume and edge-length XPBD constraints, accumulated per-substep multipliers, mass weighting, local multipoint grab constraints, pins and collision projection | Gel pebbles, elastic buttons, slider thumbs, bodies and panels |
| `PBFFluid` | Three-dimensional density constraints with Poly6 density, Spiky gradients, rebuilt spatial hash, Jacobi position correction, pairwise XSPH-style viscosity, vorticity confinement, conservative pigment exchange | Contained liquid, free particle liquid, pigment channels, impact domains |
| `ParticleSystem` | 3D inertial motion, static boundary contact, pairwise momentum impulses, Coulomb-limited friction, quaternion orientation integration, optional XPBD tether links, threshold static adhesion | Floating seeds, bead clusters, spray, tracers, collision emitters |
| `Cloth` | Triangle stretch/shear edges and opposite-vertex bend-distance XPBD, pressure drag from projected relative wind | Membranes, petals, drapes, flexible sheets |
| `Rod` | Consecutive stretch and second-neighbor bend-distance XPBD | Tethers, flexible rails, tendrils and ribbon centerlines |
| `BubbleFilm` | Closed triangular membrane, XPBD enclosed-gas volume and area-tension/edge constraints, conservative surface-film drainage | Deformable bubbles and optical films |
| `BubblePairCoupling` | Separate, volume-constrained flattened contact caps | Bubble pairs and small contact clusters |
| `ParticleCoupling` | Bilateral, mass-weighted particle contact projection and equal/opposite normal and tangential impulses | Transfer between soft-body, liquid and particle systems |
| `PhysicsWorld` | One fixed-rate shared clock with retained backlog and explicit discarded wall time | Coherent cross-component stepping |
| `ContactController` | Timed authorship of localized solver grabs and impulses | Reproducible specimen press, hold, pinch, shear, pull, twist, interrupt and multiple-contact demonstrations |

All numeric particle data uses interleaved typed arrays. No graphics package is imported by these files. The renderer uploads the current buffers and recomputes normals/surface reconstruction from them; it must not use the raster assets as a substitute for physical geometry.

## Quick example: deforming gel

```js
import {
  PhysicsWorld, SoftBody, makeTetGrid, PlaneCollider,
  ContactController
} from './src/physics/index.js';

const world = new PhysicsWorld({ fixedDt: 1 / 120, maxSubsteps: 64 });
const mesh = makeTetGrid({
  nx: 6, ny: 4, nz: 6,                 // cells, not vertices
  size: [0.9, 0.35, 0.65],
  origin: [0, 0.35, 0],
  shape: 'ellipsoid'                  // 'box' also supported
});
const gel = world.add(new SoftBody({
  ...mesh,
  edgeCompliance: 2e-5,
  volumeCompliance: 1e-9,
  damping: 1.2,
  iterations: 12,
  radius: 0.006,
  colliders: [new PlaneCollider({ offset: 0 })]
}));

// Attribute storage is gel.positions; triangle indices are gel.surfaceIndices.
// They remain stable across steps. Rendering material/shadow deformation follows
// these actual xyz positions.
function frame(elapsedSeconds) {
  world.step(elapsedSeconds);
  uploadPositions(gel.positions, gel.surfaceIndices);
}

// Select a local footprint using your 3D ray hit. The grab preserves the selected
// vertices' relative offsets and constrains their centroid toward the target.
gel.grab('pointer:7', selectedVertexIndices, [hitX, hitY, hitZ], 1e-6);
gel.moveGrab('pointer:7', [hitX + 0.015, hitY - 0.03, hitZ]);
gel.releaseGrab('pointer:7');            // release keeps the current velocity

// Optional repeatable gallery demonstration, separate from real pointer input.
const demo = new ContactController(gel, 'K04', { loop: true });
// demo.update(elapsedSeconds) before world.step(); demo.cancel() on pointer input.
```

`makeTetGrid()` returns `positions`, `tetrahedra` and `surfaceIndices`. Every cell uses six conforming tetrahedra. The ellipsoid option applies a spherified-cube mapping; it preserves a volumetric interior. Surface faces are extracted by removing shared faces, with outward winding. `SoftBody.volume`, `restVolume` and `invertedTetrahedra` provide inspection values.

### SoftBody and constraint-body API

- `positions`, `velocities`: `Float32Array`, xyz per vertex.
- `inverseMass`: `Float32Array`, one value per vertex. Zero means fixed. Constructor accepts a scalar or an array; it does not infer material density from the lattice.
- `pin(index, target)`, `unpin(index)`: persistent mounts. Repeatedly pinning a vertex updates its target without losing its original inverse mass.
- `grab(id, indices, target, compliance)`, `moveGrab(id, target)`, `releaseGrab(id)`: multiple independently owned footprints. IDs must be unique per simultaneous contact.
- `cancelInteractions()`: releases all grabs while retaining deformation and velocities. Pins remain mounted.
- `impulse(index, momentum)` and `addImpulse(point, radius, totalMomentum)`: add momentum rather than resetting movement. The radial impulse is distributed by a linear spatial falloff.
- `step(dt)`: one numerical substep. Prefer the world's fixed clock for application use.

XPBD uses `alpha = compliance / dt²` and accumulates each constraint's multiplier across iterations within a substep. Multipliers reset at the next substep. Smaller compliance means a stiffer constraint. Edge compliance and volume compliance have different dimensional meanings; do not assume equal values give equal material stiffness. Velocity damping is exponential per second. It is not a constitutive creep model.

## Liquid, pigment and material accounting

```js
import { PBFFluid, makeParticleBlock } from './src/physics/index.js';

const liquid = world.add(new PBFFluid({
  positions: makeParticleBlock({
    nx: 8, ny: 6, nz: 8, spacing: 0.065, origin: [0, 0.15, 0]
  }),
  capacity: 2048,
  particleRadius: 0.022,
  smoothingRadius: 0.14,
  restDensity: 1000,
  iterations: 6,
  viscosity: 0.8,
  pigmentDiffusion: 0.08,
  vorticity: 0.015,
  bounds: { min: [-0.65, 0, -0.45], max: [0.65, 0.9, 0.45] }
}));

const sourceParticle = liquid.inject(
  [0, 0.65, 0], [0.1, -0.25, 0], [0.2, 0.6, 1]
);
liquid.addImpulse([0, 0.3, 0], 0.25, [0.025, 0.01, 0]);
const removed = liquid.remove(sourceParticle); // mass, position, velocity, pigment
```

`positions`, `velocities` and `pigment` are capacity-sized buffers. Draw only `count` particles; `activePositions` is the corresponding position view. `pigment` has three conservative concentration channels per particle. These are material concentrations, not display-corrected RGB values. Rendering may map concentrations through an absorption/scattering palette.

If `particleMass` is omitted, the constructor calibrates it from the maximum initial kernel density. This is a practical initial lattice calibration, not a measurement of the reference videos. Pass a mass explicitly for material transfer or calibrated experiments.

`mass` is `count × particleMass`. `massLedger` tracks initial, injected and removed mass. `transferTo(target, index)` preserves position, velocity and pigment between equal-mass domains, and updates both ledgers. It returns `false` without changing either domain when the destination is full. `inject()` returns `-1` without changing mass when capacity is full. `remove()` swap-removes a particle; particle indices are not permanent identities.

The density constraint penalizes compression while avoiding artificial negative pressure in sparse free-surface regions. `artificialPressure` supplies the PBF anti-clumping correction; it is not a calibrated surface-tension coefficient. `correctionClamps` counts positional corrections limited to `0.2 × smoothingRadius` per solver iteration. Inspect it while choosing dt and resolution. Viscosity and pigment mixing are pairwise symmetric; their updates preserve total momentum and total pigment respectively, up to floating-point roundoff.

The module evolves volumetric particle state. It does not contain a marching-cubes/signed-distance reconstruction pass; use the renderer's liquid surface system for a continuous optical surface.

## Particle bodies, fields and trails

```js
import { createParticlePreset, createFieldPreset } from './src/physics/index.js';

const seeds = world.add(createParticlePreset('G07', {
  count: 160,
  seed: 27,
  origin: [0, 0.8, 0],
  fields: [createFieldPreset('H04', { strength: 0.6, lift: 0.12 })]
}));
seeds.burst([0, 0.2, 0], [0, 1, 0], 0.5, 20);
```

Every `G01`–`G18` profile is present in `PARTICLE_PRESETS`. These profiles share the numerical engine and vary actual state, force field, gravity, drag, collision response, initial distribution and trails. They also provide rendering metadata (`shape`, `color`, `transmission`, `emission`). They are not eighteen independently invented numerical algorithms.

`positions`, `velocities`, `colors`, `radii`, `inverseMass`, `ages`, `lifetimes` and `angularVelocities` are live typed arrays. `orientations` is xyzw per particle. Angular velocity is integrated and the quaternion renormalized; the ordinary particle emitter does not derive aerodynamic torque from a detailed petal surface. Use `Cloth` for an aerodynamic sheet.

Trails are recorded 3D particle history, not a fluid vortex model. Storage is `trails[(sample * capacity + particle) * 3 + axis]`; the latest sample is `trailHead`. Read older samples by wrapping backward modulo `trailLength`.

`emit()` records introduced mass; lifetime expiration and `remove()` record removed mass. `connect(pairs, compliance)` adds XPBD links. G17 uses these links for a connected tether chain. G15 enables a threshold adhesive constraint at static collision contact and releases it when the applied acceleration threshold is exceeded. It is not a capillary surface-energy solver.

### Field contract

`field.sample(x, y, z, time, out)` writes **acceleration in world space** to the three-element `out`. All bodies can receive the same field objects. This is a force-per-unit-mass authoring contract, not an implicit air-flow solver.

| Field | Behavior |
|---|---|
| `WindField` | Directional forcing, travelling sinusoidal gust, height shear and spatially correlated trigonometric disturbance |
| `VortexField` | Swirl about an arbitrary 3D axis, radial attraction and axial lift |
| `VortexRingField` | Toroidal circulation with a moving ring centre |
| `RadialField` | Bounded attractor or repulsor with quadratic falloff |
| `BuoyancyField` | Submerged-fraction upward acceleration relative to a horizontal free-surface level |

`createFieldPreset('H01' ... 'H12')` exposes twelve authored configurations. H06 is a prescribed wake-like field, not resolved Navier–Stokes flow around an obstacle. H09 requires the host to update its tangent direction when a surface turns. H11 is a quiet forcing preset; spatial drag blending remains host controlled. The limitations are included in machine-readable preset metadata.

## Collision and shared movement

`PlaneCollider({normal, offset})` describes the allowed positive half-space `dot(normal, p) >= offset`. `SphereCollider({center, radius, inside})` either excludes a solid sphere or contains particles inside it. `BoxCollider({min, max, inside})` similarly contains particles or excludes a box. All accept `restitution` and `friction`.

These are discrete particle-boundary tests. They are not continuous collision detection; a small fast particle can tunnel through a thin obstacle between steps. Use a sufficiently small fixed dt and adequate collider dimensions. Kinematic collider transforms can be updated, but these static boundary classes do not infer collider velocity or reciprocate forces into another body.

Use `world.couple(a, b, {radiusA, radiusB, restitution, friction, onContact})` for actual bilateral momentum transfer. The callback includes location, normal, impulse, penetration and both particle indices. It can start shared optical contact effects and accounted particle emission.

Coupling happens after each system's internal substep. It is staggered, particle-proxy coupling, not a monolithic fluid/FEM solve. Soft-body vertex proxies must overlap enough to form a closed contact boundary; sparse vertex spheres can leak between vertices. The coupling does not give automatic analytic solid-volume buoyancy.

## Cloth and rods

`makeClothGrid({nx, ny, size:[width,height], origin, plane:'xy'|'xz'})` returns a triangulated sheet for `new Cloth(...)`. Structural triangulation supplies stretch and shear constraints. Shared-edge opposite vertices supply bend-distance constraints. `aerodynamicWind` is an explicit velocity vector in m/s; pressure drag is evaluated from projected relative triangle velocity and area.

`makeRodPoints({count, length, origin, direction, curl})` returns xyz positions for `new Rod(...)`. The root may be pinned and the tip grabbed. Rods use stretch and bend-distance constraints, with an optional closed loop. Neither rods nor cloth implement topological tearing, robust self-contact or torsional material frames. They should not be described as a complete discrete elastic rod or finite-element shell solver.

## Input/contact recipes

`createContactRecipe('K01' ... 'K24')` returns contact regions, local displacement, compliance, timing, impulse and collision parameters. `ContactController` turns supported local constraints into repeatable physical demonstrations. Pointer cancel must call `cancel()` and release any real input grabs owned by that pointer.

The recipes include explicit limitations for features not supplied here. K10/K12/K22 need a second system and bilateral coupling. K13 needs a liquid domain. K14 can now be assembled using `BubbleFilm` and `BubblePairCoupling` for distinct flattened film caps; a shared topological wall remains unimplemented. K18 capillary wetting is **not implemented by these recipes**. Empty contact lists are intentional metadata for cross-system assembly, not completed simulations disguised as no-ops.

App values and actions must be evaluated from their own stable input coordinate frame. Elastic visual lag must never delay a button action or alter an authoritative slider endpoint.

## Clock, determinism and overload

Call `world.step(elapsedSeconds)` once per frame. Systems receive a fixed `fixedDt` in insertion order. For reproducible tests, use `world.advance(integerSteps)` and seeded particle emitters. Different render-frame chunk sizes produce identical state when they cover the same number of substeps and the same input event sequence.

`maxSubsteps` limits work in one call while **retaining** pending time in `backlogSeconds`. `maxFrameDt` clips a large wall-time jump and records it in `droppedTime`; the application can inspect it. `alpha` exposes bounded interpolation phase. There is no hidden frame-rate-dependent reset of liquid, pigment or velocity.

These CPU reference implementations do not claim mobile frame rate, GPU throughput, real-time audio-thread suitability, cross-platform bitwise determinism, calibrated continuum accuracy or production robustness at arbitrary deformation. They allocate neighbor lists and contact records during simulation. No native/Android API is present.

## Verification

Run from the package root:

```sh
node --test tests/physics.test.mjs tests/film.test.mjs
```

The 17 base tests verify closed outward tetrahedral geometry, volume recovery after 35% compression, simultaneous local contacts and cancellation, plane/sphere nonpenetration, frame-chunk determinism, overload accounting, density-error reduction, bounded finite fluid state, conserved mass and material transfer, pigment and momentum conservation, bilateral impulses, restitution, pinned aerodynamic cloth, rod length, all particle/field presets, rejected invalid inputs, and distinct contact recipes.

Six additional film tests verify a closed manifold, enclosed gas recovery, finite state, conservative drainage and thickness bounds, plane flattening, paired contact caps and grab cancellation.

Passing these tests validates those invariants for the tested scenes and parameters. It does not validate shared-wall foam topology, capillary wetting, topology-changing liquid reconstruction or arbitrary target hardware performance.

## Method sources and implementation provenance

The code in this directory was authored for Opaline from the mathematical method descriptions. It does not incorporate a third-party solver implementation.

- Macklin, Müller and Chentanez, [XPBD: Position-Based Simulation of Compliant Constrained Dynamics](https://matthias-research.github.io/pages/publications/XPBD.pdf), 2016. Supplies the compliant constraint/multiplier formulation.
- Macklin and Müller, [Position Based Fluids](https://matthias-research.github.io/pages/publications/pbf_sig_preprint.pdf), 2013. Supplies the density constraint, artificial-pressure and vorticity/viscosity construction.

Coefficients, generated lattices, preset mappings, UI contact recipes, render connections and acceptance thresholds are Opaline authoring choices rather than values recovered from the supplied videos.


## Closed bubbles, film thickness and pair contact

```js
import { BubbleFilm, BubblePairCoupling, makeBubbleMesh } from './src/physics/index.js';

const a = world.add(new BubbleFilm({
  ...makeBubbleMesh({ subdivisions: 3, radius: 0.6, center: [-0.5, 0.7, 0] }),
  thickness: 400e-9,                // metres: 400 nm
  minThickness: 80e-9,
  maxThickness: 1500e-9,
  gasCompliance: 1e-8,
  edgeCompliance: 8e-4,
  surfaceTension: 0.025,
  drainageTimeScale: 1             // real solver seconds; preview may accelerate
}));
const b = world.add(new BubbleFilm({
  ...makeBubbleMesh({ subdivisions: 3, radius: 0.6, center: [0.5, 0.7, 0] })
}));
world.couplings.push(new BubblePairCoupling(a, b, { gap: 0.002, iterations: 8 }));

// Renderer: live xyz = a.positions; triangle indices = a.triangles.
// Optical thickness at vertex i = a.thickness[i] * 1e9 nanometres.
// User contact: a.grab(id, indices, target); a.moveGrab(...); a.releaseGrab(id).
```

`makeBubbleMesh()` recursively subdivides an octahedron and projects new vertices onto a sphere. It creates a closed triangulated manifold with no duplicate seam or collapsed pole triangles. `BubbleFilm` rejects open or inconsistently wound meshes.

The enclosed-gas constraint uses the signed volume of the closed triangle surface and its pervertex gradient. It preserves a target gas volume with configurable compliance; it is not a thermal ideal-gas model. Edge constraints stabilize the material discretization. Area tension uses `C = sqrt(2 A)` and compliance `1 / surfaceTension`, giving the area-proportional constraint energy `surfaceTension × A`. Surface particles have authored inverse masses independently of the transported liquid; coefficients are not calibrated from the video references.

Film material is stored as `filmMass[i]` in kilograms on barycentric vertex areas. As the membrane stretches, `thickness[i] = filmMass[i] / (liquidDensity × area[i])`. Gravity-driven edge transport uses a thin-film mobility proportional to `h³ / (3 μ)`, geometric downhill slope and dual-edge width. Every accepted transfer subtracts mass from one vertex and adds it to another; donor/receiver capacities prevent transport from exceeding the requested thickness interval. Geometry-induced clamps redistribute material conservatively. If imposed stretching makes the interval mathematically infeasible for the retained liquid, `boundsRelaxed` becomes true and the interval is relaxed rather than creating/deleting liquid.

`drainageTimeScale` explicitly controls the drainage clock. A value of `1e5` is suitable for visibly accelerated gallery studies; it must not be presented as calibrated real-time drainage. Film flow is a conservative thin-film mobility model, not a full free-film Navier–Stokes/surfactant solution. See the primary research discussions of boundary-condition dependence in [Gravitational Drainage of a Tangentially-Immobile Thick Film](https://www.sciencedirect.com/science/article/abs/pii/S0021979799964895) and [Soap film drainage: theory of experiment](https://www.sciencedirect.com/science/article/pii/0009250994800669). The graph discretization and bounds policy here are original authoring choices.

`BubblePairCoupling` computes a contact plane from the two enclosed-gas equivalent radii, projects opposing caps to separated sides of that plane, and iterates volume restoration. Its position corrections affect inertial velocity; it does not merely overlap transparent spheres. `contactRadius` and `contactCount` expose contact state. Several pair couplings can form a small contact cluster, but a multi-bubble Plateau network is not solved.

Both films remain independent closed surfaces. There is **no shared topological wall, rupture, coalescence, evaporation, molecular disjoining-pressure model, dynamic remeshing, robust self-intersection handling or Plateau-border transport** in this module. Distinct cap surfaces must be labelled accordingly in film/foam studies.
