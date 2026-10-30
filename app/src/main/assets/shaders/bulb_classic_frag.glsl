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
//#include lib_bulb

// Bulb Classic: the power-8 bulb itself, framed whole and turned slowly on the
// shared rig (camAround). The family's free style and its cheapest: six
// fractal iterations (four at low Detail), one bounded march, one normal, no
// second pass. Lit like a sculpture on a plinth - a warm key from the upper
// right, a cool sky fill from above, a thin rim from behind - and coloured by
// the orbit trap, so the lobes carry nested bands of related hue rather than
// one flat tone. The background is a dim vertical wash with a halo hugging
// the silhouette (the closest a missed ray came).
//
// ---- audio ------------------------------------------------------------------
//
// Bass (uBandsLo.y, steadied with uBassSmooth) swells the POLAR power from 8
// toward 9.1: the lobes bloom and the crust tightens on every low note, then
// relax. Only the polar power moves - the azimuth power stays 8 so no seam
// opens (see lib_bulb). The key (uKeyHue, weighted by uKeyStrength) drifts the
// palette a little. Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the palette drift. Complexity (uMorph) adds up to two fractal
// iterations (bulbIters). Chaos (uWarp, uTurbulence, uTwist) warps and twists
// space before the fractal (bulbChaos, estimate divided by bulbChaosLip).
// Energy (uIntensity) scales the bass swell. Colour is pal() throughout; Space is
// the rig (camAround).

#define BC_MAX_STEPS 128
#define BC_BOUND 1.25
#define BC_CAM_R 3.4
const float BC_SPIN = 4.0 * CAM_SPIN_UNIT;

float gPow;
int gIters;
vec4 gTrap;

float map(vec3 p) {
    vec4 tr;
    float d = bulbDE(bulbChaos(p), gPow, 8.0, vec2(0.0), gIters, tr) / bulbChaosLip();
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
    float bass = clamp(0.6 * uBandsLo.y + 0.4 * uBassSmooth, 0.0, 1.2);
    gPow = 8.0 + 0.9 * bass * bulbEnergy();
    gIters = bulbIters(6);

    vec3 ro;
    vec3 rd;
    camAround(uv, BC_CAM_R, BC_SPIN, ro, rd);

    float hue = 0.04 * sin(bulbTime() * 0.021) + 0.12 * uKeyStrength * sin(BULB_TAU * uKeyHue);

    float hitT = -1.0;
    float used = 0.0;
    float near = 1e3;
    vec4 trap = vec4(1.0);
    vec2 tb = bulbBound(ro, rd, vec3(0.0), BC_BOUND);
    if (tb.y > 0.0) {
        float t = tb.x;
        for (int i = 0; i < BC_MAX_STEPS; i++) {
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
            t += max(d * 0.9, eps * 0.5);
        }
    }

    // Sky: a dim wash, darker below the horizon, with the silhouette halo.
    vec3 col = mix(pal(hue + 0.55) * 0.025, pal(hue + 0.45) * 0.09, smoothstep(-0.6, 0.7, rd.y));
    col += pal(hue + 0.1) * exp(-max(near, 0.0) * 9.0) * 0.18;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, bulbEps(hitT));
        vec3 key = normalize(vec3(0.55, 0.72, -0.42));
        float dif = max(dot(n, key), 0.0);
        float sky = 0.5 + 0.5 * n.y;
        float ndv = max(dot(n, -rd), 0.0);
        float rim = pow(1.0 - ndv, 3.0);
        float spec = pow(max(dot(reflect(rd, n), key), 0.0), 20.0);
        // Creases: the trap's closest approach plus the step count.
        float ao = bulbStepAO(used) * (0.4 + 0.6 * smoothstep(0.12, 0.95, trap.x));
        vec3 base = pal(hue + 0.55 * sqrt(trap.x) + 0.6 * trap.y);
        col = base * dif * vec3(1.0, 0.92, 0.80) * 0.95;
        col += base * sky * vec3(0.55, 0.65, 0.85) * 0.28;
        col += pal(hue + 0.5) * rim * 0.30;
        col += vec3(1.0, 0.95, 0.88) * spec * 0.25;
        col *= ao;
    }

    col *= 0.82 + 0.2 * swell;
    col *= 0.75 + 0.25 * smoothstep(2.2, 0.3, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
