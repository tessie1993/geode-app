/** Original Opaline numerical building blocks; right-handed metres, +Y up. */
export const EPS = 1e-10;
export const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
export function finiteArray(a, multiple = 1, name = 'array') {
  if (!a || a.length % multiple) throw new TypeError(`${name} length must be a multiple of ${multiple}`);
  for (let i = 0; i < a.length; i++) if (!Number.isFinite(a[i])) throw new TypeError(`${name}[${i}] is not finite`);
}
export function validDt(dt) { if (!(dt > 0) || !Number.isFinite(dt)) throw new RangeError('dt must be finite and positive'); }
export function seededRandom(seed = 1) {
  let state = seed >>> 0;
  return () => { state = (state + 0x6D2B79F5) | 0; let t = state; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}
export class SpatialHash {
  constructor(cellSize) { if (!(cellSize > 0)) throw new RangeError('positive cellSize required'); this.cellSize = cellSize; this.cells = new Map(); }
  key(x, y, z) { return `${x},${y},${z}`; }
  build(positions, count = positions.length / 3) {
    this.cells.clear(); const s = this.cellSize;
    for (let i = 0; i < count; i++) { const k = 3 * i; const key = this.key(Math.floor(positions[k] / s), Math.floor(positions[k+1] / s), Math.floor(positions[k+2] / s)); let cell = this.cells.get(key); if (!cell) this.cells.set(key, cell = []); cell.push(i); }
  }
  query(x, y, z, out = []) {
    out.length = 0; const s = this.cellSize, ix = Math.floor(x/s), iy = Math.floor(y/s), iz = Math.floor(z/s);
    for (let dx=-1; dx<=1; dx++) for (let dy=-1; dy<=1; dy++) for (let dz=-1; dz<=1; dz++) { const cell=this.cells.get(this.key(ix+dx,iy+dy,iz+dz)); if (cell) for (const i of cell) out.push(i); }
    return out;
  }
}
export class PlaneCollider {
  constructor({ normal = [0,1,0], offset = 0, restitution = .08, friction = .3 } = {}) { const len=Math.hypot(...normal); if (!(len > EPS)) throw new RangeError('plane normal must be nonzero'); this.normal=normal.map(v=>v/len); this.offset=offset/len; this.restitution=restitution; this.friction=friction; }
  project(p,k,r=0) { const n=this.normal, d=p[k]*n[0]+p[k+1]*n[1]+p[k+2]*n[2]-this.offset-r; if (d>=0) return null; for(let a=0;a<3;a++) p[k+a]-=d*n[a]; return {normal:n,depth:-d,collider:this}; }
}
export class SphereCollider {
  constructor({ center=[0,0,0], radius=1, inside=false, restitution=.08, friction=.25 }={}) { if (!(radius>0)) throw new RangeError('radius must be positive'); this.center=[...center]; this.radius=radius; this.inside=inside; this.restitution=restitution; this.friction=friction; }
  project(p,k,r=0) { let x=p[k]-this.center[0],y=p[k+1]-this.center[1],z=p[k+2]-this.center[2], d=Math.hypot(x,y,z); if(this.inside&&r>=this.radius)throw new RangeError('particle radius must be smaller than containment sphere');const limit=this.inside ? this.radius-r : this.radius+r; if ((this.inside && d<=limit) || (!this.inside && d>=limit)) return null; if (d<EPS) {x=1;y=0;z=0;d=1;} const nx=x/d,ny=y/d,nz=z/d; p[k]=this.center[0]+nx*limit;p[k+1]=this.center[1]+ny*limit;p[k+2]=this.center[2]+nz*limit; const sign=this.inside?-1:1; return {normal:[nx*sign,ny*sign,nz*sign],depth:Math.abs(d-limit),collider:this}; }
}
export class BoxCollider {
  constructor({min=[-1,0,-1],max=[1,2,1],inside=true,restitution=.03,friction=.2}={}) { for(let a=0;a<3;a++) if(!(max[a]>min[a])) throw new RangeError('box max must exceed min'); this.min=[...min];this.max=[...max];this.inside=inside;this.restitution=restitution;this.friction=friction; }
  project(p,k,r=0) {
    if(this.inside) { const n=[0,0,0]; let depth=0; for(let a=0;a<3;a++){ const lo=this.min[a]+r,hi=this.max[a]-r; if(lo>hi) throw new RangeError('particle diameter exceeds box'); if(p[k+a]<lo){n[a]=lo-p[k+a];depth+=n[a]*n[a];p[k+a]=lo;} else if(p[k+a]>hi){n[a]=hi-p[k+a];depth+=n[a]*n[a];p[k+a]=hi;} } if(!depth)return null;depth=Math.sqrt(depth);return {normal:n.map(v=>v/depth),depth,collider:this}; }
    const closest=[0,0,0];let d2=0;for(let a=0;a<3;a++){closest[a]=clamp(p[k+a],this.min[a],this.max[a]);d2+=(p[k+a]-closest[a])**2;}
    if(d2>EPS){if(d2>=r*r)return null;const d=Math.sqrt(d2),n=closest.map((v,a)=>(p[k+a]-v)/d);for(let a=0;a<3;a++)p[k+a]=closest[a]+n[a]*r;return {normal:n,depth:r-d,collider:this};}
    let axis=0,sign=-1,best=Infinity;for(let a=0;a<3;a++){const dl=p[k+a]-this.min[a],dh=this.max[a]-p[k+a];if(dl<best){best=dl;axis=a;sign=-1;}if(dh<best){best=dh;axis=a;sign=1;}}
    const n=[0,0,0];n[axis]=sign;p[k+axis]=(sign<0?this.min[axis]-r:this.max[axis]+r);return {normal:n,depth:best+r,collider:this};
  }
}
export function resolveContactVelocity(v,k,previous,contact) {
  const n=contact.normal, c=contact.collider;let vn=0,old=0;for(let a=0;a<3;a++){vn+=v[k+a]*n[a];old+=previous[k+a]*n[a];}
  const target=old<-.2?-c.restitution*old:0;const impulse=Math.max(0,target-vn);for(let a=0;a<3;a++)v[k+a]+=impulse*n[a];
  let tang=0;const newVn=vn+impulse;for(let a=0;a<3;a++)tang+=(v[k+a]-newVn*n[a])**2;tang=Math.sqrt(tang);
  const friction=Math.min(1,c.friction*Math.max(impulse,-old)/Math.max(tang,EPS));for(let a=0;a<3;a++)v[k+a]-=(v[k+a]-newVn*n[a])*friction;
}
/** Shared position-particle plumbing for deformable solids, rods and sheets. */
export class ConstraintBody {
  constructor({positions,inverseMass=1,gravity=[0,-9.81,0],damping=.8,radius=.005,colliders=[],fields=[],iterations=10}={}) {
    finiteArray(positions,3,'positions'); this.positions=new Float32Array(positions);this.count=positions.length/3;this.velocities=new Float32Array(positions.length);this.previous=new Float32Array(positions);this.beforeVelocity=new Float32Array(positions.length);
    this.inverseMass=typeof inverseMass==='number'?new Float32Array(this.count).fill(inverseMass):new Float32Array(inverseMass);if(this.inverseMass.length!==this.count || this.inverseMass.some(v=>!Number.isFinite(v)||v<0))throw new RangeError('invalid inverse masses');
    this.gravity=[...gravity];this.damping=damping;this.radius=radius;this.colliders=[...colliders];this.fields=[...fields];this.iterations=iterations;this.pins=new Map();this.grabs=new Map();this.contacts=new Map();this.time=0;this._force=[0,0,0];
  }
  pin(index,target=this.positions.subarray(index*3,index*3+3)) {this.checkIndex(index);finiteArray(target,3,'pin target');const saved=this.pins.get(index)?.inverseMass??this.inverseMass[index];this.pins.set(index,{target:[...target],inverseMass:saved});this.inverseMass[index]=0;this._applyPins();return this;}
  unpin(index) {const pin=this.pins.get(index);if(pin){this.inverseMass[index]=pin.inverseMass;this.pins.delete(index);}return this;}
  checkIndex(i){if(!Number.isInteger(i)||i<0||i>=this.count)throw new RangeError('particle index out of range');}
  grab(id,indices,target,compliance=1e-6) {finiteArray(target,3,'grab target');if(!(compliance>=0))throw new RangeError('grab compliance must be nonnegative');if(!indices.length)throw new RangeError('grab needs vertices');const center=[0,0,0];for(const i of indices){this.checkIndex(i);for(let a=0;a<3;a++)center[a]+=this.positions[3*i+a]/indices.length;} const offsets=new Float32Array(indices.length*3);for(let j=0;j<indices.length;j++)for(let a=0;a<3;a++)offsets[j*3+a]=this.positions[indices[j]*3+a]-center[a];this.grabs.set(id,{indices:[...indices],target:[...target],offsets,compliance,lambda:new Float32Array(indices.length*3)});return this;}
  moveGrab(id,target){finiteArray(target,3,'grab target');const g=this.grabs.get(id);if(g)g.target=[...target];return this;}
  releaseGrab(id){this.grabs.delete(id);return this;}
  cancelInteractions(){this.grabs.clear();return this;}
  impulse(index,impulse){this.checkIndex(index);for(let a=0;a<3;a++)this.velocities[3*index+a]+=impulse[a]*this.inverseMass[index];}
  _applyPins(){for(const [i,pin] of this.pins)for(let a=0;a<3;a++){this.positions[3*i+a]=pin.target[a];this.velocities[3*i+a]=0;}}
  _begin(dt){validDt(dt);this.previous.set(this.positions);this.contacts.clear();for(const g of this.grabs.values())g.lambda.fill(0);const atten=Math.exp(-this.damping*dt);for(let i=0;i<this.count;i++){if(this.inverseMass[i]===0)continue;const k=3*i;for(let a=0;a<3;a++)this.velocities[k+a]=(this.velocities[k+a]+this.gravity[a]*dt)*atten;for(const field of this.fields){this._force.fill(0);field.sample(this.positions[k],this.positions[k+1],this.positions[k+2],this.time,this._force);for(let a=0;a<3;a++)this.velocities[k+a]+=this._force[a]*dt;}for(let a=0;a<3;a++)this.positions[k+a]+=this.velocities[k+a]*dt;}this.beforeVelocity.set(this.velocities);this._applyPins();}
  _solveGrabs(dt){for(const g of this.grabs.values()){const alpha=g.compliance/(dt*dt);for(let j=0;j<g.indices.length;j++){const i=g.indices[j],w=this.inverseMass[i];if(w===0)continue;for(let a=0;a<3;a++){const s=j*3+a,k=i*3+a,C=this.positions[k]-g.target[a]-g.offsets[s],dl=(-C-alpha*g.lambda[s])/(w+alpha);g.lambda[s]+=dl;this.positions[k]+=w*dl;}}}}
  _collide(){for(let i=0;i<this.count;i++){if(!this.inverseMass[i])continue;for(let c=0;c<this.colliders.length;c++){const hit=this.colliders[c].project(this.positions,3*i,this.radius);if(hit)this.contacts.set(`${i}:${c}`,{i,...hit});}}}
  _finish(dt){for(let i=0;i<this.count;i++)for(let a=0;a<3;a++)this.velocities[3*i+a]=(this.positions[3*i+a]-this.previous[3*i+a])/dt;for(const c of this.contacts.values())resolveContactVelocity(this.velocities,3*c.i,this.beforeVelocity,c);this._applyPins();this.time+=dt;}
}
export function distanceConstraints(positions,pairs){const indices=new Uint32Array(pairs.flat()),rest=new Float32Array(pairs.length);for(let e=0;e<pairs.length;e++){const i=3*pairs[e][0],j=3*pairs[e][1];rest[e]=Math.hypot(positions[i]-positions[j],positions[i+1]-positions[j+1],positions[i+2]-positions[j+2]);}return {indices,rest,lambda:new Float32Array(pairs.length)};}
export function solveDistances(body,constraints,compliance,dt){const p=body.positions,w=body.inverseMass,alpha=compliance/(dt*dt),{indices,rest,lambda}=constraints;for(let e=0;e<rest.length;e++){const ia=indices[2*e],ib=indices[2*e+1],i=3*ia,j=3*ib,wx=w[ia],wy=w[ib];if(wx+wy===0)continue;let x=p[i]-p[j],y=p[i+1]-p[j+1],z=p[i+2]-p[j+2],len=Math.hypot(x,y,z);if(len<EPS)continue;const dl=(-(len-rest[e])-alpha*lambda[e])/(wx+wy+alpha)/len;lambda[e]+=dl*len;const d=[x*dl,y*dl,z*dl];for(let a=0;a<3;a++){p[i+a]+=wx*d[a];p[j+a]-=wy*d[a];}}}
