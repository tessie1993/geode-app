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

// Crystal Canyon: a flight down a canyon whose walls are faceted crystal
// pillars, hexagonal prisms with pointed tips standing on a grid. Short ones
// carpet the floor, and they rise into walls on both sides.
//
// The camera is the shared rig (camFly). The canyon runs straight down +z and
// every z term has a power-of-two period, so the rig's 8192 wrap is invisible.
//
// ---- audio ------------------------------------------------------------------
//
// Kick (uHits.x) grows the crystals just ahead of the eye. That is geometry:
// their tips shoot up and they thicken a little, and nothing gets brighter.
// A pitch change (uEvents.w) slides the crystal hue. Air (uBandsHi.z) thickens
// and brightens the sparkle glints on the facets. Overall level rides uSwell.
//
// ---- why the estimate is safe ----------------------------------------------
//
// Each cell measures only its own pillar. A neighbour can be taller, so the
// distance is clamped to the cell boundary unless the point is above every
// neighbour's possible tip (hiN). A sphere around the eye is carved out of
// everything, so the eye can never sit inside a crystal.

#define CC_MAX_STEPS 96
#define CC_FAR 46.0
#define CC_TAU 6.2831853
#define CC_FLOOR -3.6
/** How far the wall pillars rise above the floor. */
#define CC_RISE 9.5
/** Half-width of the open floor before the walls start to climb. */
#define CC_WIDTH 4.4
/** Horizontal run of the slope from the floor up to the wall tops. */
#define CC_BANK 3.4
/** Tip growth on a full kick, world units. */
#define CC_KICK 1.5
#define CC_BUBBLE 0.9
/** Cells along z before the hash repeats: 1024 world units, which divides 8192. */
#define CC_ZWRAP 1024.0

float gH;
float gH2;
float gWall;
float gTop;
float gMat;
/** The eye, z rebased into 0..1024 (see main). */
vec3 gEye;

float canyonWidth(float z) {
    return CC_WIDTH + 0.7 * sin(z * (CC_TAU / 64.0));
}

float wallAt(float ax, float z) {
    float w = canyonWidth(z);
    return smoothstep(w, w + CC_BANK, ax);
}

