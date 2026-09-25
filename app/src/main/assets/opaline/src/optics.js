import * as THREE from 'three';

const AIR=1;
const EPS=1e-4;
const WAVELENGTHS=[.610,.550,.460]; // micrometres, representative RGB transport wavelengths

function radicalInverse(index,base){
  let inv=1/base,weight=inv,value=0;
  for(let i=index;i>0;i=Math.floor(i/base)){value+=(i%base)*weight;weight*=inv;}
  return value;
}

/** Snell refraction. normal points into the incident medium. Null denotes TIR. */
export function refractDirection(direction,normal,etaIncident,etaTransmitted,target=new THREE.Vector3()){
  const eta=etaIncident/etaTransmitted;
  const cos=Math.min(1,Math.max(0,-direction.dot(normal)));
  const k=1-eta*eta*(1-cos*cos);
  if(k<0)return null;
  return target.copy(direction).multiplyScalar(eta).addScaledVector(normal,eta*cos-Math.sqrt(k)).normalize();
}

export function fresnelDielectric(cosIncident,n1,n2){
  const sinT2=(n1/n2)**2*Math.max(0,1-cosIncident*cosIncident);
  if(sinT2>=1)return 1;
  const cosT=Math.sqrt(1-sinT2);
  const rs=(n1*cosIncident-n2*cosT)/(n1*cosIncident+n2*cosT);
  const rp=(n1*cosT-n2*cosIncident)/(n1*cosT+n2*cosIncident);
  return (rs*rs+rp*rp)*.5;
}

function materialForHit(hit){
  const src=hit.object.userData.source;
  return Array.isArray(src.material)?src.material[hit.face?.materialIndex||0]:src.material;
}

function spectralIor(material,channel){
  const base=material?.ior||1.333;
  const dispersion=material?.dispersion||0;
  // Cauchy-like RGB sampling, explicitly an approximate three-band spectrum.
  return base+dispersion*.006*(1/(WAVELENGTHS[channel]**2)-1/(.55**2));
}

const freshLedger=()=>({launched:0,receiver:0,escaped:0,absorbed:0,scattered:0,discarded:0,cutoff:0});

