# Spatial transitions — implemented L01–L18

These are reusable, authored **three-dimensional choreography components**. They operate on persistent Three objects, current world poses, local mesh vertices and actual scene cameras. They do not use screen wipes, opacity crossfades or image scaling. The fluid-named recipes do not imply an unimplemented fluid topology solver: their actual construction and limits are listed below.

```js
import { TransitionSystem } from './src/transitions.js';

const transitions = new TransitionSystem({ scene, camera, eventBus });
const handle = transitions.play('L01', {
  object: gelPebble,
  to: [0, 1.3, 1.8], // world coordinates
  duration: 2.4
});
// Per render frame, after the host decides who owns kinematic poses:
transitions.update(dtSeconds);
// A later play on the same object cancels its previous owner without a pose reset.
transitions.play('L02', { object: gelPebble, ports: { mount: socket }, duration: 1.8 });
```

## Implemented recipes

| ID | Recipe | Actual implementation | Inputs and material limits |
|---|---|---|---|
| L01 | Lift to foreground | World-space Hermite travel, raised spatial arc, retained initial velocity, remembered source pose | `offset`, `to`, `arcHeight`; water responds through host ports/events |
| L02 | Return to mount | Descending arc, target socket resampled every update, pose alignment, zero terminal velocity | `ports.mount` can be an Object3D or callback; actual current socket is authoritative |
| L03 | Orbit reconfiguration | Coordinated 3D orbit, distinct height lanes, persistent body identity | `objects`, `center`, `angle`, `laneSpacing`; optional richer `radialReveal` mode below |
| L04 | Pebble-to-panel transformation | Vertex interpolation into a rounded volumetric panel while retaining indices, material and mesh identity | `targetGeometry` must match vertex/index topology; otherwise an authored rounded superellipsoid target is constructed |
| L05 | Seed-to-row construction | Rounded 3D seed broadens into an elongated support with a delayed advancing deformation front | `targetSize`; authored growth introduces volume, so the host must declare its material source if conservation matters |
| L06 | Panel contour reveal | Outer vertices form first, inner region follows, geometry stops at 72% of the duration, then a local material excitation sweeps across it | Separate geometry and light phases; the interior is not an alpha reveal |
| L07 | Water crest wipe | A real closed body travels across the scene and folds its cross-section into a crest, including rear-visible curl | Authored mesh deformation; it neither generates nor conserves free fluid by itself |
| L08 | Liquid curtain reveal | A dimensional sheet/body travels with spatially varying curvature and a lagging lower region | Use an authored sheet as `object`; `objects` can include real supplied trailing droplets |
| L09 | Depth fan expansion | Independent objects move into spaced world positions with orientation, depth and raised transition paths | Stores each initial pose for L10; arbitrary panel collision avoidance belongs to `ports.resolvePose` |
| L10 | Depth fan collapse | Independent objects return to remembered fan origins; fallback is a spaced stack | Keeps material and geometry state; caller controls destination stack thickness |
| L11 | Dock growth | Support vertices extend from an anchored end; supplied mounted children move with growing socket spacing | `support`, `children`, `growth`; child materials and child geometry are not restarted |
| L12 | Branch extension | A local-space Catmull–Rom centerline bends and extends the actual mesh cross-section, with a delayed leading front | `path` supplies centerline points; it is authored construction, not botanical or material-growth simulation |
| L13 | Droplet transfer | Raised flight arc and determinant-one axial stretch; one withdrawal and one deposit adapter call | `mass`, `pigment`, `ports.withdraw`, `ports.deposit`; supplied fluid solver owns neck/pinch-off topology |
| L14 | Fluid merge reveal | A connected fixed-topology bridge broadens through real vertex motion, preserving its existing material state | An authored connected bridge; **does not merge disjoint fluid manifolds or conserve dye** |
| L15 | Membrane aperture reveal | Added finite-thickness annulus opens a genuine hole; front, back, outer wall and inner rim are rendered geometry | `innerRadius`, `outerRadius`, `apertureOffset`, optional `material`; hole reveals real scene behind it |
| L16 | Particulate assembly | Persistent pieces accelerate toward actual 3D targets using damped dynamics; equal-mass sphere contacts resolve overlap | `objects`, `targets`, `radius`, `spacing`, `stiffness`, `floor`; object topology is never silently converted |
| L17 | Particulate dispersal | Persistent pieces acquire 3D flight velocity and wind response with sphere collisions and inherited momentum | `wind(position,time)`, `drag`, `spin`, `distance`; completion releases nonzero velocity to the host |
| L18 | Camera passage | The actual perspective camera travels along a world path with preserved entry momentum and optional focal target | `to`, `path`, `lookAt`; does not change gravity, object dimensions, material parameters or exposure |

Default geometry targets and paths make every ID executable. For production, author compatible source geometry, final mounts and material adapters for the intended layout rather than treating the defaults as validated collision-free arrangements.

## Radial reveal from the new motion references

