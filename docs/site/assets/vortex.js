// The logo's iris as a place: nested hexagons, each a little more twisted, receding into a
// tunnel the camera drifts down for ever, with an amber hexagon at the far end. It mounts on
// every <canvas data-vortex>. Motion is slow and brightness never jumps (no flashes); with
// prefers-reduced-motion it draws one still frame; it sleeps while off screen or in a hidden
// tab; without WebGL the canvas stays hidden and the page's CSS eye is what shows.
import * as THREE from './vendor/three.module.min.js';

const BRAND = {
  cyan: new THREE.Color('#89DDFF'),
  blue: new THREE.Color('#82AAFF'),
  violet: new THREE.Color('#7C4DFF'),
  lilac: new THREE.Color('#C792EA'),
  amber: new THREE.Color('#FFCB6B'),
};
const RINGS = 40;
const GAP = 0.85;
const TWIST = 0.16;
const RADIUS = 2.1;
const SPEED = 0.55;

const still = matchMedia('(prefers-reduced-motion: reduce)').matches;

function hexRing(outer, inner) {
  const shape = new THREE.Shape();
  const hole = new THREE.Path();
  for (let i = 0; i <= 6; i++) {
    const a = (i / 6) * Math.PI * 2 + Math.PI / 6;
    const [x, y, hx, hy] = [Math.cos(a) * outer, Math.sin(a) * outer, Math.cos(a) * inner, Math.sin(a) * inner];
    if (i === 0) { shape.moveTo(x, y); hole.moveTo(hx, hy); } else { shape.lineTo(x, y); hole.lineTo(hx, hy); }
  }
  shape.holes.push(hole);
  return new THREE.ShapeGeometry(shape);
}

function glowTexture(inner, outer) {
  const c = document.createElement('canvas');
  c.width = c.height = 128;
  const g = c.getContext('2d');
  const grad = g.createRadialGradient(64, 64, 0, 64, 64, 64);
  grad.addColorStop(0, inner);
  grad.addColorStop(0.35, outer);
  grad.addColorStop(1, 'rgba(0,0,0,0)');
  g.fillStyle = grad;
  g.fillRect(0, 0, 128, 128);
  const t = new THREE.CanvasTexture(c);
  t.colorSpace = THREE.SRGBColorSpace;
  return t;
}

// Cyan at the rim, through blue and violet, to lilac in the depths: the logo's vortex.
function depthColour(f) {
  const stops = [BRAND.cyan, BRAND.blue, BRAND.violet, BRAND.lilac];
  const x = Math.min(0.999, Math.max(0, f)) * (stops.length - 1);
  const i = Math.floor(x);
  return stops[i].clone().lerp(stops[i + 1], x - i);
}

function smooth(a, b, x) {
  const t = Math.min(1, Math.max(0, (x - a) / (b - a)));
  return t * t * (3 - 2 * t);
}

