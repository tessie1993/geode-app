#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence are spent on the corridor below (bar shape, the
// corridor's bend, bar jitter), not on the screen.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Laser Corridor (grid), in 3D: a flight down an endless corridor between two
// glowing grids, the floor and its mirror on the ceiling, racing toward a
// horizon line of light. The spectrum stands up from the floor and hangs from
// the ceiling as two banks of light bars, one column per band, lining both
// sides of the corridor all the way to the horizon: bass beside the eye's
// lane, treble out at the edges. Between the two banks a slit stays open at
// eye height, so the horizon line always shows. Still no sun: Outrun Drive
// owns the sunset horizon and Neon City the moonlit one.
//
// The camera is the shared rig (camFly). The corridor runs straight down +z,
// and every z term has a power-of-two period (grid lines 1 and 0.5, the
// bend 64, the bar hash 256), so the rig's 8192 wrap is invisible and the eye
// is rebased into 0..256 (see main). The clear lane is 5 units either side,
// wider than the glide path's +-3 wander; the floor and ceiling sit at +-2.4,
// clear of its +-1.2.
//
// ---- audio ------------------------------------------------------------------
//
// sndSpectrum() is the bars: each column's height is its band (sampled once
// per pixel before the march). The grid lines on the floor and ceiling glow
// with the band under them, as the flat style's did. Bass drives the pulses
// of light that roll down the lanes away from the eye: their phase is the
// Speed-scaled bass clock (uSoundTimeSped.x), so a heavy bass makes the floor
// race the way the flat style's bass pumped its scroll, and the kick
// (SND_KICK_ENV) flares them. Overall level rides SND_ENERGY_TREND. The mid
// band (SND_MIDDLE) turns the eye about the vertical while the mids climb and
// eases it back as they fall, as lib_flame's gFlameTurn turns its structure
// (0.6 x sndShaped x SND_DRIVE, 0.9 rad at most, 0 in silence); the corridor
// and its bars hold still. Mid gain scales it.
//
// ---- knobs ------------------------------------------------------------------
//
// k0 PULSE the rolling pulses' strength, k1 TEXTURE the half-unit minor grid,
// k2 FLOW the pulses' trail length, k3 TONE the hue, k4 BUILD the bars'
// height, k5 SPACE how far the fog lets you see.
//
// ---- why the estimate is safe ----------------------------------------------
//
// The floor and ceiling are intersected exactly, not marched; the march only
// looks for bars, and stops at the plane. Each cell measures only its own bar
// (a box, or with Morph a cylinder; the blend of two 1-Lipschitz distances is
// still one), and the distance is clamped to the cell boundary unless the
// point is above every bar's highest possible tip (GR_HMAX). A global bound
// (the slab the bars live in) lets the ray cross the empty lane in one step.
// The bars mirror across y = 0 through abs(p.y), which is exact because the
// two halves are mirror images. Warp's bend shifts x by a slope of at most
// 0.1, absorbed by the 0.85 step scale. A sphere around the eye is carved out
// of the bars, so the eye never sits inside one.

#define GR_MAX_STEPS 96
#define GR_FAR 70.0
#define GR_TAU 6.2831853
/** Floor at -GR_H, ceiling at +GR_H. */
#define GR_H 2.4
/** Highest a bar can reach from its plane: leaves the 0.5 slit at eye height. */
#define GR_HMAX 2.15
/** First bar column's |x| cell; the lane between is clear. */
#define GR_LANE 5.0
/** Columns per side, one band each. */
#define GR_BANDS 16
#define GR_BUBBLE 0.6
/** Cells along z before the bar hash repeats; divides 8192. */
#define GR_ZWRAP 256.0

/** The bands, read once per pixel in uniform control flow (no texture fetch inside the march). */
float gSpec[GR_BANDS];
vec3 gEye;
float gBand;
float gLevel;
float gBarH;
float gQy;

float bendX(float z) {
    // Warp sways the corridor; period 64, a power of two.
    return uWarp * sin(z * (GR_TAU / 64.0));
}

