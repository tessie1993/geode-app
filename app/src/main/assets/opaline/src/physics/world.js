import {SpatialHash,EPS} from './common.js';
/** Fixed-rate numerical clock; overload is reported rather than hidden. */
export class PhysicsWorld {
  constructor({fixedDt=1/120,maxSubsteps=64,maxFrameDt=.5}={}){if(!(fixedDt>0&&maxSubsteps>=1&&maxFrameDt>0))throw new RangeError('invalid world timing');this.fixedDt=fixedDt;this.maxSubsteps=maxSubsteps;this.maxFrameDt=maxFrameDt;this.systems=[];this.couplings=[];this.accumulator=0;this.time=0;this.steps=0;this.droppedTime=0;this.paused=false;}
  add(system){if(typeof system.step!=='function')throw new TypeError('system.step(dt) required');this.systems.push(system);return system;}
  remove(system){const i=this.systems.indexOf(system);if(i>=0)this.systems.splice(i,1);this.couplings=this.couplings.filter(c=>c.a!==system&&c.b!==system);}
  couple(a,b,options={}){const c=new ParticleCoupling(a,b,options);this.couplings.push(c);return c;}
  advance(steps=1){for(let i=0;i<steps;i++){for(const s of this.systems)s.step(this.fixedDt);for(const c of this.couplings)c.step(this.fixedDt);this.time+=this.fixedDt;this.steps++;}return this;}
  step(elapsed){if(!Number.isFinite(elapsed)||elapsed<0)throw new RangeError('elapsed must be finite and nonnegative');if(this.paused)return 0;const accepted=Math.min(elapsed,this.maxFrameDt);this.droppedTime+=elapsed-accepted;this.accumulator+=accepted;let count=0;while(this.accumulator+1e-12>=this.fixedDt&&count<this.maxSubsteps){this.advance();this.accumulator-=this.fixedDt;count++;}if(Math.abs(this.accumulator)<1e-12)this.accumulator=0;return count;}
  get alpha(){return Math.min(1,this.accumulator/this.fixedDt);}
  get backlogSeconds(){return this.accumulator;}
  cancelInteractions(){for(const s of this.systems)s.cancelInteractions?.();}
}
/** Bilateral particle contact: mass-weighted positional correction + momentum impulses.
 * This is staggered coupling, not a monolithic fluid/FEM pressure solve. */
export class ParticleCoupling {
  constructor(a,b,{radiusA,radiusB,restitution=.15,friction=.25,onContact=null}={}){this.a=a;this.b=b;this.radiusA=radiusA??a.particleRadius??a.radius??.015;this.radiusB=radiusB??b.particleRadius??b.radius??.015;this.restitution=restitution;this.friction=friction;this.onContact=onContact;this.contactCount=0;this._neighbors=[];}
  step(dt){const a=this.a,b=this.b,pa=a.positions,pb=b.positions,va=a.velocities,vb=b.velocities;let maxA=this.radiusA,maxB=this.radiusB;if(a.radii)for(let i=0;i<a.count;i++)maxA=Math.max(maxA,a.radii[i]);if(b.radii)for(let i=0;i<b.count;i++)maxB=Math.max(maxB,b.radii[i]);const hash=new SpatialHash(maxA+maxB);hash.build(pb,b.count);this.contactCount=0;for(let i=0;i<a.count;i++){const k=3*i;hash.query(pa[k],pa[k+1],pa[k+2],this._neighbors);for(const j of this._neighbors){const l=3*j,ra=a.radii?.[i]??this.radiusA,rb=b.radii?.[j]??this.radiusB;let x=pa[k]-pb[l],y=pa[k+1]-pb[l+1],z=pa[k+2]-pb[l+2],d=Math.hypot(x,y,z);if(d>=ra+rb)continue;let n;if(d<EPS){n=[0,1,0];d=0;}else n=[x/d,y/d,z/d];const wa=a.inverseMass?.[i]??1/a.particleMass,wb=b.inverseMass?.[j]??1/b.particleMass,sum=wa+wb;if(sum===0)continue;const penetration=ra+rb-d;for(let q=0;q<3;q++){pa[k+q]+=n[q]*penetration*wa/sum;pb[l+q]-=n[q]*penetration*wb/sum;}const rel=[va[k]-vb[l],va[k+1]-vb[l+1],va[k+2]-vb[l+2]],vn=rel[0]*n[0]+rel[1]*n[1]+rel[2]*n[2],impulse=Math.max(0,-(1+this.restitution)*vn/sum);const tang=rel.map((v,q)=>v-vn*n[q]),len=Math.hypot(...tang),fric=Math.min(this.friction*impulse,len/sum);for(let q=0;q<3;q++){const dv=n[q]*impulse-(len>EPS?tang[q]/len*fric:0);va[k+q]+=dv*wa;vb[l+q]-=dv*wb;}this.contactCount++;this.onContact?.({position:[(pa[k]+pb[l])/2,(pa[k+1]+pb[l+1])/2,(pa[k+2]+pb[l+2])/2],normal:n,impulse,penetration,indexA:i,indexB:j});}}}
}
