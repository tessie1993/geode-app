import assert from 'node:assert/strict';
import * as THREE from 'three';
import {RefractiveCaustics,PhotonVolume,createVolumeCausticsMaterial,refractDirection,fresnelDielectric} from '../src/optics.js';
import {createMaterials,enableFilmThickness,cloneMaterial} from '../src/materials.js';

const V=(x=0,y=0,z=0)=>new THREE.Vector3(x,y,z);
let checks=0;
const close=(a,b,tolerance=1e-8)=>assert.ok(Math.abs(a-b)<tolerance,`${a} != ${b}`);
const budget=c=>{const e=c.statistics.energy;close(e.launched,e.receiver+e.escaped+e.absorbed+e.scattered+e.discarded+e.cutoff,1e-7);};

close(refractDirection(V(.5,-Math.sqrt(.75),0),V(0,1,0),1,1.5).x,1/3);close(fresnelDielectric(1,1,1.5),.04);assert.equal(refractDirection(V(.9,-Math.sqrt(.19),0),V(0,1,0),1.5,1),null);checks+=3;

for(const resolution of [8,24]){
  const volume=new PhotonVolume({bounds:new THREE.Box3(V(-1,-1,-1),V(1,1,1)),resolution,extinction:.5,albedo:.8,onlyCaustics:false});
  const result=volume.segment(V(-2,0,0),V(1,0,0),4,1,0,true);close(result.energy,Math.exp(-1));
  let deposit=0;for(const sum of volume.sums)for(const x of sum)deposit+=x*volume.voxelVolume;
  close(deposit,(1-Math.exp(-1))*.8);close(result.energy+result.absorbed+result.scattered,1);
  volume.flush(2);assert.ok(volume.texture.isData3DTexture);assert.ok(volume.texture.image.data.every(Number.isFinite));
  for(let i=0;i<volume.texture.image.data.length;i+=4){let bins=0;for(let j=0;j<6;j++)bins+=volume.textures[j].image.data[i];close(bins,volume.texture.image.data[i],1e-5);}
  const noPath=volume.segment(V(-2,3,0),V(1,0,0),4,1,0,true);close(noPath.energy,1);volume.dispose();checks++;
}

const mirror=new THREE.Mesh(new THREE.PlaneGeometry(4,4),new THREE.MeshPhysicalMaterial({metalness:1,color:0xffffff}));mirror.rotation.x=-Math.PI/2;mirror.userData.optics={reflectance:.8};
const receiver=new THREE.Mesh(new THREE.PlaneGeometry(.8,.8),new THREE.MeshStandardMaterial());receiver.rotation.x=-Math.PI/2;receiver.position.set(1,2,0);
const config={reflectors:[mirror],receiver,transport:'reflection',resolution:32,photonsPerUpdate:100,lightPosition:V(-1,2,0),lightTarget:V(),aperture:new THREE.Vector2(.2,.2)};
const reflection=new RefractiveCaustics(config);reflection.update();budget(reflection);close(reflection.statistics.energy.receiver,240);close(reflection.statistics.energy.absorbed,60);assert.equal(reflection.deposited,300);checks++;

const caustics=new RefractiveCaustics({...config,volume:{bounds:new THREE.Box3(V(-.8,.05,-.4),V(.8,1.7,.4)),resolution:16,extinction:.2,albedo:.9}});caustics.update();budget(caustics);assert.ok(caustics.volume.depositedEnergy>0);assert.ok(caustics.volumeTexture.image.data.every(Number.isFinite));
assert.ok(caustics.statistics.energy.receiver<reflection.statistics.energy.receiver);
const volumeMaterial=createVolumeCausticsMaterial(caustics);assert.equal(volumeMaterial.glslVersion,THREE.GLSL3);assert.ok(volumeMaterial.fragmentShader.includes('phase(-rd.x)'));assert.equal(volumeMaterial.uniforms.uVolume0.value,caustics.volumeTextures[0]);checks++;

const original=caustics.volumeTexture.image.data.slice(),revision=caustics.revision;mirror.rotation.x=-1.28;caustics.update();budget(caustics);assert.equal(caustics.revision,revision+1);assert.equal(caustics.samples,100);assert.notDeepEqual(caustics.volumeTexture.image.data,original);checks++;

const sphere=new THREE.Mesh(new THREE.SphereGeometry(1,24,16),new THREE.MeshPhysicalMaterial({ior:1.5,attenuationColor:0xffffff,attenuationDistance:10}));sphere.position.y=1.5;
const floor=new THREE.Mesh(new THREE.PlaneGeometry(8,8),new THREE.MeshStandardMaterial());floor.rotation.x=-Math.PI/2;
const split=new RefractiveCaustics({refractors:[sphere],receiver:floor,transport:'split',resolution:32,photonsPerUpdate:100,lightPosition:V(0,6,0),lightTarget:V(),aperture:new THREE.Vector2(4,4)});split.update();budget(split);assert.ok(split.statistics.energy.receiver>0);assert.ok(split.statistics.energy.escaped>0);assert.ok(split.texture.image.data.every(Number.isFinite));checks++;

const materials=createMaterials(),film=enableFilmThickness(cloneMaterial(materials.film));
const shader={vertexShader:THREE.ShaderLib.physical.vertexShader,fragmentShader:THREE.ShaderLib.physical.fragmentShader,uniforms:{}};film.onBeforeCompile(shader);assert.ok(shader.vertexShader.includes('attribute float filmThickness'));assert.ok(shader.fragmentShader.includes('vOpFilmThickness*1.0e9'));assert.equal(film.defines.OPALINE_FILM_THICKNESS,1);assert.equal(materials.film.defines?.OPALINE_FILM_THICKNESS,undefined);checks++;

reflection.dispose();caustics.dispose();split.dispose();volumeMaterial.dispose();film.dispose();Object.values(materials).forEach(x=>x.dispose());
console.log(JSON.stringify({suite:'optics',checks,status:'pass',features:['Snell','Fresnel','TIR','reflection','split-energy-budget','voxel-DDA','resolution-normalization','optical-motion-invalidation','evolved-film-thickness']}));
