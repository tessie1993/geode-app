/** Contact recipes author input constraints; they do not replace the solver. */
const top = { region: [0, .5, 0], radius: .35, delta: [0, -.16, 0] };
export const CONTACT_RECIPES = {
  K01: { name: 'Local gel press', operation: 'press', contacts: [top] },
  K02: { name: 'Sustained hold', operation: 'hold', duration: 5, hold: 3.4, contacts: [top], limitation: 'Elastic held compression; no calibrated viscoelastic creep constitutive model.' },
  K03: { name: 'Off-centre shear', operation: 'shear', contacts: [{ region: [.28, .5, 0], radius: .26, delta: [.16, -.08, .08] }] },
  K04: { name: 'Pinch compression', operation: 'pinch', contacts: [{ region: [-.5, 0, 0], radius: .38, delta: [.17, 0, 0] }, { region: [.5, 0, 0], radius: .38, delta: [-.17, 0, 0] }] },
  K05: { name: 'Elastic pull', operation: 'pull', contacts: [{ region: [0, .5, 0], radius: .28, delta: [.05, .26, 0] }] },
  K06: { name: 'Twist contact', operation: 'twist', twistAngle: .6, contacts: [{ region: [-.32, .42, 0], radius: .25, delta: [0, 0, .18] }, { region: [.32, .42, 0], radius: .25, delta: [0, 0, -.18] }] },
  K07: { name: 'Drag with inertia', operation: 'drag', retainReleaseVelocity: true, cancelAt: .32, contacts: [{ region: [0, .5, 0], radius: .32, delta: [.33, .12, .12] }] },
  K08: { name: 'Surface stroke', operation: 'stroke', contacts: [{ region: [-.32, .5, 0], radius: .26, delta: [.55, -.08, 0] }], limitation: 'Solid surface shear; liquid wake requires coupled fluid domain.' },
  K09: { name: 'Immersion press', operation: 'immersion', contacts: [{ region: [0, .5, 0], radius: .65, delta: [0, -.3, 0] }], impulse: [0, -.025, 0], limitation: 'Attach PBF plus bilateral coupling for liquid displacement; no capillary contact-angle model.' },
  K10: { name: 'Gel-to-gel collision', operation: 'collision', impulse: [.06, 0, 0], contacts: [], restitution: .12, limitation: 'Needs two SoftBody systems and ParticleCoupling.' },
  K11: { name: 'Gel-to-rigid collision', operation: 'rigidCollision', contacts: [top], restitution: .08 },
  K12: { name: 'Particle-to-gel collision', operation: 'particleImpact', contacts: [], impulse: [0, -.025, 0], limitation: 'Needs emitter and bilateral coupling.' },
  K13: { name: 'Liquid-to-solid impact', operation: 'fluidImpact', contacts: [], impulse: [0, -.08, 0], limitation: 'Use PBF domain with collider; restitution/friction boundaries, no wetting model.' },
  K14: { name: 'Bubble-to-bubble contact', operation: 'filmContact', contacts: [], limitation: 'Explicit shared-film topology is not provided by these solvers.' },
  K15: { name: 'Frictional slide', operation: 'slide', friction: .75, contacts: [{ region: [0, .5, 0], radius: .42, delta: [.28, -.11, 0] }] },
  K16: { name: 'Restitution response', operation: 'bounce', restitution: .75, contacts: [], impulse: [0, -.08, 0] },
  K17: { name: 'Adhesion and release', operation: 'adhesion', contacts: [top], limitation: 'ParticleSystem supports threshold adhesion to static colliders; gel adhesive surface energy is not modelled.' },
  K18: { name: 'Wetting and dewetting', operation: 'wetting', contacts: [], limitation: 'Requires a capillary/free-surface contact model; not implemented by this contact recipe.' },
  K19: { name: 'Buoyant bob and roll', operation: 'buoyancy', contacts: [], impulse: [.005, -.02, .003], limitation: 'BuoyancyField is a submerged-fraction particle model; no displaced rigid-volume hydrostatics.' },
  K20: { name: 'Constrained hover', operation: 'hover', contacts: [{ region: [0, 0, 0], radius: 1, delta: [.12, .16, .08] }] },
  K21: { name: 'Dock and undock', operation: 'dock', contacts: [{ region: [0, 0, 0], radius: 1, delta: [0, .28, .1] }] },
  K22: { name: 'Neighbour impulse transfer', operation: 'neighbor', contacts: [], impulse: [.06, 0, 0], limitation: 'Needs bilateral coupling or shared attachment constraints.' },
  K23: { name: 'Interruption recovery', operation: 'cancel', cancelAt: .65, contacts: [{ region: [.18, .5, 0], radius: .3, delta: [.13, -.14, 0] }] },
  K24: { name: 'Multi-contact arbitration', operation: 'multi', contacts: [{ region: [-.3, .5, 0], radius: .27, delta: [-.09, -.17, .08] }, { region: [.3, .5, 0], radius: .27, delta: [.12, .13, -.09] }] }
};
export function createContactRecipe(id, overrides = {}) {
  const recipe = CONTACT_RECIPES[id];
  if (!recipe) throw new RangeError(`Unknown contact recipe ${id}`);
  return structuredClone({ id, duration: 2.8, hold: .8, compliance: 2e-6, friction: .3, restitution: .08, ...recipe, ...overrides });
}
/** Optional automatic specimen demonstration. No callback/action timing is delayed by this. */
export class ContactController {
  constructor(body, id, { origin, size, loop = false, ...overrides } = {}) {
    this.body = body;
    this.recipe = createContactRecipe(id, overrides);
    this.loop = loop;
    this.elapsed = 0;
    this.active = false;
    this.finished = false;
    const min = [Infinity, Infinity, Infinity], max = [-Infinity, -Infinity, -Infinity];
    for (let i = 0; i < body.count; i++) for (let a = 0; a < 3; a++) {
      min[a] = Math.min(min[a], body.positions[3 * i + a]);
      max[a] = Math.max(max[a], body.positions[3 * i + a]);
    }
    this.origin = origin ?? min.map((v, a) => (v + max[a]) / 2);
    this.size = size ?? min.map((v, a) => Math.max(.01, max[a] - v));
    this.contacts = this.recipe.contacts.map((contact, index) => {
      const center = this.origin.map((v, a) => v + contact.region[a] * this.size[a]);
      const indices = [];
      let nearest = 0, nearestDistance = Infinity;
      for (let i = 0; i < body.count; i++) {
        let distance = 0;
        for (let a = 0; a < 3; a++) distance += ((body.positions[i * 3 + a] - center[a]) / this.size[a]) ** 2;
        if (distance < nearestDistance) { nearest = i; nearestDistance = distance; }
        if (distance < contact.radius ** 2 && body.inverseMass[i] > 0) indices.push(i);
      }
      if (!indices.length) indices.push(nearest);
      const start = [0, 0, 0];
      for (const i of indices) for (let a = 0; a < 3; a++) start[a] += body.positions[3 * i + a] / indices.length;
      return { ...contact, indices, start, id: `recipe:${id}:${index}` };
    });
  }
  update(dt) {
    if (this.finished) return;
    this.elapsed += dt;
    const recipe = this.recipe;
    if (!this.active) {
      this.active = true;
      for (const c of this.contacts) this.body.grab(c.id, c.indices, c.start, recipe.compliance);
      if (recipe.impulse && this.body.addImpulse) this.body.addImpulse(this.origin, Math.max(...this.size), recipe.impulse);
    }
    const attack = .5, release = attack + recipe.hold;
    const x = Math.max(0, Math.min(1, this.elapsed / attack));
    const amount = x * x * (3 - 2 * x);
    for (const c of this.contacts) {
      this.body.moveGrab(c.id, c.start.map((v, a) => v + c.delta[a] * this.size[a] * amount));
    }
    if (this.elapsed >= (recipe.cancelAt ?? release)) this.cancel(false);
    if (this.elapsed >= recipe.duration) {
      if (this.loop) { this.elapsed = 0; this.active = false; }
      else this.finished = true;
    }
  }
  cancel(finish = true) {
    for (const c of this.contacts) this.body.releaseGrab(c.id);
    if (finish) this.finished = true;
  }
}
