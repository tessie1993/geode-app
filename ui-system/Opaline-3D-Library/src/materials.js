import * as THREE from 'three';
import {SURFACE_DECLARATIONS,SURFACE_COLOR,SURFACE_ROUGHNESS,SURFACE_EMISSION,SURFACE_FILM,VOLUME_VERTEX,VOLUME_FRAGMENT} from './shaders/opaline.js';

/** Scene-wide art direction. Colors are authored in sRGB; Three converts to linear. */
export const THEMES = Object.freeze({
  tidal:{name:'Tidal',gel:'#99cedd',blue:'#448dc1',deep:'#285f80',accent:'#a8fff1',water:'#5baaaa',stone:'#526d78',leaf:'#467c66',background:'#122b3c'},
  opal:{name:'Opal',gel:'#f1e5f3',blue:'#9daede',deep:'#c99fc6',accent:'#fff0c9',water:'#95bdc9',stone:'#a8a8b4',leaf:'#77978c',background:'#252c44'},
  moss:{name:'Moss',gel:'#cee7cb',blue:'#77b8a3',deep:'#426c55',accent:'#d9f8a7',water:'#739e8a',stone:'#5e7265',leaf:'#7fa665',background:'#162f28'},
  obsidian:{name:'Obsidian',gel:'#788296',blue:'#657ca8',deep:'#27384c',accent:'#a3dcf0',water:'#405c70',stone:'#303a47',leaf:'#476477',background:'#0b111d'},
  aurora:{name:'Aurora',gel:'#d0cef5',blue:'#8da5ef',deep:'#8371bd',accent:'#a0ffe0',water:'#668ba9',stone:'#616c86',leaf:'#829c99',background:'#171e39'},
  amber:{name:'Amber',gel:'#f0d7af',blue:'#d4ab7c',deep:'#b77744',accent:'#ffe4a3',water:'#92b5a4',stone:'#877668',leaf:'#8b976a',background:'#302c2a'},
});

function physical(name,parameters,theme,{cloud=0,flow=0,grain=0}={}){
  const mat=new THREE.MeshPhysicalMaterial(parameters);
  mat.name=`Opaline/${theme.name}/${name}`;
  const uniforms={
    uOpTime:{value:0},uOpFlow:{value:flow},uOpCloud:{value:cloud},uOpGrain:{value:grain},
    uOpFilm:{value:name==='film'?1:name==='nacre'?.4:0},
    uOpExcitation:{value:0},uOpRadius:{value:.42},uOpTouch:{value:new THREE.Vector3(0,0,0)},
    uOpVelocity:{value:new THREE.Vector3()},uOpAccent:{value:new THREE.Color(theme.accent)},uOpDeep:{value:new THREE.Color(theme.deep)}
  };
  mat.userData.opaline={uniforms,family:name,theme:theme.name};
  mat.userData.setInteraction=(point,strength=0,velocity)=>{
    if(point)uniforms.uOpTouch.value.copy(point);
    if(velocity)uniforms.uOpVelocity.value.copy(velocity);
    uniforms.uOpExcitation.value=Math.max(0,Math.min(4,Number(strength)||0));
  };
  mat.onBeforeCompile=shader=>{
    Object.assign(shader.uniforms,uniforms);
    shader.vertexShader='varying vec3 vOpPosition;\n#ifdef OPALINE_FILM_THICKNESS\nattribute float filmThickness;\nvarying float vOpFilmThickness;\n#endif\n'+shader.vertexShader;
    shader.vertexShader=shader.vertexShader.replace('#include <begin_vertex>','#include <begin_vertex>\nvOpPosition=position;\n#ifdef OPALINE_FILM_THICKNESS\nvOpFilmThickness=filmThickness;\n#endif');
    shader.fragmentShader=SURFACE_DECLARATIONS+shader.fragmentShader;
    shader.fragmentShader=shader.fragmentShader.replace('#include <color_fragment>','#include <color_fragment>\n'+SURFACE_COLOR);
    shader.fragmentShader=shader.fragmentShader.replace('#include <roughnessmap_fragment>','#include <roughnessmap_fragment>\n'+SURFACE_ROUGHNESS);
    shader.fragmentShader=shader.fragmentShader.replace('#include <emissivemap_fragment>','#include <emissivemap_fragment>\n'+SURFACE_EMISSION);
    shader.fragmentShader=shader.fragmentShader.replace('#include <lights_physical_fragment>','#include <lights_physical_fragment>\n'+SURFACE_FILM);
    mat.userData.shader=shader;
  };
  mat.customProgramCacheKey=()=>`opaline-surface-v3:${name}`;
  return mat;
}

