import { ConstraintBody, distanceConstraints, solveDistances, EPS, finiteArray, validDt } from './common.js';

/** Closed, seam-free octahedral sphere; subdivisions grow true 3D triangle density. */
export function makeBubbleMesh({ subdivisions = 3, radius = .65, center = [0, 0, 0] } = {}) {
  if (!Number.isInteger(subdivisions) || subdivisions < 0 || subdivisions > 6 || !(radius > 0)) throw new RangeError('invalid bubble resolution/radius');
  finiteArray(center, 3, 'center');
  const points = [[1,0,0],[-1,0,0],[0,1,0],[0,-1,0],[0,0,1],[0,0,-1]];
  let faces = [[2,4,0],[2,1,4],[2,5,1],[2,0,5],[3,0,4],[3,4,1],[3,1,5],[3,5,0]];
  for (let level = 0; level < subdivisions; level++) {
    const cache = new Map(), next = [];
    const midpoint = (a, b) => {
      const key = a < b ? `${a}:${b}` : `${b}:${a}`;
      if (cache.has(key)) return cache.get(key);
      const p = points[a].map((v, q) => (v + points[b][q]) / 2), length = Math.hypot(...p);
      const index = points.length; points.push(p.map(v => v / length)); cache.set(key, index); return index;
    };
    for (const [a, b, c] of faces) {
      const ab = midpoint(a,b), bc = midpoint(b,c), ca = midpoint(c,a);
      next.push([a,ab,ca],[ab,b,bc],[ca,bc,c],[ab,bc,ca]);
    }
    faces = next;
  }
  const positions = new Float32Array(points.flatMap(p => p.map((v,a) => center[a] + radius*v)));
  const triangles = new Uint32Array(faces.flat());
  return { positions, triangles, surfaceIndices: triangles };
}

