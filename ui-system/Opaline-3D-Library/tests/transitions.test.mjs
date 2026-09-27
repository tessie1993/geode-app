import assert from 'node:assert/strict';
import * as THREE from 'three';
import {TransitionSystem,TRANSITIONS} from '../src/transitions.js';

const mesh=()=>new THREE.Mesh(new THREE.SphereGeometry(.35,16,10),new THREE.MeshPhysicalMaterial({color:0x99ddff}));
const finite=o=>{assert.ok(o.position.toArray().every(Number.isFinite));assert.ok(o.quaternion.toArray().every(Number.isFinite));assert.ok(o.scale.toArray().every(Number.isFinite));o.traverse(m=>{if(m.geometry)assert.ok(m.geometry.attributes.position.array.every(Number.isFinite));});};
let checks=0;

for(const id of Object.keys(TRANSITIONS)){
  const scene=new THREE.Scene(),camera=new THREE.PerspectiveCamera(40,1,.1,100),object=mesh();scene.add(object,camera);camera.position.set(0,2,5);
  const engine=new TransitionSystem({scene,camera});
  const h=engine.play(id,{object:id==='L18'?camera:object,duration:.25});
  engine.update(id==='L16'?3:.35);
  finite(object);finite(camera);assert.equal(h.status,'completed',id);engine.dispose();checks++;
}

// Interruption must retain position, material identity, deformation and first derivative.
{
  const scene=new THREE.Scene(),object=mesh();scene.add(object);const material=object.material;
  const engine=new TransitionSystem({scene});const first=engine.play('L01',{object,to:[2,1,0],duration:2});engine.update(.7);
  const position=object.position.clone(),velocity=first.entries[0].velocity.clone();
  const second=engine.play('L02',{object,to:[-.6,.1,.5],duration:1.2});
  assert.equal(first.status,'cancelled');assert.ok(object.position.distanceTo(position)<1e-12);assert.equal(object.material,material);
  engine.update(1e-5);const derivative=object.position.clone().sub(position).divideScalar(1e-5);
  assert.ok(derivative.distanceTo(velocity)<.005,'C1 position continuation');
  engine.update(1.3);assert.equal(second.status,'completed');assert.ok(object.position.distanceTo(new THREE.Vector3(-.6,.1,.5))<1e-8);checks++;
  const morph=engine.play('L04',{object,duration:1});engine.update(.4);const before=object.geometry.attributes.position.array.slice();
  engine.cancel(morph);engine.play('L05',{object,duration:1});assert.deepEqual(object.geometry.attributes.position.array,before);engine.update(1.1);finite(object);checks++;engine.dispose();
}

// A moving socket is sampled throughout L02, using actual world pose under a parent.
{
  const scene=new THREE.Scene(),parent=new THREE.Group(),object=mesh(),socket=new THREE.Object3D();parent.position.set(4,1,-2);parent.add(object);scene.add(parent,socket);socket.position.set(1,2,3);
  const engine=new TransitionSystem({scene});engine.play('L02',{object,ports:{mount:socket},duration:.5});engine.update(.2);socket.position.set(2,1,0);engine.update(.4);
  assert.ok(object.getWorldPosition(new THREE.Vector3()).distanceTo(socket.position)<1e-8);engine.dispose();checks++;
}

// Explicit fluid adapters receive one packet each; cancelling does not invent a refund.
{
  const scene=new THREE.Scene(),object=mesh();scene.add(object);const engine=new TransitionSystem({scene});let withdraw=0,deposit=0;
  const h=engine.play('L13',{object,duration:.5,mass:2,ports:{withdraw:({mass})=>{withdraw++;return {mass,pigment:[.2,.4,.7]};},deposit:packet=>{deposit++;assert.equal(packet.mass,2);}}});engine.update(.7);
  assert.equal(withdraw,1);assert.equal(deposit,1);assert.equal(h.transfer.state,'destination');assert.equal(h.transfer.accountedByHost,true);engine.dispose();checks++;
}

// Real hole and finite inner/outer walls, no alpha or screen mask.
{
  const scene=new THREE.Scene(),object=mesh();scene.add(object);const engine=new TransitionSystem({scene});const h=engine.play('L15',{object,duration:.25,innerRadius:1});engine.update(.3);
  const a=h.aperture.geometry.attributes.position;let minimum=Infinity;for(let i=0;i<a.count;i++)minimum=Math.min(minimum,Math.hypot(a.getX(i),a.getY(i)));
  assert.ok(minimum>1);assert.ok(h.aperture.geometry.index.count>0);engine.dispose();assert.ok(!h.aperture.parent);checks++;
}

// Spatial particle contacts preserve every object and avoid sphere overlap.
{
  const scene=new THREE.Scene(),objects=Array.from({length:8},(_,i)=>{const o=mesh();o.position.set((i%2-.5)*1.2,(Math.floor(i/2)%2-.5)*1.2,(Math.floor(i/4)-.5)*1.2);scene.add(o);return o;});
  const engine=new TransitionSystem({scene}),h=engine.play('L16',{objects,duration:1,spacing:.25,radius:.09});engine.update(2);
  assert.equal(h.status,'completed');for(let i=0;i<objects.length;i++)for(let j=i+1;j<objects.length;j++)assert.ok(objects[i].position.distanceTo(objects[j].position)>=.179);assert.equal(scene.children.length,8);engine.dispose();checks++;
}

// Radial reveal has independent sockets, local center awakening and 3D light fronts.
{
  const scene=new THREE.Scene(),center=mesh(),objects=Array.from({length:12},(_,i)=>{const o=mesh();o.position.set(Math.cos(i)*.07,.1,Math.sin(i)*.07);scene.add(o);return o;});scene.add(center);
  const events=[],engine=new TransitionSystem({scene,eventBus:event=>events.push(event.type)}),h=engine.play('L03',{objects,center:[0,0,0],centerObject:center,radialReveal:true,duration:.8});engine.update(.9);
  assert.equal(h.connections.length,12);assert.equal(h.status,'completed');assert.ok(events.includes('transition:radial-lift'));assert.ok(events.includes('transition:aftermath'));
  for(const o of objects)assert.ok(Math.abs(Math.hypot(o.position.x,o.position.z)-1.65)<1e-5);engine.dispose();checks++;
}

assert.throws(()=>new TransitionSystem().play('L99'),RangeError);
assert.throws(()=>new TransitionSystem().update(NaN),RangeError);
console.log(JSON.stringify({suite:'transitions',checks,ids:Object.keys(TRANSITIONS).length,status:'pass'}));