function mount(canvas) {
  let renderer;
  try {
    renderer = new THREE.WebGLRenderer({ canvas, alpha: true, antialias: true, powerPreference: 'low-power' });
  } catch {
    return;
  }
  renderer.setPixelRatio(Math.min(devicePixelRatio || 1, 1.75));
  renderer.setClearColor(0x000000, 0);
  canvas.classList.add('is-live');

  const scene = new THREE.Scene();
  const camera = new THREE.PerspectiveCamera(58, 1, 0.1, 80);
  camera.position.set(0, 0, 4);

  const tunnel = new THREE.Group();
  scene.add(tunnel);
  const core = hexRing(RADIUS, RADIUS * 0.965);
  const halo = hexRing(RADIUS * 1.05, RADIUS * 0.9);
  const rings = [];
  for (let i = 0; i < RINGS; i++) {
    const coreMat = new THREE.MeshBasicMaterial({ transparent: true, blending: THREE.AdditiveBlending, depthWrite: false });
    const haloMat = coreMat.clone();
    const ring = new THREE.Group();
    ring.add(new THREE.Mesh(core, coreMat), new THREE.Mesh(halo, haloMat));
    ring.userData = { coreMat, haloMat, phase: i };
    ring.position.z = -i * GAP;
    tunnel.add(ring);
    rings.push(ring);
  }
  const span = RINGS * GAP;

  // The pupil: an amber hexagon far down the tunnel, with a soft glow.
  const pupil = new THREE.Group();
  const pupilHex = new THREE.Mesh(hexRing(0.55, 0.42), new THREE.MeshBasicMaterial({ color: BRAND.amber, transparent: true, opacity: 0.9, blending: THREE.AdditiveBlending, depthWrite: false }));
  const pupilGlow = new THREE.Sprite(new THREE.SpriteMaterial({ map: glowTexture('rgba(255,236,200,0.9)', 'rgba(255,203,107,0.35)'), blending: THREE.AdditiveBlending, depthWrite: false, transparent: true }));
  pupilGlow.scale.set(3.2, 3.2, 1);
  pupil.add(pupilGlow, pupilHex);
  pupil.position.z = -span * 0.82;
  scene.add(pupil);

  // Dust drifting past, as in the splash clip.
  const DUST = 700;
  const pos = new Float32Array(DUST * 3);
  for (let i = 0; i < DUST; i++) {
    const a = Math.random() * Math.PI * 2;
    const r = RADIUS * (0.3 + Math.random() * 1.4);
    pos.set([Math.cos(a) * r, Math.sin(a) * r, 4 - Math.random() * span], i * 3);
  }
  const dustGeo = new THREE.BufferGeometry();
  dustGeo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  const dust = new THREE.Points(dustGeo, new THREE.PointsMaterial({
    size: 0.06, map: glowTexture('rgba(255,255,255,1)', 'rgba(199,146,234,0.5)'), color: BRAND.lilac,
    transparent: true, opacity: 0.7, blending: THREE.AdditiveBlending, depthWrite: false,
  }));
  scene.add(dust);

  const pointer = { x: 0, y: 0 };
  if (!still && matchMedia('(pointer: fine)').matches) {
    addEventListener('pointermove', (e) => {
      pointer.x = (e.clientX / innerWidth - 0.5) * 2;
      pointer.y = (e.clientY / innerHeight - 0.5) * 2;
    }, { passive: true });
  }

  function resize() {
    const w = canvas.clientWidth || 1;
    const h = canvas.clientHeight || 1;
    renderer.setSize(w, h, false);
    camera.aspect = w / h;
    camera.updateProjectionMatrix();
  }
  new ResizeObserver(resize).observe(canvas);
  resize();

  function frame(t, dt) {
    const travel = SPEED * dt;
    for (const ring of rings) {
      ring.position.z += travel;
      if (ring.position.z > camera.position.z - 0.3) {
        ring.position.z -= span;
        ring.userData.phase += RINGS;
      }
      const depth = (camera.position.z - ring.position.z) / span;
      const colour = depthColour(depth);
      const fade = smooth(0.04, 0.3, depth) * (1 - smooth(0.7, 1, depth));
      ring.userData.coreMat.color.copy(colour);
      ring.userData.haloMat.color.copy(colour);
      ring.userData.coreMat.opacity = 0.85 * fade;
      ring.userData.haloMat.opacity = 0.12 * fade;
      // Each ring sits a little more twisted than the one before; a slow wave runs down the tunnel.
      ring.rotation.z = ring.userData.phase * TWIST + t * 0.05;
      const s = 1 + 0.035 * Math.sin(t * 0.8 - ring.userData.phase * 0.35);
      ring.scale.set(s, s, 1);
    }
    const p = dustGeo.attributes.position;
    for (let i = 0; i < DUST; i++) {
      let z = p.getZ(i) + travel * 1.4;
      if (z > camera.position.z) z -= span;
      p.setZ(i, z);
    }
    p.needsUpdate = true;
    pupil.rotation.z = -t * 0.12;
    pupilGlow.material.opacity = 0.75 + 0.1 * Math.sin(t * 0.6);
    camera.position.x += (pointer.x * 0.5 - camera.position.x) * 0.04;
    camera.position.y += (-pointer.y * 0.4 - camera.position.y) * 0.04;
    camera.lookAt(0, 0, -span * 0.5);
    renderer.render(scene, camera);
  }

  if (still) {
    frame(4, 0);
    return;
  }

  let running = false;
  let visible = true;
  let last = 0;
  let clock = 0;
  function loop(now) {
    if (!running) return;
    const dt = Math.min(0.05, (now - last) / 1000 || 0);
    last = now;
    clock += dt;
    frame(clock, dt);
    requestAnimationFrame(loop);
  }
  function update() {
    const should = visible && !document.hidden;
    if (should && !running) {
      running = true;
      last = performance.now();
      requestAnimationFrame(loop);
    } else if (!should) {
      running = false;
    }
  }
  new IntersectionObserver(([e]) => { visible = e.isIntersecting; update(); }).observe(canvas);
  document.addEventListener('visibilitychange', update);
  update();
}

document.querySelectorAll('canvas[data-vortex]').forEach(mount);
