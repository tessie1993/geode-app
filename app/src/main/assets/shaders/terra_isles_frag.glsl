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
//#include lib_terra

// Terrain Isles: skimming an archipelago of low green islands set in a calm,
// reflective sea. The land is lib_terra's fbm sunk so most of it lies under
// water; the sea is an analytic plane, so the ray stops at it for the price
// of one division and the march never runs out over open water. Where the
// sea meets the land the water runs from deep blue through turquoise
// shallows (the depth is one extra height read) to a line of surf. Waves are
// a normal on the plane only: two crossing swells with a whole number of
// crests per tile, wandering with a noise. Beaches give way to green, green
// to bare rock on the few high tops.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (uBandsLo.x and y, steadied with uBassSmooth) is the TIDE: the sea
// level rises over the beaches on low notes and draws back as they fade, so
// shallows flood and the islands shrink and grow with the music. The rise is
// clamped below the eye. Highs (uBandsHi.y) roughen the swell. Brightness
// follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the swell's drift. Complexity (uMorph) fades in an
// octave of coastline. Chaos (uWarp, uTurbulence, uTwist) bends the islands
// (terraChaos). Energy (uIntensity) scales the tide and the chop. Colour is
// pal() in the sea, the foliage and the sky. Space is the rig.

#define IS_MAX_STEPS 112
#define IS_FAR 40.0
#define IS_EYE 1.9
#define IS_AMP 2.4
#define IS_SINK 0.56
/** The two swells' phase per lattice cell: integers, so they tile. */
const vec2 IS_SWELL_A = vec2(3.0, 5.0);
const vec2 IS_SWELL_B = vec2(-4.0, 3.0);

float gOct;

float height(vec2 xz) {
    float f = terraFbm(terraChaos(terraWrap(xz)), gOct);
    return IS_AMP * (f - IS_SINK);
}

void main() {
    vec2 uv = view();
    float t0 = terraTime();
    float swell = clamp(uSwell, 0.0, 1.5);
    float energy = terraEnergy();
    float bass = clamp(0.3 * (uBandsLo.x + uBandsLo.y) + 0.4 * uBassSmooth, 0.0, 1.2);
    float chop = clamp(uBandsHi.y * energy, 0.0, 1.0);
    float sea = 0.02 + 0.07 * bass * min(energy, 1.5);
    gOct = terraOctaves(4.0);
    float hMax = IS_AMP * (1.0 - IS_SINK);

    vec3 ro;
    vec3 rd;
    terraFly(uv, IS_EYE, 0.2, ro, rd);

    vec3 sun = normalize(vec3(0.6, 0.45, 0.7));
    vec3 sunCol = vec3(1.0, 0.94, 0.82);
    vec3 zenith = mix(vec3(0.15, 0.40, 0.78), pal(0.6) * 0.6, 0.2);
    vec3 horizon = mix(vec3(0.72, 0.84, 0.92), pal(0.5), 0.12);

    // The sea plane caps the march: past it the ray is in the water.
    float tSea = rd.y < 0.0 ? (sea - ro.y) / rd.y : IS_FAR;
    float hitT = -1.0;
    vec2 span = terraSlab(ro, rd, hMax, min(IS_FAR, tSea));
    if (span.x >= 0.0) {
        float t = span.x;
        float tPrev = t;
        float dyPrev = 1.0;
        for (int i = 0; i < IS_MAX_STEPS; i++) {
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
            t += terraStep(dy, t, 0.5);
        }
        // Out of budget short of the far end: a grazing ray creeping along a
        // slope. Shade the ground where it got to rather than show sky through it.
        if (hitT < 0.0 && t <= span.y) hitT = t;
    }

    vec3 col = terraSky(rd, sun, zenith, horizon, sunCol);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = TERRA_NORMAL(height, p.xz, 0.003 * hitT + 0.01);
        float above = p.y - sea;
        float dif = max(dot(n, sun), 0.0);
        float sky = 0.5 + 0.5 * n.y;
        vec3 sand = vec3(0.86, 0.78, 0.58);
        vec3 green = mix(vec3(0.16, 0.38, 0.12), pal(0.3) * 0.4, 0.3);
        vec3 rock = vec3(0.42, 0.40, 0.36);
        vec3 base = mix(sand, green, smoothstep(0.03, 0.10, above) * smoothstep(0.55, 0.8, n.y));
        base = mix(base, rock, smoothstep(0.45, 0.7, above / hMax));
        col = base * (dif * sunCol + sky * zenith * 0.45);
        col = terraFog(col, rd, hitT, 0.03, horizon, sun, sunCol);
    } else if (rd.y < 0.0 && tSea < IS_FAR) {
        vec3 p = ro + rd * tSea;
        vec2 q = terraWrap(p.xz) * TERRA_FREQ;
        float wob = terraNoise(q * 2.0);
        float pa = TERRA_TAU * (dot(q, IS_SWELL_A) + 0.7 * wob) - t0 * 1.3;
        float pb = TERRA_TAU * (dot(q, IS_SWELL_B) + 0.5 * wob) - t0 * 1.1;
        float amp = (0.05 + 0.08 * chop) * exp(-tSea * 0.05);
        vec2 slope = amp * (normalize(IS_SWELL_A) * cos(pa) + normalize(IS_SWELL_B) * cos(pb));
        vec3 wn = normalize(vec3(-slope.x, 1.0, -slope.y));
        float depth = sea - height(p.xz);
        float fres = 0.04 + 0.96 * pow(1.0 - max(dot(wn, -rd), 0.0), 5.0);
        vec3 refl = terraSky(reflect(rd, wn), sun, zenith, horizon, sunCol);
        vec3 deepC = mix(vec3(0.02, 0.10, 0.22), pal(0.62) * 0.2, 0.3);
        vec3 shallowC = mix(vec3(0.10, 0.62, 0.60), pal(0.48) * 0.7, 0.2);
        vec3 water = mix(deepC, shallowC, exp(-max(depth, 0.0) * 5.0));
        col = mix(water, refl, fres);
        // Surf: a broken white line where the water is thinnest.
        float surf = (1.0 - smoothstep(0.0, 0.05, depth)) * (0.6 + 0.4 * sin(pa * 2.0 + wob * 6.0));
        col = mix(col, vec3(0.92, 0.96, 1.0), clamp(surf, 0.0, 1.0) * 0.8);
        col = terraFog(col, rd, tSea, 0.03, horizon, sun, sunCol);
    }

    col *= 0.84 + 0.18 * swell;
    col *= 0.8 + 0.2 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
