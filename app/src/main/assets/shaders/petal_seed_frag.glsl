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
// Morph lifts the florets and opens them, from closed beads on the skin to
// open cups standing clear of it. Worst case 96 steps and 4 sheets, no noise.

vec3 petalMap(vec3 p) {
    p.xz = petalRot(uTime * PETAL_RATE(60)) * p.xz;
    float rho = max(length(p), 1.0e-4);
    float lat = asin(clamp(p.y / rho, -1.0, 1.0));
    float lon = petalAtan(p.xz);
    // Mercator's height: a spiral on the ball is a straight line in (lon, mer), and a cell is square at every latitude.
    float cl = clamp(lat, -1.35, 1.35);
    float mer = log(tan(0.7853982 + 0.5 * cl));
    vec2 g = vec2(8.0 * lon + 13.0 * mer, 13.0 * lon - 8.0 * mer) / PETAL_TAU;
    vec2 c = fract(g) - 0.5;
    // A cell's side on the ball, world units: 15.264 is the length of (8, 13).
    float side = 1.5 * cos(cl) * PETAL_TAU / 15.264;
    // The cell's floret frame: across the skin, and up from it.
    vec3 q = vec3(c * side, rho - 1.5);
    float lift = side * mix(0.05, 0.6, gPetalForm);
    float cr = 0.4 * side;
    // How high the cup's wall rises, in its radius: near 2 a closed bead, under 1 a shallow cup.
    float lip = mix(1.75, 0.8, gPetalForm);
    vec3 b = q - vec3(0.0, 0.0, lift + cr);
    // No florets where the cells crowd at the poles.
    float beyond = max(q.z - lift - lip * cr, (abs(lat) - 1.25) * rho);
    vec3 cup = vec3(petalCut(length(b) - cr, beyond), (q.z - lift) / (lip * cr), 8.0 * petalAtan(b.xy) / PETAL_TAU);
    // Within the florets' layer a neighbour's cup may be nearer than this cell's.
    cup = petalHold(cup, (0.5 - max(abs(c.x), abs(c.y))) * side + 0.1 * side + max(q.z - lift - 2.0 * cr, 0.0));
    vec3 skin = vec3(rho - 1.5, 0.0, g.x);
    return petalNear(cup, skin);
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
