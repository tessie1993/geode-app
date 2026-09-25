import * as THREE from 'three';
import {cloneMaterial} from './materials.js';

/** A reusable, critically damped interaction coordinate. Values are semantic 0–1. */
export class Spring {
  constructor(value=0,frequency=5,damping=.72){this.value=value;this.target=value;this.velocity=0;this.frequency=frequency;this.damping=damping;}
  step(dt){const h=Math.min(dt,1/20), n=Math.ceil(h/(1/240)), d=h/n, w=this.frequency*2*Math.PI;for(let i=0;i<n;i++){this.velocity+=(w*w*(this.target-this.value)-2*this.damping*w*this.velocity)*d;this.value+=this.velocity*d;}return this.value;}
  reset(v=0){this.target=this.value=v;this.velocity=0;}
}

/** Geometry-level elastic preview binding. Physics specimens use XPBD separately.
 * Contact is local in object space and never displaces text/content planes.
 */
export class MotionController {
  constructor(root,{onChange=()=>{},id=''}={}){
    this.root=root;this.id=id;this.onChange=onChange;this.pressure=new Spring(0,4,.68);this.value=new Spring(.5,4,.8);this.contact=new THREE.Vector3();this.velocity=new THREE.Vector3();this.active=false;this.initialPosition=root.position.clone();this.baseRotation=root.rotation.clone();this.baseScale=root.scale.clone();this.parts=[];this.deform=[];
    root.traverse(mesh=>{const name=(mesh.name||'').toLowerCase();if(mesh.userData.motion||/thumb|knob|rotor|puck|dial-cap|inner-dial|outer-dial/.test(name))this.parts.push({mesh,position:mesh.position.clone(),rotation:mesh.rotation.clone(),scale:mesh.scale.clone(),config:mesh.userData.motion||{kind:'dial'},value:new Spring(.5,4,.8)});if(!mesh.isMesh)return;if(mesh.material?.userData?.opaline)mesh.material=cloneMaterial(mesh.material);
      // Clone only closed gel bodies; water, film and mechanical collars retain their own solvers.
      if(!mesh.geometry?.attributes?.position||mesh.geometry.attributes.position.count>50000)return;
      if(['gel','blue','nacre','pigment'].includes(mesh.material?.userData?.opaline?.family)&&['body','thumb','panel'].includes(mesh.userData.role)){const geo=mesh.geometry.clone();mesh.geometry=geo;this.deform.push({mesh,geometry:geo,rest:geo.attributes.position.array.slice()});}
    });
    this.kind=this.inferKind(id);this.lastPressure=0;this.activePart=null;this.value2=new Spring(.5,4,.8);for(const p of this.parts){if(p.config.kind==='slider'){const axis=p.config.axis||'x';p.value.reset(THREE.MathUtils.clamp((p.position[axis]-p.config.min)/(p.config.max-p.config.min),0,1));}}if(this.parts[0]&&this.parts[0].config.kind==='slider')this.value.reset(this.parts[0].value.value);
  }
  inferKind(id){const n=+id.slice(1);if(id[0]!=='B')return 'press';if(n>=9&&n<=11)return 'toggle';if(n===18||n===19||n===21)return 'xy';if(n>=13&&n<=17||n===22||n===24)return 'rotary';if(n===20||n===23)return 'press';return 'slider';}
  begin(worldPoint,hitMesh){this.active=true;this.pressure.target=1;this.contact.copy(this.root.worldToLocal(worldPoint.clone()));this.activePart=this.parts.find(p=>p.mesh===hitMesh||p.mesh.children.includes(hitMesh))||this.parts[0];if(this.id==='B04'&&hitMesh){const p=hitMesh.getWorldPosition(new THREE.Vector3());this.activePart=[...this.parts].sort((a,b)=>a.mesh.getWorldPosition(new THREE.Vector3()).distanceTo(p)-b.mesh.getWorldPosition(new THREE.Vector3()).distanceTo(p))[0];}if(this.activePart&&(this.id==='B04'||this.id==='B15'))this.value.reset(this.activePart.value.target);if(this.kind==='toggle')this.setValue(this.value.target>.5?0:1);this.root.dispatchEvent({type:'press',point:worldPoint.clone()});}
  drag(worldPoint,delta){this.contact.copy(this.root.worldToLocal(worldPoint.clone()));this.velocity.set(delta.x,delta.y,0);if(this.kind==='slider')this.setValue(this.value.target+(this.id==='B03'?-delta.y:delta.x)*3.6);if(this.kind==='rotary')this.setValue(this.value.target+(delta.x-delta.y)*2.4);if(this.kind==='xy'){this.setValue(this.value.target+delta.x*3);this.value2.target=THREE.MathUtils.clamp(this.value2.target-delta.y*3,0,1);}}
  setValue(v){this.value.target=THREE.MathUtils.clamp(v,0,1);if(this.id==='B05')this.value.target=Math.round(this.value.target*6)/6;if(this.id==='B12')this.value.target=Math.round(this.value.target*2)/2;if(this.id==='B04'&&this.activePart){const other=this.parts.find(p=>p!==this.activePart&&p.config.kind==='slider');if(other)this.value.target=this.activePart.config.index===0?Math.min(this.value.target,other.value.target-.14):Math.max(this.value.target,other.value.target+.14);}if(this.activePart)this.activePart.value.target=this.value.target;this.onChange(this.value.target);this.root.dispatchEvent({type:'change',value:this.value.target});}
  release(){if(!this.active)return;this.active=false;this.pressure.target=0;this.velocity.set(0,0,0);this.root.dispatchEvent({type:'release',value:this.value.target});}
  reset(){this.release();this.pressure.reset();this.value.reset(.5);this.value2.reset(.5);this.parts.forEach(p=>p.value.reset(.5));this.update(0,0);}
  update(dt,time){const p=this.pressure.step(dt),v=this.value.step(dt);const needs=Math.abs(p-this.lastPressure)>.00005||this.active;
    if(needs){for(const d of this.deform){const arr=d.geometry.attributes.position.array,local=d.mesh.worldToLocal(this.root.localToWorld(this.contact.clone()));for(let i=0;i<arr.length;i+=3){const dx=d.rest[i]-local.x,dy=d.rest[i+1]-local.y,dz=d.rest[i+2]-local.z;const force=p*Math.exp(-(dx*dx+dy*dy+dz*dz*.3)/.72);arr[i]=d.rest[i]+dx*force*.07;arr[i+1]=d.rest[i+1]+dy*force*.07;arr[i+2]=d.rest[i+2]-force*.12;}d.geometry.attributes.position.needsUpdate=true;d.geometry.computeVertexNormals();d.geometry.computeBoundingSphere();}this.lastPressure=p;}
    const v2=this.value2.step(dt);for(const part of this.parts){const c=part.config,independent=this.id==='B04'||this.id==='B15';const pv=independent?part.value.step(dt):v;const mesh=part.mesh;
      if(c.kind==='liquidRail'&&mesh.morphTargetInfluences){const x=pv*((c.steps||7)-1),lo=Math.floor(x),hi=Math.min((c.steps||7)-1,lo+1);mesh.morphTargetInfluences.fill(0);mesh.morphTargetInfluences[lo]=1-(x-lo);mesh.morphTargetInfluences[hi]+=x-lo;}
      else if(c.kind==='slider'){mesh.position[c.axis||'x']=THREE.MathUtils.lerp(c.min??-.94,c.max??.94,pv);if(this.id==='B04'&&part!==this.activePart){const own=part.value.value;mesh.position[c.axis||'x']=THREE.MathUtils.lerp(c.min??-.94,c.max??.94,own);}}
      else if(c.kind==='arc'||c.kind==='orbit'){const a=THREE.MathUtils.lerp(c.start??0,c.end??Math.PI*2,pv)+(c.phase||0);mesh.position.x=(c.center?.[0]||0)+Math.cos(a)*(c.radius||.8);mesh.position.y=(c.center?.[1]||0)+Math.sin(a)*(c.radius||.8);}
      else if(c.kind==='rocker')mesh.rotation[c.axis||'y']=THREE.MathUtils.lerp(c.min,c.max,pv);
      else if(c.kind==='dial'||c.kind==='wheel')mesh.rotation[c.axis||'z']=part.rotation[c.axis||'z']-(pv-.5)*Math.PI*1.6;
      else if(c.kind==='trackball'){mesh.rotation.x=part.rotation.x+(pv-.5)*Math.PI;mesh.rotation.y=part.rotation.y+(v2-.5)*Math.PI;}
      else if(c.kind==='xy'){mesh.position.x=THREE.MathUtils.lerp(c.bounds[0],c.bounds[1],pv);mesh.position.y=THREE.MathUtils.lerp(c.bounds[2],c.bounds[3],v2);}
      else if(c.kind==='joystick'){mesh.rotation.x=(v2-.5)*(c.maxAngle||.5)*2;mesh.rotation.y=(pv-.5)*(c.maxAngle||.5)*2;}
      else if(c.kind==='iris')mesh.rotation.z=c.baseAngle+(pv-.5)*.9;
      else if(c.kind==='wind')mesh.rotation.z=part.rotation.z+Math.sin(time*.7+(c.phase||0))*.04;
      else if(c.kind==='hinge')mesh.rotation[c.axis||'y']=part.rotation[c.axis||'y']+p*.18;
      if(c.kind==='slider'){const stretch=1+Math.min(.28,Math.abs(this.value.velocity)*.12);mesh.scale.copy(part.scale);mesh.scale.x*=stretch;mesh.scale.y/=Math.sqrt(stretch);mesh.scale.z/=Math.sqrt(stretch);}
    }
    this.root.position.z=this.initialPosition.z-p*.035;
    this.root.traverse(o=>{const u=o.material?.userData?.opaline?.uniforms;if(u)u.uOpTime.value=time;if(o.material?.userData?.setInteraction){const local=o.worldToLocal(this.root.localToWorld(this.contact.clone()));o.material.userData.setInteraction(local,p,this.velocity);}});
  }
  dispose(){this.release();for(const d of this.deform)d.geometry.dispose();this.root.traverse(o=>{if(o.material?.userData?.opaline)o.material.dispose();});this.deform=[];}
}

export class InteractionBus extends THREE.EventDispatcher {
  constructor(){super();this.fields=new Map();this.links=[];}
  connect(source,target,transform=x=>x){const handle=e=>{const value=transform(e.value);if(target.setValue)target.setValue(value);else target.dispatchEvent({type:'input',value});};source.addEventListener('change',handle);const unlink=()=>source.removeEventListener('change',handle);this.links.push(unlink);return unlink;}
  emit(type,position,strength=1,payload={}){this.dispatchEvent({type,position:position.clone(),strength,...payload});}
  dispose(){this.links.forEach(fn=>fn());this.links=[];this.fields.clear();}
}