/** Closed triangular film, enclosed gas volume and conserved surface liquid. */
export class BubbleFilm extends ConstraintBody {
  constructor({ triangles, thickness = 400e-9, minThickness = 80e-9, maxThickness = 1500e-9,
    liquidDensity = 1000, dynamicViscosity = .001, drainageGravity = 9.81, drainageTimeScale = 1,
    gasCompliance = 1e-8, edgeCompliance = 8e-4, surfaceTension = .025, ...options } = {}) {
    super({ gravity: [0,0,0], damping: 1.2, iterations: 12, radius: .001, ...options });
    finiteArray(triangles, 3, 'triangles');
    if (!(thickness > 0 && minThickness > 0 && maxThickness >= minThickness && thickness >= minThickness && thickness <= maxThickness && liquidDensity > 0 && dynamicViscosity > 0)) throw new RangeError('invalid film material parameters');
    this.triangles = new Uint32Array(triangles); this.surfaceIndices = this.triangles;
    this.gasCompliance = gasCompliance; this.edgeCompliance = edgeCompliance; this.surfaceTension = surfaceTension;
    this.minThickness = minThickness; this.maxThickness = maxThickness; this.liquidDensity = liquidDensity;
    this.dynamicViscosity = dynamicViscosity; this.drainageGravity = drainageGravity; this.drainageTimeScale = drainageTimeScale;
    this.area = new Float64Array(this.count); this.thickness = new Float32Array(this.count);
    this.filmMass = new Float64Array(this.count); this._volumeGradient = new Float64Array(this.count*3);
    this._areaGradient = new Float64Array(9); this._areaLambda = new Float64Array(this.triangles.length/3);
    this._volumeLambda = 0; this.boundsRelaxed = false; this.drainageFlux = 0;
    const edges = new Map();
    for (let t=0; t<this.triangles.length; t+=3) {
      const face = this.triangles.subarray(t,t+3); face.forEach(i=>this.checkIndex(i));
      for(let q=0;q<3;q++) {
        const a=face[q],b=face[(q+1)%3],key=a<b?`${a}:${b}`:`${b}:${a}`;
        const entry=edges.get(key); if(entry) { entry.faces++; entry.orientation += a<b?1:-1; }
        else edges.set(key,{pair:[a,b],faces:1,orientation:a<b?1:-1});
      }
    }
    if([...edges.values()].some(e=>e.faces!==2 || e.orientation!==0)) throw new RangeError('bubble requires a closed consistently wound two-manifold triangle mesh');
    this.edges = distanceConstraints(this.positions,[...edges.values()].map(e=>e.pair));
    this._measure(); this.restVolume = this.volume;
    if(!(this.restVolume>1e-10)) throw new RangeError('bubble triangles must be outward wound and enclose positive volume');
    for(let i=0;i<this.count;i++) this.filmMass[i]=this.area[i]*liquidDensity*thickness;
    this.initialFilmMass=this.totalFilmMass; this._updateThickness();
  }
  get totalFilmMass() { let sum=0; for(const m of this.filmMass)sum+=m; return sum; }
  get totalArea() { let sum=0; for(const a of this.area)sum+=a; return sum; }
  get equivalentRadius() { return Math.cbrt(3*this.volume/(4*Math.PI)); }
  get center() { const c=[0,0,0];for(let i=0;i<this.count;i++)for(let a=0;a<3;a++)c[a]+=this.positions[3*i+a]/this.count;return c; }
  get volume() {
    // Translation-relative signed volume avoids cancellation far from world origin.
    const p=this.positions,c=this.center;let result=0;
    for(let t=0;t<this.triangles.length;t+=3) {
      const ia=3*this.triangles[t],ib=3*this.triangles[t+1],ic=3*this.triangles[t+2];
      const ax=p[ia]-c[0],ay=p[ia+1]-c[1],az=p[ia+2]-c[2],bx=p[ib]-c[0],by=p[ib+1]-c[1],bz=p[ib+2]-c[2],cx=p[ic]-c[0],cy=p[ic+1]-c[1],cz=p[ic+2]-c[2];
      result+=(ax*(by*cz-bz*cy)+ay*(bz*cx-bx*cz)+az*(bx*cy-by*cx))/6;
    }
    return result;
  }
  _measure() {
    const p=this.positions;this.area.fill(0);
    for(let t=0;t<this.triangles.length;t+=3) {
      const ids=this.triangles.subarray(t,t+3),a=3*ids[0],b=3*ids[1],c=3*ids[2],ux=p[b]-p[a],uy=p[b+1]-p[a+1],uz=p[b+2]-p[a+2],vx=p[c]-p[a],vy=p[c+1]-p[a+1],vz=p[c+2]-p[a+2];
      const share=Math.hypot(uy*vz-uz*vy,uz*vx-ux*vz,ux*vy-uy*vx)/6;
      for(const i of ids)this.area[i]+=share;
    }
  }
  _solveVolume(dt) {
    const p=this.positions,g=this._volumeGradient,center=this.center;g.fill(0);
    for(let t=0;t<this.triangles.length;t+=3) {
      const a=3*this.triangles[t],b=3*this.triangles[t+1],c=3*this.triangles[t+2];
      const ax=p[a]-center[0],ay=p[a+1]-center[1],az=p[a+2]-center[2],bx=p[b]-center[0],by=p[b+1]-center[1],bz=p[b+2]-center[2],cx=p[c]-center[0],cy=p[c+1]-center[1],cz=p[c+2]-center[2];
      g[a]+=(by*cz-bz*cy)/6;g[a+1]+=(bz*cx-bx*cz)/6;g[a+2]+=(bx*cy-by*cx)/6;
      g[b]+=(cy*az-cz*ay)/6;g[b+1]+=(cz*ax-cx*az)/6;g[b+2]+=(cx*ay-cy*ax)/6;
      g[c]+=(ay*bz-az*by)/6;g[c+1]+=(az*bx-ax*bz)/6;g[c+2]+=(ax*by-ay*bx)/6;
    }
    const alpha=this.gasCompliance/(dt*dt);let denominator=alpha;
    for(let i=0;i<this.count;i++)for(let a=0;a<3;a++)denominator+=this.inverseMass[i]*g[3*i+a]**2;
    if(denominator<EPS)return;
    const dl=(-(this.volume-this.restVolume)-alpha*this._volumeLambda)/denominator;this._volumeLambda+=dl;
    for(let i=0;i<this.count;i++)for(let a=0;a<3;a++)p[3*i+a]+=this.inverseMass[i]*g[3*i+a]*dl;
  }
  _solveTension(dt) {
    if(!(this.surfaceTension>0))return;
    const p=this.positions,g=this._areaGradient,alpha=1/(this.surfaceTension*dt*dt);
    for(let t=0;t<this.triangles.length;t+=3) {
      const ids=this.triangles.subarray(t,t+3),a=3*ids[0],b=3*ids[1],c=3*ids[2],ux=p[b]-p[a],uy=p[b+1]-p[a+1],uz=p[b+2]-p[a+2],vx=p[c]-p[a],vy=p[c+1]-p[a+1],vz=p[c+2]-p[a+2];
      let nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;const area2=Math.hypot(nx,ny,nz);if(area2<EPS)continue;nx/=area2;ny/=area2;nz/=area2;
      const edges=[[p[b]-p[c],p[b+1]-p[c+1],p[b+2]-p[c+2]],[vx,vy,vz],[-ux,-uy,-uz]];
      let den=alpha;for(let j=0;j<3;j++){const [x,y,z]=edges[j];g[3*j]=(y*nz-z*ny)*.5;g[3*j+1]=(z*nx-x*nz)*.5;g[3*j+2]=(x*ny-y*nx)*.5;for(let q=0;q<3;q++)den+=this.inverseMass[ids[j]]*g[3*j+q]**2;}
      const constraint=Math.sqrt(area2);den=alpha;for(let j=0;j<3;j++)for(let q=0;q<3;q++){g[3*j+q]/=constraint;den+=this.inverseMass[ids[j]]*g[3*j+q]**2;}
      const index=t/3,dl=(-constraint-alpha*this._areaLambda[index])/den;this._areaLambda[index]+=dl;
      for(let j=0;j<3;j++)for(let q=0;q<3;q++)p[3*ids[j]+q]+=this.inverseMass[ids[j]]*g[3*j+q]*dl;
    }
  }
  _massBounds() {
    const avg=this.totalFilmMass/(this.liquidDensity*Math.max(this.totalArea,EPS));
    // If imposed geometry makes the requested interval infeasible, preserve mass
    // and explicitly relax the interval instead of silently creating liquid.
    const min=Math.min(this.minThickness,avg),max=Math.max(this.maxThickness,avg);
    this.boundsRelaxed=min!==this.minThickness||max!==this.maxThickness;
    return {min,max};
  }
  _updateThickness() {
    this._measure(); const original=this.totalFilmMass,{min,max}=this._massBounds();let clamped=0;
    for(let i=0;i<this.count;i++){const area=this.area[i]*this.liquidDensity;this.filmMass[i]=Math.max(area*min,Math.min(area*max,this.filmMass[i]));clamped+=this.filmMass[i];}
    const remaining=original-clamped;
    if(Math.abs(remaining)>1e-24){let capacity=0;for(let i=0;i<this.count;i++)capacity+=remaining>0?this.area[i]*this.liquidDensity*max-this.filmMass[i]:this.filmMass[i]-this.area[i]*this.liquidDensity*min;
      if(capacity>0)for(let i=0;i<this.count;i++){const available=remaining>0?this.area[i]*this.liquidDensity*max-this.filmMass[i]:this.filmMass[i]-this.area[i]*this.liquidDensity*min;this.filmMass[i]+=remaining*available/capacity;}
    }
    for(let i=0;i<this.count;i++)this.thickness[i]=this.filmMass[i]/(this.liquidDensity*Math.max(this.area[i],1e-16));
  }
  _drain(dt) {
    this._updateThickness(); if(!(this.drainageTimeScale>0&&this.drainageGravity>0))return;
    const proposed=[],out=new Float64Array(this.count),incoming=new Float64Array(this.count),{min,max}=this._massBounds();
    for(let e=0;e<this.edges.rest.length;e++) {
      const a=this.edges.indices[2*e],b=this.edges.indices[2*e+1],ka=3*a,kb=3*b,dy=this.positions[ka+1]-this.positions[kb+1];if(Math.abs(dy)<EPS)continue;
      const donor=dy>0?a:b,receiver=dy>0?b:a,length=Math.hypot(this.positions[ka]-this.positions[kb],dy,this.positions[ka+2]-this.positions[kb+2]);if(length<EPS)continue;
      const h=(this.thickness[a]+this.thickness[b])*.5,width=(this.area[a]+this.area[b])/(3*length),slope=Math.abs(dy)/length;
      // Thin-film gravity mobility h^3/(3 mu); explicit time scaling is authored.
      const flux=this.liquidDensity**2*this.drainageGravity*slope*h**3*width*dt*this.drainageTimeScale/(3*this.dynamicViscosity);
      proposed.push([donor,receiver,flux]);out[donor]+=flux;incoming[receiver]+=flux;
    }
    const delta=new Float64Array(this.count);this.drainageFlux=0;
    for(const [a,b,flux] of proposed){const available=Math.max(0,this.filmMass[a]-this.area[a]*this.liquidDensity*min),space=Math.max(0,this.area[b]*this.liquidDensity*max-this.filmMass[b]);const transfer=flux*Math.min(1,available/Math.max(out[a],1e-30),space/Math.max(incoming[b],1e-30));delta[a]-=transfer;delta[b]+=transfer;this.drainageFlux+=transfer;}
    for(let i=0;i<this.count;i++)this.filmMass[i]+=delta[i];this._updateThickness();
  }
  step(dt) {
    validDt(dt);this._begin(dt);this.edges.lambda.fill(0);this._areaLambda.fill(0);this._volumeLambda=0;
    for(let i=0;i<this.iterations;i++){solveDistances(this,this.edges,this.edgeCompliance,dt);this._solveTension(dt);this._solveVolume(dt);this._solveGrabs(dt);this._collide();this._applyPins();}
    this._finish(dt);this._drain(dt);return this;
  }
}