/** Actual world-space photon segments integrated through a bounded voxel medium. */
export class PhotonVolume {
  constructor({bounds=new THREE.Box3(new THREE.Vector3(-2,0,-2),new THREE.Vector3(2,3,2)),resolution=32,extinction=.18,albedo=.92,onlyCaustics=true}={}){
    this.bounds=bounds.clone();this.extinction=Math.max(0,extinction);this.albedo=Math.max(0,Math.min(1,albedo));this.onlyCaustics=onlyCaustics;
    const r=typeof resolution==='number'?[resolution,resolution,resolution]:resolution.toArray?.()||resolution;
    this.resolution=r.map(v=>Math.max(4,Math.floor(v)));this.size=this.bounds.getSize(new THREE.Vector3());
    if(this.size.toArray().some(v=>v<=0||!Number.isFinite(v)))throw new RangeError('Photon volume must have positive finite bounds.');
    this.cell=this.size.clone().divide(new THREE.Vector3(...this.resolution));this.voxelVolume=this.cell.x*this.cell.y*this.cell.z;
    const n=this.resolution.reduce((a,b)=>a*b,1);
    this.sums=Array.from({length:6},()=>new Float64Array(n*3));
    this.textures=Array.from({length:7},(_,i)=>{
      const texture=new THREE.Data3DTexture(new Float32Array(n*4),...this.resolution);
      texture.format=THREE.RGBAFormat;texture.type=THREE.FloatType;texture.minFilter=texture.magFilter=THREE.LinearFilter;
      texture.wrapS=texture.wrapT=texture.wrapR=THREE.ClampToEdgeWrapping;texture.unpackAlignment=1;
      texture.colorSpace=THREE.NoColorSpace;texture.name=i<6?`Opaline/Photon-volume-direction-${i}`:'Opaline/Photon-volume-total';return texture;
    });
    this.texture=this.textures[6];this.depositedEnergy=0;
  }
  reset(){for(const sum of this.sums)sum.fill(0);for(const texture of this.textures){texture.image.data.fill(0);texture.needsUpdate=true;}this.depositedEnergy=0;}
  interval(origin,direction,maxDistance=Infinity){
    let near=0,far=maxDistance;
    for(const axis of ['x','y','z']){
      if(Math.abs(direction[axis])<1e-12){if(origin[axis]<this.bounds.min[axis]||origin[axis]>this.bounds.max[axis])return null;continue;}
      const a=(this.bounds.min[axis]-origin[axis])/direction[axis],b=(this.bounds.max[axis]-origin[axis])/direction[axis];
      near=Math.max(near,Math.min(a,b));far=Math.min(far,Math.max(a,b));if(far<=near)return null;
    }
    return [near,far];
  }
  /** Exact voxel DDA path lengths; energy deposits are normalized by voxel volume. */
  segment(origin,direction,maxDistance,energy,channel,record=true){
    const interval=this.interval(origin,direction,maxDistance);if(!interval||this.extinction===0)return {energy,absorbed:0,scattered:0};
    const initial=energy,[entry,exit]=interval,axes=['x','y','z'];
    let t=entry;
    const p=origin.clone().addScaledVector(direction,entry+1e-8),cell=[],step=[],next=[],delta=[];
    for(let a=0;a<3;a++){
      const axis=axes[a],d=direction[axis];cell[a]=Math.max(0,Math.min(this.resolution[a]-1,Math.floor((p[axis]-this.bounds.min[axis])/this.cell[axis])));
      step[a]=d>=0?1:-1;delta[a]=Math.abs(d)<1e-12?Infinity:this.cell[axis]/Math.abs(d);
      next[a]=Math.abs(d)<1e-12?Infinity:(this.bounds.min[axis]+(cell[a]+(d>=0?1:0))*this.cell[axis]-origin[axis])/d;
    }
    const weights=[direction.x>0?direction.x**2:0,direction.x<0?direction.x**2:0,direction.y>0?direction.y**2:0,direction.y<0?direction.y**2:0,direction.z>0?direction.z**2:0,direction.z<0?direction.z**2:0];
    for(let guard=0;guard<this.resolution[0]+this.resolution[1]+this.resolution[2]+6&&t<exit-1e-10;guard++){
      const end=Math.min(exit,...next),length=Math.max(0,end-t),transmission=Math.exp(-this.extinction*length),scatter=energy*(1-transmission)*this.albedo;
      if(record&&scatter>0){
        const index=(cell[0]+this.resolution[0]*(cell[1]+this.resolution[1]*cell[2]))*3+channel;
        for(let bin=0;bin<6;bin++)if(weights[bin]>0)this.sums[bin][index]+=scatter*weights[bin]/this.voxelVolume;
        this.depositedEnergy+=scatter;
      }
      energy*=transmission;t=end;
      for(let a=0;a<3;a++)if(next[a]<=end+1e-10){cell[a]+=step[a];next[a]+=delta[a];}
      if(cell.some((v,a)=>v<0||v>=this.resolution[a]))break;
    }
    const loss=initial-energy;return {energy,absorbed:loss*(1-this.albedo),scattered:loss*this.albedo};
  }
  flush(scale){
    const total=this.texture.image.data;total.fill(0);
    for(let bin=0;bin<6;bin++){
      const data=this.textures[bin].image.data,sum=this.sums[bin];
      for(let i=0;i<sum.length/3;i++){for(let c=0;c<3;c++){data[i*4+c]=sum[i*3+c]*scale;total[i*4+c]+=data[i*4+c];}data[i*4+3]=1;total[i*4+3]=1;}
      this.textures[bin].needsUpdate=true;
    }
    this.texture.needsUpdate=true;
  }
  dispose(){for(const texture of this.textures)texture.dispose();}
}

/**
 * CPU geometric photon transport through actual current triangle meshes.
 * This is a deliberately expensive reference-quality component, independent of
 * the raster renderer's approximate transmission pass. It traces Snell/Fresnel,
 * Beer-Lambert attenuation, nested media and total internal reflection.
 *
 * The receiver needs nonoverlapping UVs. Irradiance is normalized by the local
 * world-area/UV-area Jacobian. Accumulation resets when geometry, transforms,
 * optics or source change. It is not a complete global illumination renderer:
 * optional split transport traces energy-weighted reflection branches. A supplied
 * PhotonVolume receives single-scattering photon segments; this is not full GI.
 */
