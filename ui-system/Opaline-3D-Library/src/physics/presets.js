import {ParticleSystem,WindField,VortexField,RadialField,VortexRingField,BuoyancyField} from './particles.js';
import {PlaneCollider} from './common.js';
export const FIELD_PRESETS={
 H01:{name:'Shared wind volume',kind:'wind',options:{strength:.45,gust:.25}},
 H02:{name:'Directional gust',kind:'wind',options:{direction:[1,.2,.15],strength:1.1,gust:.95,frequency:1.7,turbulence:.04}},
 H03:{name:'Crosswind shear',kind:'wind',options:{strength:.2,shear:1.1,gust:.05,turbulence:0}},
 H04:{name:'Vortex column',kind:'vortex',options:{strength:1.4,radial:.5,lift:.4}},
 H05:{name:'Vortex ring',kind:'ring',options:{radius:.8,core:.5,strength:1.3,speed:.05}},
 H06:{name:'Obstacle wake study',kind:'wind',options:{strength:.7,gust:.4,turbulence:.8,scale:5},limitation:'Prescribed correlated wake-like forcing; not a resolved obstacle-flow simulation.'},
 H07:{name:'Touch attractor',kind:'radial',options:{strength:2,radius:2,attract:true}},
 H08:{name:'Touch repulsor',kind:'radial',options:{strength:4,radius:2,attract:false}},
 H09:{name:'Surface current',kind:'wind',options:{direction:[.8,0,.5],strength:.4,gust:.1,turbulence:0},limitation:'World-space tangent-direction current; host must update direction from surface.'},
 H10:{name:'Buoyancy region',kind:'buoyancy',options:{surfaceY:.5,bodyDensity:650}},
 H11:{name:'Quiet volume',kind:'quiet',options:{strength:0,turbulence:0},limitation:'Pair with raised system drag; spatial drag blending is host controlled.'},
 H12:{name:'Turbulent inflow',kind:'wind',options:{direction:[.5,.05,.2],strength:.7,gust:.6,frequency:1.2,turbulence:1,scale:3},limitation:'Analytic divergence-free trigonometric disturbance, not a turbulence cascade solver.'}
};
export function createFieldPreset(id,overrides={}){const preset=FIELD_PRESETS[id];if(!preset)throw new RangeError(`Unknown field preset ${id}`);const C={wind:WindField,vortex:VortexField,ring:VortexRingField,radial:RadialField,buoyancy:BuoyancyField,quiet:WindField}[preset.kind];const field=new C({...preset.options,...overrides});field.metadata={id,name:preset.name,limitation:preset.limitation??null};return field;}
export const PARTICLE_PRESETS={
 G01:{name:'Glow floaters',shape:'sphere',radius:.024,count:96,color:[.2,.85,1],emission:3,gravity:[0,.02,0],field:'H01',drag:1.2},
 G02:{name:'Clear microbeads',shape:'sphere',radius:.025,count:112,color:[.62,.89,1],transmission:1,field:'H09',gravity:[0,-.03,0]},
 G03:{name:'Milky microbeads',shape:'sphere',radius:.034,count:80,color:[.84,.84,1],field:'H03',gravity:[0,-.1,0]},
 G04:{name:'Pearl seeds',shape:'ellipsoid',radius:.036,count:64,color:[.9,.79,.99],field:'H01',spin:2,gravity:[0,-.05,0]},
 G05:{name:'Botanical spores',shape:'seed',radius:.014,count:160,color:[.56,.94,.77],field:'H02',spread:.2,gravity:[0,-.025,0],drag:1.1},
 G06:{name:'Petal fragments',shape:'petal',radius:.04,count:52,color:[.91,.66,.87],field:'H12',spin:5,gravity:[0,-.08,0],limitation:'Prescribed tumble angular velocity; full aerodynamic torque requires a shell body.'},
 G07:{name:'Glowing seed tails',shape:'seed',radius:.026,count:64,color:[.46,.93,1],field:'H04',emission:2,trailLength:20,gravity:[0,0,0]},
 G08:{name:'Buoyant bubbles',shape:'sphere',radius:.045,count:64,color:[.75,.92,1],transmission:1,field:'H10',gravity:[0,-9.81,0],drag:2,limitation:'Buoyant particle spheres, not explicit gas-film surfaces.'},
 G09:{name:'Impact spray',shape:'sphere',radius:.02,count:84,color:[.55,.86,1],field:'H01',gravity:[0,-4,0],burst:true,restitution:.25},
 G10:{name:'Pinch-off satellites',shape:'sphere',radius:.012,count:80,color:[.45,.7,1],field:'H03',gravity:[0,-.8,0],spread:.35,limitation:'Emission accepts supplied source velocity; automatic topology/pinch-off detection is not included.'},
 G11:{name:'Mist condensate',shape:'sphere',radius:.01,count:170,color:[.84,.94,1],field:'H01',gravity:[0,-.2,0],drag:1.5,limitation:'Droplet dynamics; no humidity/phase-change solver.'},
 G12:{name:'Contact shedding',shape:'seed',radius:.019,count:80,color:[.61,.76,1],field:'H02',gravity:[0,-1.2,0],burst:true,limitation:'Host collision event must call burst() with contact velocity.'},
 G13:{name:'Orbital particle band',shape:'sphere',radius:.02,count:96,color:[.39,.81,1],field:'H04',gravity:[0,0,0],ring:true,trailLength:12},
 G14:{name:'Wake tracers',shape:'sphere',radius:.012,count:120,color:[.54,1,.9],field:'H06',gravity:[0,0,0],trailLength:24,limitation:'Analytic wake forcing unless host supplies simulated velocity/force field.'},
 G15:{name:'Adhesive bead scatter',shape:'sphere',radius:.026,count:92,color:[.63,.75,1],field:'H01',gravity:[0,-1,0],adhesionAcceleration:2,stickSpeed:2,limitation:'Threshold adhesion law to static colliders; no surface-energy wetting model.'},
 G16:{name:'Elastic bead cluster',shape:'sphere',radius:.056,count:48,color:[.57,.86,1],field:'H07',gravity:[0,-.03,0],spread:.4,restitution:.8},
 G17:{name:'Particle tether chain',shape:'sphere',radius:.026,count:36,color:[.46,.92,1],field:'H02',gravity:[0,-.25,0],chain:true,trailLength:0},
 G18:{name:'Collision burst emitter',shape:'sphere',radius:.016,count:112,color:[.44,.86,1],field:'H01',gravity:[0,-1.8,0],burst:true,emission:1.5}
};
export function createParticlePreset(id,{count,seed=32,origin=[0,.6,0],colliders,fields,...overrides}={}){const preset=PARTICLE_PRESETS[id];if(!preset)throw new RangeError(`Unknown particle preset ${id}`);const n=count??preset.count;const system=new ParticleSystem({capacity:Math.max(n*3,256),seed,gravity:preset.gravity,drag:preset.drag??.7,fields:fields??[createFieldPreset(preset.field)],colliders:colliders??[new PlaneCollider({offset:-.6,restitution:preset.restitution??.25})],restitution:preset.restitution??.35,trailLength:preset.trailLength??0,...overrides});system.metadata={id,...preset,implementation:'3D particle dynamics with authored parameters'};system.adhesionAcceleration=preset.adhesionAcceleration??0;system.stickSpeed=preset.stickSpeed??0;const r=system.random;for(let i=0;i<n;i++){const angle=i/n*Math.PI*2,s=preset.spread??1,position=preset.ring?[origin[0]+Math.cos(angle)*.65,origin[1]+(r()-.5)*.2,origin[2]+Math.sin(angle)*.65]:preset.chain?[origin[0]+(i/(n-1)-.5)*1.8,origin[1],origin[2]]:origin.map((v,a)=>v+(r()-.5)*s*(a===1?.9:1.6));const velocity=preset.burst?[(r()-.5)*1.4,.8+r()*2,(r()-.5)*1.4]:[(r()-.5)*.15,(r()-.5)*.15,(r()-.5)*.15];system.emit({position,velocity,radius:preset.radius*(.7+r()*.6),color:preset.color,mass:.001,spin:[r()-.5,r()-.5,r()-.5].map(v=>v*(preset.spin??.4))});}if(preset.chain){const links=[];for(let i=0;i<n-1;i++)links.push([i,i+1]);system.connect(links,1e-7);}return system;}