/**
 * Distinct physical material families, deliberately not one universal glass.
 * Thickness is in local mesh units: per-instance optical thickness should track geometry.
 * Use cloneMaterial() rather than Material.clone() to retain custom shader callbacks.
 */
export function createMaterials(theme='tidal'){
  const t=typeof theme==='string'?(THEMES[theme.toLowerCase()]||THEMES.tidal):{...THEMES.tidal,...theme};
  const pearl=t.name==='Opal';
  const shared={metalness:0,envMapIntensity:1.25};
  return {
    gel:physical('gel',{...shared,color:t.gel,roughness:pearl?.24:.15,transmission:pearl?.54:.74,thickness:.65,ior:1.39,attenuationColor:t.gel,attenuationDistance:1.25,clearcoat:.8,clearcoatRoughness:pearl?.13:.085,iridescence:.16,iridescenceIOR:1.29,iridescenceThicknessRange:[210,390],dispersion:.10},t,{cloud:pearl?.42:.18,flow:.34,grain:.025}),
    blue:physical('blue',{...shared,color:t.blue,roughness:pearl?.17:.12,transmission:pearl?.65:.78,thickness:.85,ior:1.4,attenuationColor:t.blue,attenuationDistance:1.1,clearcoat:.9,clearcoatRoughness:.07,iridescence:.12,dispersion:.11},t,{cloud:pearl?.36:.14,flow:.23,grain:.018}),
    water:physical('water',{...shared,color:'#e1f8fa',roughness:.045,transmission:.97,thickness:1.4,ior:1.333,attenuationColor:t.water,attenuationDistance:4.5,clearcoat:.35,clearcoatRoughness:.035,dispersion:.045},t),
    shell:physical('shell',{...shared,color:'#f2fbff',roughness:.065,transmission:1,thickness:.085,ior:1.46,attenuationColor:t.gel,attenuationDistance:8,clearcoat:1,clearcoatRoughness:.045,dispersion:.13},t,{grain:.01}),
    pigment:physical('pigment',{...shared,color:t.blue,roughness:.2,transmission:.38,thickness:.55,ior:1.37,attenuationColor:t.deep,attenuationDistance:.8,clearcoat:.6,clearcoatRoughness:.13},t,{cloud:.78,flow:1.1,grain:.022}),
    film:physical('film',{...shared,color:'#f8fdff',roughness:.035,transmission:1,thickness:.0008,ior:1.333,iridescence:1,iridescenceIOR:1.333,iridescenceThicknessRange:[130,590],side:THREE.DoubleSide,clearcoat:1,clearcoatRoughness:.025},t,{cloud:.015,flow:.2}),
    nacre:physical('nacre',{...shared,color:t.gel,roughness:.24,transmission:.27,thickness:.45,ior:1.52,attenuationColor:t.gel,attenuationDistance:1.1,iridescence:.72,iridescenceIOR:1.36,iridescenceThicknessRange:[170,450],clearcoat:.8,clearcoatRoughness:.12},t,{cloud:.27,flow:.07,grain:.042}),
    stone:physical('stone',{...shared,color:t.stone,roughness:.69,transmission:0,clearcoat:.74,clearcoatRoughness:.14},t,{cloud:.25,grain:.25}),
    leaf:physical('leaf',{...shared,color:t.leaf,roughness:.52,transmission:.2,thickness:.015,ior:1.35,attenuationColor:t.leaf,attenuationDistance:.1,side:THREE.DoubleSide,sheen:.15,sheenColor:t.gel,sheenRoughness:.8,clearcoat:.15,clearcoatRoughness:.3},t,{cloud:.23,grain:.12}),
    glow:physical('glow',{...shared,color:t.accent,roughness:.2,transmission:.35,thickness:.15,ior:1.37,emissive:t.accent,emissiveIntensity:2.4,clearcoat:1,clearcoatRoughness:.08},t,{cloud:.18,flow:.5}),
  };
}