export class RefractiveCaustics {
  constructor({refractors=[],reflectors=[],receiver,occluders=[],resolution=128,photonsPerUpdate=768,
    lightPosition=new THREE.Vector3(3,7,3),lightTarget=new THREE.Vector3(),
    aperture=new THREE.Vector2(8,8),irradiance=2,maxBounces=12,splatRadius=1.3,
    transport='transmission',volume=null,minPhotonEnergy=1e-5,maxBranches=128}={}){
    if(!receiver?.geometry)throw new TypeError('RefractiveCaustics requires a receiver mesh.');
    if(!receiver.geometry.attributes.uv)throw new TypeError('Caustic receiver geometry needs UV coordinates.');
    if(!['transmission','reflection','split'].includes(transport))throw new RangeError('transport must be transmission, reflection or split.');
    this.refractors=refractors;this.reflectors=reflectors;this.receiver=receiver;this.occluders=occluders;this.transport=transport;
    this.volume=volume instanceof PhotonVolume?volume:volume?new PhotonVolume(volume):null;
    this.volumeTexture=this.volume?.texture||null;this.volumeTextures=this.volume?.textures.slice(0,6)||[];
    this.minPhotonEnergy=minPhotonEnergy;this.maxBranches=maxBranches;this.energyLedger=freshLedger();
    this.resolution=Math.max(16,Math.floor(resolution));
    this.photonsPerUpdate=Math.max(1,Math.floor(photonsPerUpdate));
    this.lightPosition=lightPosition.clone();this.lightTarget=lightTarget.clone();this.aperture=aperture.clone();
    this.irradiance=irradiance;this.maxBounces=maxBounces;this.splatRadius=splatRadius;
    this.samples=0;this.deposited=0;this.transportEvents=0;this.revision=0;
    this._sum=new Float64Array(this.resolution*this.resolution*3);
    this._pixels=new Float32Array(this.resolution*this.resolution*4);
    this.texture=new THREE.DataTexture(this._pixels,this.resolution,this.resolution,THREE.RGBAFormat,THREE.FloatType);
    this.texture.name='Opaline/Geometric-photon-caustics';
    this.texture.minFilter=THREE.LinearFilter;this.texture.magFilter=THREE.LinearFilter;
    this.texture.wrapS=this.texture.wrapT=THREE.ClampToEdgeWrapping;
    this.texture.colorSpace=THREE.NoColorSpace;
    this._raycaster=new THREE.Raycaster();
    this._proxyMaterial=new THREE.MeshBasicMaterial({side:THREE.DoubleSide});
    this._proxies=[];this._sourceMap=new Map();this._geometryVersions=new WeakMap();
    this._normalMatrix=new THREE.Matrix3();
    this._refreshProxies();this._signature='';
  }

  _refreshProxies(){
    const sources=[...new Set([...this.refractors,...this.reflectors,this.receiver,...this.occluders])];
    this._proxies=sources.map(source=>{
      let proxy=this._sourceMap.get(source);
      if(!proxy){proxy=new THREE.Mesh(source.geometry,this._proxyMaterial);proxy.matrixAutoUpdate=false;proxy.userData.source=source;this._sourceMap.set(source,proxy);}
      proxy.geometry=source.geometry;
      source.updateWorldMatrix(true,false);
      proxy.matrixWorld.copy(source.matrixWorld);
      proxy.userData.kind=source===this.receiver?'receiver':this.occluders.includes(source)?'occluder':this.reflectors.includes(source)?'reflector':'refractor';
      const version=source.geometry.attributes.position.version;
      if(this._geometryVersions.get(source.geometry)!==version){
        source.geometry.computeBoundingSphere();source.geometry.computeBoundingBox();
        this._geometryVersions.set(source.geometry,version);
      }
      return proxy;
    });
  }

