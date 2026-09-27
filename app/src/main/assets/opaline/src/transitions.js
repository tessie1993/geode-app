import * as THREE from 'three';

const V=(x=0,y=0,z=0)=>new THREE.Vector3(x,y,z);
const clamp=(x,a=0,b=1)=>Math.max(a,Math.min(b,x));
const smooth=t=>{t=clamp(t);return t*t*t*(t*(t*6-15)+10);};
const bump=t=>16*t*t*(1-t)*(1-t);
const vec=(x,fallback=V())=>x?.isVector3?x.clone():Array.isArray(x)?V(...x):fallback.clone();
const finiteVector=v=>Number.isFinite(v.x)&&Number.isFinite(v.y)&&Number.isFinite(v.z);

export const TRANSITIONS=Object.freeze({
  L01:'Lift to foreground',L02:'Return to mount',L03:'Orbit reconfiguration',
  L04:'Pebble-to-panel transformation',L05:'Seed-to-row construction',L06:'Panel contour reveal',
  L07:'Water crest wipe',L08:'Liquid curtain reveal',L09:'Depth fan expansion',
  L10:'Depth fan collapse',L11:'Dock growth',L12:'Branch extension',
  L13:'Droplet transfer',L14:'Fluid merge reveal',L15:'Membrane aperture reveal',
  L16:'Particulate assembly',L17:'Particulate dispersal',L18:'Camera passage'
});

function worldPose(object){
  object.updateWorldMatrix(true,false);
  return {position:object.getWorldPosition(V()),quaternion:object.getWorldQuaternion(new THREE.Quaternion()),scale:object.getWorldScale(V(1,1,1))};
}
function copyPose(p){return {position:p.position.clone(),quaternion:p.quaternion.clone(),scale:p.scale.clone()};}
function resolvePose(value,fallback){
  if(typeof value==='function')value=value();
  if(value?.isObject3D)return worldPose(value);
  if(value?.isVector3||Array.isArray(value))return {...copyPose(fallback),position:vec(value)};
  const p=copyPose(fallback);
  if(value?.position)p.position=vec(value.position);
  if(value?.scale)p.scale=vec(value.scale);
  if(value?.quaternion)p.quaternion=value.quaternion.isQuaternion?value.quaternion.clone():new THREE.Quaternion(...value.quaternion);
  if(value?.rotation)p.quaternion.setFromEuler(new THREE.Euler(...value.rotation));
  return p;
}
function setWorldPose(object,p){
  if(!finiteVector(p.position)||!finiteVector(p.scale))throw new RangeError('Transition produced a non-finite pose.');
  const matrix=new THREE.Matrix4().compose(p.position,p.quaternion,p.scale);
  if(object.parent){object.parent.updateWorldMatrix(true,false);matrix.premultiply(new THREE.Matrix4().copy(object.parent.matrixWorld).invert());}
  matrix.decompose(object.position,object.quaternion,object.scale);
  object.updateMatrix();object.updateWorldMatrix(false,true);
}
function hermite(a,b,velocity,duration,t,target=V()){
  const t2=t*t,t3=t2*t;
  return target.copy(a).multiplyScalar(2*t3-3*t2+1).addScaledVector(velocity,(t3-2*t2+t)*duration).addScaledVector(b,-2*t3+3*t2);
}
function angularVelocity(previous,current,dt){
  const q=current.clone().multiply(previous.clone().invert()).normalize();
  if(q.w<0)q.set(-q.x,-q.y,-q.z,-q.w);
  const angle=2*Math.acos(clamp(q.w,-1,1)),sin=Math.sqrt(Math.max(0,1-q.w*q.w));
  return sin<1e-7?V():V(q.x,q.y,q.z).multiplyScalar(angle/(sin*dt));
}
function meshList(object){const meshes=[];object?.traverse(o=>{if(o.isMesh&&o.geometry?.attributes.position&&!o.isInstancedMesh)meshes.push(o);});return meshes;}
function objectPieces(object){
  const children=object?.children?.filter(o=>o.isMesh||o.isGroup)||[];
  return children.length>1?children:[object].filter(Boolean);
}
function sameTopology(a,b){
  if(!b?.attributes?.position||a.attributes.position.count!==b.attributes.position.count)return false;
  if(Boolean(a.index)!==Boolean(b.index))return false;
  if(a.index){if(a.index.count!==b.index.count)return false;for(let i=0;i<a.index.count;i++)if(a.index.array[i]!==b.index.array[i])return false;}
  return true;
}
function normalizedCoordinates(state,index){
  const p=state.base,k=index*3,b=state.bounds;
  return V((p[k]-b.center.x)/b.half.x,(p[k+1]-b.center.y)/b.half.y,(p[k+2]-b.center.z)/b.half.z);
}
function roundedTarget(q,half,exponent=4){
  const d=q.clone().normalize(),den=(Math.abs(d.x)**exponent+Math.abs(d.y)**exponent+Math.abs(d.z)**exponent)**(1/exponent)||1;
  return d.multiplyScalar(1/den).multiply(half);
}

