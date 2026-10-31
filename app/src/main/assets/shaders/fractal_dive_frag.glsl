#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Fractal Dive: a fly-through corridor bored through a space-filling fold
// fractal (box fold by fract() then sphere inversion, the Apollonian /
// pseudo-Kleinian branch of the mandelbox family). The first fold tiles the
// world with period 4 along z (2 / FD_WSCALE), a power of two, so the rig's
// 8192 travel wrap is invisible. The corridor is a cylinder subtracted around the
// z axis, wide enough for the rig's +-2 wander. Flown with camFly.
//
// ---- audio ------------------------------------------------------------------
//
// Kick (SND_KICK_ENV) pulses the inversion scale a little: the walls breathe. Drop
// (SND_HELD_SURGE) twists every fold, as a bump on the envelope so it glides in and
// out. The fold variation (inversion scale, inner fold size) follows uOrbit,
// which the CPU re-targets on a section boundary and eases over 3 s: there is
// no section counter to hash, so that is the held step. The section envelope
// (SND_HELD_HARD_CUT) swells the inner folds on the same bump shape while it lands. Brightness follows SND_ENERGY_TREND only.

#define FD_MAX_STEPS 96
#define FD_ITERS 7
/** World to fractal space. The first fract() has period 2 in fractal space, so 4 in the world. */
#define FD_WSCALE 0.5
#define FD_HOLE 3.4
#define FD_FAR 22.0
#define FD_FOG 0.10

float gS;
float gBox;
mat2 gTwA;
mat2 gTwB;
float gTrap;

float map(vec3 w) {
    vec3 p = w * FD_WSCALE;
    float scale = 1.0;
    float trap = 1e9;
    for (int i = 0; i < FD_ITERS; i++) {
        // Box fold, then sphere inversion. The first fold is the world tile,
        // so it stays size 1 and untwisted (the twists come after the
        // inversion) and the z period holds; the inner folds take gBox.
        float b = i == 0 ? 1.0 : gBox;
        p = b * (-1.0 + 2.0 * fract(0.5 * p / b + 0.5));
        float r2 = dot(p, p);
        trap = min(trap, r2);
        float k = gS / max(r2, 1e-4);
        p *= k;
        scale *= k;
        p.xy = gTwA * p.xy;
        p.yz = gTwB * p.yz;
    }
    gTrap = trap;
    float de = 0.25 * abs(p.y) / scale / FD_WSCALE;
    // Bore the corridor. smax overestimates inside the cavity a little, which
    // the 0.75 step scale below absorbs.
    return smax(de, FD_HOLE - length(w.xy), 0.35);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    vec2 uv = view();

    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float sect = clamp(SND_HELD_HARD_CUT, 0.0, 1.0);
    // 0 at the instant of the event and at its end, 1 in between: an envelope
    // with an instant rise, reshaped so the geometry never snaps.
    float dropBump = 4.0 * drop * (1.0 - drop);
    float sectBump = sin(3.14159 * sect);

    gS = 1.22 + 0.10 * uOrbit.x + 0.03 * sin(uTime * 0.11) + 0.035 * kick;
    gBox = 1.0 + 0.10 * uOrbit.y + 0.04 * sin(uTime * 0.07 + 0.8) + 0.10 * sectBump;
    // Untwisted at rest: any standing rotation compounds over the folds into
    // noise, so only the drop turns them.
    float twA = 0.30 * dropBump;
    float twB = 0.14 * dropBump;
    gTwA = rot2(twA);
    gTwB = rot2(twB);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // The world tiles every 4 along z, so this shift is invisible and exact
    // (power of two). Without it, far down the 8192 travel a highp z has only
    // ~0.001 of fraction left, which quantises every sample and the normals.
    ro.z = mod(ro.z, 4.0);

    float t = 0.02;
    float hitT = -1.0;
    float trap = 1.0;
    float glow = 0.0;
    float used = 0.0;
    for (int i = 0; i < FD_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (t > FD_FAR) break;
        used = float(i);
        float d = map(ro + rd * t);
        float eps = 0.0009 * t + 0.0004;
        if (d < eps) {
            hitT = t;
            trap = gTrap;
            break;
        }
        glow += exp(-d * 18.0) * 0.012;
        t += max(d * 0.75, eps);
    }

    float hue = 0.62 + 0.04 * sin(uTime * 0.03) + 0.06 * uOrbit.y;
    // The fog glows toward the far end of the corridor, so the dive has a
    // destination rather than a black hole.
    float ahead = pow(max(rd.z, 0.0), 12.0);
    vec3 fogCol = pal(hue) * (0.05 + 0.22 * ahead) + vec3(0.004, 0.004, 0.008);
    vec3 col = fogCol;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, 0.0015 * hitT + 0.0006);
        float band = log(max(trap, 1e-4)) * 0.16;
        vec3 base = pal(hue + 0.25 + band);
        // A headlamp at the eye, plus a cool fill from the corridor axis.
        float diff = max(dot(n, -rd), 0.0);
        vec3 axisDir = normalize(vec3(-p.xy, 0.0) + 1e-4);
        float fill = 0.5 + 0.5 * dot(n, axisDir);
        float spec = pow(max(dot(reflect(rd, n), -rd), 0.0), 24.0);
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.2, 0.25, 1.0);
        col = base * (0.3 + 0.9 * diff) * ao + pal(hue + 0.5) * fill * 0.12 * ao + vec3(spec) * 0.35 * ao;
        // Trap glow on the thinnest shells: the filigree lights up.
        col += pal(hue + 0.1) * (1.0 - smoothstep(0.0, 0.06, trap)) * 0.25;
        col = mix(col, fogCol, 1.0 - exp(-hitT * FD_FOG));
    }
    // Light threaded past the walls, coloured off the corridor's hue.
    col += pal(hue + 0.15) * glow * (0.6 + 0.3 * swell);

    col *= 0.85 + 0.2 * swell;
    col *= 0.72 + 0.28 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
