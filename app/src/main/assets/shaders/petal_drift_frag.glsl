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

// Petal Drift: a flight through a slow fall of loose petals, one to a cell of
// a lattice, each its own size and turn, tumbling on its own clock; every
// petal a curved sheet with veins fanning from its foot.
//
// Morph curls the petals, from flat sheets to deep cups. Worst case 96 steps
// and 4 sheets, no noise.

vec3 petalMap(vec3 p) {
    vec3 c = floor(p * 0.5);
    vec3 q0 = p - 2.0 * c - 1.0;
    float h = petalHash(c.xy + 17.0 * mod(c.z, 8.0));
    float h2 = fract(h * 57.31);
    float h3 = fract(h * 13.7);
    vec3 q = q0 - 0.15 * (vec3(h, h2, h3) - 0.5);
    // Each petal tumbles on its own clock, a whole number of turns per wrap.
    q.xy = petalRot(PETAL_TAU * h + uTime * PETAL_RATE(90.0 + 40.0 * floor(3.0 * h2))) * q.xy;
    q.yz = petalRot(PETAL_TAU * h3 + uTime * PETAL_RATE(70.0 + 30.0 * floor(3.0 * h))) * q.yz;
    float size = 0.8 + 0.3 * h2;
    q /= size;
    // A piece of a ball through the petal's middle, curved toward +z, cut to an oval and to the ball's near half.
    float cr = 1.0 / mix(0.8, 3.0, gPetalForm);
    float f = length(q - vec3(0.0, 0.0, cr)) - cr;
    vec2 e = vec2(q.x / 0.28, (q.y - 0.08) / 0.5);
    float o = max((length(e) - 1.0) * 0.28, q.z - cr);
    // Veins fan out from the petal's foot.
    float vein = 30.0 * petalAtan(vec2(q.y + 0.42, q.x)) / PETAL_TAU;
    vec3 m = vec3(petalCut(f, o) * size, length(e), vein);
    // A petal stays inside its cell; a neighbour's is at least a fifth past the wall.
    return petalHold(m, 1.0 - max(max(abs(q0.x), abs(q0.y)), abs(q0.z)) + 0.2);
}

//#include lib_petal_march

void main() {
    vec2 uv = view();
    petalSetup();
    gPetalStride = 0.7;
    gPetalLamp = vec3(0.0, 0.0, 4.0);
    gPetalGap = 8.0;
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    fragColor = petalFinish(petalTrace(petalEye(ro, 1.0), rd), 1.1);
}
