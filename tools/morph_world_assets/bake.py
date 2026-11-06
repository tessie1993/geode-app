"""Bakes Morph World's assets in headless Blender: material detail maps and model distance volumes.

Run (after fetch.py; autoexec stays off, the downloaded .blend files are data, never code):

    blender -b --factory-startup --disable-autoexec -noaudio \
        --python tools/morph_world_assets/bake.py -- <cache-dir> <out-dir>

Writes into <out-dir> (the app's is app/src/main/assets/shaders):

  world_textures.bin  MATERIALS x 128 x 128 bytes, R8, row-major, layer after layer. Each layer is a
                      material's base colour baked flat onto a unit plane (Cycles, its base colour
                      re-routed through an emission shader and baked as EMIT, so no light is in it),
                      taken to luminance and stretched to its 2nd..98th percentile. Grey on purpose:
                      lib_world reads it as surface DETAIL (albedo variation and bump), and the colour
                      still comes only from pal(). The GPU mirrors it on every edge
                      (GL_MIRRORED_REPEAT), so a map that does not tile still meets itself seamlessly.
  world_shapes.bin    MODELS x 32 x 32 x 32 bytes, R8, x fastest, then y, then z, slab after slab.
                      Each slab is a model's signed distance on a 32^3 grid over the cube [-1, 1]^3
                      (the model centred and scaled to fit 0.9 of it), in cube units mapped
                      [-0.25, 0.75] -> [0, 255]. A distance beyond 0.75 is stored as 0.75, so every
                      stored value is at most the true distance and a sphere tracer never overshoots.
                      Sign: ray parity along x, y and z, by majority (robust to open seams).
                      Every model wears a SKIN of 0.04, so parts thinner than a voxel still draw.
                      Only LOD0 of a file that stacks levels of detail is baked.

The counts and sizes match core/viz/world/WorldAssets.hpp; world_genome_test checks the file sizes.
"""
import json
import pathlib
import sys

import bpy
import numpy as np
from mathutils import Vector
from mathutils.bvhtree import BVHTree

TEX = 128
VOX = 32
D_LO = -0.25
D_HI = 0.75
# A skin on every model, in cube units (about 0.6 of a voxel): leaves, stems and trunks thinner
# than a voxel still have an inside the grid can see, so they draw instead of vanishing.
SKIN = 0.04

HERE = pathlib.Path(__file__).resolve().parent


def args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    if len(argv) != 2:
        raise SystemExit(__doc__)
    return pathlib.Path(argv[0]), pathlib.Path(argv[1])


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def append_all(path, collection):
    with bpy.data.libraries.load(str(path), link=False) as (src, dst):
        setattr(dst, collection, list(getattr(src, collection)))
    return list(getattr(dst, collection))


# ---- materials --------------------------------------------------------------------

