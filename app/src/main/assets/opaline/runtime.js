import * as THREE from 'three';
import {RoomEnvironment} from './vendor/three/examples/jsm/environments/RoomEnvironment.js';
import {createElement, listGeometryIds} from './src/geometry.js';
import {createMaterials, updateMaterials} from './src/materials.js';
import {MotionController, Spring} from './src/motion.js';
import {TransitionSystem} from './src/transitions.js';
import {PhysicsWorld} from './src/physics/world.js';
import {SoftBody, makeTetGrid} from './src/physics/soft-body.js';
import {ParticleSystem, WindField} from './src/physics/particles.js';
import {RefractiveCaustics, attachCaustics} from './src/optics.js';
import {sanitizeSnapshot, projectBounds, frameBudget} from './protocol.js';

const pending = window.Opaline?.pending;
const supported = new Set(listGeometryIds());
let renderer, environment, animation = 0, previous = 0, elapsed = 0, disposed = false, firstFrame = false;
let viewport = sanitizeSnapshot(pending || {}), needsRender = true, slowFrames = 0;
let renderFrames = 0, caustics = null, causticSource = null;
let causticBinding = null, causticPose = '', causticStableFrames = 0, causticEpoch = 0;
let causticCostMs = 0;
const causticLightOffset = new THREE.Vector3(-5, 7, 10);
let section = viewport.section, ratio = Math.min(devicePixelRatio || 1, 2);
const scene = new THREE.Scene();
scene.background = new THREE.Color('#33465f');
const camera = new THREE.PerspectiveCamera(38, 1, 0.1, 70);
camera.position.set(0, 0, 16);
const materials = createMaterials('tidal');
// Flagship art direction: pearl shoulders retain solid depth; clear gel carries
// cyan/lavender interference and a distinct, optically denser interior.
materials.gel.roughness = 0.115;
materials.gel.transmission = 0.8;
materials.gel.iridescence = 0.72;
materials.gel.iridescenceThicknessRange = [170, 520];
materials.gel.color.set('#c6f0f3');
materials.nacre.transmission = 0.65;
materials.nacre.roughness = 0.15;
materials.nacre.color.set('#e3f5f2');
materials.blue.iridescence = 0.72;
materials.blue.roughness = 0.1;
const controls = new Map(), ownedPointers = new Map();
const transitions = new TransitionSystem({scene, camera});
const world = new PhysicsWorld({fixedDt: 1 / 120, maxSubsteps: 4, maxFrameDt: 1 / 30});
const disposables = [];

function releaseTree(root, disposeMaterials = false) {
  const geometry = new Set(), mats = new Set();
  root.traverse(object => {
    if (object.geometry) geometry.add(object.geometry);
    if (disposeMaterials && object.material) {
      for (const material of (Array.isArray(object.material) ? object.material : [object.material])) mats.add(material);
    }
  });
  geometry.forEach(item => item.dispose());
  mats.forEach(item => item.dispose());
  root.removeFromParent();
}

function makePart(spec) {
  const root = createElement(spec.element, materials, {dimensions: [1, 1, 1], center: true});
  const originalGeometry = new Set();
  root.traverse(object => { if (object.geometry) originalGeometry.add(object.geometry); });
  const wrapper = new THREE.Group();
  wrapper.add(root);
  scene.add(wrapper);
  const controller = /^[ABN]/.test(spec.element) ? new MotionController(root, {id: spec.element}) : null;
  const retainedGeometry = new Set();
  root.traverse(object => { if (object.geometry) retainedGeometry.add(object.geometry); });
  originalGeometry.forEach(geometry => { if (!retainedGeometry.has(geometry)) geometry.dispose(); });
  const lift = new Spring(0, 5, 0.82);
  const entry = {spec, root, wrapper, controller, lift, depth: 0, layout: null};
  controls.set(spec.id, entry);
  return entry;
}

function removePart(id) {
  const entry = controls.get(id);
  if (!entry) return;
  for (const [pointer, owner] of ownedPointers) if (owner === id) ownedPointers.delete(pointer);
  entry.controller?.dispose();
  if (causticSource && (entry.root === causticSource || entry.root.getObjectById(causticSource.id))) clearCaustics();
  releaseTree(entry.wrapper);
  controls.delete(id);
}

