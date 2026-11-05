// lib_generator: Generators (#471), other styles rendered offscreen into textures this style samples.
//
// One sampler per slot of geode::scene::generator::Slot, bound by ShaderScene on units 4..7
// (viz/GeneratorBinding.hpp), and uGenMix = (sky, wall, material, plane): the slot's mix, 0 when no
// generator is assigned. Every helper takes the colour the style would draw anyway and returns it
// untouched while its channel is 0 (a branch, not mix(), so not even a texture fetch happens), which
// keeps every style that includes this library pixel-identical to today until a generator is set.
//
// A generator texture is a finished 2D frame (graded by its own scene), clamped at its edges.
//   genSky(rd, base)          the sky: the frame stood on the horizon and repeated around it in
//                             mirrored tiles (GEN_SKY_TILES across 360 degrees, no seam), faded
//                             back to the base toward the zenith and nadir.
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
/**
 * Mirrored tiles of the sky frame around the horizon. Even, so the mirror pattern closes on itself
 * at the atan wrap behind the eye (azimuth * 4 + 0.5 runs -1.5..2.5, two whole mirror periods);
 * four tiles are 90 degrees each, about one tile across a phone's view, so a 512 frame is not
 * magnified into blocks the way one frame stretched over 360 degrees was.
 */
#define GEN_SKY_TILES 4.0

/** A tile coordinate that mirrors at every whole unit, so a non-tiling frame repeats without seams. */
vec2 genMirror(vec2 q) {
    return 1.0 - abs(1.0 - mod(q, 2.0));
}

vec3 genSky(vec3 rd, vec3 fallback) {
    if (uGenMix.x <= 0.0) return fallback;
    vec3 d = normalize(rd);
    // u: azimuth in turns (-0.5..0.5), mirrored GEN_SKY_TILES times around, continuous everywhere,
    // offset half a tile so a tile's centre (not a fold) faces forward.
    // v: elevation in quarter turns, 0 at the horizon and 1 at the zenith, so the frame's bottom
    // edge stands on the horizon and its height spans horizon to zenith; below the horizon it mirrors
    // (abs), so a downward ray sees the same band upside down rather than a clamped edge.
    // The frame used to be centred on forward-horizon, which put its middle (kifs: the Menger
    // cube's dark central hole) right on the horizon, magnified into a dark rectangle there.
    float el = asin(clamp(d.y, -1.0, 1.0)) * (2.0 * GEN_INV_PI);
    vec2 uv = genMirror(vec2(atan(d.x, d.z) * GEN_INV_TWO_PI * GEN_SKY_TILES + 0.5, abs(el)));
    // Toward the poles the tiles pinch to a point (every azimuth meets there); fade back to the
    // base before that shows.
    float w = uGenMix.x * (1.0 - smoothstep(0.55, 0.9, abs(el)));
    // Explicit lod 0: the generator targets carry no mips, and genSky is called from non-uniform
    // control flow (a sea hit or not), where derivatives are undefined anyway; uv itself has no
    // jump left for a derivative to trip on.
    return mix(fallback, textureLod(uGenSky, uv, 0.0).rgb, w);
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