The additional `L03` mode implements the observed causal sequence: local central awakening → light moving along curved links → individually staggered bodies lifting through depth → distinct radial sockets → quiet final structure. It creates actual curved connection tubes and small luminous heads moving along their 3D curves. Optional point lights make these heads illuminate nearby matter. The center receives a localized material excitation; all bodies do not flash together.

```js
const reveal = transitions.play('L03', {
  objects: twelvePersistentLensBeads,
  center: [0, 0.15, 0],
  centerObject: seed,
  radialReveal: true,
  radius: 1.65,
  lift: 0.6,
  duration: 6,
  stagger: 0.012,
  connectionLights: true
});
```

The bodies begin wherever the caller placed them. The engine does not teleport an existing assembly back into the seed. Build the initial compact composition explicitly when presenting this sequence. Generated tubes and light heads remain as part of the finished arrangement until `dispose()`; `handle.auxiliary` exposes them for further choreography.

Named stage events are `transition:awakening`, `transition:connection-front`, `transition:radial-lift`, `transition:socket-approach`, and `transition:aftermath`. A shared water simulation can consume these to produce residual waves and contact responses. The transition module does not create a second isolated water simulation.

## API and ownership

`new TransitionSystem({scene, camera, eventBus})`

`play(id, options)` returns a handle with `key`, `id`, `status`, `progress`, `elapsed`, `duration`, `entries`, `auxiliary`, optional `transfer`, and `cancel(options)`. `objects` overrides `object`. For assembly IDs the direct children of a supplied group are used if there are multiple pieces. L18 defaults to the constructor camera. `duration` is in seconds.

`to`, `targets[i]`, `from` and `ports.mount` accept world-space vectors, `[x,y,z]`, `{position,quaternion,scale}`, an Object3D, or a callback returning one of these. `path` is local space for branch deformation and world space for camera passage. Geometry `targetSize` is local space. `from` is an explicit initial-pose instruction; it does not override the current pose when a transition is replacing an active owner.

`update(dt)` advances with substeps no longer than 1/120 second. Large valid `dt` values are integrated rather than silently discarded. `cancel(handle,{preserveVelocity:true})` leaves current transforms and deformed vertices in place. Starting a new transition on an owned object first cancels its previous transition, including the previous coordinated assembly.

World-position first derivatives are retained during ordinary interrupted retargeting. Quaternion interpolation also retains a residual angular-momentum correction. Mesh deformation starts from the exact current vertex snapshot, preserving positional continuity; it does **not** infer the original soft body's strain or per-vertex velocity state. A host soft-body solver must supply that state through its own constraint handoff.

The system preserves objects, materials and their shader uniforms. Deformation clones a mesh's geometry before editing it, so another object sharing the original geometry is not altered. Replaced transition-owned clones are disposed. Installed geometry clones belong to the caller after the system is disposed; dispose them with the containing object tree. Generated aperture and connector helper geometry/materials are removed and disposed by the system.

An active particle assembly waits after the nominal duration until its positional and velocity tolerances are met. Incompatible targets that cannot satisfy contact constraints produce `status: 'blocked'` and `transition:blocked` after `maxSettleTime` (default six seconds). They are not snapped through each other. Dispersal ends at its duration with a real residual velocity, rather than promising to land on an exact destination.

## Physics ports

| Port | Callback |
|---|---|
| `acquire` | `({object,pose,velocity,handle})` — host grants kinematic ownership or switches a constraint target |
| `update` | `({object,pose,velocity,handle,progress,dt})` — synchronize collider/contact/constraint state |
| `resolvePose` | `({object,pose,velocity,handle,progress}) => pose` — host collision or mechanical guide may correct an authored pose |
| `release` | `({object,pose,velocity,angularVelocity,handle})` — return to dynamics with current momentum |
| `mount` | Object3D/callback/pose — current destination socket for L02 |
| `withdraw` | `({mass,object,handle}) => packet` — source removes one fluid packet for L13 |
| `deposit` | `(packet,{object,handle})` — destination accepts that same packet once |

For L13, absent withdrawal/deposit callbacks are explicitly reported by `handle.transfer.accountedByHost === false`. The internal ledger describes the authored transfer, not a simulated mass-conservation proof. A cancelled in-transit transfer remains attached to its handle so the caller can continue or explicitly return it; it is not silently duplicated or refunded.

`eventBus` may be a function receiving an event, an emitter with `emit(type,event)`, or a Three-style object with `dispatchEvent(event)`. `onEvent`, `onUpdate`, and `onComplete` callbacks can be supplied per transition. Lifecycle events are `transition:start`, `transition:complete`, `transition:cancel`, and `transition:blocked`; L13 also emits `transition:detach` and `transition:deposit`.

## Verification

Run `node tests/transitions.test.mjs`. The suite executes all 18 IDs and checks 25 cases: finite geometry and poses, endpoints, material identity, interrupted first derivative, retained mesh shape, dynamic world-space mounts under parents, single packet withdrawal/deposit, an actual aperture, particle separation, and the 12-socket radial reveal with stage events. These checks verify the implemented contracts, not universal layout collision freedom or fluid conservation.
