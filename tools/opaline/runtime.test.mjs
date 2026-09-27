import test from 'node:test';
import assert from 'node:assert/strict';
import {register} from 'node:module';
import {readFile} from 'node:fs/promises';
import {sanitizeSnapshot, projectBounds, frameBudget} from '../../app/src/main/assets/opaline/protocol.js';
register('./three-loader.mjs', import.meta.url);
const THREE = await import('three');
const {createElement} = await import('../../app/src/main/assets/opaline/src/geometry.js');
const {createMaterials} = await import('../../app/src/main/assets/opaline/src/materials.js');
const {MotionController} = await import('../../app/src/main/assets/opaline/src/motion.js');
const {SoftBody, makeTetGrid} = await import('../../app/src/main/assets/opaline/src/physics/soft-body.js');
const {PhysicsWorld} = await import('../../app/src/main/assets/opaline/src/physics/world.js');
const {RefractiveCaustics, attachCaustics} = await import('../../app/src/main/assets/opaline/src/optics.js');

const materials = createMaterials('tidal');
test('native coordinates retain alignment across density and aspect ratios', () => {
  for (const [width, height] of [[390, 844], [1080, 2400], [2560, 1600]]) {
    const part = {x: width * 0.1, y: height * 0.2, width: width * 0.8, height: height * 0.1};
    const box = projectBounds(part, {width, height});
    const camera = new THREE.PerspectiveCamera(38, width / height, 0.1, 70);
    camera.position.set(0, 0, 16);
    camera.updateMatrixWorld();
    const projected = new THREE.Vector3(box.x, box.y, 0).project(camera);
    assert.ok(Math.abs((projected.x + 1) / 2 * width - width / 2) < 0.001);
    assert.ok(Math.abs((1 - projected.y) / 2 * height - height * 0.25) < 0.001);
  }
});

test('bridge rejects nonfinite bounds and caps dynamic work', () => {
  const part = {id: 'test', element: 'A01', x: 1, y: 1, width: 40, height: 30, value: 4};
  const result = sanitizeSnapshot({width: 100, height: 100, parts: [part,
    {...part, id: 'bad', width: NaN}, {...part, id: 'offscreen', y: 200},
    {...part, id: 'fake', element: '<script>'}, ...Array.from({length: 130}, (_, i) => ({...part, id: String(i)}))]});
  assert.equal(result.parts.length, 96);
  assert.equal(result.parts[0].value, 1);
  assert.ok(result.parts.every(item => item.id !== 'bad' && item.id !== 'offscreen' && item.id !== 'fake'));
});

test('all UI adapter elements produce finite dimensional geometry', () => {
  for (const id of ['A01', 'A03', 'A05', 'A22', 'B01', 'B03', 'B07', 'B09', 'B13', 'B21', 'C01', 'C03', 'C04', 'C20', 'D03']) {
    const element = createElement(id, materials, {dimensions: [2, 1, 0.2], center: true});
    const size = new THREE.Box3().setFromObject(element).getSize(new THREE.Vector3());
    assert.ok(Math.abs(size.x - 2) < 0.001 && Math.abs(size.y - 1) < 0.001 && Math.abs(size.z - 0.2) < 0.001, id);
    element.traverse(mesh => {
      if (mesh.geometry) {
        assert.ok(mesh.geometry.attributes.position.array.every(Number.isFinite), id);
        mesh.geometry.dispose();
      }
    });
  }
});

test('off-centre native press deforms live vertices and cancellation settles', () => {
  const root = createElement('A01', materials, {center: true});
  const controller = new MotionController(root, {id: 'A01'});
  const body = controller.deform[0];
  assert.ok(body);
  root.updateMatrixWorld(true);
  controller.begin(root.localToWorld(new THREE.Vector3(0.4, 0.1, 0.3)));
  for (let i = 0; i < 40; i++) controller.update(1 / 120, i / 120);
  const displacement = Math.max(...body.geometry.attributes.position.array.map((n, i) => Math.abs(n - body.rest[i])));
  assert.ok(displacement > 0.03);
  controller.release();
  for (let i = 0; i < 300; i++) controller.update(1 / 120, i / 120);
  assert.equal(controller.active, false);
  assert.ok(Math.max(...body.geometry.attributes.position.array.map((n, i) => Math.abs(n - body.rest[i]))) < 0.002);
  controller.dispose();
});

test('native slider value updates before visual settling', () => {
  const root = createElement('B07', materials);
  const controller = new MotionController(root, {id: 'B07'});
  controller.value.target = 0.91;
  assert.equal(controller.value.target, 0.91);
  assert.notEqual(controller.value.value, 0.91);
  for (let i = 0; i < 240; i++) controller.update(1 / 120, i / 120);
  assert.ok(Math.abs(controller.value.value - 0.91) < 0.001);
  assert.ok(controller.parts.some(part => part.mesh.morphTargetInfluences?.some(value => value > 0)));
  controller.dispose();
});