function layoutPart(entry, spec, settle = false) {
  const isSupport = /^[CD]/.test(spec.element);
  entry.depth = isSupport ? -0.16 : 0.1;
  const box = projectBounds(spec, viewport, camera.fov, camera.position.z, entry.depth);
  entry.layout = box;
  entry.wrapper.position.set(box.x, box.y, entry.depth);
  entry.wrapper.scale.set(box.width, box.height, box.thickness);
  entry.root.traverse(mesh => {
    mesh.castShadow = !isSupport;
    mesh.receiveShadow = true;
  });
  const value = spec.element === 'B09' || spec.element === 'B10' ? Number(spec.selected) : spec.value;
  if (entry.controller) {
    entry.controller.value.target = value;
    for (const part of entry.controller.parts) part.value.target = value;
    if (settle || viewport.reducedMotion) {
      entry.controller.value.reset(value);
      entry.controller.parts.forEach(part => part.value.reset(value));
      entry.controller.pressure.reset(0);
      entry.controller.update(0, elapsed);
    }
  }
  entry.lift.target = spec.selected ? 0.09 : 0;
  if (settle || viewport.reducedMotion) entry.lift.reset(entry.lift.target);
  entry.spec = spec;
}

// One prioritized visible closed gel shell refracts onto the shared UV receiver.
function clearCaustics() {
  if (causticBinding) causticBinding.uOpCaustics.value = causticFallback;
  caustics?.dispose();
  caustics = null;
  causticSource = null;
  causticPose = '';
  causticStableFrames = 0;
}

function selectCausticSource() {
  const priority = {A22: 4, B07: 3, C01: 2, C03: 2};
  const candidates = [...controls.values()].filter(entry => entry.spec.enabled && priority[entry.spec.element]);
  candidates.sort((a, b) => priority[b.spec.element] - priority[a.spec.element] ||
    Number(b.spec.selected) - Number(a.spec.selected) ||
    Math.min(b.layout.width, b.layout.height) - Math.min(a.layout.width, a.layout.height));
  let mesh = null;
  for (const entry of candidates) {
    entry.root.traverse(object => {
      if (!mesh && object.isMesh && object.geometry?.attributes?.uv && object.material?.transmission > 0.3 &&
          ['body', 'panel'].includes(object.userData.role)) mesh = object;
    });
    if (mesh) break;
  }
  if (mesh === causticSource) return;
  clearCaustics();
  if (!mesh?.geometry?.attributes?.uv) return;
  scene.updateMatrixWorld(true);
  const target = mesh.getWorldPosition(new THREE.Vector3());
  causticSource = mesh;
  caustics = new RefractiveCaustics({
    refractors: [mesh], receiver, photonsPerUpdate: 32, resolution: 96,
    lightPosition: target.clone().add(new THREE.Vector3(-5, 7, 10)), lightTarget: target,
    aperture: new THREE.Vector2(2.8, 2.8), irradiance: 4, maxBounces: 4, maxBranches: 8,
  });
  causticEpoch = elapsed;
  causticBinding.uOpCaustics.value = caustics.texture;
}

function traceCaustics() {
  if (!caustics || !causticSource) return;
  scene.updateMatrixWorld(true);
  caustics.lightTarget.copy(causticSource.getWorldPosition(new THREE.Vector3()));
  caustics.lightPosition.copy(caustics.lightTarget).add(causticLightOffset);
  const pose = `${causticSource.geometry.attributes.position.version}:` + causticSource.matrixWorld.elements.join(',');
  if (pose === causticPose) causticStableFrames++;
  else { causticPose = pose; causticStableFrames = 0; }
  // Moving geometry invalidates prior samples. Keep a bounded low-confidence
  // pattern alive, then spend more of the frame budget as the pose settles.
  const started = performance.now();
  const moving = causticStableFrames < 4;
  const targetMs = slowFrames > 8 ? 1 : moving ? 2 : 4;
  const maxBatches = moving ? 8 : 32;
  let batches = 0;
  do {
    caustics.update(elapsed, {photons: 1});
    batches++;
  } while (batches < maxBatches && performance.now() - started < targetMs);
  causticCostMs = performance.now() - started;
}

