// lib_generator: Generators (#471), other styles rendered offscreen into textures this style samples.
//
// One sampler per slot of geode::scene::generator::Slot, bound by ShaderScene on units 4..7
// (viz/GeneratorBinding.hpp), and uGenMix = (sky, wall, material, plane): the slot's mix, 0 when no
// generator is assigned. Every helper takes the colour the style would draw anyway and returns it
// untouched while its channel is 0 (a branch, not mix(), so not even a texture fetch happens), which
// keeps every style that includes this library pixel-identical to today until a generator is set.
//
// A generator texture is a finished 2D frame (graded by its own scene), clamped at its edges.
//   genSky(rd, base)          the sky: an equirect lookup of a world direction, forward at the
//                             image's centre, its seam straight behind the eye, the horizon across
//                             the middle.
//   genWall(p, n, base)       a wall: triplanar on the world position, mirrored tiles (no seams).
//   genMaterial(p, n, base)   an object's surface, triplanar like the wall at a finer scale.
//   genPlane(uv, base)        the camera plane: screen uv 0..1, the generator filling the view.

uniform sampler2D uGenSky;
uniform sampler2D uGenWall;
uniform sampler2D uGenMaterial;
uniform sampler2D uGenPlane;
uniform vec4 uGenMix;

#define GEN_INV_PI 0.31830988
#define GEN_INV_TWO_PI 0.15915494

vec3 genSky(vec3 rd, vec3 fallback) {
    if (uGenMix.x <= 0.0) return fallback;
    vec3 d = normalize(rd);
    vec2 uv = vec2(0.5 + atan(d.x, d.z) * GEN_INV_TWO_PI, 0.5 + asin(clamp(d.y, -1.0, 1.0)) * GEN_INV_PI);
    // Lod 0: the atan seam behind the eye would otherwise pick a tiny mip from the derivative jump.
    return mix(fallback, textureLod(uGenSky, uv, 0.0).rgb, uGenMix.x);
}

/** A tile coordinate that mirrors at every whole unit, so a non-tiling frame repeats without seams. */
vec2 genMirror(vec2 q) {
    return 1.0 - abs(1.0 - mod(q, 2.0));
}

/** Triplanar: three projections of the world position, weighted by how squarely the normal faces each. */
vec3 genTriplanar(sampler2D tex, vec3 p, vec3 n, float scale) {
    vec3 w = pow(abs(n), vec3(4.0));
    w /= max(w.x + w.y + w.z, 1e-4);
    vec3 cx = textureLod(tex, genMirror(p.zy * scale), 0.0).rgb;
    vec3 cy = textureLod(tex, genMirror(p.xz * scale), 0.0).rgb;
    vec3 cz = textureLod(tex, genMirror(p.xy * scale), 0.0).rgb;
    return cx * w.x + cy * w.y + cz * w.z;
}

vec3 genWall(vec3 p, vec3 n, vec3 base) {
    if (uGenMix.y <= 0.0) return base;
    return mix(base, genTriplanar(uGenWall, p, n, 0.25), uGenMix.y);
}

vec3 genMaterial(vec3 p, vec3 n, vec3 base) {
    if (uGenMix.z <= 0.0) return base;
    return mix(base, genTriplanar(uGenMaterial, p, n, 1.0), uGenMix.z);
}

vec3 genPlane(vec2 uv, vec3 base) {
    if (uGenMix.w <= 0.0) return base;
    return mix(base, textureLod(uGenPlane, clamp(uv, 0.0, 1.0), 0.0).rgb, uGenMix.w);
}