test('bounded XPBD sculpture preserves volume and pause prevents simulation', () => {
  const body = new SoftBody({...makeTetGrid({nx: 4, ny: 3, nz: 4, size: [1.35, 1, 0.8], shape: 'ellipsoid'}),
    gravity: [0, 0, 0], damping: 2.5, iterations: 5});
  for (let i = 0; i < body.count; i++) if (body.positions[i * 3 + 2] < -0.25) body.pin(i);
  const world = new PhysicsWorld({maxSubsteps: 4, maxFrameDt: 1 / 30});
  world.add(body);
  body.addImpulse([0, 0, 0.35], 0.7, [0.001, 0.001, -0.006]);
  for (let i = 0; i < 240; i++) world.step(frameBudget(1 / 60, false, true));
  assert.ok(body.positions.every(Number.isFinite));
  assert.ok(Math.abs(body.volume / body.restVolume - 1) < 0.015);
  const steps = world.steps;
  world.step(frameBudget(2, true, true));
  world.step(frameBudget(2, false, false));
  assert.equal(world.steps, steps);
  assert.equal(frameBudget(10, false, true), 1 / 30);
});

test('A22 geometry changes produce a different UV receiver caustic map', () => {
  const root = createElement('A22', materials, {dimensions: [2, 2, 0.7], center: true});
  let refractor;
  root.traverse(object => { if (!refractor && object.isMesh) refractor = object; });
  assert.ok(refractor?.geometry.attributes.uv);
  const positions = refractor.geometry.attributes.position;
  const indices = refractor.geometry.index;
  const edgeCounts = new Map();
  let signedVolume = 0;
  for (let offset = 0; offset < (indices?.count || positions.count); offset += 3) {
    const ids = [0, 1, 2].map(i => indices ? indices.getX(offset + i) : offset + i);
    const vertices = ids.map(index => new THREE.Vector3().fromBufferAttribute(positions, index));
    signedVolume += vertices[0].dot(new THREE.Vector3().crossVectors(vertices[1], vertices[2])) / 6;
    for (let edge = 0; edge < 3; edge++) {
      const keyFor = vector => vector.toArray().map(value => value.toFixed(5)).join(',');
      const a = keyFor(vertices[edge]), b = keyFor(vertices[(edge + 1) % 3]);
      const key = a < b ? `${a}|${b}` : `${b}|${a}`;
      edgeCounts.set(key, (edgeCounts.get(key) || 0) + 1);
    }
  }
  assert.ok([...edgeCounts.values()].every(count => count === 2), 'A22 must be a closed two-manifold');
  assert.ok(signedVolume > 0, 'A22 triangle winding must point outward');
  const receiver = new THREE.Mesh(new THREE.PlaneGeometry(8, 8), new THREE.MeshStandardMaterial());
  receiver.position.z = -3;
  const binding = attachCaustics(receiver.material, new THREE.DataTexture(new Uint8Array([0, 0, 0, 255]), 1, 1));
  const photons = new RefractiveCaustics({refractors: [refractor], receiver,
    lightPosition: new THREE.Vector3(0, 0, 5), lightTarget: new THREE.Vector3(0, 0, -3),
    aperture: new THREE.Vector2(2, 2), resolution: 64, maxBounces: 3, maxBranches: 4});
  binding.uOpCaustics.value = photons.texture;
  photons.update(0, {photons: 128});
  const rest = new Float32Array(photons.texture.image.data);
  const firstRevision = photons.statistics.revision;
  assert.ok(photons.statistics.deposited > 0);
  refractor.rotation.y = 0.32;
  photons.update(1, {photons: 128});
  assert.equal(photons.statistics.revision, firstRevision + 1);
  assert.ok(photons.statistics.deposited > 0);
  assert.ok(photons.texture.image.data.some((value, index) => value !== rest[index]),
    'rotating the registered refractor must change receiver irradiance');
  photons.dispose();
  receiver.geometry.dispose();
  receiver.material.dispose();
  root.traverse(object => object.geometry?.dispose());
});

test('runtime connects visible hero geometry to one bounded, lifecycle-owned receiver', async () => {
  const runtime = await readFile(new URL('../../app/src/main/assets/opaline/runtime.js', import.meta.url), 'utf8');
  assert.match(runtime, /const priority = \{A22: 4, B07: 3, C01: 2, C03: 2\}/);
  assert.match(runtime, /object\.material\?\.transmission > 0\.3/);
  assert.match(runtime, /caustics\.lightTarget\.copy\(causticSource\.getWorldPosition/);
  assert.match(runtime, /new THREE\.PlaneGeometry\(1, 1\)/);
  assert.match(runtime, /attachCaustics\(receiverMaterial/);
  assert.match(runtime, /renderFrames % 3 === 0\) traceCaustics\(\)/);
  assert.match(runtime, /performance\.now\(\) - started < targetMs/);
  assert.match(runtime, /batches < maxBatches/);
  assert.match(runtime, /slowFrames > 8 \? 1 : moving \? 2 : 4/);
  assert.match(runtime, /caustics\.statistics\.samples < 4096/);
  assert.match(runtime, /caustics\.statistics\.samples \/ \(elapsed - causticEpoch\)/);
  assert.match(runtime, /clearCaustics\(\)/);
});

test('offline entry point preserves explicit renderer failure status', async () => {
  const html = await readFile(new URL('../../app/src/main/assets/opaline/index.html', import.meta.url), 'utf8');
  const runtime = await readFile(new URL('../../app/src/main/assets/opaline/runtime.js', import.meta.url), 'utf8');
  assert.ok(html.includes('Content-Security-Policy'));
  assert.ok(runtime.includes("status: 'failed'"));
  assert.ok(runtime.includes("'webglcontextlost'"));
  assert.match(runtime, /renderer\.render\(scene, camera\);\s*if \(!firstFrame\) \{\s*firstFrame = true;\s*window\.Opaline\.status = 'ready'/);
});
