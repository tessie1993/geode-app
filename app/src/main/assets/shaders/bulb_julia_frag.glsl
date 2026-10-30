#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_dmt
//#include lib_bulb

// Bulb Julia: a power-6 Julia bulb - the orbit starts at the point and adds a
// FIXED c - so the body is not the one bulb but a family of them, one per c,
// and walking c sculpts it continuously: lobes bud, fuse, hollow and pinch
// off. Framed close (camAround at 2.7, a third of the body past the edges) and
// shaded with lib_dmt's jewel material, the orbit trap as its banding
// coordinate and the trap's closeness to the pole as its translucency. The
// space behind is velvet black with a coloured halo hugging the silhouette.
//
// ---- audio ------------------------------------------------------------------
//
// PITCH walks c. The detected key (uKeyHue, a pitch class eased over 3 s) is
// an angle on a closed loop of c values, so a key change carries the body
// round the loop to a new form and a song that sits in one key holds one
// sculpture. Each pitch change (uEvents.w) kicks c off the loop sideways on a
// bump envelope, so melody makes the surface flinch and settle. Treble
// sharpens the material's specular (lib_dmt). Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the drift of c round its loop. Complexity (uMorph) adds up to two fractal
// iterations (bulbIters). Chaos (uWarp, uTurbulence, uTwist) warps and twists
// space before the fractal (bulbChaos, estimate divided by bulbChaosLip);
// turbulence also wobbles c itself.
// Energy (uIntensity) scales the pitch kick on c. Colour is pal() throughout; Space is
// the rig (camAround).

#define BJ_MAX_STEPS 128
#define BJ_BOUND 1.4
#define BJ_CAM_R 2.7
const float BJ_SPIN = 6.0 * CAM_SPIN_UNIT;

vec3 gC;
int gIters;
vec4 gTrap;

float map(vec3 p) {
    vec4 tr;
    float d = bulbJuliaDE(bulbChaos(p), gC, 6.0, gIters, tr) / bulbChaosLip();
    gTrap = tr;
    return d;
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

void main() {
    vec2 uv = view();
    float swell = clamp(uSwell, 0.0, 1.5);
    float pe = clamp(uEvents.w, 0.0, 1.0);
    float pitchBump = 4.0 * pe * (1.0 - pe);

    // The loop of c: a tilted figure-of-eight in the horizontal plane, |c|
    // between 0.45 and 0.6, where power-6 Julia bulbs stay connected.
    float a = BULB_TAU * uKeyHue + 0.025 * bulbTime();
    gC = vec3(0.5 * cos(a), 0.12 + 0.22 * sin(2.0 * a), 0.5 * sin(a));
    gC += pitchBump * 0.12 * bulbEnergy() * vec3(-sin(a), 0.7, cos(a));
    // Chaos also wobbles c itself.
    gC += 0.08 * clamp(uTurbulence, 0.0, 1.0) * sin(vec3(1.3, 1.7, 2.3) * bulbTime());
    gIters = bulbIters(7);

    vec3 ro;
    vec3 rd;
    camAround(uv, BJ_CAM_R, BJ_SPIN, ro, rd);

    float hue = 0.3 + 0.05 * sin(bulbTime() * 0.017);

    float hitT = -1.0;
    float used = 0.0;
    float near = 1e3;
    vec4 trap = vec4(1.0);
    vec2 tb = bulbBound(ro, rd, vec3(0.0), BJ_BOUND);
    if (tb.y > 0.0) {
        float t = tb.x;
        for (int i = 0; i < BJ_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > tb.y) break;
            used = float(i);
            float d = map(ro + rd * t);
            near = min(near, d);
            float eps = bulbEps(t);
            if (d < eps) {
                hitT = t;
                trap = gTrap;
                break;
            }
            t += max(d * 0.85, eps * 0.5);
        }
    }

    vec3 col = vec3(0.006, 0.004, 0.010) + pal(hue + 0.6) * 0.02 * (1.0 - length(uv) * 0.4);
    col += dmtHalo(near, hue + 0.4, 7.0, 0.22);

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, bulbEps(hitT));
        float ao = bulbStepAO(used) * (0.45 + 0.55 * smoothstep(0.05, 0.7, trap.x));
        float thin = smoothstep(0.25, 0.0, trap.z);
        float band = 1.4 * sqrt(trap.x) + trap.w;
        col = dmtShade(n, rd, hue, band, ao, thin, clamp(uTrebleSmooth, 0.0, 1.5)) * 0.85;
    }

    col *= 0.8 + 0.22 * swell;
    fragColor = vec4(grade(col), 1.0);
}
