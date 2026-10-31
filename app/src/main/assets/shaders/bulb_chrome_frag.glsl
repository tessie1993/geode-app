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
//#include lib_bulb

// Bulb Chrome: a power-7 bulb as polished liquid metal in a photo studio. The
// surface is almost pure reflection of a procedural studio - black floor,
// graded ceiling, a horizontal softbox band, four tall coloured strip lights
// round the walls and a bright overhead panel - with a thin-film iridescence
// at mid angles, like heat-blued steel. The creases go dark from the step
// count, which is what keeps chrome from looking like a flat mirror. The
// background is the same studio, dimmed and seen directly.
//
// The power-7 body also takes a per-iteration POLAR PHASE (lib_bulb's
// bulbPow phase): an angle added after every multiply, which slews the lobes
// sideways and morphs the body rather than merely rotating it.
//
// ---- audio ------------------------------------------------------------------
//
// MIDS swirl the metal. The mid band (sndSpectrum(0.57), steadied with SND_MID_PRESENCE)
// drives the polar phase, so chords and vocals melt the lobes into a swirl
// and they set back into seven-fold order in the sparse passages. Hats
// (SND_HAT_ENV) tighten the specular strips. Brightness follows (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the studio lights' slow turn. Complexity (uMorph)
// adds up to two fractal iterations (bulbIters). Chaos (uWarp, uTurbulence,
// uTwist) warps the metal (bulbChaos, estimate divided by bulbChaosLip), and
// turbulence jitters the azimuth phase. Energy (uIntensity) scales the mid
// swirl. Colour is pal() (strip lights, film); Space is the rig (camAround).

#define BM_MAX_STEPS 128
#define BM_BOUND 1.3
#define BM_CAM_R 3.5
const float BM_SPIN = 5.0 * CAM_SPIN_UNIT;

vec2 gPhase;
float gStudioTurn;
float gHue;
float gStrip;
int gIters;

float map(vec3 p) {
    vec4 tr;
    return bulbDE(bulbChaos(p), 7.0, 7.0, gPhase, gIters, tr) / bulbChaosLip();
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

// A HIGH-KEY studio: a white cyclorama that grades from a mid-grey floor up
// to a bright ceiling, broad softboxes and the coloured strips. Chrome is only
// ever as bright as what it reflects; in a black room the metal read as a
// dark lump that could be any Bulb. In this one it is the family's only
// bright, mirror-silver body.
vec3 studio(vec3 r) {
    vec3 c = mix(vec3(0.10, 0.10, 0.11), vec3(0.62, 0.64, 0.68), smoothstep(-0.35, 0.8, r.y));
    float ang = atan(r.z, r.x + 1e-12) + gStudioTurn;
    float strips = pow(max(1.0 - abs(sin(ang * 2.0)), 0.0), gStrip);
    c += pal(gHue + 0.25 * floor(mod((ang + 3.14159) / 1.5708, 4.0))) * strips * smoothstep(-0.3, 0.2, r.y) * 1.3;
    c += vec3(1.0, 0.97, 0.92) * pow(max(1.0 - abs(r.y - 0.18) / 0.2, 0.0), 2.0) * 1.1;
    c += vec3(1.0) * smoothstep(0.6, 0.95, r.y) * 1.6;
    // The floor's dark seam at the horizon, which is what makes a mirror read as one.
    c *= 0.35 + 0.65 * smoothstep(0.0, 0.08, abs(r.y + 0.05));
    return c;
}

void main() {
    vec2 uv = view();
    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float mid = clamp(0.35 * sndSpectrum(0.57) + 0.65 * SND_MID_PRESENCE / 1.5, 0.0, 1.0);
    float hat = clamp(SND_HAT_ENV, 0.0, 1.0);
    float jitter = 0.15 * clamp(uTurbulence, 0.0, 1.0) * sin(bulbTime() * 1.7);
    gPhase = vec2(0.85 * mid * bulbEnergy(), jitter);
    gStudioTurn = 0.03 * bulbTime();
    gHue = 0.55 + 0.04 * sin(bulbTime() * 0.013);
    gStrip = 3.0 + 14.0 * hat * bulbEnergy();
    gIters = bulbIters(6);

    vec3 ro;
    vec3 rd;
    camAround(uv, BM_CAM_R, BM_SPIN, ro, rd);

    float hitT = -1.0;
    float used = 0.0;
    vec2 tb = bulbBound(ro, rd, vec3(0.0), BM_BOUND);
    if (tb.y > 0.0) {
        float t = tb.x;
        for (int i = 0; i < BM_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > tb.y) break;
            used = float(i);
            float d = map(ro + rd * t);
            float eps = bulbEps(t);
            if (d < eps) {
                hitT = t;
                break;
            }
            t += max(d * 0.85, eps * 0.5);
        }
    }

    vec3 col = studio(rd) * 0.55;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, bulbEps(hitT));
        float ndv = max(dot(n, -rd), 0.0);
        float ao = bulbStepAO(used);
        vec3 metal = mix(vec3(0.9, 0.92, 0.95), pal(gHue + 0.1), 0.12);
        float fres = 0.75 + 0.25 * pow(1.0 - ndv, 4.0);
        col = studio(reflect(rd, n)) * metal * fres * ao;
        // Heat-blued film at mid angles.
        col += pal(gHue + 0.6 + 0.5 * ndv) * ndv * (1.0 - ndv) * 0.35 * ao;
    }

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
