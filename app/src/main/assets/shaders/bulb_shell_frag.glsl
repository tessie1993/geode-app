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
//#include lib_sdf3
//#include lib_bulb

// Bulb Shell: a power-4 bulb hollowed to a thin skin, like a lantern cast in
// dark bronze, with round windows cut through it and a small sun burning at
// its heart. The skin is the band just under the surface (|d + offset| <
// thickness), so it follows every lobe of the fractal and leaves the whole
// interior empty. The windows sit on a staggered lattice of latitude rings,
// eight to a ring; their distance is the angle to the nearest centre times the
// radius, close enough to 1-Lipschitz to march.
//
// Grid cell INSIDE x GLOW (see lib_bulb). The eye is INSIDE the lantern,
// circling the sun between it and the skin, and looks along the wall. Nothing
// is lit by a normal: the inner wall glows with the sun's light by its
// distance from the centre, the air between carries a halo of it (the ray's
// closest approach to the sun, analytic), and the windows are round DARK
// holes onto the night outside, ringed with light at their edges. A warm
// glowing room pierced by black discs: the inverse of every bulb seen from
// outside. Which face a hit belongs to is read from the ray (heading out from
// the centre: the inner face) instead of from a normal, so the style takes no
// normal at all.
//
// The sun is an analytic sphere, not part of the march: one quadratic, and
// whichever of it or the skin is nearer wins the pixel.
//
// ---- audio ------------------------------------------------------------------
//
// SUB-BASS opens the windows. The sub band (sndSpectrum(0.10), steadied with
// SND_BASS_PRESENCE) widens every aperture from a pinhole to most of its cell, so a
// sub drop throws the lantern open onto the night and the quiet passages
// leave it shuttered and glowing. The sun's colour walks with uKeyHue.
// Brightness follows SND_ENERGY_TREND only: the sun burns a little hotter in
// loud passages.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the slow turn of the window lattice. Complexity
// (uMorph) adds up to two fractal iterations (bulbIters). Chaos (uWarp,
// uTurbulence, uTwist) warps the bulb before it is hollowed (bulbChaos,
// estimate divided by bulbChaosLip). Energy (uIntensity) scales the sub's
// reach on the apertures. Colour is tone(), the lantern ramp pal() tints.
// Space is the rig: travel carries the eye round the sun, look and shake ride
// on top (bulbAim), the dolly swings it a little nearer the wall.

#define BH_MAX_STEPS 128
#define BH_BOUND 1.35
#define BH_OFF 0.03
#define BH_THICK 0.018
#define BH_SECTORS 8.0
#define BH_RING 0.62
/** The sun's radius, and the eye's circle round it (inside the skin, outside the sun). */
#define BH_CORE 0.13
#define BH_EYE_R 0.38
const float BH_SPIN = -5.0 * CAM_SPIN_UNIT;

float gWin;
float gLatticeTurn;
int gIters;
vec4 gTrap;
float gWinD;

// Its own ramp: candle amber, ember red and a smoky brown.
vec3 tone(float t) {
    return bulbRamp(t, vec3(0.62, 0.40, 0.22), vec3(0.35, 0.25, 0.15), vec3(1.0, 0.8, 0.6), vec3(0.02, 0.10, 0.20));
}

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

void main() {
    vec2 uv = view();
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float sub = clamp(0.6 * sndSpectrum(0.10) + 0.4 * SND_BASS_PRESENCE / 1.5, 0.0, 1.0);
    gWin = 0.04 + 0.17 * clamp(sub * bulbEnergy(), 0.0, 1.0);
    gLatticeTurn = 0.04 * bulbTime();
    gIters = bulbIters(6);

    // INSIDE: circle the sun and look along the wall, a little outward.
    float a = uCamTravel * BH_SPIN;
    vec3 outward = vec3(cos(a), 0.0, sin(a));
    vec3 tangent = vec3(-sin(a), 0.0, cos(a));
    vec3 eye = outward * BH_EYE_R * (1.0 + 0.15 * uCamDolly) + vec3(0.0, 0.08 * sin(2.0 * a), 0.0);
    vec3 fwd = normalize(tangent * 0.9 + outward * 0.18 + vec3(0.0, 0.12, 0.0));
    vec3 ro;
    vec3 rd;
    bulbAim(uv, eye, eye + fwd, ro, rd);

    float hue = 0.08 * uKeyStrength * sin(BULB_TAU * uKeyHue);
    vec3 sunCol = mix(tone(hue), vec3(1.0, 0.9, 0.7), 0.45) * (1.3 + 0.4 * swell);

    // The sun, analytically.
    vec2 tc = bulbBound(ro, rd, vec3(0.0), BH_CORE);
    float coreT = tc.y > 0.0 ? tc.x : 1e9;

    float hitT = -1.0;
    float used = 0.0;
    float winD = 1.0;
    float endT = 1e9;
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
    if (hitT > 0.0) endT = hitT;
    if (coreT < endT) endT = coreT;

    // Night through the windows.
    vec3 col = vec3(0.006, 0.007, 0.012) + tone(hue + 0.55) * 0.01;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        float ao = bulbStepAO(used);
        if (dot(rd, p) > 0.0) {
            // Inner face: the sun's light on the wall, falling off with distance.
            float fall = 1.0 / (1.0 + 4.0 * dot(p, p));
            float grain = 0.78 + 0.22 * sin(sqrt(trap.x) * 14.0);
            col = mix(tone(hue + 0.05), vec3(0.6, 0.45, 0.3), 0.4) * sunCol * fall * grain * 1.5 * ao;
            col += sunCol * exp(-max(winD, 0.0) * 45.0) * 0.5;
        } else {
            // A lobe's outer face seen back through a window: dark bronze,
            // lit only by the spill at its own window edges.
            col = tone(hue + 0.03) * 0.05 * ao + sunCol * exp(-max(winD, 0.0) * 45.0) * 0.25;
        }
    } else if (coreT < 1e8) {
        float ndv = max(dot(normalize(ro + rd * coreT), -rd), 0.0);
        col = sunCol * (0.7 + 0.3 * ndv);
    }

    // The air carries the sun's halo: the ray's closest approach to the
    // centre, if it came that close before it stopped.
    float tMin = max(-dot(ro, rd), 0.0);
    float halo = tMin < endT ? exp(-max(length(ro + rd * tMin) - BH_CORE, 0.0) * 7.0) : 0.0;
    col += sunCol * halo * 0.3;

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
