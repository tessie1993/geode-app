#version 300 es
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
//#include lib_bulb

// Bulb Shell: a power-4 bulb hollowed to a thin skin, like a lantern cast in
// dark bronze, with round windows cut through it and a small sun burning at
// its heart. The skin is the band just under the surface (|d + offset| <
// thickness), so it follows every lobe of the fractal. The windows sit on a
// staggered lattice of latitude rings, eight to a ring; their distance is the
// angle to the nearest centre times the radius, close enough to 1-Lipschitz
// to march. Through them the eye sees the core and the inside of the far wall
// lit warm by it; outside, the bronze is lit only by a cool rim and by the
// light spilling from each window's edge.
//
// The core is an analytic sphere, not part of the march: one quadratic, and
// whichever of it or the skin is nearer wins the pixel.
//
// ---- audio ------------------------------------------------------------------
//
// SUB-BASS opens the windows. The sub band (uBandsLo.x, steadied with
// uBassSmooth) widens every aperture from a pinhole to most of its cell, so a
// sub drop throws the lantern open and the quiet passages leave it shuttered
// and glowing through slits. The core's colour walks with uKeyHue. Brightness
// follows uSwell only: the core burns a little hotter in loud passages.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the slow turn of the window lattice. Complexity
// (uMorph) adds up to two fractal iterations (bulbIters). Chaos (uWarp,
// uTurbulence, uTwist) warps the bulb before it is hollowed (bulbChaos,
// estimate divided by bulbChaosLip). Energy (uIntensity) scales the sub's
// reach on the apertures. Colour is pal(); Space is the rig (camAround).

#define BH_MAX_STEPS 128
#define BH_BOUND 1.35
#define BH_CAM_R 3.0
#define BH_OFF 0.03
#define BH_THICK 0.018
#define BH_SECTORS 8.0
#define BH_RING 0.62
const float BH_SPIN = -5.0 * CAM_SPIN_UNIT;

float gWin;
float gLatticeTurn;
float gCore;
int gIters;
vec4 gTrap;
float gWinD;

float map(vec3 p) {
    vec4 tr;
    float db = bulbDE(bulbChaos(p), 4.0, 4.0, vec2(0.0), gIters, tr) / bulbChaosLip();
    gTrap = tr;
    float shell = abs(db + BH_OFF) - BH_THICK;
    // The nearest window centre on the staggered lattice.
    float r = max(length(p), 1e-6);
    float lat = asin(clamp(p.y / r, -1.0, 1.0));
    float lon = atan(p.z, p.x + 1e-12) + gLatticeTurn;
    float cell = BULB_TAU / BH_SECTORS;
    float ring = clamp(floor(lat / BH_RING + 0.5), -1.0, 1.0);
    float stagger = mod(ring, 2.0) * 0.5 * cell;
    float dl = lat - ring * BH_RING;
    float dn = mod(lon - stagger + 0.5 * cell, cell) - 0.5 * cell;
    float win = length(vec2(dn * cos(lat), dl)) * r - gWin;
    gWinD = win;
    return max(shell, -win);
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

void main() {
    vec2 uv = view();
    float swell = clamp(uSwell, 0.0, 1.5);
    float sub = clamp(0.6 * uBandsLo.x + 0.4 * uBassSmooth / 1.5, 0.0, 1.0);
    gWin = 0.04 + 0.17 * clamp(sub * bulbEnergy(), 0.0, 1.0);
    gLatticeTurn = 0.04 * bulbTime();
    gCore = 0.22;
    gIters = bulbIters(6);

    vec3 ro;
    vec3 rd;
    camAround(uv, BH_CAM_R, BH_SPIN, ro, rd);

    float hue = 0.07 + 0.08 * uKeyStrength * sin(BULB_TAU * uKeyHue);
    vec3 coreCol = mix(pal(hue), vec3(1.0, 0.9, 0.7), 0.45) * (1.3 + 0.4 * swell);

    // The core, analytically.
    vec2 tc = bulbBound(ro, rd, vec3(0.0), gCore);
    float coreT = tc.y > 0.0 ? tc.x : 1e9;

    float hitT = -1.0;
    float used = 0.0;
    float winD = 1.0;
    vec4 trap = vec4(1.0);
    vec2 tb = bulbBound(ro, rd, vec3(0.0), BH_BOUND);
    if (tb.y > 0.0) {
        float t = tb.x;
        float tEnd = min(tb.y, coreT);
        for (int i = 0; i < BH_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > tEnd) break;
            used = float(i);
            float d = map(ro + rd * t);
            float eps = bulbEps(t);
            if (d < eps) {
                hitT = t;
                trap = gTrap;
                winD = gWinD;
                break;
            }
            t += max(d * 0.8, eps * 0.5);
        }
    }

    // Night behind it, with the faintest warm haze around the lantern.
    vec3 col = vec3(0.008, 0.009, 0.014) + pal(hue + 0.55) * 0.015;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, bulbEps(hitT));
        vec3 toCore = -normalize(p);
        float inner = step(0.0, dot(n, toCore));
        float ao = bulbStepAO(used);
        if (inner > 0.5) {
            // Inside face: lit by the core, falling off with distance.
            float fall = 1.0 / (1.0 + 5.0 * dot(p, p));
            float dif = max(dot(n, toCore), 0.0);
            col = mix(pal(hue + 0.05), vec3(0.6, 0.45, 0.3), 0.5) * coreCol * dif * fall * 1.6 * ao;
        } else {
            // Outside face: dark bronze, a cool rim, and the spill at each window.
            float ndv = max(dot(n, -rd), 0.0);
            float rim = pow(1.0 - ndv, 3.0);
            vec3 key = normalize(vec3(-0.5, 0.6, -0.6));
            float dif = max(dot(n, key), 0.0);
            vec3 bronze = mix(vec3(0.20, 0.12, 0.06), pal(hue + 0.03) * 0.25, 0.3);
            float spec = pow(max(dot(reflect(rd, n), key), 0.0), 24.0);
            col = bronze * (0.15 + 0.6 * dif) * ao;
            col += bronze * 2.0 * spec * ao;
            col += pal(hue + 0.6) * rim * 0.18;
            col += coreCol * exp(-max(winD, 0.0) * 45.0) * 0.45;
            col *= 0.9 + 0.2 * sin(sqrt(trap.x) * 14.0);
        }
    } else if (coreT < 1e8) {
        float ndv = max(dot(normalize(ro + rd * coreT), -rd), 0.0);
        col = coreCol * (0.7 + 0.3 * ndv);
    }

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
