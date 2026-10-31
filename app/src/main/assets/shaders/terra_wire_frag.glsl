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
//#include lib_terra

// Terrain Wire: a neon wireframe landscape, flown down a valley toward a
// striped sun on the horizon. The ground is solid and black; what shows is a
// glowing grid drawn ON the marched surface - a line every world unit in x
// and z, its width set by fwidth() so every line is the same few pixels wide
// near and far, fading to an even glow where the cells get smaller than a
// pixel. The valley floor under the flight path is flat; the slopes either
// side rise from lib_terra's fbm, so the grid bends up over hills on both
// hands. The grid's z lines run on a whole-unit lattice and the field tiles,
// so lib_terra's z wrap leaves no seam in the grid either.
//
// ---- audio ------------------------------------------------------------------
//
// The SPECTRUM (the spectrum) PUSHES THE ROWS: each row of the grid ahead is lifted
// by one band of the spectrum, bass on the nearest rows through to the air
// band on the far ones, so the hills either side become a running spectrum
// display that the flight sweeps along. The rows' colour follows their band
// through the palette. Brightness follows SND_INTENSITY only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the Chaos drift. Complexity (uMorph) fades in an
// octave of hill. Chaos (uWarp, uTurbulence, uTwist) bends the hills under
// the grid (terraChaos). Energy (uIntensity) scales the spectrum's push.
// Colour is pal() in the grid and the sky. Space is the rig.

#define WR_MAX_STEPS 112
#define WR_FAR 40.0
#define WR_EYE 1.1
/** World units ahead over which the spectrum is spread, bass nearest. */
#define WR_SPAN 26.0

float gOct;
float gPush;
vec3 gEye;

/** Where along the spectrum a row at world z sits: 0 at the eye, 1 at WR_SPAN ahead. */
float rowBand(float z) {
    return clamp((z - gEye.z) / WR_SPAN, 0.0, 1.0);
}

float height(vec2 xz) {
    float f = terraFbm(terraChaos(terraWrap(xz)), gOct);
    float side = smoothstep(0.6, 2.8, abs(xz.x - gEye.x));
    float band = sndSpectrum(0.02 + 0.7 * rowBand(xz.y));
    return 0.25 * f + side * (0.35 + 0.9 * f) * (0.4 + gPush * clamp(band, 0.0, 1.0));
}

void main() {
    vec2 uv = view();
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    gPush = min(0.8 * terraEnergy(), 1.0);
    gOct = terraOctaves(3.0);
    float hMax = 0.25 + 1.25 * (0.4 + gPush);

    vec3 ro;
    vec3 rd;
    terraFly(uv, WR_EYE, 0.08, ro, rd);
    gEye = ro;

    // Sky: indigo overhead to a hot horizon, and the striped sun ahead.
    vec3 sky = mix(mix(vec3(0.9, 0.25, 0.45), pal(0.95), 0.3), mix(vec3(0.05, 0.02, 0.15), pal(0.7) * 0.1, 0.3), pow(clamp(rd.y * 2.5, 0.0, 1.0), 0.6));
    vec3 sunDir = normalize(vec3(0.0, 0.1, 1.0));
    vec3 sx = normalize(cross(vec3(0.0, 1.0, 0.0), sunDir));
    vec2 s = vec2(dot(rd, sx), dot(rd, cross(sunDir, sx))) / 0.22;
    vec3 col = sky + vec3(0.02) * terraStars(rd, 0.004) * 20.0 * smoothstep(0.15, 0.4, rd.y);
    if (dot(s, s) < 1.0 && dot(rd, sunDir) > 0.0) {
        float cut = s.y < 0.15 ? step(0.35 + 0.25 * s.y, fract(s.y * 6.0)) : 1.0;
        col = mix(col, mix(vec3(1.0, 0.85, 0.3), vec3(1.0, 0.3, 0.6), 0.5 - 0.5 * s.y), cut);
    }
    col += vec3(1.0, 0.4, 0.6) * 0.25 * pow(max(dot(rd, sunDir), 0.0), 12.0);

    float hitT = -1.0;
    vec2 span = terraSlab(ro, rd, hMax, WR_FAR);
    if (span.x >= 0.0) {
        float t = span.x;
        float tPrev = t;
        float dyPrev = 1.0;
        for (int i = 0; i < WR_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > span.y) break;
            vec3 p = ro + rd * t;
            float dy = p.y - height(p.xz);
            if (dy < terraEps(t)) {
                hitT = terraRefine(tPrev, dyPrev, t, dy);
                break;
            }
            tPrev = t;
            dyPrev = dy;
            t += terraStep(dy, t, 0.45);
        }
        // Out of budget short of the far end: a grazing ray creeping along a
        // slope. Shade the ground where it got to rather than show sky through it.
        if (hitT < 0.0 && t <= span.y) hitT = t;
    }

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = TERRA_NORMAL(height, p.xz, 0.003 * hitT + 0.01);
        vec2 wq = terraWrap(p.xz);
        vec2 fw = max(fwidth(p.xz), vec2(1e-4));
        vec2 d = abs(fract(wq - 0.5) - 0.5) / fw;
        // Core and halo of each line, in pixels; fade to an even glow once a
        // cell is under a few pixels.
        vec2 lit = exp(-d * d * 0.35) + 0.25 * exp(-d * 0.35);
        vec2 fade = (1.0 - smoothstep(0.15, 0.45, fw));
        lit = lit * fade + (1.0 - fade) * 0.18;
        float band = rowBand(p.z);
        vec3 rowCol = mix(vec3(1.0, 0.2, 0.75), pal(0.85 + 0.5 * band), 0.45);
        vec3 colCol = mix(vec3(0.2, 0.8, 1.0), pal(0.55), 0.35);
        vec3 ground = vec3(0.02, 0.0, 0.04) * (0.4 + 0.6 * max(n.y, 0.0));
        col = ground + rowCol * lit.y + colCol * lit.x * 0.8;
        col = mix(col, sky, 1.0 - exp(-hitT * 0.06));
    }

    col *= 0.85 + 0.17 * swell;
    col *= 0.8 + 0.2 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