float bars(vec3 p) {
    float px = p.x - bendX(p.z);
    float qy = GR_H - abs(p.y);
    vec2 cell = floor(vec2(px, p.z));
    vec2 f = vec2(px, p.z) - (cell + 0.5);
    float col = floor(abs(cell.x + 0.5)) - GR_LANE;

    float dc = 1e3;
    gBand = 0.0;
    gLevel = 0.0;
    gBarH = 0.0;
    if (col >= 0.0 && col < float(GR_BANDS)) {
        int ci = int(col);
        uint u = uhash3(uvec3(ivec3(int(cell.x), 0, int(mod(cell.y, GR_ZWRAP)))));
        float hh = uhashUnit(u);
        float s = gSpec[ci];
        float gain = 0.6 + 0.8 * uKnob[4];
        float jit = 1.0 + uTurbulence * 0.35 * sin(uTime * (1.5 + 2.0 * hh) + hh * 40.0);
        float h = clamp((0.12 + 1.9 * s * gain) * (0.8 + 0.2 * hh) * jit, 0.06, GR_HMAX);
        float r = 0.32;
        vec2 a = abs(f) - r;
        float box = length(max(a, 0.0)) + min(max(a.x, a.y), 0.0);
        float cyl = length(f) - r * 1.1;
        float side = mix(box, cyl, clamp(uMorph, 0.0, 1.0));
        dc = max(side, qy - h);
        gBand = (col + 0.5) / float(GR_BANDS);
        gLevel = s;
        gBarH = h;
    }
    vec2 af = abs(f);
    float toEdge = 0.5 - max(af.x, af.y);
    float d = min(dc, max(toEdge + 0.02, qy - GR_HMAX));
    // The slab every bar lives in: GR_LANE + 0.5 - 0.36 out to the last column's outer face.
    float ax = abs(px);
    float slab = max(max(GR_LANE + 0.1 - ax, ax - (GR_LANE + float(GR_BANDS) + 0.4)), qy - GR_HMAX);
    d = max(d, slab);
    d = max(d, GR_BUBBLE - length(p - gEye));
    gQy = qy;
    return d;
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * bars(p + k.xyy * eps) + k.yyx * bars(p + k.yyx * eps)
                   + k.yxy * bars(p + k.yxy * eps) + k.xxx * bars(p + k.xxx * eps));
}

/** Distance to the nearest grid line of `period`, both axes, antialiased by the footprint `fp`. */
float gridLines(vec2 w, float period, float fp) {
    vec2 q = abs(fract(w / period - 0.5) - 0.5) * period;
    float lw = 0.025;
    vec2 a = 1.0 - smoothstep(lw * 0.5, lw + fp, q);
    float sharp = max(a.x, a.y) + 0.35 * max(exp(-q.x * 9.0), exp(-q.y * 9.0));
    // Far off the lines merge: fade to their average so the distance does not shimmer.
    return mix(sharp, 2.0 * lw / period + 0.1, smoothstep(0.08, 0.45, fp / period));
}

