/** Export every authored physical module as a portable GLB. No browser required. */
import fs from 'node:fs/promises';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { GLTFExporter } from '../vendor/three/examples/jsm/exporters/GLTFExporter.js';
import { createElement, listGeometryIds } from '../src/geometry.js';
import { createMaterials } from '../src/materials.js';

// Three's browser exporter only needs FileReader for Blob conversion here.
if (typeof globalThis.FileReader === 'undefined') globalThis.FileReader=class {
 readAsArrayBuffer(blob){blob.arrayBuffer().then(result=>{this.result=result;this.onloadend?.({target:this});}).catch(e=>this.onerror?.(e));}
 readAsDataURL(blob){blob.arrayBuffer().then(buf=>{this.result=`data:${blob.type};base64,${Buffer.from(buf).toString('base64')}`;this.onloadend?.({target:this});}).catch(e=>this.onerror?.(e));}
};
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const out=path.join(root,'assets/models');await fs.mkdir(out,{recursive:true});
const requested=process.argv.slice(2);
const ids=requested.length?requested:listGeometryIds();
const summary=[];const exporter=new GLTFExporter();
for(const id of ids){
 const palette=createMaterials('tidal');
 for(const mat of Object.values(palette)){const state=mat.userData.opaline;mat.userData={opaline:{family:state?.family,theme:state?.theme},exportNote:'Tidal base PBR parameters exported; custom runtime GLSL, flow and touch fields are supplied in src/materials.js.'};}
 const group=createElement(id,palette);let vertices=0,triangles=0,meshes=0;const roles={},materials=new Set(),morphTargets=[];
 group.traverse(o=>{if(!o.isMesh)return;meshes++;const p=o.geometry.attributes.position;
  for(const v of p.array)if(!Number.isFinite(v))throw new Error(id+' has invalid position');
  const normal=o.geometry.attributes.normal;for(const v of normal.array)if(!Number.isFinite(v))throw new Error(id+' has invalid normal');
  vertices+=p.count;triangles+=(o.geometry.index?.count??p.count)/3;
  roles[o.userData.role]=(roles[o.userData.role]||0)+1;materials.add(o.material.name);if(o.geometry.morphAttributes.position?.length)morphTargets.push({mesh:o.name,count:o.geometry.morphAttributes.position.length,relative:o.geometry.morphTargetsRelative,values:o.geometry.userData.values||null});
 });
 const unsupported=[];group.traverse(o=>{if(o.isHemisphereLight)unsupported.push(o);});for(const o of unsupported)o.removeFromParent();if(unsupported.length)group.userData.exportOmissions=['Hemisphere light is represented in lightRig metadata; GLB KHR_lights_punctual has no hemisphere type.'];
 const binary=await exporter.parseAsync(group,{binary:true,onlyVisible:false,trs:true,includeCustomExtensions:true});
 const buffer=Buffer.from(binary);
 if(buffer.toString('ascii',0,4)!=='glTF'||buffer.readUInt32LE(8)!==buffer.length)throw new Error(id+' invalid GLB container');
 const filename=id+'.glb';await fs.writeFile(path.join(out,filename),buffer);
 summary.push({id,name:group.userData.name,path:'assets/models/'+filename,bytes:buffer.length,sha256:crypto.createHash('sha256').update(buffer).digest('hex'),meshes,vertices,triangles,roles,materials:[...materials],morphTargets,bounds:group.userData.bounds,ports:group.userData.ports,control:group.userData.control,solverDomain:group.userData.solverDomain||null});
 console.log(`${id}: ${meshes} meshes / ${triangles} triangles / ${(buffer.length/1024).toFixed(0)} KiB`);
 const seenG=new Set(),seenM=new Set();group.traverse(o=>{if(o.geometry&&!seenG.has(o.geometry)){seenG.add(o.geometry);o.geometry.dispose();}if(o.material&&!seenM.has(o.material)){seenM.add(o.material);o.material.dispose();}});
}
const manifestPath=path.join(out,'models-manifest.json');
let previous=[];try{previous=JSON.parse(await fs.readFile(manifestPath,'utf8')).models;}catch{}
const map=new Map(previous.map(x=>[x.id,x]));for(const x of summary)map.set(x.id,x);
const models=[...map.values()].sort((a,b)=>a.id.localeCompare(b.id));
await fs.writeFile(manifestPath,JSON.stringify({schemaVersion:'1.0',dependencyNotices:'See THIRD-PARTY-NOTICES.md for included dependency notices; no blanket rights claim over reference inputs',coordinateSystem:'Controls face +Z in XY; environment Y-up',note:'Authoring meshes; liquid and film geometries are solver initial conditions. Tidal base PBR uses the same material parameters as the workbench; custom runtime shader fields are not encoded in GLB. Portable material extensions are appearance approximations, not volumetric simulations. J19/J20 GLB contains density-domain bounds; J24 contains punctual lights.',models},null,2)+'\n');
console.log(`Exported ${summary.length} GLB files; manifest contains ${models.length} modules.`);
