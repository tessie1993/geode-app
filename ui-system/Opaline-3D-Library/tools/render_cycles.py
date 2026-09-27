#!/usr/bin/env python3
"""Render Opaline GLBs with Blender Cycles (4.5+).

blender -b --python tools/render_cycles.py -- --ids A01 B01 C03 C10 E08 F01
blender -b --python tools/render_cycles.py -- --specimen --samples 256 --size 1600
blender -b --python tools/render_cycles.py -- --all --views front threequarter underside

No image assets are used as geometry or as a substitute for light transport.
This is an offline reference pipeline, independent of the web authoring viewer.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import struct
import sys
import time

import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[1]
THEMES = {
    "tidal": {"gel":"c6f0f3", "blue":"549fd8", "deep":"2d709c", "accent":"a8fff1", "water":"5baaaa", "stone":"526d78", "leaf":"467c66", "background":"122b3c"},
    "opal": {"gel":"f1e5f3", "blue":"9daede", "deep":"c99fc6", "accent":"fff0c9", "water":"95bdc9", "stone":"a8a8b4", "leaf":"77978c", "background":"252c44"},
    "moss": {"gel":"cee7cb", "blue":"77b8a3", "deep":"426c55", "accent":"d9f8a7", "water":"739e8a", "stone":"5e7265", "leaf":"7fa665", "background":"162f28"},
    "obsidian": {"gel":"788296", "blue":"657ca8", "deep":"27384c", "accent":"a3dcf0", "water":"405c70", "stone":"303a47", "leaf":"476477", "background":"0b111d"},
    "aurora": {"gel":"d0cef5", "blue":"8da5ef", "deep":"8371bd", "accent":"a0ffe0", "water":"668ba9", "stone":"616c86", "leaf":"829c99", "background":"171e39"},
    "amber": {"gel":"f0d7af", "blue":"d4ab7c", "deep":"b77744", "accent":"ffe4a3", "water":"92b5a4", "stone":"877668", "leaf":"8b976a", "background":"302c2a"},
}

def color(value, alpha=1):
    if isinstance(value, str):
        value = value.lstrip("#")
        rgb = [int(value[i:i+2],16)/255 for i in (0,2,4)]
        rgb = [v/12.92 if v <= .04045 else ((v+.055)/1.055)**2.4 for v in rgb]
    else:
        rgb = list(value)[:3]
    return (*rgb, alpha)

def input_value(node, name, value):
    if name in node.inputs:
        node.inputs[name].default_value = value

def aim(obj, point=(0,0,0)):
    obj.rotation_euler = (Vector(point)-obj.location).to_track_quat('-Z','Y').to_euler()

def reset_scene(args, transparent=True):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    s = bpy.context.scene
    s.render.engine = 'CYCLES'
    s.cycles.device = 'CPU'
    if args.device != 'CPU':
        prefs=bpy.context.preferences.addons['cycles'].preferences
        prefs.compute_device_type=args.device
        prefs.get_devices()
        available=[d for d in prefs.devices if d.type==args.device]
        if not available:raise RuntimeError(f'No {args.device} rendering device is available')
        for d in prefs.devices:d.use=d in available
        s.cycles.device='GPU'
    s.cycles.samples = args.samples
    s.cycles.use_adaptive_sampling = True
    s.cycles.adaptive_threshold = .008
    s.cycles.adaptive_min_samples = min(32,args.samples)
    s.cycles.use_denoising = True
    s.cycles.max_bounces = 18
    s.cycles.diffuse_bounces = 6
    s.cycles.glossy_bounces = 8
    s.cycles.transmission_bounces = 16
    s.cycles.volume_bounces = 6
    s.cycles.transparent_max_bounces = 16
    s.cycles.caustics_reflective = True
    s.cycles.caustics_refractive = True
    s.cycles.blur_glossy = .15
    s.cycles.seed = args.seed
    s.render.resolution_x = args.size
    s.render.resolution_y = args.size
    s.render.resolution_percentage = 100
    s.render.image_settings.file_format = 'PNG'
    s.render.image_settings.color_mode = 'RGBA'
    s.render.image_settings.color_depth = '16'
    s.render.film_transparent = transparent
    s.render.threads_mode = 'FIXED'
    s.render.threads = args.threads
    s.view_settings.view_transform = 'AgX'
    try:s.view_settings.look = 'AgX - Medium High Contrast'
    except TypeError:pass
    s.view_settings.exposure = .35
    s.world = bpy.data.worlds.new('Abstract studio · no photographs')
    s.world.use_nodes = True
    bg = s.world.node_tree.nodes.get('Background')
    bg.inputs['Color'].default_value = color('acbacb')
    bg.inputs['Strength'].default_value = .5
    return s

def material(family, theme):
    """Separate interfaces and actual participating media for each matter family."""
    palette = THEMES[theme]
    m = bpy.data.materials.new(f'Opaline/Cycles/{theme}/{family}')
    m.use_nodes = True
    m['offline_physical_material'] = True
    m['family'] = family
    tree = m.node_tree; n=tree.nodes; link=tree.links
    n.clear()
    out=n.new('ShaderNodeOutputMaterial'); out.location=(900,80)
    bs=n.new('ShaderNodeBsdfPrincipled');bs.location=(580,160)
    link.new(bs.outputs['BSDF'],out.inputs['Surface'])
    settings={
      'gel':('gel',.16,1.39,.96,.42,.85),
      'blue':('blue',.14,1.40,.97,.28,.65),
      'shell':('gel',.045,1.46,1,.008,.055),
      'water':('water',.022,1.333,1,.004,.07),
      'pigment':('blue',.20,1.37,.80,1.1,.40),
      # Effective thin optical layer on an air substrate. Using water IOR for
      # both the substrate and layer suppresses interference and makes a solid bead.
      'film':('gel',.018,1.0,1,0,0),
      'nacre':('gel',.23,1.52,.48,.60,.10),
      'stone':('stone',.46,1.48,0,0,0),
      'leaf':('leaf',.40,1.35,.12,0,0),
      'glow':('accent',.16,1.37,.65,.12,.10),
    }
    key,rough,ior,trans,scatter,absorb=settings.get(family,settings['gel'])
    tint=color(palette[key]); deep=color(palette['deep'])
    # White dielectric surface; coloration comes mainly from depth, not paint.
    surface=tuple(.94+.06*v for v in tint[:3])+(1,)
    if family in ('stone','leaf','nacre'):surface=tint
    input_value(bs,'Base Color',surface)
    input_value(bs,'Roughness',rough)
    input_value(bs,'IOR',ior)
    input_value(bs,'Transmission Weight',trans)
    input_value(bs,'Coat Weight',.28 if family=='stone' else .45)
    if family=='film':input_value(bs,'Coat Weight',0)
    input_value(bs,'Coat Roughness',.09 if family=='stone' else .065)
    input_value(bs,'Coat IOR',1.4)
    tex=n.new('ShaderNodeTexCoord');tex.location=(-1000,100)
    noise=n.new('ShaderNodeTexNoise');noise.location=(-800,100)
    noise.noise_dimensions='3D'
    input_value(noise,'Scale',3.2);input_value(noise,'Detail',4.2)
    input_value(noise,'Roughness',.62);input_value(noise,'Distortion',1.6)
    link.new(tex.outputs['Generated'],noise.inputs['Vector'])
    ramp=n.new('ShaderNodeValToRGB');ramp.location=(-550,80)
    ramp.color_ramp.elements[0].position=.22
    ramp.color_ramp.elements[0].color=deep
    ramp.color_ramp.elements[1].position=.76
    ramp.color_ramp.elements[1].color=tint
    link.new(noise.outputs['Fac'],ramp.inputs['Fac'])
    if scatter or absorb:
        va=n.new('ShaderNodeVolumeAbsorption');va.location=(230,-220)
        vs=n.new('ShaderNodeVolumeScatter');vs.location=(220,-20)
        add=n.new('ShaderNodeAddShader');add.location=(620,-80)
        link.new(add.outputs[0],out.inputs['Volume'])
        link.new(vs.outputs[0],add.inputs[0]);link.new(va.outputs[0],add.inputs[1])
        link.new(ramp.outputs['Color'],va.inputs['Color'])
        input_value(va,'Density',absorb)
        input_value(vs,'Color',tint)
        input_value(vs,'Anisotropy',.22)
        density=n.new('ShaderNodeMapRange');density.location=(-100,-50)
        input_value(density,'From Min',.28);input_value(density,'From Max',.72)
        input_value(density,'To Min',scatter*.08);input_value(density,'To Max',scatter*1.8)
        link.new(noise.outputs['Fac'],density.inputs['Value'])
        link.new(density.outputs['Result'],vs.inputs['Density'])
    if family in ('film','nacre'):
        input_value(bs,'Thin Film IOR',1.333)
        thickness=n.new('ShaderNodeMapRange');thickness.location=(0,340)
        input_value(thickness,'To Min',140);input_value(thickness,'To Max',540)
        link.new(noise.outputs['Fac'],thickness.inputs['Value'])
        if 'Thin Film Thickness' in bs.inputs:
            link.new(thickness.outputs['Result'],bs.inputs['Thin Film Thickness'])
        m['film_model']='Principled thin-film interference; thickness authored in nanometres'
    if family=='glow':
        input_value(bs,'Emission Color',tint);input_value(bs,'Emission Strength',1.4)
    if family in ('gel','blue','stone','leaf','nacre'):
        micro=n.new('ShaderNodeTexNoise');micro.location=(-800,-430)
        input_value(micro,'Scale',90 if family=='stone' else 180)
        input_value(micro,'Detail',2)
        link.new(tex.outputs['Generated'],micro.inputs['Vector'])
        bump=n.new('ShaderNodeBump');bump.location=(290,360)
        input_value(bump,'Strength',.10 if family=='stone' else .025)
        input_value(bump,'Distance',.012 if family=='stone' else .0015)
        link.new(micro.outputs['Fac'],bump.inputs['Height'])
        link.new(bump.outputs['Normal'],bs.inputs['Normal'])
    return m

def load_model(identifier, theme, scale=1):
    path=ROOT/'assets/models'/f'{identifier}.glb'
    if not path.exists():raise FileNotFoundError(path)
    before=set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(path))
    imported=list(set(bpy.data.objects)-before)
    root=bpy.data.objects.new(f'{identifier} · authoring root',None)
    bpy.context.collection.objects.link(root)
    mats={}
    for obj in imported:
        if obj.parent is None:obj.parent=root
        if obj.type!='MESH':continue
        for slot in obj.material_slots:
            name=(slot.material.name if slot.material else 'gel').split('/')[-1].split('.')[0]
            if name not in mats:mats[name]=material(name,theme)
            slot.material=mats[name]
        if obj.material_slots and all(slot.material.get('family')=='film' for slot in obj.material_slots):
            obj['film_geometry']='Closed gas envelope; effective optical film boundary on an air substrate; no solid-water interior'
        for poly in obj.data.polygons:poly.use_smooth=True
        try:obj.cycles.is_caustics_caster=True
        except AttributeError:pass
    bpy.context.view_layer.update()
    meshes=[o for o in imported if o.type=='MESH']
    bounds=[o.matrix_world@Vector(v) for o in meshes for v in o.bound_box]
    if not bounds:raise ValueError(f'{identifier}: no mesh geometry')
    lo=Vector(tuple(min(v[a] for v in bounds) for a in range(3)))
    hi=Vector(tuple(max(v[a] for v in bounds) for a in range(3)))
    center=(lo+hi)/2
    factor=scale/max(hi-lo)
    root.scale=(factor,)*3
    root.location=-center*factor
    bpy.context.view_layer.update()
    return root,meshes

def area(name,position,power,size,color_hex,target=(0,0,0),ratio=1):
    data=bpy.data.lights.new(name,'AREA');data.energy=power
    data.shape='RECTANGLE';data.size=size;data.size_y=size*ratio
    data.color=color(color_hex)[:3]
    try:data.cycles.is_caustics_light=True
    except AttributeError:pass
    obj=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(obj)
    obj.location=position;aim(obj,target)
    return obj

def lighting():
    area('Broad pearl key',(-3.6,-4.6,6.2),1150,5.0,'e9f5ff',ratio=.6)
    area('Aqua edge',(4.5,1.2,3.5),1500,3.0,'8ad7e9',ratio=.26)
    area('Warm reflection strip',(-3.5,2.4,2.0),850,4.0,'ffe0b4',ratio=.18)
    area('Soft frontal fill',(1.3,-5,-.8),350,3.5,'ddd9f7',ratio=.7)

def camera(view,size=1,point=(0,0,0)):
    data=bpy.data.cameras.new('Perspective camera')
    data.type='PERSP';data.lens=65
    obj=bpy.data.objects.new('Perspective camera',data);bpy.context.collection.objects.link(obj)
    positions={'front':(0,-6.4,.06),'threequarter':(2.8,-5.5,2.6),'underside':(-2.0,-5.5,-3.0)}
    obj.location=Vector(positions[view])*size+Vector(point)
    aim(obj,point);bpy.context.scene.camera=obj
    return obj

def floor_material(theme):
    m=bpy.data.materials.new('Abstract satin stage');m.use_nodes=True
    bs=m.node_tree.nodes.get('Principled BSDF')
    input_value(bs,'Base Color',color(THEMES[theme]['background']))
    input_value(bs,'Roughness',.40);input_value(bs,'Metallic',.06)
    input_value(bs,'Coat Weight',.15);input_value(bs,'Coat Roughness',.22)
    return m

def abstract_stage(theme):
    bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.75))
    floor=bpy.context.object;floor.name='Continuous abstract satin ground'
    floor.data.materials.append(floor_material(theme))
    try:floor.cycles.is_caustics_receiver=True
    except AttributeError:pass
    # Broad geometry ribbons offer a deliberately abstract non-photographic backdrop.
    mat=material('nacre',theme)
    for j in range(3):
        curve=bpy.data.curves.new(f'Abstract backdrop sweep {j}','CURVE')
        curve.dimensions='3D';curve.resolution_u=24
        curve.bevel_depth=.13+.09*j;curve.bevel_resolution=6
        poly=curve.splines.new('BEZIER');poly.bezier_points.add(4)
        points=[(-9,5+j*1.1,-.2),( -4,6+j*.8,.4),(0,7+j*.7,1.2),(4,6+j*.8,2.2),(9,5+j,3.6)]
        for p,v in zip(poly.bezier_points,points):p.co=v;p.handle_left_type='AUTO';p.handle_right_type='AUTO'
        obj=bpy.data.objects.new(f'Pearl contour {j}',curve);bpy.context.collection.objects.link(obj)
        obj.data.materials.append(mat)

def save_and_render(args,name,metadata):
    out=ROOT/'assets/pathtraced';out.mkdir(parents=True,exist_ok=True)
    blends=ROOT/'assets/blender';blends.mkdir(parents=True,exist_ok=True)
    path=out/f'{name}.png'
    bpy.context.scene.render.filepath=str(path)
    if not args.no_blend:
        bpy.ops.wm.save_as_mainfile(filepath=str(blends/f'{name}.blend'),compress=True)
    start=time.time()
    bpy.ops.render.render(write_still=True)
    metadata.update({'name':name,'file':str(path.relative_to(ROOT)),'engine':'Blender Cycles','blenderVersion':bpy.app.version_string,'samplesMaximum':args.samples,'adaptiveThreshold':.008,'denoised':True,'resolution':[bpy.context.scene.render.resolution_x,bpy.context.scene.render.resolution_y],'theme':args.theme,'seconds':round(time.time()-start,2),'transparent':bpy.context.scene.render.film_transparent,'source':'Original GLB geometry rendered offline, not image-generation output','transport':'RGB path tracing; volumetric absorption/scattering; dielectric refraction; thin-film approximation. No spectral dispersion claim.','renderDevice':args.device})
    (out/f'{name}.json').write_text(json.dumps(metadata,indent=2)+'\n')
    print(f'OPALINE_RENDERED {path}',flush=True)

def write_render_index():
    out=ROOT/'assets/pathtraced'
    records=[]
    for sidecar in sorted(out.glob('*.json')):
        if sidecar.name=='render-index.json':continue
        record=json.loads(sidecar.read_text())
        png=sidecar.with_suffix('.png')
        if record.get('engine')!='Blender Cycles' or not png.exists():continue
        with png.open('rb') as handle:header=handle.read(33)
        if header[:8]!=b'\x89PNG\r\n\x1a\n':raise ValueError(f'Invalid PNG: {png}')
        width,height,depth,ctype=struct.unpack('>IIBB',header[16:26])
        if [width,height]!=record['resolution']:raise ValueError(f'Resolution metadata mismatch: {png}')
        record['png']={'width':width,'height':height,'bitDepth':depth,'colorType':ctype,'bytes':png.stat().st_size}
        record['scene']=f"assets/blender/{record['name']}.blend"
        ids=record.get('ids',[record['id']] if 'id' in record else [])
        record['sourceModels']=[f'assets/models/{identifier}.glb' for identifier in ids]
        records.append(record)
    payload={'schemaVersion':1,'renderCount':len(records),'note':'Only successfully rendered PNGs are counted. A batch command supports more models; unrendered combinations are not counted.','renders':records}
    (out/'render-index.json').write_text(json.dumps(payload,indent=2)+'\n')

def render_asset(args,identifier,view):
    reset_scene(args)
    load_model(identifier,args.theme,scale=2.45)
    lighting();camera(view,size=1.05)
    save_and_render(args,f'{identifier}-{args.theme}-{view}',{'id':identifier,'view':view})

def render_specimen(args):
    s=reset_scene(args,transparent=False)
    s.render.resolution_y=round(args.size*.75)
    abstract_stage(args.theme)
    layout=[('A01',(-2.5,-2,.0),2),('B01',(.1,-2.5,.1),2.7),('C03',(-2.8,.5,.2),2.8),('C10',(.1,.3,.12),2.5),('C11',(.0,2.9,.5),2.0),('E08',(2.5,.8,.35),2.1),('F01',(2.7,-1.7,.2),1.65)]
    for identifier,pos,scale in layout:
        root,_=load_model(identifier,args.theme,scale)
        # glTF Y-up -> Blender Z-up. Control face -Y rotates toward +Z.
        center_offset=root.location.copy()
        # E08 is authored along Y-up rather than along a control's face normal.
        root.rotation_euler.x=0 if identifier=='E08' else -math.pi/2
        root.location=Vector(pos)+root.rotation_euler.to_matrix()@center_offset
    lighting()
    area('Caustic focused source',(-1,-1,8),800,.45,'edffff')
    cam=camera('threequarter');cam.location=(9,-13,12);cam.data.lens=57;aim(cam,(0,0,.1))
    save_and_render(args,f'specimen-{args.theme}',{'ids':[r[0] for r in layout],'view':'staged perspective','background':'Original abstract geometric satin stage, no photograph','caustics':'Cycles reflective/refractive paths and shadow-caustic object/light flags enabled. Noise/convergence depends on sample count; this is not a spectral caustic solution.'})

def parse_args():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ids',nargs='+',default=['A01','B01','C03','C10','E08','F01'])
    parser.add_argument('--all',action='store_true',help='Render every GLB actually present in assets/models')
    parser.add_argument('--views',nargs='+',choices=['front','threequarter','underside'],default=['threequarter'])
    parser.add_argument('--theme',choices=THEMES,default='tidal')
    parser.add_argument('--samples',type=int,default=128)
    parser.add_argument('--size',type=int,default=1024)
    parser.add_argument('--threads',type=int,default=8)
    parser.add_argument('--device',choices=['CPU','CUDA','OPTIX','HIP','METAL','ONEAPI'],default='CPU')
    parser.add_argument('--seed',type=int,default=47)
    parser.add_argument('--specimen',action='store_true')
    parser.add_argument('--no-blend',action='store_true')
    argv=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
    return parser.parse_args(argv)

def main():
    if bpy.app.version < (4,5,0):
        raise RuntimeError('This material pipeline requires Blender 4.5 or later')
    args=parse_args()
    if args.samples<1 or args.size<32:raise ValueError('Samples must be positive; size must be at least 32')
    if args.specimen:render_specimen(args);write_render_index();return
    ids=sorted(p.stem for p in (ROOT/'assets/models').glob('*.glb')) if args.all else args.ids
    if not ids:raise ValueError('No source models. First run the geometry export tool.')
    for identifier in ids:
        for view in args.views:render_asset(args,identifier,view)
    write_render_index()

if __name__=='__main__':main()