/** Two closed films with flattened, separated contact caps. No shared-wall topology. */
export class BubblePairCoupling {
  constructor(a,b,{gap=.002,iterations=8,restitution=.05}={}){this.a=a;this.b=b;this.gap=gap;this.iterations=iterations;this.restitution=restitution;this.contactCount=0;this.contactRadius=0;}
  step(dt) {
    const a=this.a,b=this.b,ca=a.center,cb=b.center,dv=cb.map((v,i)=>v-ca[i]),distance=Math.hypot(...dv),ra=Math.cbrt(a.restVolume*3/(4*Math.PI)),rb=Math.cbrt(b.restVolume*3/(4*Math.PI));
    this.contactCount=0;if(distance>=ra+rb+this.gap||distance<EPS){this.contactRadius=0;return;}
    const n=dv.map(v=>v/distance),offset=(distance*distance+ra*ra-rb*rb)/(2*distance),plane=ca.map((v,i)=>v+n[i]*offset),savedA=a.positions.slice(),savedB=b.positions.slice();
    this.contactRadius=Math.sqrt(Math.max(0,ra*ra-offset*offset));
    const project=(body,sign)=>{let count=0;for(let i=0;i<body.count;i++){if(body.inverseMass[i]===0)continue;const k=3*i,d=(body.positions[k]-plane[0])*n[0]+(body.positions[k+1]-plane[1])*n[1]+(body.positions[k+2]-plane[2])*n[2],violation=sign*d+this.gap*.5;if(violation>0){for(let q=0;q<3;q++)body.positions[k+q]-=sign*n[q]*violation;count++;}}return count;};
    for(let iteration=0;iteration<this.iterations;iteration++){project(a,1);project(b,-1);a._solveVolume(dt);b._solveVolume(dt);}
    this.contactCount=project(a,1)+project(b,-1);
    // Position-level contact correction becomes inertial velocity, with normal
    // inward movement removed. Internal volume gradients sum to zero.
    for(const [body,before,sign] of [[a,savedA,1],[b,savedB,-1]]) {
      for(let i=0;i<body.count;i++){if(body.inverseMass[i]===0)continue;const k=3*i;for(let q=0;q<3;q++)body.velocities[k+q]+=(body.positions[k+q]-before[k+q])/dt;
        const side=(body.positions[k]-plane[0])*n[0]+(body.positions[k+1]-plane[1])*n[1]+(body.positions[k+2]-plane[2])*n[2],vn=(body.velocities[k]*n[0]+body.velocities[k+1]*n[1]+body.velocities[k+2]*n[2])*sign;
        if(Math.abs(side)<this.gap+.003&&vn>0)for(let q=0;q<3;q++)body.velocities[k+q]-=sign*n[q]*vn*(1+this.restitution);
      }
      body._updateThickness();
    }
  }
}