/** A finite-thickness annulus with front, back, inner wall and outer wall. */
function apertureGeometry(segments=96,radial=8){
  const positions=[],uvs=[],indices=[],meta=[];
  const surface=(side)=>{
    const start=positions.length/3;
    for(let j=0;j<=radial;j++)for(let i=0;i<=segments;i++){
      const a=i/segments*Math.PI*2,r=j/radial;
      positions.push(Math.cos(a),Math.sin(a),side*.045);uvs.push(i/segments,r);meta.push(a,r,side);
    }
    for(let j=0;j<radial;j++)for(let i=0;i<segments;i++){
      const a=start+j*(segments+1)+i,b=a+1,c=a+segments+1,d=c+1;
      if(side>0)indices.push(a,b,d,a,d,c);else indices.push(a,d,b,a,c,d);
    }
    return start;
  };
  const front=surface(1),back=surface(-1);
  for(let i=0;i<segments;i++){
    const a=front+i,b=a+1,c=back+i,d=c+1;
    indices.push(a,c,d,a,d,b);
    const e=front+radial*(segments+1)+i,f=e+1,g=back+radial*(segments+1)+i,h=g+1;
    indices.push(e,f,h,e,h,g);
  }
  const g=new THREE.BufferGeometry();g.setAttribute('position',new THREE.Float32BufferAttribute(positions,3));g.setAttribute('uv',new THREE.Float32BufferAttribute(uvs,2));g.setIndex(indices);g.userData.aperture=meta;g.computeVertexNormals();return g;
}

/**
 * Authored 3D choreography, with explicit optional physics ownership ports.
 * Poses are world-space; geometry deformation is local-space. One active writer
 * owns each object. Replacing a transition begins at its current pose and velocity.
 * This is not a liquid topology solver or an implicit promise of collision-free
 * arbitrary UI arrangements. L16/L17 have sphere contact dynamics; other recipes
 * expose `ports.resolvePose` for the host collision/constraint solver.
 */