  _stateSignature(){
    const values=[...this.lightPosition,...this.lightTarget,...this.aperture,this.irradiance,this.transport];
    if(this.volume)values.push(...this.volume.bounds.min,...this.volume.bounds.max,this.volume.extinction,this.volume.albedo,this.volume.onlyCaustics);
    for(const p of this._proxies){
      const m=Array.isArray(p.userData.source.material)?p.userData.source.material[0]:p.userData.source.material;
      values.push(p.geometry.id,p.geometry.attributes.position.version,...p.matrixWorld.elements,m?.ior||0,m?.dispersion||0,m?.attenuationDistance||0,m?.attenuationColor?.getHex()||0,m?.metalness||0,m?.color?.getHex()||0,JSON.stringify(p.userData.source.userData.optics||{}));
    }
    return values.join(',');
  }

  reset(){this._sum.fill(0);this._pixels.fill(0);this.samples=0;this.deposited=0;this.transportEvents=0;this.revision++;this.texture.needsUpdate=true;this.volume?.reset();this.energyLedger=freshLedger();}

  _intersect(origin,direction){
    this._raycaster.set(origin,direction);this._raycaster.near=EPS*.25;
    return this._raycaster.intersectObjects(this._proxies,false)[0]||null;
  }

  _pixelArea(hit){
    const {a,b,c}=hit.face,g=hit.object.geometry;
    const pa=new THREE.Vector3().fromBufferAttribute(g.attributes.position,a).applyMatrix4(hit.object.matrixWorld);
    const pb=new THREE.Vector3().fromBufferAttribute(g.attributes.position,b).applyMatrix4(hit.object.matrixWorld);
    const pc=new THREE.Vector3().fromBufferAttribute(g.attributes.position,c).applyMatrix4(hit.object.matrixWorld);
    const area=pb.sub(pa).cross(pc.sub(pa)).length()*.5;
    const ua=new THREE.Vector2().fromBufferAttribute(g.attributes.uv,a);
    const ub=new THREE.Vector2().fromBufferAttribute(g.attributes.uv,b).sub(ua);
    const uc=new THREE.Vector2().fromBufferAttribute(g.attributes.uv,c).sub(ua);
    const uvArea=Math.abs(ub.x*uc.y-ub.y*uc.x)*.5;
    return uvArea>1e-12?area/uvArea/(this.resolution*this.resolution):Infinity;
  }

  _deposit(hit,channel,energy){
    if(!hit.uv||hit.uv.x<0||hit.uv.x>1||hit.uv.y<0||hit.uv.y>1)return;
    const area=this._pixelArea(hit);if(!Number.isFinite(area)||area<=1e-12)return;
    const r=this.resolution,x=hit.uv.x*(r-1),y=hit.uv.y*(r-1),radius=this.splatRadius;
    const minX=Math.max(0,Math.floor(x-radius*2)),maxX=Math.min(r-1,Math.ceil(x+radius*2));
    const minY=Math.max(0,Math.floor(y-radius*2)),maxY=Math.min(r-1,Math.ceil(y+radius*2));
    let weightSum=0;
    for(let py=minY;py<=maxY;py++)for(let px=minX;px<=maxX;px++)weightSum+=Math.exp(-((px-x)**2+(py-y)**2)/(2*radius*radius));
    for(let py=minY;py<=maxY;py++)for(let px=minX;px<=maxX;px++){
      const w=Math.exp(-((px-x)**2+(py-y)**2)/(2*radius*radius))/weightSum;
      this._sum[(py*r+px)*3+channel]+=energy*w/area;
    }
    this.deposited++;
  }

