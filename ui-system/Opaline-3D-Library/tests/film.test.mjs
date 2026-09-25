import test from 'node:test';
import assert from 'node:assert/strict';
import {BubbleFilm,BubblePairCoupling,makeBubbleMesh,PlaneCollider,PhysicsWorld} from '../src/physics/index.js';
const finite=values=>assert.ok(Array.from(values).every(Number.isFinite));
const near=(a,b,error=1e-5)=>assert.ok(Math.abs(a-b)<=error,`${a} != ${b} within ${error}`);
const bubble=(options={})=>new BubbleFilm({...makeBubbleMesh({subdivisions:2,radius:.55}),...options});

test('closed bubble mesh has positive volume and near-spherical gas capacity',()=>{
  const b=new BubbleFilm({...makeBubbleMesh({subdivisions:3,radius:.6,center:[3,2,-4]})});
  near(b.volume/(4*Math.PI*.6**3/3),1,.04);assert.ok(b.totalArea>0);assert.equal(b.count,258);
  assert.throws(()=>new BubbleFilm({positions:[0,0,0,1,0,0,0,1,0],triangles:[0,1,2]}));
});

test('gas-volume constraint recovers compression without nonfinite geometry',()=>{
  const b=bubble({damping:3,gasCompliance:1e-10});
  for(let i=0;i<b.count;i++)b.positions[3*i+1]*=.65;
  for(let i=0;i<180;i++)b.step(1/120);
  finite(b.positions);finite(b.velocities);near(b.volume/b.restVolume,1,.004);
});

test('gravity drainage transports film downwards while conserving liquid mass and thickness bounds',()=>{
  const b=bubble({drainageTimeScale:2e5,edgeCompliance:1e-6,gasCompliance:1e-10});const initial=b.totalFilmMass;
  const weightedY=()=>{let moment=0;for(let i=0;i<b.count;i++)moment+=b.positions[3*i+1]*b.filmMass[i];return moment/b.totalFilmMass;};
  const start=weightedY();for(let i=0;i<240;i++)b.step(1/120);
  near(b.totalFilmMass,initial,initial*1e-10);assert.ok(weightedY()<start-.001);assert.equal(b.boundsRelaxed,false);
  for(const h of b.thickness)assert.ok(h>=b.minThickness*.999&&h<=b.maxThickness*1.001);finite(b.thickness);
});

test('local bubble contact with a rigid plane flattens the contact while retaining gas',()=>{
  const b=new BubbleFilm({...makeBubbleMesh({subdivisions:3,radius:.55,center:[0,.43,0]}),colliders:[new PlaneCollider({offset:0})],gasCompliance:1e-10,iterations:18,damping:3});
  for(let i=0;i<120;i++)b.step(1/120);
  for(let i=0;i<b.count;i++)assert.ok(b.positions[3*i+1]>=.0009);near(b.volume/b.restVolume,1,.02);finite(b.positions);
});

test('paired bubbles flatten into distinct contact caps without losing film mass',()=>{
  const a=new BubbleFilm({...makeBubbleMesh({subdivisions:2,radius:.55,center:[-.43,0,0]}),gasCompliance:1e-10,damping:4});
  const b=new BubbleFilm({...makeBubbleMesh({subdivisions:2,radius:.55,center:[.43,0,0]}),gasCompliance:1e-10,damping:4});
  const mass=a.totalFilmMass+b.totalFilmMass,coupling=new BubblePairCoupling(a,b,{iterations:16}),world=new PhysicsWorld();world.add(a);world.add(b);world.couplings.push(coupling);
  world.advance(1);assert.ok(coupling.contactRadius>0);assert.ok(coupling.contactCount>0);
  near(a.volume/a.restVolume,1,.025);near(b.volume/b.restVolume,1,.025);
  for(let i=0;i<a.count;i++)assert.ok(a.positions[3*i]<=.00001);for(let i=0;i<b.count;i++)assert.ok(b.positions[3*i]>=-.00001);
  world.advance(100);finite(a.positions);finite(b.positions);near(a.totalFilmMass+b.totalFilmMass,mass,mass*1e-10);
});

test('bubble grab cancellation preserves liquid mass through local deformation',()=>{
  const b=bubble(),mass=b.totalFilmMass;let highest=0;for(let i=0;i<b.count;i++)if(b.positions[3*i+1]>b.positions[3*highest+1])highest=i;
  b.grab('touch',[highest],[0,.7,0],1e-6);for(let i=0;i<30;i++)b.step(1/120);b.cancelInteractions();for(let i=0;i<120;i++)b.step(1/120);
  assert.equal(b.grabs.size,0);near(b.totalFilmMass,mass,mass*1e-10);finite(b.thickness);near(b.volume/b.restVolume,1,.02);
});