def bake_material(path, name):
    reset()
    mats = [m for m in append_all(path, "materials") if m is not None and m.use_nodes]
    if not mats:
        raise SystemExit(f"{path.name}: no node material")
    # The asset's own material: by its name, else the richest node tree (a .blend often also carries
    # Blender's default "Material").
    named = [m for m in mats if m.name.lower() == name.lower()]
    mat = named[0] if named else max(mats, key=lambda m: len(m.node_tree.nodes))
    bpy.ops.mesh.primitive_plane_add(size=2.0)
    plane = bpy.context.active_object
    plane.data.materials.append(mat)
    img = bpy.data.images.new("bake", TEX, TEX, float_buffer=True)
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    # The base colour, re-routed through an emission shader and baked as EMIT: the one bake that
    # reads a material's colour with no light in it.
    bsdfs = [n for n in nodes if n.bl_idname == "ShaderNodeBsdfPrincipled"]
    out = next((n for n in nodes if n.bl_idname == "ShaderNodeOutputMaterial" and n.is_active_output), None)
    out = out or next(n for n in nodes if n.bl_idname == "ShaderNodeOutputMaterial")
    emit = nodes.new("ShaderNodeEmission")
    # Where the material's pattern lives, in order: a linked base colour, a linked emission colour
    # (a glowing material keeps its pattern there), the factor of a shader mix at the output.
    source = None
    for socket in ("Base Color", "Emission Color"):
        for bsdf in bsdfs:
            if source is None and bsdf.inputs[socket].is_linked:
                source = bsdf.inputs[socket].links[0].from_socket
    surface = out.inputs["Surface"].links[0].from_node if out.inputs["Surface"].is_linked else None
    if source is None and surface is not None and surface.bl_idname == "ShaderNodeMixShader" and surface.inputs[0].is_linked:
        source = surface.inputs[0].links[0].from_socket
    if source is None:
        raise SystemExit(f"{path.name}: no pattern to bake")
    links.new(source, emit.inputs["Color"])
    for link in list(out.inputs["Surface"].links) + list(out.inputs["Displacement"].links):
        links.remove(link)
    links.new(emit.outputs["Emission"], out.inputs["Surface"])
    target = nodes.new("ShaderNodeTexImage")
    target.image = img
    for n in nodes:
        n.select = False
    target.select = True
    nodes.active = target
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 16
    scene.render.bake.margin = 0
    bpy.ops.object.bake(type="EMIT")
    px = np.empty(TEX * TEX * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    px = px.reshape(TEX, TEX, 4)
    lum = 0.2126 * px[..., 0] + 0.7152 * px[..., 1] + 0.0722 * px[..., 2]
    lo, hi = np.percentile(lum, 2), np.percentile(lum, 98)
    lum = np.clip((lum - lo) / max(hi - lo, 1e-4), 0.0, 1.0)
    # Blender's pixel rows run bottom to top; GL's texture rows do too, so keep them.
    return (lum * 255.0 + 0.5).astype(np.uint8), mat.name


# ---- models -----------------------------------------------------------------------

def model_bvh(path):
    reset()
    objs = [o for o in append_all(path, "objects") if o is not None]
    # Keep the model itself: when the file stacks levels of detail in one place (Poly Haven's
    # _LOD0.._LOD3), only LOD0, since four coincident surfaces would cancel the ray parity; and
    # never Blender's default scene cube a file was saved with.
    if any("_LOD" in o.name for o in objs):
        objs = [o for o in objs if "_LOD" not in o.name or "_LOD0" in o.name]
    objs = [o for o in objs if not (o.type == "MESH" and o.name.startswith("Cube") and len(o.data.polygons) == 6)]
    scene = bpy.context.scene
    for o in objs:
        scene.collection.objects.link(o)
    depsgraph = bpy.context.evaluated_depsgraph_get()
    verts, polys = [], []
    for o in objs:
        if o.type != "MESH":
            continue
        ev = o.evaluated_get(depsgraph)
        mesh = ev.to_mesh()
        mw = o.matrix_world
        base = len(verts)
        verts.extend(mw @ v.co for v in mesh.vertices)
        polys.extend([base + i for i in p.vertices] for p in mesh.polygons)
        ev.to_mesh_clear()
    if not polys:
        raise SystemExit(f"{path.name}: no mesh")
    pts = np.array([tuple(v) for v in verts], dtype=np.float64)
    lo, hi = pts.min(axis=0), pts.max(axis=0)
    centre = (lo + hi) * 0.5
    scale = 0.9 / max((hi - lo).max() * 0.5, 1e-6)
    norm = [Vector(tuple((p - centre) * scale)) for p in pts]
    return BVHTree.FromPolygons(norm, polys, all_triangles=False, epsilon=0.0)


def crossings(bvh, axis, u, v):
    """Every hit along the grid line (u, v) parallel to `axis`, from -2 to +2, as positions on it."""
    origin = [0.0, 0.0, 0.0]
    others = [i for i in range(3) if i != axis]
    origin[others[0]], origin[others[1]] = u, v
    origin[axis] = -2.0
    direction = Vector([1.0 if i == axis else 0.0 for i in range(3)])
    hits = []
    start = Vector(origin)
    for _ in range(256):
        loc, _, _, _ = bvh.ray_cast(start, direction, 4.0)
        if loc is None:
            break
        hits.append(loc[axis])
        start = loc + direction * 1e-4
    return hits


def bake_volume(path):
    bvh = model_bvh(path)
    centres = [(i + 0.5) / VOX * 2 - 1 for i in range(VOX)]
    # Inside by ray parity, voted over the three axes: a sample is inside when at least two of its
    # three grid lines cross the surface an odd number of times before reaching it. Robust to the
    # open seams and stray faces real models have, where a nearest-face normal flips at random.
    votes = np.zeros((VOX, VOX, VOX), dtype=np.int8)  # z, y, x
    for axis in range(3):
        for a in range(VOX):
            for b in range(VOX):
                others = [i for i in range(3) if i != axis]
                hits = sorted(crossings(bvh, axis, centres[a], centres[b]))
                for k, c in enumerate(centres):
                    if sum(1 for h in hits if h < c) % 2 == 1:
                        idx = [0, 0, 0]
                        idx[axis] = k
                        idx[others[0]] = a
                        idx[others[1]] = b
                        votes[idx[2], idx[1], idx[0]] += 1
    out = np.empty((VOX, VOX, VOX), dtype=np.uint8)
    for z in range(VOX):
        for y in range(VOX):
            for x in range(VOX):
                p = Vector((centres[x], centres[y], centres[z]))
                loc, _, _, dist = bvh.find_nearest(p)
                d = D_HI if loc is None else (-dist if votes[z, y, x] >= 2 else dist)
                d -= SKIN
                v = (min(max(d, D_LO), D_HI) - D_LO) / (D_HI - D_LO)
                out[z, y, x] = int(v * 255.0 + 0.5)
    return out


def main():
    cache, out = args()
    pins = json.loads((HERE / "assets.json").read_text())
    out.mkdir(parents=True, exist_ok=True)
    layers = []
    for pin in pins["materials"]:
        layer, used = bake_material(cache / f"{pin['id']}.blend", pin["name"])
        layers.append(layer)
        print(f"BAKED material {pin['name']} ({used}) mean {layer.mean():.0f}")
    (out / "world_textures.bin").write_bytes(np.stack(layers).tobytes())
    slabs = []
    for pin in pins["models"]:
        slab = bake_volume(cache / f"{pin['id']}.blend")
        inside = float((slab < int(-D_LO / (D_HI - D_LO) * 255)).mean())
        slabs.append(slab)
        print(f"BAKED model {pin['name']} inside {inside:.2f}")
    (out / "world_shapes.bin").write_bytes(np.stack(slabs).tobytes())


main()