export class TransitionSystem {
  constructor({scene,camera,eventBus}={}){
    this.scene=scene;this.camera=camera;this.eventBus=eventBus;
    this.active=new Map();this._owners=new WeakMap();this._motion=new WeakMap();this._mounts=new WeakMap();this._fanOrigins=new WeakMap();
    this._next=1;this._generated=[];this._geometry=new Set();this.time=0;this.disposed=false;
  }
  _emit(type,handle,extra={}){
    const event={type,id:handle.id,handle,time:this.time,...extra};
    if(typeof this.eventBus==='function')this.eventBus(event);
    else if(this.eventBus?.emit)this.eventBus.emit(type,event);
    else this.eventBus?.dispatchEvent?.(event);
    handle.options.onEvent?.(event);
  }
  _geometryState(mesh){
    const original=mesh.geometry;
    const geometry=original.clone();mesh.geometry=geometry;this._geometry.add(geometry);
    if(this._geometry.has(original)){original.dispose();this._geometry.delete(original);}
    geometry.computeBoundingBox();
    const center=geometry.boundingBox.getCenter(V()),half=geometry.boundingBox.getSize(V()).multiplyScalar(.5);
    half.set(Math.max(half.x,1e-5),Math.max(half.y,1e-5),Math.max(half.z,1e-5));
    return {mesh,geometry,base:geometry.attributes.position.array.slice(),bounds:{center,half}};
  }
  play(id,options={}){
    if(this.disposed)throw new Error('TransitionSystem has been disposed.');
    id=String(id).toUpperCase();if(!TRANSITIONS[id])throw new RangeError(`Unknown transition ${id}`);
    const duration=options.duration??2.4;if(!(duration>0&&Number.isFinite(duration)))throw new RangeError('duration must be positive and finite.');
    if(options.mass!==undefined&&!(Number.isFinite(options.mass)&&options.mass>0))throw new RangeError('Transfer mass must be positive and finite.');
    let objects=options.objects?.slice()||(id==='L18'?[options.object||this.camera]:['L03','L09','L10','L16','L17'].includes(id)?objectPieces(options.object):[options.object]);
    if(objects.some(o=>!o?.isObject3D)||!objects.length)throw new TypeError(`${id} requires object, objects, or a camera for L18.`);
    objects=[...new Set(objects)];
    if(options.targetGeometry){for(const o of objects)for(const m of meshList(o))if(!sameTopology(m.geometry,options.targetGeometry))throw new RangeError('targetGeometry must have identical vertex count and index topology.');}
    const interrupted=new Set();for(const object of objects){const owner=this._owners.get(object);if(owner)interrupted.add(owner);}
    for(const owner of interrupted)this.cancel(owner,{preserveVelocity:true});
    const handle={key:this._next++,id,name:TRANSITIONS[id],status:'running',progress:0,elapsed:0,duration,options,entries:[],geometry:[],auxiliary:[],transfer:null,stages:new Set(),connections:[]};
    const center=objects.reduce((p,o)=>p.add(worldPose(o).position),V()).multiplyScalar(1/objects.length);
    handle.center=vec(options.center,center);
    objects.forEach((object,index)=>{
      let start=worldPose(object);
      if(options.from&&!interrupted.size&&objects.length===1){start=resolvePose(options.from,start);setWorldPose(object,start);}
      const motion=this._motion.get(object)||{velocity:V(),angularVelocity:V(),scaleVelocity:V()};
      const entry={object,index,start:copyPose(start),end:copyPose(start),velocity:motion.velocity.clone(),initialVelocity:motion.velocity.clone(),angularVelocity:motion.angularVelocity.clone(),initialAngularVelocity:motion.angularVelocity.clone(),scaleVelocity:motion.scaleVelocity.clone(),initialScaleVelocity:motion.scaleVelocity.clone(),last:copyPose(start),radius:options.radius??object.userData.transitionRadius??.07};
      if(id==='L01'&&!this._mounts.has(object))this._mounts.set(object,copyPose(start));
      if(id==='L09')this._fanOrigins.set(object,copyPose(start));
      this._defaultEnd(handle,entry,objects.length);
      const explicit=options.targets?.[index]??(objects.length===1?options.to:undefined);
      if(explicit!==undefined)entry.end=resolvePose(explicit,entry.end);
      entry.targetSource=explicit;
      handle.entries.push(entry);this._owners.set(object,handle);
      options.ports?.acquire?.({object,pose:copyPose(start),velocity:entry.velocity.clone(),handle});
    });
    const deform=['L04','L05','L06','L07','L08','L11','L12','L14'].includes(id);
    if(deform){
      const roots=id==='L11'?[options.support||meshList(objects[0])[0]||objects[0]]:objects;
      for(const root of roots)for(const mesh of meshList(root))handle.geometry.push(this._geometryState(mesh));
    }
    if(id==='L12'){
      const points=(options.path||[[-.8,0,0],[0,.4,.2],[.8,.6,.5],[1.8,.35,.8]]).map(p=>vec(p));
      handle.branchPath=new THREE.CatmullRomCurve3(points,false,'centripetal');
    }
    if(id==='L15')this._createAperture(handle);
    if(id==='L03'&&options.radialReveal)this._createConnections(handle);
    if(id==='L11')handle.childStates=(options.children||[]).map(object=>({object,start:worldPose(object)}));
    if(id==='L13')handle.transfer={mass:options.mass??1,state:'source',packet:null,accountedByHost:Boolean(options.ports?.withdraw&&options.ports?.deposit)};
    handle.cancel=(opts)=>this.cancel(handle,opts);
    this.active.set(handle.key,handle);this._emit('transition:start',handle);
    return handle;
  }
  _defaultEnd(h,e,count){
    const {id,options:o,center}=h,p=e.end.position,i=e.index,order=i-(count-1)/2;
    switch(id){
      case 'L01':p.add(vec(o.offset,V(0,.75,1.25)));break;
      case 'L02':e.end=copyPose(this._mounts.get(e.object)||e.start);break;
      case 'L03':{
        if(o.radialReveal){const angle=(o.startAngle??0)+i/count*Math.PI*2,r=o.radius??1.65;p.copy(center).add(V(Math.cos(angle)*r,o.lift??.6,Math.sin(angle)*r));}
        else{const d=e.start.position.clone().sub(center);if(d.length()<.01)d.set(.65+count*.12,0,0);d.applyAxisAngle(V(0,1,0),o.angle??Math.PI*.85);p.copy(center).add(d);}break;
      }
      case 'L07':p.add(vec(o.offset,V(3.2,0,0)));break;
      case 'L08':p.add(vec(o.offset,V(2.8,0,-.35)));break;
      case 'L09':p.copy(center).add(V(order*(o.spacing??1.0),Math.abs(order)*.22,-Math.abs(order)*.38));e.end.quaternion.premultiply(new THREE.Quaternion().setFromAxisAngle(V(0,1,0),order*.22));break;
      case 'L10':e.end=copyPose(this._fanOrigins.get(e.object)||e.start);if(!this._fanOrigins.has(e.object))e.end.position.copy(center).add(V(0,order*(o.spacing??.26),-i*.06));break;
      case 'L13':p.add(vec(o.offset,V(1.8,0,.5)));break;
      case 'L16':{const n=Math.ceil(Math.cbrt(count)),space=o.spacing??.19;p.copy(center).add(V((i%n-(n-1)/2)*space,(Math.floor(i/n)%n-(n-1)/2)*space,(Math.floor(i/(n*n))-(n-1)/2)*space));break;}
      case 'L17':{const a=i*2.3999632297,z=1-2*(i+.5)/count,r=Math.sqrt(1-z*z),direction=V(r*Math.cos(a),z*.6+.4,r*Math.sin(a));p.copy(e.start.position).addScaledVector(direction,o.distance??2.4);break;}
      case 'L18':p.add(vec(o.offset,V(1.25,-.15,-1.7)));break;
    }
  }
  _createAperture(h){
    const original=meshList(h.entries[0].object)[0]?.material;
    const material=h.options.material||new THREE.MeshPhysicalMaterial({color:original?.color||0xd6e9f4,roughness:.18,transmission:.65,thickness:.12,ior:1.37,clearcoat:1});
    const mesh=new THREE.Mesh(apertureGeometry(),material);mesh.name='Opaline/Animated-membrane-aperture';
    const root=this.scene||h.entries[0].object.parent;if(!root)throw new Error('L15 requires a scene or object parent.');root.add(mesh);
    const pose=copyPose(h.entries[0].start);pose.position.add(vec(h.options.apertureOffset,V(0,0,.65)));setWorldPose(mesh,pose);
    h.aperture=mesh;h.auxiliary.push(mesh);this._generated.push({mesh,ownMaterial:!h.options.material});
    this._updateAperture(h,0);
  }
  _createConnections(h){
    const root=this.scene||h.entries[0].object.parent;if(!root)return;
    const center=h.options.centerObject?worldPose(h.options.centerObject).position:h.center.clone();
    for(const e of h.entries){
      const end=this._target(h,e).position,middle=center.clone().lerp(end,.55).add(V(0,.18,0));
      const curve=new THREE.CatmullRomCurve3([center,middle,end],false,'centripetal');
      const material=new THREE.MeshPhysicalMaterial({color:h.options.connectionColor??0x77cce6,roughness:.16,transmission:.75,thickness:.03,ior:1.333,emissive:h.options.connectionColor??0x77cce6,emissiveIntensity:.025});
      const tube=new THREE.Mesh(new THREE.TubeGeometry(curve,48,h.options.connectionRadius??.013,8,false),material);tube.name='Opaline/Radial-connection';root.add(tube);
      const headMaterial=new THREE.MeshPhysicalMaterial({color:0xc7faff,roughness:.12,emissive:0x7ce8ff,emissiveIntensity:0,transmission:.4,thickness:.05});
      const head=new THREE.Mesh(new THREE.SphereGeometry(.028,12,8),headMaterial);head.name='Opaline/Traveling-light-front';root.add(head);
      const light=h.options.connectionLights?new THREE.PointLight(0x7feaff,0,.7,2):null;if(light)head.add(light);
      h.connections.push({curve,tube,head,light,index:e.index});h.auxiliary.push(tube,head);
      this._generated.push({mesh:tube,ownMaterial:true},{mesh:head,ownMaterial:true});
      // Vertices are already world-space: remove a transformed scene root's influence.
      if(root.matrixWorld){root.updateWorldMatrix(true,false);tube.applyMatrix4(new THREE.Matrix4().copy(root.matrixWorld).invert());}
    }
  }
  _radialUpdate(h,t){
    for(const {curve,tube,head,light,index} of h.connections){
      const travel=clamp((t-.16-index*.009)/.43),p=curve.getPoint(travel),pose=worldPose(head);pose.position.copy(p);setWorldPose(head,pose);
      const pulse=Math.sin(Math.PI*travel)**2;
      head.material.emissiveIntensity=3.5*pulse;tube.material.emissiveIntensity=.025+.08*smooth(clamp((t-.3)/.4));
      if(light)light.intensity=.45*pulse;
    }
    const center=h.options.centerObject;
    if(center){
      const intensity=.8*Math.exp(-(((t-.2)/.13)**2));
      for(const mesh of meshList(center))for(const m of Array.isArray(mesh.material)?mesh.material:[mesh.material])m.userData.setInteraction?.(V(0,0,0),intensity);
    }
    for(const [threshold,name] of [[.08,'awakening'],[.18,'connection-front'],[.33,'radial-lift'],[.6,'socket-approach'],[.8,'aftermath']])if(t>=threshold&&!h.stages.has(name)){h.stages.add(name);this._emit(`transition:${name}`,h,{center:h.center.clone()});}
  }
  _updateAperture(h,t){
    const g=h.aperture.geometry,a=g.attributes.position,meta=g.userData.aperture;
    const outer=h.options.outerRadius??1.55,inner=(h.options.innerRadius??1.15)*smooth(t)+.002;
    for(let i=0;i<a.count;i++){
      const angle=meta[i*3],fraction=meta[i*3+1],side=meta[i*3+2],radius=inner+(outer-inner)*fraction;
      const strain=Math.sin(angle*3+t*4)*bump(t)*.025*(1-fraction);
      a.setXYZ(i,Math.cos(angle)*(radius+strain),Math.sin(angle)*(radius+strain),side*.045+Math.sin(angle*2+t*3)*bump(t)*.055*(1-fraction));
    }
    a.needsUpdate=true;g.computeVertexNormals();g.computeBoundingSphere();
  }
  _target(h,e){
    const o=h.options,source=o.targets?.[e.index]??(h.entries.length===1?o.to:undefined)??(h.id==='L02'?o.ports?.mount:undefined);
    return source===undefined?e.end:resolvePose(source,e.end);
  }
  _pose(h,e,t){
    const o=h.options,target=this._target(h,e),s=smooth(t),p={position:hermite(e.start.position,target.position,e.initialVelocity,h.duration,t),quaternion:e.start.quaternion.clone().slerp(target.quaternion,s),scale:hermite(e.start.scale,target.scale,e.initialScaleVelocity,h.duration,t)};
    const w=e.initialAngularVelocity,angle=w.length()*h.duration*t*(1-t)*(1-t);
    if(angle>1e-8)p.quaternion.premultiply(new THREE.Quaternion().setFromAxisAngle(w.clone().normalize(),angle));
    const lift=o.arcHeight??.4;
    if(['L01','L02','L09','L10'].includes(h.id))p.position.y+=bump(t)*lift;
    if(h.id==='L03'){
      if(o.radialReveal){
        const local=clamp((t-.28-e.index*(o.stagger??.012))/.55),phase=smooth(local);
        p.position.copy(e.start.position).lerp(target.position,phase).addScaledVector(e.initialVelocity,h.duration*t*(1-t)*(1-t));
        p.position.y+=bump(local)*(o.arcHeight??.32);
        p.quaternion.copy(e.start.quaternion).slerp(target.quaternion,phase);
      }else{
      const delta=e.start.position.clone().sub(h.center),angle=o.angle??Math.PI*.85;
      if(delta.length()<.01)delta.set(.65+h.entries.length*.12,0,0);
      const orbit=delta.clone().applyAxisAngle(V(0,1,0),angle*s);
      const end=delta.clone().applyAxisAngle(V(0,1,0),angle);
      p.position.add(orbit.sub(delta.clone().lerp(end,s)));
      p.position.y+=bump(t)*(lift+e.index*(o.laneSpacing??.25));
      p.quaternion.premultiply(new THREE.Quaternion().setFromAxisAngle(V(0,1,0),Math.sin(Math.PI*s)*.2));
      }
    }
    if(h.id==='L13'){
      p.position.y+=bump(t)*(o.arcHeight??1.1);
      const stretch=1+bump(t)*.5;
      p.scale.multiply(V(1/Math.sqrt(stretch),stretch,1/Math.sqrt(stretch)));
    }
    if(h.id==='L18'){
      if(o.path){
        h.cameraPath??=new THREE.CatmullRomCurve3([e.start.position,...o.path.map(x=>vec(x)),target.position],false,'centripetal');
        const curve=h.cameraPath.getPoint(s),linear=e.start.position.clone().lerp(target.position,s);
        p.position.add(curve.sub(linear));
      }else p.position.add(V(Math.sin(Math.PI*s)*.22,bump(t)*.12,0));
      if(o.lookAt){
        const look=typeof o.lookAt==='function'?o.lookAt():o.lookAt;
        const lookPoint=look?.isObject3D?worldPose(look).position:vec(look);
        const lookQ=new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().lookAt(p.position,lookPoint,V(0,1,0)));
        p.quaternion.slerp(lookQ,s);
      }
    }
    const resolved=o.ports?.resolvePose?.({object:e.object,pose:p,velocity:e.velocity.clone(),handle:h,progress:t});
    return resolved||p;
  }
  _deform(h,t){
    const o=h.options,s=smooth(t);
    for(const state of h.geometry){
      const {geometry:g,base,bounds:b}=state,a=g.attributes.position;
      for(let i=0;i<a.count;i++){
        const k=i*3,original=V(base[k],base[k+1],base[k+2]),q=normalizedCoordinates(state,i);
        let target=original.clone(),blend=s;
        if(o.targetGeometry)target.fromBufferAttribute(o.targetGeometry.attributes.position,i);
        else if(['L04','L05','L06'].includes(h.id)){
          const size=vec(o.targetSize,h.id==='L05'?V(3.5,.38,.58):V(2.8,.4,1.95));
          target=roundedTarget(q,size.multiplyScalar(.5),h.id==='L05'?3.5:4.5).add(b.center);
        }
        switch(h.id){
          case 'L04':target.y+=Math.sin(Math.PI*t)*.09*(1-q.x*q.x);break;
          case 'L05':blend=smooth(clamp((t-.32*(q.x+1)*.5)/.68));break;
          case 'L06':{
            const edge=Math.max(Math.abs(q.x),Math.abs(q.z));
            blend=smooth(clamp((t/.72-.26*(1-edge))/.74));break;
          }
          case 'L07':{
            // Fold the actual closed mesh cross-section. The rear camera sees the curl.
            const wave=bump(t),phase=q.z*Math.PI*.8;
            target.y+=wave*(.5+.55*Math.cos(phase));
            target.z+=wave*.46*Math.sin(phase);
            target.x+=Math.sin(q.z*3+t*5)*wave*.12;blend=1;break;
          }
          case 'L08':{
            const envelope=bump(t),vertical=(q.y+1)*.5;
            target.z+=Math.sin(q.x*3.8+t*8+q.y*1.2)*envelope*.27;
            target.x+=Math.sin(q.y*2.4-t*5)*envelope*.08;
            target.y-=vertical*envelope*.12;blend=1;break;
          }
          case 'L11':{
            const anchor=b.center.x-b.half.x;
            target.x=anchor+(original.x-anchor)*(o.growth??1.8);
            blend=smooth(clamp((t-.25*(q.x+1)*.5)/.75));break;
          }
          case 'L12':{
            const u=clamp((q.x+1)*.5),point=h.branchPath.getPoint(u),tangent=h.branchPath.getTangent(u).normalize();
            const reference=Math.abs(tangent.y)>.95?V(1,0,0):V(0,1,0),side=new THREE.Vector3().crossVectors(tangent,reference).normalize(),up=new THREE.Vector3().crossVectors(side,tangent).normalize();
            target=point.addScaledVector(up,q.y*b.half.y).addScaledVector(side,q.z*b.half.z);
            blend=smooth(clamp((t-.38*u)/.62));break;
          }
          case 'L14':{
            // Fixed-topology bridge broadening. It does not merge disjoint fluid meshes.
            const width=1+.5*s,neck=Math.exp(-q.x*q.x*7)*.46*s;
            target.x=b.center.x+(original.x-b.center.x)*width;
            target.y=b.center.y+(original.y-b.center.y)*(1+neck);
            target.z=b.center.z+(original.z-b.center.z)*(1+neck);
            blend=1;break;
          }
        }
        const result=original.lerp(target,blend);a.setXYZ(i,result.x,result.y,result.z);
      }
      a.needsUpdate=true;g.computeVertexNormals();g.computeBoundingSphere();g.computeBoundingBox();
      if(h.id==='L06'&&t>.72){
        const sweep=(t-.72)/.28,point=V(b.center.x+(sweep*2-1)*b.half.x,b.center.y+b.half.y,b.center.z);
        const materials=Array.isArray(state.mesh.material)?state.mesh.material:[state.mesh.material];
        for(const m of materials)m.userData.setInteraction?.(point,Math.sin(Math.PI*sweep)*.7);
      }
    }
    if(h.id==='L11'){
      // Mounted children keep their own geometry/material state while socket spacing grows.
      for(const child of h.childStates||[]){
        const p=copyPose(child.start),anchor=h.entries[0].start.position;
        p.position.x=anchor.x+(p.position.x-anchor.x)*(1+((o.growth??1.8)-1)*s);setWorldPose(child.object,p);
      }
    }
  }
  _particleStep(h,dt,t){
    const o=h.options,omega=o.stiffness??11,drag=o.drag??2.2;
    for(const e of h.entries){
      const current=worldPose(e.object),target=this._target(h,e).position;
      let acceleration;
      if(h.id==='L16')acceleration=target.clone().sub(current.position).multiplyScalar(omega*omega).addScaledVector(e.velocity,-2*omega);
      else{
        const flight=target.clone().sub(e.start.position).multiplyScalar(1/h.duration);
        const wind=typeof o.wind==='function'?vec(o.wind(current.position,this.time)):vec(o.wind,V(.3,.12,.08));
        acceleration=flight.add(wind).sub(e.velocity).multiplyScalar(drag);
      }
      e.velocity.addScaledVector(acceleration,dt);current.position.addScaledVector(e.velocity,dt);
      const spin=vec(o.spin,V(.2+e.index*.013,.35,.17));
      e.angularVelocity.copy(spin);
      const angle=spin.length()*dt;if(angle)current.quaternion.premultiply(new THREE.Quaternion().setFromAxisAngle(spin.normalize(),angle));
      if(o.floor!==undefined&&current.position.y<o.floor+e.radius){current.position.y=o.floor+e.radius;if(e.velocity.y<0)e.velocity.y*=-.3;}
      setWorldPose(e.object,current);
    }
    // Actual sphere contacts between persistent pieces, with equal-mass impulses.
    for(let iteration=0;iteration<3;iteration++)for(let i=0;i<h.entries.length;i++)for(let j=i+1;j<h.entries.length;j++){
      const a=h.entries[i],b=h.entries[j],pa=worldPose(a.object),pb=worldPose(b.object),d=pb.position.clone().sub(pa.position),distance=d.length(),contact=a.radius+b.radius;
      if(distance>=contact)continue;
      const normal=distance>1e-8?d.divideScalar(distance):V(i%2?1:-1,0,0),correction=(contact-distance)*.5;
      pa.position.addScaledVector(normal,-correction);pb.position.addScaledVector(normal,correction);
      const approach=b.velocity.clone().sub(a.velocity).dot(normal);
      if(approach<0){const impulse=-(1+(o.restitution??.15))*approach*.5;a.velocity.addScaledVector(normal,-impulse);b.velocity.addScaledVector(normal,impulse);}
      setWorldPose(a.object,pa);setWorldPose(b.object,pb);
    }
    if(h.id==='L16'&&t>=1){
      return h.entries.every(e=>worldPose(e.object).position.distanceTo(this._target(h,e).position)<(o.settleTolerance??.003)&&e.velocity.length()<.025);
    }
    return h.id==='L17'&&t>=1;
  }
  _transfer(h,t){
    const ledger=h.transfer,ports=h.options.ports||{};if(!ledger)return;
    if(t>=.18&&ledger.state==='source'){
      if(ports.withdraw){ledger.packet=ports.withdraw({mass:ledger.mass,object:h.entries[0].object,handle:h});if(!ledger.packet)throw new Error('Fluid source did not provide a transfer packet.');}
      else ledger.packet={mass:ledger.mass,pigment:h.options.pigment||null};
      ledger.state='in-transit';this._emit('transition:detach',h,{transfer:ledger});
    }
    if(t>=.88&&ledger.state==='in-transit'){
      ports.deposit?.(ledger.packet,{object:h.entries[0].object,handle:h});ledger.state='destination';this._emit('transition:deposit',h,{transfer:ledger});
    }
  }
  update(dt){
    if(!Number.isFinite(dt)||dt<0)throw new RangeError('dt must be finite and nonnegative.');
    if(this.disposed||dt===0)return;
    let remaining=dt;
    while(remaining>1e-9){
      const step=Math.min(remaining,1/120);remaining-=step;this.time+=step;
      for(const h of [...this.active.values()]){
        if(h.status!=='running')continue;
        h.elapsed+=step;const t=clamp(h.elapsed/h.duration);h.progress=t;
        let complete=t>=1;
        if(h.id==='L16'||h.id==='L17')complete=this._particleStep(h,step,t);
        else for(const e of h.entries){
          const pose=this._pose(h,e,t);setWorldPose(e.object,pose);
          e.velocity.copy(pose.position).sub(e.last.position).divideScalar(step);
          e.angularVelocity=angularVelocity(e.last.quaternion,pose.quaternion,step);
          e.scaleVelocity.copy(pose.scale).sub(e.last.scale).divideScalar(step);
        }
        if(h.geometry.length)this._deform(h,t);
        if(h.aperture)this._updateAperture(h,t);
        if(h.connections.length)this._radialUpdate(h,t);
        if(h.transfer)this._transfer(h,t);
        for(const e of h.entries){
          e.last=worldPose(e.object);this._motion.set(e.object,{velocity:e.velocity.clone(),angularVelocity:e.angularVelocity.clone(),scaleVelocity:e.scaleVelocity.clone()});
          h.options.ports?.update?.({object:e.object,pose:copyPose(e.last),velocity:e.velocity.clone(),handle:h,progress:t,dt:step});
        }
        h.options.onUpdate?.(h);
        if(complete)this._finish(h);
        // Impossible packed particle destinations are reported instead of being snapped through each other.
        else if(h.elapsed>h.duration+(h.options.maxSettleTime??6)){
          h.status='blocked';this._release(h,true);this._emit('transition:blocked',h,{reason:'Particle target/contact constraints did not settle.'});
        }
      }
    }
  }
  _finish(h){
    h.status='completed';h.progress=1;
    if(h.id!=='L17')for(const e of h.entries){e.velocity.set(0,0,0);e.angularVelocity.set(0,0,0);e.scaleVelocity.set(0,0,0);this._motion.set(e.object,{velocity:V(),angularVelocity:V(),scaleVelocity:V()});}
    this._release(h,true);this._emit('transition:complete',h);h.options.onComplete?.(h);
  }
  _release(h,preserveVelocity){
    this.active.delete(h.key);
    for(const e of h.entries){
      if(this._owners.get(e.object)===h)this._owners.delete(e.object);
      h.options.ports?.release?.({object:e.object,pose:worldPose(e.object),velocity:preserveVelocity?e.velocity.clone():V(),angularVelocity:preserveVelocity?e.angularVelocity.clone():V(),handle:h});
    }
  }
  cancel(handle,{preserveVelocity=true}={}){
    const h=typeof handle==='number'?this.active.get(handle):handle;
    if(!h||h.status!=='running')return false;
    h.status='cancelled';
    if(!preserveVelocity)for(const e of h.entries){e.velocity.set(0,0,0);e.angularVelocity.set(0,0,0);e.scaleVelocity.set(0,0,0);this._motion.set(e.object,{velocity:V(),angularVelocity:V(),scaleVelocity:V()});}
    this._release(h,preserveVelocity);this._emit('transition:cancel',h,{preserveVelocity});return true;
  }
  dispose(){
    for(const h of [...this.active.values()])this.cancel(h,{preserveVelocity:true});
    for(const {mesh,ownMaterial} of this._generated){mesh.removeFromParent();mesh.geometry.dispose();if(ownMaterial)mesh.material.dispose();}
    // Authored deformation geometry stays installed on user objects. Ownership of
    // those clones passes to the caller, who disposes them with their object tree.
    this._generated.length=0;this._geometry.clear();this.disposed=true;
  }
}
