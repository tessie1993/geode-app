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

// Loom Basket: woven cane, turned about. Flat strips each over the next they
// cross and under the one after, with a lantern of soft skin hung inside,
// seen through the gaps and lighting the far side from within.
//
// Three formulas (Formula, lib_layers), each its own carrier for the weave:
//   0  a woven ball, strips pole to pole and round in rings
//   1  a woven ring: the strips round a torus, one set round its tube and
//      one the long way, with a ring of lantern inside the tube
//   2  a cluster: a smaller ball with six woven satellites folded about it
//      on the three axes, and a third generation of beads that grows in
//      with the iteration (Detail, k1 TEXTURE, k4 BUILD)
//
// Morph opens the weave: the strips narrow from a closed basket to a sparse
// cage. Worst case 90 steps, two balls a step in the cluster (and two
// formulas inside a crossfade), no noise.

/** The woven ball, 1.9 across, with its lantern: (strip distance, fibre, lantern skin, hue). */
vec4 basketBall(vec3 p) {
    float r = length(p);
    float rho = length(p.xz);
    float lon = loomAngle(p.xz);
    float lat = loomAngle(vec2(rho, p.y));
    float big = 1.9;
    float lift = 0.05;
    float wide = mix(0.27, 0.07, gLoomForm);
    float stepLon = LOOM_TAU / 16.0;
    float stepLat = LOOM_PI / 10.0;
    float i = floor(lon / stepLon + 0.5);
    float j = clamp(floor(lat / stepLat + 0.5), -4.0, 4.0);
    float si = 1.0 - 2.0 * mod(i, 2.0);
    float sj = 1.0 - 2.0 * mod(j, 2.0);
    // Where strip i meets ring j, cos(10 lat) is sj and cos(8 lon) is si: the one is out where the other is in.
    vec3 a = loomTape(vec2(rho * (lon - stepLon * i), r - big - lift * si * cos(10.0 * lat)), wide, 4.0, 0.0);
    vec3 b = loomTape(vec2(r * (lat - stepLat * j), r - big + lift * sj * cos(8.0 * lon)), wide, 4.0, 0.45);
    vec3 s = loomNearer(a, b);
    float lantern = r - 1.2 - 0.06 * sin(2.6 * p.x + 1.7 * p.y + uTime * LOOM_RATE(150)) * cos(2.2 * p.z - uTime * LOOM_RATE(110));
    return vec4(s.x, s.y, lantern, s.z);
}

/** The ball grown or shrunk to `big` across. */
vec4 basketSized(vec3 p, float big) {
    float k = big / 1.9;
    vec4 m = basketBall(p / k);
    return vec4(m.x * k, m.y, m.z * k, m.w);
}

/** The woven ring: ten strips round the tube, twenty-two the long way, each over one and under the next. */
vec4 basketRing(vec3 p) {
    float ring = 1.55;
    float tube = 0.8;
    float rho = length(p.xz);
    float lon = loomAngle(p.xz);
    vec2 v = vec2(rho - ring, p.y);
    float rt = length(v);
    float lat = loomAngle(v);
    float lift = 0.045;
    float wide = mix(0.2, 0.06, gLoomForm);
    float stepLat = LOOM_TAU / 10.0;
    float stepLon = LOOM_TAU / 22.0;
    float i = floor(lat / stepLat + 0.5);
    float j = floor(lon / stepLon + 0.5);
    float si = 1.0 - 2.0 * mod(i, 2.0);
    float sj = 1.0 - 2.0 * mod(j, 2.0);
    // At the crossing of band i and hoop j, cos(5 lat) is si and cos(11 lon) is sj: one out, one in.
    vec3 a = loomTape(vec2(rho * (lon - stepLon * j), rt - tube - lift * sj * cos(5.0 * lat)), wide, 4.0, 0.15);
    vec3 b = loomTape(vec2(rt * (lat - stepLat * i), rt - tube + lift * si * cos(11.0 * lon)), wide, 4.0, 0.6);
    vec3 s = loomNearer(a, b);
    float lantern = rt - 0.42 - 0.05 * sin(5.0 * lon + uTime * LOOM_RATE(140));
    return vec4(s.x, s.y, lantern, s.z);
}

/** The cluster: a ball, six satellites folded onto the axes, and the beads of a third generation. */
vec4 basketCluster(vec3 p) {
    vec4 m = basketSized(p, 1.15);
    vec3 q = abs(p);
    // The largest coordinate to x: the nearest of the six satellites.
    if (q.y > q.x) q.xy = q.yx;
    if (q.z > q.x) q.xz = q.zx;
    q.x -= 2.25;
    vec4 s = basketSized(q, 0.6);
    float skin = min(m.z, s.z);
    m = s.x < m.x ? s : m;
    // The third generation, grown in by the iteration: a bead on each of a satellite's five outer axes.
    float grow = clamp(layerIters(-0.3, 1.4), 0.0, 1.0);
    vec3 b = abs(q);
    if (b.y > b.x) b.xy = b.yx;
    if (b.z > b.x) b.xz = b.zx;
    float bead = length(b - vec3(0.95, 0.0, 0.0)) - 0.16 * grow + 0.4 * (1.0 - grow);
    if (bead < m.x) m = vec4(bead, 0.0, skin, 0.85);
    m.z = skin;
    return m;
}

vec4 loomMap(vec3 p) {
    p.xz = loomRot(uTime * LOOM_RATE(70)) * p.xz;
    vec4 m = vec4(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * basketBall(p);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * basketRing(p);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * basketCluster(p);
    return m;
}

//#include lib_loom_march

void main() {
    vec2 uv = view();
    loomSetup();
    vec3 ro;
    vec3 rd;
    camAround(uv, 4.6, CAM_SPIN_UNIT, ro, rd);
    fragColor = loomFinish(loomTrace(ro, rd), 1.1);
}