vec3 horizon(vec3 rd, float hue, float swell) {
    float up = abs(rd.y);
    // The line of light where floor and ceiling meet, and a faint haze around it.
    vec3 col = pal(hue + 0.05) * (0.8 * exp(-up * 60.0) + 0.12 * exp(-up * 8.0));
    float ahead = max(rd.z, 0.0);
    col += pal(hue + 0.05) * pow(ahead, 24.0) * (0.15 + 0.2 * swell);
    return col;
}

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // The mids turn the eye about the vertical (see the header).
    rd.xz = rot2(0.6 * sndShaped(SND_MIDDLE) * clamp(SND_DRIVE, 0.0, 1.0)) * rd.xz;
    // Every z term tiles with a period dividing 256, so this shift is invisible
    // and exact (power of two). Without it, far down the 8192 travel a highp z
    // has ~0.001 of fraction left and the grid lines quantise into speckle.
    ro.z = mod(ro.z, GR_ZWRAP);
    gEye = ro;

    for (int i = 0; i < GR_BANDS; i++) {
        float b = (float(i) + 0.5) / float(GR_BANDS);
        gSpec[i] = clamp(sndSpectrum(0.02 + 0.88 * b), 0.0, 1.2);
    }

    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float hue = (uKnob[3] - 0.5) * 0.2;
    float fogK = mix(0.055, 0.018, uKnob[5]);

    // The floor or ceiling, exactly.
    float tPlane = GR_FAR + 1.0;
    if (abs(rd.y) > 1e-4) {
        float tp = ((rd.y > 0.0 ? GR_H : -GR_H) - ro.y) / rd.y;
        if (tp > 0.0) tPlane = tp;
    }
    float tEnd = min(tPlane, GR_FAR);

    float t = 0.02;
    float hitT = -1.0;
    for (int i = 0; i < GR_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        float d = bars(ro + rd * t);
        if (d < 0.0015 + 0.0008 * t) {
            hitT = t;
            break;
        }
        t += d * 0.85;
        if (t > tEnd) break;
    }

    vec3 sky = horizon(rd, hue, swell);
    vec3 col = sky;
    // Angular size of one pixel, for the grid's antialiasing.
    float pix = 2.0 / (uResolution.y * max(uCamFocal, 0.1));

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.001 * hitT));
        // Restore the hit cell's globals after the normal taps.
        bars(p);
        float e = 1.0 - 0.7 * clamp(gQy / max(gBarH, 0.01), 0.0, 1.0);
        float tip = smoothstep(gBarH - 0.1, gBarH, gQy);
        vec3 toLane = normalize(vec3(-sign(p.x - bendX(p.z)), -sign(p.y) * 0.3, 0.4));
        float dif = max(dot(n, toLane), 0.0);
        float fres = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.0);
        vec3 c = pal(gBand + 0.1 + hue);
        col = c * (0.22 * e + 0.75 * tip + 0.18 * dif + 0.3 * fres) * (0.45 + 1.0 * gLevel);
        col = mix(col, sky, 1.0 - exp(-hitT * fogK));
    } else if (tPlane < GR_FAR) {
        vec3 p = ro + rd * tPlane;
        vec2 w = vec2(p.x - bendX(p.z), p.z);
        float fp = tPlane * pix / max(abs(rd.y), 0.03);
        float line = gridLines(w, 1.0, fp) + uKnob[1] * 0.45 * gridLines(w, 0.5, fp);
        // The band under the line, as the flat style read it across x.
        int bi = int(mod(floor(abs(w.x) * 0.12 * float(GR_BANDS)), float(GR_BANDS)));
        float hb = gSpec[bi];
        vec3 tint = rd.y < 0.0 ? pal(0.75 + hue) : pal(0.25 + hue);

        // Light rolling down the lanes away from the eye, at the bass clock's pace.
        float ph = fract((p.z - gEye.z) * 0.05 - uSoundTimeSped.x * 0.35);
        float trail = mix(14.0, 3.0, uKnob[2]);
        float pulse = exp(-(1.0 - ph) * trail) * (0.3 + 0.9 * uKnob[0]) * (0.5 + 0.8 * kick);

        col = tint * 0.025 + tint * line * (0.5 + hb * 1.4) * (0.7 + pulse * 1.6);
        // The bars' light pooled on the plane beneath them.
        float ax = abs(w.x);
        if (ax > GR_LANE - 0.4) {
            float bc = clamp(floor(ax) - GR_LANE, 0.0, float(GR_BANDS) - 1.0);
            col += pal((bc + 0.5) / float(GR_BANDS) + 0.1 + hue) * gSpec[int(bc)] * 0.12
                 * smoothstep(GR_LANE - 0.4, GR_LANE + 0.2, ax);
        }
        col = mix(col, sky, 1.0 - exp(-tPlane * fogK));
    }

    col *= 0.8 + 0.3 * swell;
    fragColor = vec4(grade(col), 1.0);
}
