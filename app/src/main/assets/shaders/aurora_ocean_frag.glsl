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

// Aurora Ocean: low flight over a dark, mirror-still-to-choppy sea under
// aurora curtains. The sea is a four-octave heightfield marched from the
// wave-top plane down; the sky is a stack of curtain layers projected from the
// eye, which the water reflects. Flown with camFly; the sea sits at AO_SEA_Y,
// below the rig's +-1.5 y wander, and the eye is also clamped above the local
// wave so a deep swell never swallows it.
//
// Every wave's z wavenumber is a whole multiple of 2pi/64, so the sea tiles
// along z with period 64 and the rig's 8192 travel wrap is invisible. The
// sky also parallaxes with the eye's xz: its z wavenumbers along q.y are whole
// multiples of AO_QK so it repeats over the travel wrap too.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (uBassSmooth with a little uBandsLo.y) swells the waves. Snare (uHits.y)
// sends a bright shimmer running along the curtains. Hats (uHits.z) scatter
// sparkles on the water. A drop (uEvents.y) leans the curtains over and lights
// one patch of them, never the whole frame. Overall level follows uSwell.

#define AO_MAX_STEPS 96
#define AO_LAYERS 12
#define AO_SEA_Y -2.0
#define AO_FAR 60.0
/** 2pi / 64: the z wavenumber unit that keeps the sea periodic over the travel wrap. */
#define AO_KZ 0.09817477
/**
 * 2pi / 147.456: the curtains' q.y wavenumber unit. One 8192 travel wrap moves
 * q.y by 8192 * 0.15 * 0.12 = 147.456, so every q.y term must be a whole
 * multiple of this or the whole sky jumps at the wrap.
 */
#define AO_QK 0.04261057

float gAmp;

/** Height of the sea above AO_SEA_Y at xz. Four crested octaves, each warped by the last. */
float sea(vec2 xz) {
    float t = uTime;
    float h = 0.0;
    float w = 0.0;
    // Each octave: (kx, kz) with kz a multiple of AO_KZ, a speed, a weight.
    vec2 k0 = vec2(0.31, 4.0 * AO_KZ);
    vec2 k1 = vec2(-0.63, 9.0 * AO_KZ);
    vec2 k2 = vec2(1.27, 17.0 * AO_KZ);
    vec2 k3 = vec2(-2.33, 35.0 * AO_KZ);
    float s;
    s = 0.5 + 0.5 * sin(dot(k0, xz) + t * 0.55);
    h += s * s * 1.0;
    w = s;
    s = 0.5 + 0.5 * sin(dot(k1, xz) + t * 0.83 + w * 1.2);
    h += s * s * 0.5;
    w = s;
    s = 0.5 + 0.5 * sin(dot(k2, xz) - t * 1.21 + w * 1.4);
    h += s * s * 0.25;
    w = s;
    s = 0.5 + 0.5 * sin(dot(k3, xz) + t * 1.73 + w * 1.6);
    h += s * s * 0.12;
    // Centred, so gAmp swells the sea both ways about its rest level.
    return (h - 0.93) * gAmp;
}

/** The curtains, plus a dim star field and a horizon glow. `rd.y` must be > 0 for curtains to show. */
vec3 sky(vec3 rd, vec3 ro, float hue, float drop, float dropLean, float snare) {
    float up = max(rd.y, 0.0);
    vec3 col = mix(pal(hue + 0.5) * 0.05, vec3(0.004, 0.006, 0.014), smoothstep(0.0, 0.5, up));

    vec3 sc = rd * 140.0;
    float h = hashCell(ivec3(floor(sc)));
    col += vec3(step(0.991, h) * (1.0 - smoothstep(0.0, 0.35, length(fract(sc) - 0.5)))) * 0.6 * smoothstep(0.05, 0.3, up);

    if (rd.y > 0.02) {
        vec3 acc = vec3(0.0);
        // The patch a drop lights: a fixed region ahead and up, in sky direction.
        vec3 patchDir = normalize(vec3(0.25, 0.55, 1.0));
        float patchMask = smoothstep(0.75, 0.97, dot(rd, patchDir));
        for (int i = 0; i < AO_LAYERS; i++) {
            float fi = float(i);
            // Each layer is a horizontal plane higher up the curtain. Projecting
            // the ray onto it makes one ribbon line on the ground plane into a
            // vertical streak on screen, which is what a curtain is.
            float lh = 3.0 + fi * 0.45;
            vec2 q = ro.xz * 0.15 + rd.xz * (lh / rd.y);
            q *= 0.12;
            // The drop leans the curtain: every layer slides further along x.
            q.x += fi * 0.06 * dropLean;
            float tm = uTime * 0.06;
            float v = q.x + 0.9 * sin(q.y * (13.0 * AO_QK) + tm * 2.0) + 0.35 * sin(q.y * (40.0 * AO_QK) - tm * 3.0 + q.x * 0.6);
            float rib = (1.0 - smoothstep(0.0, 0.24, abs(fract(v * 0.35) - 0.5)));
            // Vertical rays inside the ribbon.
            rib *= 0.65 + 0.35 * sin(v * 23.0 + q.y * (70.0 * AO_QK) + tm * 5.0);
            // The snare's shimmer: a bright pulse racing along the ribbon's length.
            float run = 0.5 + 0.5 * sin(q.y * (56.0 * AO_QK) - uTime * 7.0);
            rib *= 1.0 + snare * 1.2 * run * run * run * run;
            // Bottom edge brightest and greener, fading out up the curtain.
            float fade = exp(-fi * 0.22) * smoothstep(0.0, 1.5, fi + 0.5);
            acc += pal(hue + fi * 0.035) * rib * fade;
        }
        // Bright enough to read as aurora on a phone: at 0.16 the curtains were a
        // faint teal smudge and the style read as a black screen.
        acc *= 0.45 * smoothstep(0.02, 0.18, rd.y);
        acc *= 1.0 + drop * 1.8 * patchMask;
        col += acc;
    }
    return col;
}