  _trace(origin,direction,channel){
    const pending=[{origin,direction,energy:1,stack:[],depth:0,hadRefraction:false,hadReflection:false}],ledger=this.energyLedger;
    ledger.launched++;let branches=0;
    const enqueue=packet=>{
      if(packet.energy<this.minPhotonEnergy||packet.depth>=this.maxBounces||branches>=this.maxBranches){ledger.cutoff+=packet.energy;return;}
      pending.push(packet);branches++;
    };
    while(pending.length){
      const packet=pending.pop();let {origin,direction,energy,stack,depth,hadRefraction,hadReflection}=packet;
      const hit=this._intersect(origin,direction),medium=stack[stack.length-1];
      const segmentLength=hit?.distance??this.volume?.interval(origin,direction)?.[1]??0;
      if(medium){
        const attenuation=medium.material.attenuationColor;
        const distance=medium.material.attenuationDistance;
        if(attenuation&&distance>0&&Number.isFinite(distance)){
          const absorptionColor=[attenuation.r,attenuation.g,attenuation.b][channel];
          const previous=energy;energy*=Math.pow(Math.max(1e-6,absorptionColor),segmentLength/distance);ledger.absorbed+=previous-energy;
        }
      }
      // Mist occupies the exterior medium, not the interior of dielectric solids.
      if(this.volume&&!medium){
        const result=this.volume.segment(origin,direction,segmentLength,energy,channel,!this.volume.onlyCaustics||hadRefraction||hadReflection);
        energy=result.energy;ledger.absorbed+=result.absorbed;ledger.scattered+=result.scattered;
      }
      if(!hit){ledger.escaped+=energy;continue;}
      if(hit.object.userData.kind==='occluder'){ledger.absorbed+=energy;continue;}
      if(hit.object.userData.kind==='receiver'){
        const eligible=this.transport==='reflection'?hadReflection:this.transport==='transmission'?hadRefraction:hadRefraction||hadReflection;
        if(eligible){this._deposit(hit,channel,energy);ledger.receiver+=energy;}else ledger.discarded+=energy;
        continue;
      }
      const material=materialForHit(hit);
      const geometricNormal=hit.face.normal.clone().applyMatrix3(this._normalMatrix.getNormalMatrix(hit.object.matrixWorld)).normalize();
      const entering=direction.dot(geometricNormal)<0;
      const normal=entering?geometricNormal:geometricNormal.negate();
      const cosIncident=Math.min(1,Math.max(0,-direction.dot(normal)));
      if(hit.object.userData.kind==='reflector'){
        const override=hit.object.userData.source.userData.optics?.reflectance;
        const color=[material.color?.r??1,material.color?.g??1,material.color?.b??1][channel];
        const fresnel=fresnelDielectric(cosIncident,medium?spectralIor(medium.material,channel):AIR,spectralIor(material,channel));
        const metal=material.metalness||0;
        const reflectance=Math.max(0,Math.min(1,Array.isArray(override)?override[channel]:typeof override==='number'?override:metal*color+(1-metal)*fresnel));
        const reflected=direction.clone().reflect(normal).normalize();ledger.absorbed+=energy*(1-reflectance);
        enqueue({origin:hit.point.clone().addScaledVector(reflected,EPS),direction:reflected,energy:energy*reflectance,stack:stack.slice(),depth:depth+1,hadRefraction,hadReflection:true});
        this.transportEvents++;continue;
      }
      let n1=medium?spectralIor(medium.material,channel):AIR,n2;
      const proposed=stack.slice();
      if(entering){n2=spectralIor(material,channel);proposed.push({mesh:hit.object,material});}
      else{
        const index=proposed.findLastIndex(m=>m.mesh===hit.object);
        if(index>=0)proposed.splice(index,1);
        else n1=spectralIor(material,channel); // origin was inside a boundary
        n2=proposed.length?spectralIor(proposed[proposed.length-1].material,channel):AIR;
      }
      const transmitted=refractDirection(direction,normal,n1,n2);
      const fresnel=transmitted?fresnelDielectric(cosIncident,n1,n2):1;
      const reflect=()=>{const reflected=direction.clone().reflect(normal).normalize();enqueue({origin:hit.point.clone().addScaledVector(reflected,EPS),direction:reflected,energy:energy*fresnel,stack:stack.slice(),depth:depth+1,hadRefraction,hadReflection:true});};
      if(this.transport==='split'||this.transport==='reflection'||!transmitted)reflect();else ledger.discarded+=energy*fresnel;
      if(transmitted){
        if(this.transport!=='reflection')enqueue({origin:hit.point.clone().addScaledVector(transmitted,EPS),direction:transmitted,energy:energy*(1-fresnel),stack:proposed,depth:depth+1,hadRefraction:true,hadReflection});
        else ledger.discarded+=energy*(1-fresnel);
      }
      this.transportEvents++;
    }
  }