function applySnapshot(input) {
  const next = sanitizeSnapshot(input);
  const previousReduced = viewport.reducedMotion;
  viewport = next;
  camera.aspect = next.width / next.height;
  camera.updateProjectionMatrix();
  const visible = new Set(next.parts.filter(part => supported.has(part.element)).map(part => part.id));
  for (const id of controls.keys()) if (!visible.has(id)) removePart(id);
  for (const part of next.parts) {
    if (!supported.has(part.element)) continue;
    let entry = controls.get(part.id);
    if (entry && entry.spec.element !== part.element) {removePart(part.id); entry = null;}
    const fresh = !entry;
    entry ||= makePart(part);
    layoutPart(entry, part, fresh || previousReduced !== next.reducedMotion);
  }
  selectCausticSource();
  if (next.section !== section) {
    section = next.section;
    if (!next.reducedMotion) transitions.play('L01', {object: sculpture,
      to: [sculpture.position.x, sculpture.position.y, -1.15], duration: 0.62, arcHeight: 0.12});
  }
  resize();
  if (!next.active || next.reducedMotion) cancelContacts();
  needsRender = true;
  schedule();
}

function cancelContacts() {
  ownedPointers.clear();
  controls.forEach(entry => entry.controller?.release());
  world.cancelInteractions();
}

function touch(event) {
  if (disposed || !viewport.active || viewport.reducedMotion) return;
  const entry = controls.get(String(event?.id));
  if (!entry || !entry.spec.enabled || !entry.controller) return;
  const pointer = String(event.pointer ?? 0);
  if (event.action === 'cancel' || event.action === 'up') {
    if (ownedPointers.get(pointer) === entry.spec.id) {
      ownedPointers.delete(pointer);
      if (![...ownedPointers.values()].includes(entry.spec.id)) entry.controller.release();
    }
    return;
  }
  const x = Math.max(0, Math.min(1, Number(event.x) || 0));
  const y = Math.max(0, Math.min(1, Number(event.y) || 0));
  const worldPoint = new THREE.Vector3(entry.layout.x + (x - 0.5) * entry.layout.width,
    entry.layout.y + (0.5 - y) * entry.layout.height, entry.depth + entry.layout.thickness * 0.5);
  scene.updateMatrixWorld(true);
  if (event.action === 'down') {
    ownedPointers.set(pointer, entry.spec.id);
    // Native UI is authoritative: begin()'s toggle preview never commits a value.
    const target = entry.controller.value.target;
    entry.controller.begin(worldPoint);
    entry.controller.value.target = target;
    gel.addImpulse([0, 0, 0.35], 0.7, [(x - 0.5) * 0.003, (0.5 - y) * 0.003, -0.006]);
  } else if (ownedPointers.get(pointer) === entry.spec.id) {
    entry.controller.contact.copy(entry.root.worldToLocal(worldPoint));
  }
  needsRender = true;
  schedule();
}

// Small bounded tetrahedral sculpture: true XPBD volume and edge constraints.
// It is independent of semantic controls and never runs on the audio thread.
const domain = makeTetGrid({nx: 6, ny: 4, nz: 6, size: [1.35, 1.0, 0.8], shape: 'ellipsoid'});
const gel = world.add(new SoftBody({...domain, gravity: [0, 0, 0], damping: 2.5,
  edgeCompliance: 2e-5, volumeCompliance: 1e-9, iterations: 8}));
for (let index = 0; index < gel.count; index++) if (gel.positions[index * 3 + 2] < -0.25) gel.pin(index);
const gelGeometry = new THREE.BufferGeometry();
gelGeometry.setAttribute('position', new THREE.BufferAttribute(gel.positions, 3));
gelGeometry.setIndex(new THREE.BufferAttribute(gel.surfaceIndices, 1));
gelGeometry.computeVertexNormals();
const sculpture = new THREE.Mesh(gelGeometry, materials.blue);
sculpture.rotation.set(0.15, -0.35, -0.28);
sculpture.position.set(1.6, 2.5, -1.15);
scene.add(sculpture);

const seeds = world.add(new ParticleSystem({capacity: 48, seed: 76, gravity: [0, 0.018, 0],
  drag: 1.7, fields: [new WindField({strength: 0.014, turbulence: 0.025, gust: 0.1})],
  particleCollisions: false}));
for (let index = 0; index < 48; index++) seeds.emit({
  position: [(seeds.random() - 0.5) * 10, (seeds.random() - 0.5) * 12, -1.8 - seeds.random() * 2],
  radius: 0.015 + seeds.random() * 0.018, mass: 0.002, velocity: [0, 0.01, 0],
});
const seedMaterial = new THREE.MeshBasicMaterial({color: '#a8fff1', transparent: true, opacity: 0.38});
const seedMesh = new THREE.InstancedMesh(new THREE.SphereGeometry(1, 8, 6), seedMaterial, seeds.capacity);
seedMesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
scene.add(seedMesh);
const seedPose = new THREE.Object3D();