void main() {
    vec2 uv = view();

    float swell = clamp(uSwell, 0.0, 1.5);
    float bass = clamp(uBassSmooth, 0.0, 1.5);
    float hats = clamp(uHits.z, 0.0, 1.0);
    float snare = clamp(uHits.y, 0.0, 1.0);
    float drop = clamp(uEvents.y, 0.0, 1.0);
    // A bump on the envelope: 0 at the drop's instant and at its end, 1 in
    // between, so the lean glides in and out instead of snapping.
    float dropLean = 4.0 * drop * (1.0 - drop);
    float hue = 0.33 + 0.04 * sin(uTime * 0.02);

    gAmp = 0.22 * (1.0 + 0.7 * bass + 0.25 * clamp(uBandsLo.y, 0.0, 1.0));
    float waveTop = AO_SEA_Y + gAmp * 0.95;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // The sky parallaxes on the true eye; the sea is marched with z rebased
    // into one 64 tile (exact, power of two), since far down the 8192 travel
    // a highp z has ~0.001 of fraction left and the normals would speckle.
    vec3 eye = ro;
    ro.z = mod(ro.z, 64.0);
    ro.y = max(ro.y, AO_SEA_Y + sea(ro.xz) + 0.45);
    eye.y = ro.y;

    vec3 col;
    float hitT = -1.0;
    if (rd.y < -0.001) {
        // Start on the wave-top plane: nothing above it can be water.
        float t = max((ro.y - waveTop) / -rd.y, 0.0);
        float tPrev = t;
        float hPrev = 1.0;
        for (int i = 0; i < AO_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > AO_FAR) break;
            vec3 p = ro + rd * t;
            float h = p.y - (AO_SEA_Y + sea(p.xz));
            if (h < 0.002 * t) {
                // Interpolate between the last two samples for a smooth surface.
                hitT = mix(tPrev, t, clamp(hPrev / max(hPrev - h, 1e-4), 0.0, 1.0));
                break;
            }
            tPrev = t;
            hPrev = h;
            t += max(h * 0.6, 0.01 + 0.006 * t);
        }
    }

    vec3 horizon = pal(hue + 0.5) * 0.05;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        float e = 0.02 + 0.002 * hitT;
        float h0 = sea(p.xz);
        vec3 n = normalize(vec3(h0 - sea(p.xz + vec2(e, 0.0)), e, h0 - sea(p.xz + vec2(0.0, e))));
        vec3 r = reflect(rd, n);
        r.y = abs(r.y);
        float fres = 0.02 + 0.98 * pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 5.0);
        vec3 deep = pal(hue + 0.55) * 0.012 + vec3(0.002, 0.004, 0.008);
        col = mix(deep, sky(r, eye, hue, drop, dropLean, snare) * 1.2, fres);
        // Hat sparkles: hashed glints on a grid tiled every 64 along z, lit only
        // where a facet tilts toward the curtains.
        vec2 g = p.xz * 5.0;
        vec2 cell = floor(g);
        cell.y = mod(cell.y, 320.0);
        float sh = hashCell(ivec3(int(cell.x), int(cell.y), 7));
        float glint = step(0.93, sh) * (1.0 - smoothstep(0.0, 0.3, length(fract(g) - 0.5)));
        glint *= smoothstep(0.1, 0.5, r.y) * exp(-hitT * 0.06);
        col += mix(pal(hue + 0.1), vec3(1.0), 0.5) * glint * hats * 1.5;
        col = mix(col, horizon, 1.0 - exp(-hitT * 0.045));
    } else if (rd.y < 0.0) {
        col = horizon;
    } else {
        col = sky(rd, eye, hue, drop, dropLean, snare);
    }

    col *= 0.8 + 0.25 * swell;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
