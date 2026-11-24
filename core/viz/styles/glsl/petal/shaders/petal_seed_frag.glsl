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
//#include lib_petal

// Petal Seed: a seed head, turned about. Florets cover a ball along two
// families of spirals, eight one way and thirteen the other, the way seeds
// pack in a flower's head; each floret is a small cup facing out, and the
// ball's own thin skin, lit from the heart, carries the spirals as veins.
//
// Three formulas (Formula, lib_layers), each a different head:
//   0  a ball 1.5 across, its florets sat on the skin
//   1  a clock: a small seed ball 0.85 across, every floret a shallow
//      parasol on a stalk standing out to 1.9, the way a seed clock carries
//      its seeds
//   2  a cluster: a ball with six smaller heads on the axes about it, a
//      generation down; they swell with the iteration (Detail, k1, k4)
//
// Morph lifts the florets and opens them, from closed beads on the skin to
// open cups standing clear of it. Worst case 96 steps and 4 sheets, two heads
// a step in the cluster, no noise.

/** A seed head about the origin: skin of radius `ball`, florets at `out` on stalks from the skin (none when out is ball). */
vec3 seedHead(vec3 p, float ball, float outR, float open) {
    float rho = max(length(p), 1.0e-4);
    float lat = asin(clamp(p.y / rho, -1.0, 1.0));
    float lon = petalAtan(p.xz);
    // Mercator's height: a spiral on the ball is a straight line in (lon, mer), and a cell is square at every latitude.
    float cl = clamp(lat, -1.35, 1.35);
    float mer = log(tan(0.7853982 + 0.5 * cl));
    vec2 g = vec2(8.0 * lon + 13.0 * mer, 13.0 * lon - 8.0 * mer) / PETAL_TAU;
    vec2 c = fract(g) - 0.5;
    // A cell's side at the florets' radius, world units: 15.264 is the length of (8, 13).
    float side = outR * cos(cl) * PETAL_TAU / 15.264;
    // The cell's floret frame: across the skin, and up from where the florets sit.
    vec3 q = vec3(c * side, rho - outR);
    float lift = side * mix(0.05, 0.6, gPetalForm);
    float cr = 0.4 * side * open;
    // How high the cup's wall rises, in its radius: near 2 a closed bead, under 1 a shallow cup.
    float lip = mix(1.75, 0.8, gPetalForm) / open;
    vec3 b = q - vec3(0.0, 0.0, lift + cr);
    // No florets where the cells crowd at the poles.
    float beyond = max(q.z - lift - lip * cr, (abs(lat) - 1.25) * rho);
    vec3 cup = vec3(petalCut(length(b) - cr, beyond), (q.z - lift) / (lip * cr), 8.0 * petalAtan(b.xy) / PETAL_TAU);
    // Within the florets' layer a neighbour's cup may be nearer than this cell's.
    cup = petalHold(cup, (0.5 - max(abs(c.x), abs(c.y))) * side + 0.1 * side + max(q.z - lift - 2.0 * cr, 0.0));
    vec3 m = petalNear(cup, vec3(rho - ball, 0.0, g.x));
    if (outR > ball) {
        // The stalk, from the skin out to the floret, a thin tube along the cell's middle.
        float st = length(c * side * rho / outR) - 0.012;
        vec3 stalk = vec3(petalCut(st, max(ball - rho, rho - outR - lift)), 0.5, 0.0);
        stalk = petalHold(stalk, (0.5 - max(abs(c.x), abs(c.y))) * side * rho / outR + 0.02);
        m = petalNear(m, stalk);
    }
    return m;
}

vec3 seedCluster(vec3 p) {
    vec3 m = seedHead(p, 1.05, 1.05, 1.0);
    vec3 q = abs(p);
    // The largest coordinate to x: the nearest of the six small heads.
    if (q.y > q.x) q.xy = q.yx;
    if (q.z > q.x) q.xz = q.zx;
    float s = mix(0.38, 0.55, gLayerIter);
    vec3 h = seedHead((q - vec3(1.85, 0.0, 0.0)) / s, 1.0, 1.0, 1.0);
    h.x *= s;
    return petalNear(m, h);
}

vec3 petalMap(vec3 p) {
    p.xz = petalRot(uTime * PETAL_RATE(60)) * p.xz;
    vec3 m = vec3(0.0);
    if (gLayerPick.x > 0.0) m += gLayerPick.x * seedHead(p, 1.5, 1.5, 1.0);
    if (gLayerPick.y > 0.0) m += gLayerPick.y * seedHead(p, 0.85, 1.9, 1.6);
    if (gLayerPick.z > 0.0) m += gLayerPick.z * seedCluster(p);
    return m;
}

//#include lib_petal_march

void main() {
    vec2 uv = view();
    petalSetup();
    gPetalStride = 0.6;
    gPetalFall = 2.0;
    vec3 ro;
    vec3 rd;
    camAround(uv, 4.6, CAM_SPIN_UNIT, ro, rd);
    fragColor = petalFinish(petalTrace(ro, rd), 1.0);
}