float map(vec3 p) {
    vec2 cell = floor(p.xz);
    vec2 cc = cell + 0.5;
    vec2 f = p.xz - cc;
    uint u = uhash3(uvec3(ivec3(int(cell.x), 0, int(mod(cell.y, CC_ZWRAP)))));
    float h = uhashUnit(u);
    float h2 = uhashUnit(uhash(u));

    float ax = abs(cc.x);
    float wall = wallAt(ax, cc.y);
    float kickAll = clamp(uHits.x, 0.0, 1.0);
    // Only the crystals just ahead of the eye answer the kick.
    float kick = kickAll * exp(-abs(cc.y - gEye.z - 4.0) * 0.14);
    float top = CC_FLOOR + 0.25 + 0.7 * h + CC_RISE * wall * (0.62 + 0.38 * h2)
              + CC_KICK * kick * (0.3 + 0.7 * wall);
    // Largest apothem 0.341 plus the 0.07 jitter stays inside the half-cell.
    float r = (0.23 + 0.08 * h2) * (1.0 + 0.1 * kick);

    vec2 q = rot2(h2 * CC_TAU) * (f - (vec2(h, h2) - 0.5) * 0.1);
    vec2 aq = abs(q);
    // Hexagonal cross-section (max of face planes: a lower bound at corners).
    float pr = max(aq.x * 0.8660254 + aq.y * 0.5, aq.y);
    float side = pr - r;
    // Pointed tip: the face planes tilted by 1.3, normalised by sqrt(1 + 1.69).
    float cap = (p.y - top + 1.3 * pr) / 1.64;
    float dc = max(side, cap);

    // A neighbour one cell further out (plus the width's z wobble) is the
    // tallest one could be.
    float hiN = CC_FLOOR + 0.95 + CC_RISE * wallAt(ax + 1.1, cc.y) + CC_KICK * kickAll;
    vec2 af = abs(f);
    float toEdge = 0.5 - max(af.x, af.y);
    float d = min(dc, max(toEdge + 0.02, p.y - hiN));

    float df = p.y - CC_FLOOR;
    gMat = df < dc ? 1.0 : 0.0;
    d = min(d, df);
    d = max(d, CC_BUBBLE - length(p - gEye));

    gH = h;
    gH2 = h2;
    gWall = wall;
    gTop = top;
    return d;
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

vec3 skyColour(vec3 rd, float hueBase) {
    float up = clamp(rd.y, -1.0, 1.0);
    vec3 col = mix(pal(hueBase + 0.55) * 0.10, pal(hueBase + 0.70) * 0.03, smoothstep(0.0, 0.7, up));
    // The glow at the end of the canyon, straight down the travel axis.
    float ahead = max(dot(rd, normalize(vec3(0.0, 0.12, 1.0))), 0.0);
    col += pal(hueBase + 0.08) * (0.30 * pow(ahead, 10.0) + 0.08 * ahead * ahead);
    vec3 sp = rd * 140.0;
    float s = hashCell(ivec3(floor(sp)));
    float star = (1.0 - smoothstep(0.0, 0.3, length(fract(sp) - 0.5))) * step(0.996, s);
    col += vec3(0.8) * star * smoothstep(0.05, 0.3, up) * (0.6 + 0.4 * sin(uTime * 3.0 + s * 500.0));
    return col;
}

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 1024, so this shift is
    // invisible and exact (power of two). Without it, far down the 8192
    // travel a highp z has ~0.001 of fraction left: every sample and the
    // finite-difference normals quantise into speckle.
    ro.z = mod(ro.z, CC_ZWRAP);
    gEye = ro;

    float swell = clamp(uSwell, 0.0, 1.5);
    float energy = clamp(uEnergySmooth, 0.0, 1.5);
    float air = clamp(uBandsHi.z, 0.0, 1.0);
    float hueBase = 0.22 * clamp(uEvents.w, 0.0, 1.0);

    float t = 0.05;
    float hitT = -1.0;
    for (int i = 0; i < CC_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        float d = map(p);
        if (d < 0.0015 + 0.0008 * t) {
            hitT = t;
            break;
        }
        t += d * 0.9;
        if (t > CC_FAR) break;
    }

    vec3 sky = skyColour(rd, hueBase);
    vec3 col = sky;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.001 * hitT));
        float occ = 0.0;
        float sc = 1.0;
        for (int k = 0; k < 3; k++) {
            float hk = 0.1 + 0.16 * float(k);
            occ += (hk - map(p + n * hk)) * sc;
            sc *= 0.65;
        }
        float ao = clamp(1.0 - 1.6 * occ, 0.25, 1.0);
        // Restore the hit cell's globals after the normal and AO taps.
        map(p);

        vec3 L = normalize(vec3(-0.35, 0.85, 0.4));
        float dif = max(dot(n, L), 0.0);
        float fres = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.0);
        float spec = pow(max(dot(reflect(rd, n), L), 0.0), 48.0);
        float hue = hueBase + 0.18 * gH + 0.22 * gWall + 0.08 * sin(p.z * (CC_TAU / 32.0));
        // Fake refraction: the bent ray picks a spot on the palette, and the
        // three channels read it at staggered offsets, which is the dispersion.
        vec3 rf = refract(rd, n, 0.67);
        float kp = hue + 0.45 * (0.5 + 0.5 * dot(rf, vec3(0.42, 0.62, 0.66)));
        vec3 prism = vec3(pal(kp).r, pal(kp + 0.07).g, pal(kp + 0.14).b);

        if (gMat > 0.5) {
            col = pal(hue + 0.55) * 0.05 * ao + prism * fres * 0.25;
        } else {
            // Tips glow from inside; the shaft stays darker glass.
            float tipGlow = smoothstep(gTop - 2.4, gTop, p.y);
            vec3 body = pal(hue) * (0.10 + 0.30 * dif) * ao + pal(hue + 0.1) * tipGlow * (0.18 + 0.12 * swell);
            col = body + prism * (0.15 + 0.85 * fres) * (0.35 + 0.4 * ao)
                + mix(vec3(1.0), prism, 0.4) * spec * 0.55;
        }

        // Glints: sparse points on the facets that twinkle; air brings more of
        // them and brightens them. Each is a few pixels, never the frame.
        // z wrapped like the cells (1024 * 7 is whole, so the seam is a cell
        // wall), or the glints would re-hash at every rebase of the eye.
        vec3 gp = vec3(p.xy, mod(p.z, CC_ZWRAP)) * 7.0;
        float g = hashCell(ivec3(floor(gp)));
        float tw = 0.5 + 0.5 * sin(uTime * (9.0 + 13.0 * g) + g * 60.0);
        float pt = (1.0 - smoothstep(0.0, 0.32, length(fract(gp) - 0.5)));
        col += mix(vec3(1.0), prism, 0.3) * step(0.93 - 0.06 * air, g) * pt * tw
             * (0.12 + 1.4 * air) * (0.3 + 0.7 * fres + spec);

        col = mix(col, sky, 1.0 - exp(-hitT * 0.05));
    }

    col *= 0.78 + 0.30 * swell + 0.10 * energy;
    col *= 0.7 + 0.3 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