/** Returns an independently animated instance without dropping onBeforeCompile. */
export function cloneMaterial(source){
  const state=source.userData.opaline;
  if(!state)return source.clone();
  const palette=createMaterials(state.theme.toLowerCase());
  const clone=palette[state.family];
  for(const material of Object.values(palette))if(material!==clone)material.dispose();
  // Preserve per-instance physical overrides, while retaining fresh uniform objects.
  for(const key of ['roughness','metalness','transmission','thickness','ior','attenuationDistance','clearcoat','clearcoatRoughness','iridescence','dispersion','envMapIntensity','emissiveIntensity','side'])clone[key]=source[key];
  clone.color.copy(source.color);clone.attenuationColor.copy(source.attenuationColor);clone.emissive.copy(source.emissive);
  clone.envMap=source.envMap;
  clone.defines={...source.defines};
  for(const [key,u] of Object.entries(state.uniforms)){
    const dest=clone.userData.opaline.uniforms[key];
    if(dest.value?.copy)dest.value.copy(u.value);else dest.value=u.value;
  }
  return clone;
}

/** Enable physically evolved film thickness (metres) from geometry attribute filmThickness. */
export function enableFilmThickness(material){
  if(!material.userData.opaline)throw new TypeError('enableFilmThickness requires an Opaline material.');
  material.defines={...material.defines,OPALINE_FILM_THICKNESS:1};
  material.needsUpdate=true;
  material.userData.filmThicknessUnit='metres';
  return material;
}

export function updateMaterials(materials,time,interaction){
  const list=Array.isArray(materials)?materials:Object.values(materials);
  for(const mat of list){
    if(!mat?.isMaterial)continue;
    const u=mat.userData.opaline?.uniforms;
    if(u)u.uOpTime.value=Number.isFinite(time)?time:0;
    if(interaction&&mat.userData.setInteraction)mat.userData.setInteraction(interaction.point,interaction.strength,interaction.velocity);
    if(mat.userData.opaline?.family==='film'){
      // Slow global drainage baseline. Full spatial drainage can replace the thickness map.
      const drift=25*Math.sin(time*.09);
      mat.iridescenceThicknessRange=[130+drift,590+drift];
    }
    if(mat.uniforms?.uTime)mat.uniforms.uTime.value=time;
  }
}

/**
 * Genuine bounded 3D participating medium: 96 view samples, 8 light samples,
 * Beer-Lambert extinction and Henyey-Greenstein single scattering. Put on a box
 * with half-extents `bounds`, inside a separate refractive shell. The implicit
 * density boundary is ellipsoidal or box-shaped; it does not infer arbitrary meshes.
 * This is not multiple scattering or a recursive transparent-object path tracer.
 */
export function createVolumeMaterial({color='#c3eff2',absorption='#2d5360',density=1.6,bounds=[1,1,1],lightPosition=[4,6,3],lightColor='#fff5e4',shape='ellipsoid',anisotropy=.35}={}){
  const mat=new THREE.ShaderMaterial({
    name:'Opaline/Participating-medium',vertexShader:VOLUME_VERTEX,fragmentShader:VOLUME_FRAGMENT,
    side:THREE.BackSide,transparent:true,depthWrite:false,
    uniforms:{uWorldToLocal:{value:new THREE.Matrix4()},uBounds:{value:new THREE.Vector3(...bounds)},uScatterColor:{value:new THREE.Color(color)},uAbsorption:{value:new THREE.Color(absorption)},uLightPosition:{value:new THREE.Vector3(...lightPosition)},uLightColor:{value:new THREE.Color(lightColor).multiplyScalar(12)},uAmbient:{value:new THREE.Vector3(.12,.16,.19)},uDensity:{value:density},uTime:{value:0},uAnisotropy:{value:anisotropy},uShape:{value:shape==='box'?1:0},uExcitation:{value:0},uTouch:{value:new THREE.Vector3()}}
  });
  mat.userData.bindVolume=mesh=>{
    const original=mesh.onBeforeRender;
    mesh.onBeforeRender=function(...args){
      original?.apply(this,args);
      mat.uniforms.uWorldToLocal.value.copy(mesh.matrixWorld).invert();
    };
    return mesh;
  };
  mat.userData.setInteraction=(point,strength=0)=>{
    if(point)mat.uniforms.uTouch.value.copy(point);
    mat.uniforms.uExcitation.value=strength;
  };
  return mat;
}
