# Geometric reflection, refraction and volume caustics

`src/optics.js` traces actual current triangles. I01 refractive receiver caustics, I02 reflective receiver caustics and I04 photon-lit volume beams use this common transport core. Moving optical geometry changes the ray paths and invalidates accumulation; no caustic texture is scrolled across a receiver.

## Surface transport

```js
const caustics = new RefractiveCaustics({
  refractors: [waterLens],
  reflectors: [curvedMirror],
  receiver,
  occluders: [obstacle],
  transport: 'split', // 'transmission' remains the compatible default; also 'reflection'
  photonsPerUpdate: 768,
  maxBounces: 12,
  maxBranches: 128,
  lightPosition: new THREE.Vector3(2, 5, 1),
  lightTarget: new THREE.Vector3(0, 0, 0),
  aperture: new THREE.Vector2(4, 4)
});
attachCaustics(receiver.material, caustics.texture);
caustics.update(); // repeated batches converge static geometry
```

Dielectric interfaces use Snell refraction and exact dielectric Fresnel energy weights. Split mode sends `E*F` down the reflected branch and `E*(1-F)` down the transmitted branch. Total internal reflection carries all remaining energy. Nested dielectric media retain their own index and absorption; RGB bands use three representative wavelengths. Rays are offset after each boundary to avoid self-intersection.

Dedicated `reflectors` use reflected geometric rays. For metals, material color and metalness determine the specular energy; for dielectric reflectors, the dielectric Fresnel term applies. An explicit calibrated value can be supplied as `mirror.userData.optics = {reflectance: 0.9}` or as a linear RGB array. This optional override is an authoring input, not a measured material database.

The receiver requires valid nonoverlapping UVs. Deposited flux is divided by the local world-area/UV-area Jacobian, so the result is an irradiance density rather than a raw hit-count image. Opaque blockers absorb intersecting paths. Reflection mode requires a receiver on the reflected path; a plane positioned between the emitter and mirror will correctly intercept light before it reaches the mirror.

`statistics.energy` reports launched, receiver, escaped, absorbed, scattered, deliberately discarded and cutoff energy. These categories sum to the launched energy. Branch limits and minimum-energy termination are explicit cutoff entries, not hidden increases in brightness. In transmission-only mode rejected reflected energy is marked discarded. Default batches are progressive samples, not a claim of convergence.

## Photon-lit volume beams

```js
const caustics = new RefractiveCaustics({
  refractors: [waterLens], receiver, transport: 'split',
  volume: {
    bounds: new THREE.Box3(new THREE.Vector3(-2, 0, -2), new THREE.Vector3(2, 2, 2)),
    resolution: 32,
    extinction: 0.18,
    albedo: 0.92,
    onlyCaustics: true
  }
});
const mediumMaterial = createVolumeCausticsMaterial(caustics, {anisotropy: 0.35});
const size = caustics.volume.bounds.getSize(new THREE.Vector3());
const medium = new THREE.Mesh(new THREE.BoxGeometry(size.x, size.y, size.z), mediumMaterial);
medium.position.copy(caustics.volume.bounds.getCenter(new THREE.Vector3()));
mediumMaterial.userData.bindVolume(medium);
scene.add(medium);
```

Photon segments traverse the world-aligned volume with voxel DDA. Each cell receives the analytic Beer–Lambert scattering loss over the exact ray length inside that cell, divided by cell volume. Energy that scatters is removed from the surviving beam. Volume absorption and scattering are accounted separately. The medium is exterior mist; rays inside solid dielectrics do not also traverse mist.

Six directional grids retain positive/negative X, Y and Z contributions, weighted by squared ray-direction components. The weights sum to one. `volumeTexture` is the aggregate HDR `Data3DTexture`; `volumeTextures` contains the six directional textures. `createVolumeCausticsMaterial` raymarches those 3D fields and evaluates Henyey–Greenstein phase weights for their respective directions, including extinction between the sample and camera. It uses 128 view samples by default (`steps` accepts 32–512).

This is single scattering with six-direction angular quadrature, not an exact arbitrary-angle phase integral or multiple scattering. Photon paths still use their actual geometric directions; the angular approximation is in the volumetric display representation. The default `onlyCaustics:true` deposits visible scattering after a reflection/refraction event. Direct-path extinction still affects the energy ledger. Set it false to include direct incident light beams as well.

Supply `depthTexture` to `createVolumeCausticsMaterial` when the camera ray must terminate at an opaque object inside the volume. The bind callback updates inverse projection, camera matrix and drawing-buffer size. Without this optional depth texture, box depth testing cannot correctly clip every interior opaque intersection; place the specimen medium in an unobstructed region or provide the host render graph's depth texture. Photon-side geometry occlusion is evaluated regardless.

## Evolving film thickness

`enableFilmThickness(material)` in `src/materials.js` enables a geometry attribute named `filmThickness`, in metres. The interpolated value is converted to nanometres and passed into the material's actual thin-film interference evaluation. The BubbleFilm simulation can therefore drive optical thickness directly. Bind it only to a per-film material whose geometry supplies the attribute; other film materials retain their procedural default thickness field.

## Explicit limits

- Ideal specular reflection/refraction; rough optical-surface microfacet directions are not sampled by the photon solver.
- Three spectral bands, finite photons, finite voxel resolution and six directional volume bins.
- Homogeneous exterior volume extinction/albedo; no heterogeneous scattering coefficients, multiple scattering or volumetric polarization.
- No diffuse bounce transport, complete global illumination or automatic recursive raster transmission. The offline reference path tracer remains a separate rendering route.
- Dynamic scenes reset the accumulated photon estimate; they need substantial repeated tracing to maintain convergence.
- Receiver UVs and closed outward-wound dielectric meshes are required. The system does not repair invalid geometry.

Run `node tests/optics.test.mjs` for Snell/Fresnel/TIR, mirror energy, split-branch budget closure, exact exponential attenuation, voxel normalization across resolutions, optical-movement invalidation and evolved film-thickness binding. GPU shader compilation is a separate browser verification gate.