const leftContour = createElement('E07', materials, {dimensions: [3.4, 5.8, 1.15], center: true});
const rightContour = createElement('A16', materials, {dimensions: [3.0, 5.0, 0.6], center: true});
leftContour.rotation.set(0.3, 0.3, -0.3);
rightContour.rotation.set(-0.15, -0.35, 0.35);
scene.add(leftContour, rightContour);
const backplate = new THREE.Mesh(new THREE.PlaneGeometry(1, 1),
  new THREE.MeshBasicMaterial({color: '#b5b5db', transparent: true, opacity: 0.72, depthWrite: false}));
backplate.position.z = -7;
scene.add(backplate);
const receiverMaterial = new THREE.MeshStandardMaterial({color: '#173747', roughness: 0.84,
  metalness: 0.04, transparent: true, opacity: 0.84});
const causticFallback = new THREE.DataTexture(new Uint8Array([0, 0, 0, 255]), 1, 1, THREE.RGBAFormat);
causticFallback.needsUpdate = true;
const receiver = new THREE.Mesh(new THREE.PlaneGeometry(1, 1), receiverMaterial);
receiver.position.z = -5.5;
receiver.receiveShadow = true;
receiver.castShadow = false;
causticBinding = attachCaustics(receiverMaterial, causticFallback, 1.8);
scene.add(receiver);
new THREE.TextureLoader().load('./artwork/background-tidal.png', texture => {
  if (disposed) {texture.dispose(); return;}
  texture.colorSpace = THREE.SRGBColorSpace;
  backplate.material.map = texture;
  backplate.material.needsUpdate = true;
  disposables.push(texture);
  needsRender = true;
  schedule();
});

function resize() {
  if (!renderer) return;
  renderer.setSize(innerWidth || 1, innerHeight || 1, false);
  const height = 2 * 16 * Math.tan(camera.fov * Math.PI / 360);
  const width = height * camera.aspect;
  leftContour.position.set(-width / 2 - 1.4, height * 0.05, -3.4);
  rightContour.position.set(width / 2 + 1.3, -height * 0.2, -3.0);
  sculpture.position.x = width * 0.43;
  sculpture.position.y = height * 0.33;
  backplate.scale.set(Math.max(width * 1.5, height * 2.7), height * 1.5, 1);
  receiver.scale.set(width * 1.55, height * 1.55, 1);
}

function schedule() {
  if (animation || disposed || !renderer || !viewport.active || document.hidden) return;
  animation = requestAnimationFrame(render);
}

function render(now) {
  animation = 0;
  if (disposed || !viewport.active || document.hidden) {previous = 0; return;}
  try {
  const rawDt = previous ? (now - previous) / 1000 : 1 / 60;
  previous = now;
  const dt = frameBudget(rawDt, viewport.reducedMotion, viewport.active);
  elapsed += dt;
  if (dt > 0) {
    world.step(dt);
    transitions.update(dt);
    gelGeometry.attributes.position.needsUpdate = true;
    gelGeometry.computeVertexNormals();
    leftContour.rotation.y = 0.3 + Math.sin(elapsed * 0.16) * 0.025;
  }
  updateMaterials(materials, elapsed);
  for (const entry of controls.values()) {
    entry.controller?.update(dt, elapsed);
    entry.wrapper.position.z = entry.depth + entry.lift.step(dt);
  }
  for (let index = 0; index < seeds.count; index++) {
    const offset = index * 3;
    if (seeds.positions[offset + 1] > 7) seeds.positions[offset + 1] = -7;
    if (seeds.positions[offset] > 6) seeds.positions[offset] = -6;
    seedPose.position.fromArray(seeds.positions, offset);
    seedPose.scale.setScalar(seeds.radii[index]);
    seedPose.updateMatrix();
    seedMesh.setMatrixAt(index, seedPose.matrix);
  }
  seedMesh.instanceMatrix.needsUpdate = true;
  renderFrames++;
  // Give a settled hero a measured progressive budget, with sparse feedback
  // while its geometry moves. Signatures invalidate stale maps at either rate.
  if (caustics && renderFrames % 3 === 0) traceCaustics();
  renderer.render(scene, camera);
  if (!firstFrame) {
    firstFrame = true;
    window.Opaline.status = 'ready';
  }
  needsRender = false;
  if (rawDt > 0.03) slowFrames++; else slowFrames = Math.max(0, slowFrames - 1);
  if (slowFrames > 80 && ratio > 0.85) {
    ratio = Math.max(0.85, ratio - 0.2);
    renderer.setPixelRatio(ratio);
    resize();
    slowFrames = 0;
  }
  if (!viewport.reducedMotion || needsRender || (caustics && caustics.statistics.samples < 4096)) schedule();
  } catch (error) {
    console.error('Opaline frame failed', error);
    window.Opaline.status = 'failed';
    setActive(false);
  }
}