  /** Each call adds one deterministic photon batch; changed scenes invalidate accumulation. */
  update(_time=0,{photons=this.photonsPerUpdate,forceReset=false}={}){
    this._refreshProxies();
    const signature=this._stateSignature();
    if(forceReset||signature!==this._signature){this.reset();this._signature=signature;}
    const direction=this.lightTarget.clone().sub(this.lightPosition).normalize();
    if(direction.lengthSq()<.5)return this.texture;
    const up=Math.abs(direction.y)>.95?new THREE.Vector3(1,0,0):new THREE.Vector3(0,1,0);
    const right=new THREE.Vector3().crossVectors(direction,up).normalize();
    const vertical=new THREE.Vector3().crossVectors(right,direction).normalize();
    for(let i=0;i<photons;i++){
      const index=this.samples+i+1;
      const u=radicalInverse(index,2)-.5,v=radicalInverse(index,3)-.5;
      const origin=this.lightPosition.clone().addScaledVector(right,u*this.aperture.x).addScaledVector(vertical,v*this.aperture.y);
      // Transport all three spectral bands through the same incident sample.
      // Choosing index%3 would correlate color with the base-3 Halton dimension.
      for(let channel=0;channel<3;channel++)this._trace(origin.clone(),direction.clone(),channel);
    }
    this.samples+=photons;
    const scale=this.irradiance*this.aperture.x*this.aperture.y/Math.max(1,this.samples);
    for(let i=0;i<this._sum.length/3;i++){
      this._pixels[i*4]=this._sum[i*3]*scale;
      this._pixels[i*4+1]=this._sum[i*3+1]*scale;
      this._pixels[i*4+2]=this._sum[i*3+2]*scale;
      this._pixels[i*4+3]=1;
    }
    this.texture.needsUpdate=true;
    this.volume?.flush(scale);
    return this.texture;
  }

  get statistics(){return {samples:this.samples,deposited:this.deposited,transportEvents:this.transportEvents,revision:this.revision,volumeDepositedEnergy:this.volume?.depositedEnergy||0,energy:{...this.energyLedger}};}
  dispose(){this.texture.dispose();this.volume?.dispose();this._proxyMaterial.dispose();this._proxies.length=0;this._sourceMap.clear();}
}

