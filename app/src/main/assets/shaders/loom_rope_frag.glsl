#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_neon
//#include lib_loom

// Loom Rope: a flight among ropes, each three strands laid round each other,
// each strand spun of four plies twisted against the lay.
//
// Three formulas (Formula, lib_layers), each a different rigging:
//   0  coils: two ropes wound round the way like a double spring, and two
//      more round them the other way
//   1  a cable: five ropes laid straight along the way in a ring, the ring
//      twisting once round the way in every LOOM_PERIOD
//   2  a chain: rings of rope round the way, every other one tilted the other
//      way, linked through each other; they thicken with the iteration
//
// Morph draws the ropes in close about the way and thickens them. Worst case
// 90 steps, no noise.

/** Three strands laid round a rope's core: q across the rope, `lay` the turn of the lay there, `along` its fibre run. */
vec3 ropeStrands(vec2 q, float lay, float thick, float along, float hue) {
    q = loomRot(lay) * q;
    float sector = LOOM_TAU / 3.0;
    float a = loomAngle(q);
    float off = loomRep(a, sector);
    vec2 c = length(q) * vec2(cos(off), sin(off)) - vec2(0.15 * thick, 0.0);
    float strand = mod(floor(a / sector + 0.5), 3.0);
    return loomYarn(c, 0.13 * thick, 4.0, along, hue + 0.1 * strand);
}

/** Two ropes half a turn apart, coiled at radius `coil`, a turn every `pitch` units (its sign the hand). */
vec3 ropeCoil(float r, float th, float z, float coil, float pitch, float thick, float hue) {
    float zc = z - pitch * th / LOOM_TAU;
    float apart = 0.5 * abs(pitch);
    float n = floor(zc / apart + 0.5);
    float lap = LOOM_TAU * r;
    // Across the rope: out from the coil, and along the way scaled to the rope's own slant.
    vec2 q = vec2(r - coil, (zc - apart * n) * lap / sqrt(lap * lap + pitch * pitch));
    // Three strands round the rope's core, laid four times round in a lap.
    return ropeStrands(q, 4.0 * th, thick, -6.0 * th / LOOM_TAU, hue + 0.4 * mod(n, 2.0));
}

vec4 ropeCoils(vec3 p, float coil, float thick) {
    float r = length(p.xy);
    float th = loomAngle(p.xy);
    vec3 s = loomNearer(ropeCoil(r, th, p.z, coil, 4.0, thick, 0.0), ropeCoil(r, th, p.z, coil + 1.4, -4.0, thick, 0.2));
    return vec4(s.x, s.y, 8.0, s.z);
}

vec4 ropeCable(vec3 p, float coil, float thick) {
    float zc = p.z * (LOOM_TAU / LOOM_PERIOD);
    vec2 c = loomRot(zc) * p.xy;
    vec2 q = layerSector(c, 5.0) - vec2(coil + 0.2, 0.0);
    // Laid six times in a period; the ring's twist shears space, which bounds the step.
    vec3 s = ropeStrands(q, 6.0 * zc, 1.2 * thick, 2.0 * p.z, 0.3);
    return vec4(s.x * 0.8, s.y, 8.0, s.z);
}

/** One link of the chain: a ring of rope of radius `ring`, centred on the way at z0, tilted by `tilt` about x. */
vec3 ropeLink(vec3 p, float z0, float tilt, float ring, float thick, float hue) {
    vec3 q = p - vec3(0.0, 0.0, z0);
    q.yz = loomRot(tilt) * q.yz;
    float th = loomAngle(q.xy);
    vec2 v = vec2(length(q.xy) - ring, q.z);
    return ropeStrands(v, 5.0 * th, thick, -4.0 * th / LOOM_TAU, hue);
}

vec4 ropeChain(vec3 p, float coil, float thick) {
    // A link every 2 units, eight to a period; the nearer of the two on either side.
    float k = floor(p.z * 0.5);
    float t = (1.0 - 2.0 * mod(k, 2.0)) * 0.5;
    float grow = mix(1.1, 1.6, gLayerIter);
    vec3 s = ropeLink(p, 2.0 * k, t, coil, grow * thick, 0.1);
    s = loomNearer(s, ropeLink(p, 2.0 * k + 2.0, -t, coil, grow * thick, 0.5));
    return vec4(s.x * 0.9, s.y, 8.0, s.z);
}

vec4 loomMap(vec3 p) {
    float coil = mix(2.0, 1.25, gLoomForm);
    float thick = mix(1.0, 1.5, gLoomForm);
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * ropeCoils(p, coil, thick);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * ropeCable(p, coil, thick);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * ropeChain(p, coil, thick);
    return m;
}

//#include lib_loom_march

void main() {
    vec2 uv = view();
    loomSetup();
    gLoomStride = 0.65;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = loomFinish(loomTrace(loomEye(ro), rd), 1.0);
}