function setActive(active) {
  viewport.active = active === true;
  if (!viewport.active) {
    cancelAnimationFrame(animation);
    animation = 0;
    cancelContacts();
  }
  previous = 0;
  schedule();
}

function dispose() {
  if (disposed) return;
  disposed = true;
  cancelAnimationFrame(animation);
  cancelContacts();
  transitions.dispose();
  [...controls.keys()].forEach(removePart);
  clearCaustics();
  releaseTree(scene);
  causticFallback.dispose();
  [...Object.values(materials), seedMaterial, backplate.material, receiver.material, ...disposables].forEach(item => item.dispose());
  environment?.dispose();
  renderer?.dispose();
  window.removeEventListener('resize', resize);
  document.removeEventListener('visibilitychange', onVisibility);
  window.Opaline.status = 'disposed';
}

function onVisibility() {
  previous = 0;
  if (document.hidden) {
    cancelAnimationFrame(animation);
    animation = 0;
    cancelContacts();
  } else schedule();
}

try {
  renderer = new THREE.WebGLRenderer({antialias: true, alpha: false, powerPreference: 'default'});
  renderer.setPixelRatio(ratio);
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.05;
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  document.body.appendChild(renderer.domElement);
  const pmrem = new THREE.PMREMGenerator(renderer), room = new RoomEnvironment();
  environment = pmrem.fromScene(room, 0.045);
  scene.environment = environment.texture;
  scene.environmentIntensity = 0.8;
  room.dispose();
  pmrem.dispose();
  scene.add(new THREE.HemisphereLight('#e2faff', '#122b3c', 2.3));
  const key = new THREE.DirectionalLight('#f0f8ff', 3.4);
  key.position.set(-5, 7, 9);
  key.castShadow = true;
  key.shadow.mapSize.set(1024, 1024);
  Object.assign(key.shadow.camera, {left: -10, right: 10, top: 10, bottom: -10, near: 0.1, far: 35});
  key.shadow.normalBias = 0.018;
  scene.add(key);
  const rim = new THREE.DirectionalLight('#acbbff', 2.0);
  rim.position.set(6, -2, 3);
  scene.add(rim);
  renderer.domElement.addEventListener('webglcontextlost', event => {
    event.preventDefault();
    setActive(false);
    firstFrame = false;
    window.Opaline.status = 'lost';
  });
  renderer.domElement.addEventListener('webglcontextrestored', () => location.reload());
  window.Opaline = {status: 'loading', update: applySnapshot, touch, pause: () => setActive(false),
    resume: () => setActive(true), dispose,
    stats: () => ({parts: controls.size, geometries: renderer.info.memory.geometries,
      textures: renderer.info.memory.textures, triangles: renderer.info.render.triangles,
      physicsSteps: world.steps, backlog: world.backlogSeconds, reducedMotion: viewport.reducedMotion,
      causticSamples: caustics?.statistics.samples || 0, causticDeposits: caustics?.statistics.deposited || 0,
      causticLastBudgetMs: causticCostMs,
      causticSamplesPerSecond: caustics && elapsed > causticEpoch ? caustics.statistics.samples / (elapsed - causticEpoch) : 0,
      causticRefractor: causticSource?.name || causticSource?.userData?.element || null})};
  window.addEventListener('resize', resize);
  document.addEventListener('visibilitychange', onVisibility);
  applySnapshot(pending || viewport);
} catch (error) {
  console.error('Opaline renderer unavailable', error);
  window.Opaline = {status: 'failed', update() {}, touch() {}, pause() {}, resume() {}, dispose};
  renderer?.domElement.remove();
}