/** Photon-driven single-scattering volume, six directional HG phase quadratures. */
export function createVolumeCausticsMaterial(transport,{anisotropy=.35,intensity=1,steps=128,depthTexture=null}={}){
  const volume=transport instanceof PhotonVolume?transport:transport.volume;
  if(!volume)throw new TypeError('Volume-caustic material needs a PhotonVolume or transport with volume enabled.');
  const uniforms={uBoundsMin:{value:volume.bounds.min},uBoundsMax:{value:volume.bounds.max},uExtinction:{value:volume.extinction},uAnisotropy:{value:Math.max(-.9,Math.min(.9,anisotropy))},uIntensity:{value:intensity},uSceneDepth:{value:depthTexture},uUseDepth:{value:depthTexture?1:0},uResolution:{value:new THREE.Vector2(1,1)},uProjectionInverse:{value:new THREE.Matrix4()},uCameraWorld:{value:new THREE.Matrix4()}};
  for(let i=0;i<6;i++)uniforms[`uVolume${i}`]={value:volume.textures[i]};
  const material=new THREE.ShaderMaterial({name:'Opaline/Photon-volume-caustics',glslVersion:THREE.GLSL3,transparent:true,depthWrite:false,side:THREE.BackSide,uniforms,
    vertexShader:`out vec3 vPhotonWorld;void main(){vec4 world=modelMatrix*vec4(position,1.0);vPhotonWorld=world.xyz;gl_Position=projectionMatrix*viewMatrix*world;}`,
    fragmentShader:`precision highp float;precision highp sampler3D;
in vec3 vPhotonWorld;out vec4 photonOutput;
#define gl_FragColor photonOutput
uniform sampler3D uVolume0,uVolume1,uVolume2,uVolume3,uVolume4,uVolume5;
uniform vec3 uBoundsMin,uBoundsMax;uniform float uExtinction,uAnisotropy,uIntensity;
uniform sampler2D uSceneDepth;uniform float uUseDepth;uniform vec2 uResolution;uniform mat4 uProjectionInverse,uCameraWorld;
float phase(float mu){float g=uAnisotropy;return (1.0-g*g)/(12.5663706*pow(max(.001,1.0+g*g-2.0*g*mu),1.5));}
void main(){
 vec3 ro=cameraPosition,rd=normalize(vPhotonWorld-ro),inv=1.0/(rd+vec3(1e-8));
 vec3 a=(uBoundsMin-ro)*inv,b=(uBoundsMax-ro)*inv,lo=min(a,b),hi=max(a,b);
 float start=max(0.0,max(max(lo.x,lo.y),lo.z)),end=min(min(hi.x,hi.y),hi.z);
 if(uUseDepth>.5){float depth=texture(uSceneDepth,gl_FragCoord.xy/uResolution).r;vec4 view=uProjectionInverse*vec4(gl_FragCoord.xy/uResolution*2.0-1.0,depth*2.0-1.0,1.0);view/=view.w;vec3 surface=(uCameraWorld*view).xyz;end=min(end,dot(surface-ro,rd));}
 if(end<=start)discard;
 float ds=(end-start)/float(${Math.max(32,Math.min(512,Math.floor(steps)))}),Tr=1.0;vec3 L=vec3(0.0);
 float segmentT=exp(-uExtinction*ds),integral=uExtinction>.00001?(1.0-segmentT)/uExtinction:ds;
 for(int i=0;i<${Math.max(32,Math.min(512,Math.floor(steps)))};i++){
  vec3 p=(ro+rd*(start+(float(i)+.5)*ds)-uBoundsMin)/(uBoundsMax-uBoundsMin);
  vec3 source=texture(uVolume0,p).rgb*phase(-rd.x)+texture(uVolume1,p).rgb*phase(rd.x)+texture(uVolume2,p).rgb*phase(-rd.y)+texture(uVolume3,p).rgb*phase(rd.y)+texture(uVolume4,p).rgb*phase(-rd.z)+texture(uVolume5,p).rgb*phase(rd.z);
  L+=Tr*source*integral*uIntensity;Tr*=segmentT;if(Tr<.001)break;
 }
 float alpha=1.0-Tr;gl_FragColor=vec4(L/max(alpha,.00001),alpha);
 #include <tonemapping_fragment>
 #include <colorspace_fragment>
}`});
  material.userData.bindVolume=(mesh)=>{const previous=mesh.onBeforeRender;mesh.onBeforeRender=function(renderer,scene,camera,...rest){previous?.call(this,renderer,scene,camera,...rest);uniforms.uExtinction.value=volume.extinction;uniforms.uProjectionInverse.value.copy(camera.projectionMatrixInverse);uniforms.uCameraWorld.value.copy(camera.matrixWorld);renderer.getDrawingBufferSize(uniforms.uResolution.value);};return mesh;};
  return material;
}

/** Adds Lambertian response to a receiver without replacing its existing PBR shader. */
export function attachCaustics(material,texture,strength=1){
  const previousCompile=material.onBeforeCompile;
  const previousKey=material.customProgramCacheKey?.bind(material);
  const oldKey=previousKey?previousKey():'';
  const uniforms={uOpCaustics:{value:texture},uOpCausticStrength:{value:strength}};
  material.onBeforeCompile=function(shader,...args){
    previousCompile?.call(this,shader,...args);
    Object.assign(shader.uniforms,uniforms);
    shader.vertexShader='varying vec2 vOpCausticUV;\n'+shader.vertexShader;
    shader.vertexShader=shader.vertexShader.replace('#include <begin_vertex>','#include <begin_vertex>\nvOpCausticUV=uv;');
    shader.fragmentShader='varying vec2 vOpCausticUV;\nuniform sampler2D uOpCaustics;\nuniform float uOpCausticStrength;\n'+shader.fragmentShader;
    shader.fragmentShader=shader.fragmentShader.replace('#include <opaque_fragment>',
      'outgoingLight+=diffuseColor.rgb*texture2D(uOpCaustics,vOpCausticUV).rgb*uOpCausticStrength/3.14159265;\n#include <opaque_fragment>');
  };
  // Capture the old string immediately: Material's default key reads onBeforeCompile.
  material.customProgramCacheKey=()=>`${oldKey}:opaline-photon-receiver-v1`;
  material.userData.caustics=uniforms;material.needsUpdate=true;
  return uniforms;
}
