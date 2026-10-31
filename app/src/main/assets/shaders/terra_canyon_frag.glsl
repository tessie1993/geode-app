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

// Terrain Canyon: flying low down a river canyon between sandstone walls. A
// high plateau with a winding channel cut out of it: the channel follows the
// travel axis (the rig flies down +z), meandering by a couple of sines with a
// whole number of bends per tile, and its walls step back in ledges - the
// height is half terraced - so the rock reads as bedded strata. The midday
// sun lights one wall and leaves the other in blue shade; the floor is a
// slow river that mirrors the strip of sky overhead.
//
// The walls are steep, so the march takes small steps (k 0.22): a vertical
// clearance says little about how close a wall is. Chaos warps the walls'
// rock, never the channel's line, so it cannot swing a wall into the eye; and
// in case the user widens the path's wander until the eye leaves the channel,
// the eye is held above the ground under it (one height read per pixel).
//
// ---- audio ------------------------------------------------------------------
//
// Bass (sndLevel(SND_BASS_ATT) with SND_BASS_PRESENCE) DRAWS THE WALLS IN: the channel narrows
// on low notes and opens as they pass, so the canyon squeezes the flight in
// time with the bass. Each kick (SND_KICK_ENV) sends a shimmer racing away down
// the river ahead - a band of light on the water only, never on the frame.
// Brightness follows SND_ENERGY_TREND only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the river's flow. Complexity (uMorph) fades in an
// octave of rock. Chaos (uWarp, uTurbulence, uTwist) warps the walls' rock
// (terraChaos). Energy (uIntensity) scales the squeeze and the shimmer.
// Colour is pal() in the strata bands and the sky. Space is the rig.

#define CY_MAX_STEPS 128
#define CY_FAR 34.0
#define CY_EYE 1.1
#define CY_TOP 4.4

float gOct;
float gHalf;

/** The channel's centre line at world z (wrapped): 3 and 7 bends per tile, so it tiles. */
float channel(float z) {
    return 0.55 * sin(TERRA_TAU * 3.0 * z / TERRA_PERIOD) + 0.3 * sin(TERRA_TAU * 7.0 * z / TERRA_PERIOD + 1.0);
}

float height(vec2 xz) {
    xz = terraWrap(xz);
    vec2 w = terraChaos(xz);
    float f = terraFbm(w, gOct);
    float d = abs(xz.x - channel(xz.y)) + 0.9 * (f - 0.5);
    float carve = smoothstep(gHalf, gHalf + 1.5, d);
    float h = (CY_TOP + 0.6 * f) * carve;
    // Ledges: a terraced copy (continuous: the smoothstep runs 0 to 1 within
    // each step) mixed half in, so the walls step without turning to stairs.
    float s = h * 3.0;
    float terr = (floor(s) + smoothstep(0.25, 0.75, fract(s))) / 3.0;
    return mix(h, terr, 0.5) + 0.04 * f;
}

void main() {
    vec2 uv = view();
    float t0 = terraTime();
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float energy = terraEnergy();
    float bass = clamp(0.6 * sndLevel(SND_BASS_ATT) + 0.4 * SND_BASS_PRESENCE, 0.0, 1.2);
    float kick = clamp(SND_KICK_ENV * energy, 0.0, 1.0);
    gHalf = 1.7 - 0.3 * bass * min(energy, 1.5);
    gOct = terraOctaves(3.0);
    float hMax = CY_TOP + 0.65;

    vec3 ro;
    vec3 rd;
    terraFly(uv, CY_EYE, 0.06, ro, rd);
    ro.y = max(ro.y, height(ro.xz) + 0.5);

    vec3 sun = normalize(vec3(0.55, 0.8, 0.25));
    vec3 sunCol = vec3(1.0, 0.9, 0.75);
    vec3 zenith = mix(vec3(0.12, 0.30, 0.65), pal(0.6) * 0.5, 0.2);
    vec3 horizon = mix(vec3(0.70, 0.62, 0.55), pal(0.05) * 0.8, 0.2);

    float hitT = -1.0;
    float used = 0.0;
    vec2 span = terraSlab(ro, rd, hMax, CY_FAR);
    if (span.x >= 0.0) {
        float t = span.x;
        float tPrev = t;
        float dyPrev = 1.0;
        for (int i = 0; i < CY_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (t > span.y) break;
            used = float(i);
            vec3 p = ro + rd * t;
            float dy = p.y - height(p.xz);
            if (dy < terraEps(t)) {
                hitT = terraRefine(tPrev, dyPrev, t, dy);
                break;
            }
            tPrev = t;
            dyPrev = dy;
            t += min(terraStep(dy, t, 0.22), 0.6 + 0.05 * t);
        }
        // Out of budget short of the far end: a grazing ray creeping along a
        // slope. Shade the ground where it got to rather than show sky through it.
        if (hitT < 0.0 && t <= span.y) hitT = t;
    }

    vec3 col = terraSky(rd, sun, zenith, horizon, sunCol);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = TERRA_NORMAL(height, p.xz, 0.003 * hitT + 0.008);
        float dif = max(dot(n, sun), 0.0);
        float sky = 0.5 + 0.5 * n.y;
        float ao = terraStepAO(used);
        float wz = mod(p.z, TERRA_PERIOD);
        float strata = 0.5 + 0.5 * sin(p.y * 9.0 + 2.0 * terraNoise(vec2(p.y * 3.0, wz * 0.25)));
        vec3 rock = mix(vec3(0.72, 0.36, 0.20), pal(0.04 + 0.12 * strata), 0.2) * (0.75 + 0.35 * strata);
        // The dry floor either side of the river: pale sand and gravel.
        rock = mix(rock, vec3(0.62, 0.52, 0.40), (1.0 - smoothstep(0.1, 0.35, p.y)) * smoothstep(0.8, 0.95, n.y));
        col = rock * (dif * sunCol * 1.3 + sky * zenith * 0.8 + 0.08) * ao;
        // The river: the flat floor of the channel, a dark mirror of the sky.
        float water = (1.0 - smoothstep(0.6, 0.9, abs(p.x - channel(wz)))) * (1.0 - smoothstep(0.08, 0.2, p.y));
        if (water > 0.0) {
            float flow = sin(wz * (TERRA_TAU * 244.0 / TERRA_PERIOD) - t0 * 2.0 + 4.0 * terraNoise(vec2(p.x * 2.0, wz)));
            vec3 rn = normalize(vec3(0.03 * flow, 1.0, 0.05 * flow));
            float fres = 0.08 + 0.9 * pow(1.0 - max(dot(rn, -rd), 0.0), 4.0);
            vec3 refl = terraSky(reflect(rd, rn), sun, zenith, horizon, sunCol);
            vec3 river = mix(vec3(0.03, 0.10, 0.12), refl, fres);
            // The kick's shimmer: a band running away from the eye down the river.
            float ahead = p.z - ro.z;
            float band = exp(-pow((ahead - 2.0 - 22.0 * (1.0 - kick)) * 0.35, 2.0)) * kick;
            river += pal(0.55 + 0.1 * flow) * band * (0.5 + 0.5 * flow) * 0.6;
            col = mix(col, river, water);
        }
        col = terraFog(col, rd, hitT, 0.035, horizon, sun, sunCol);
    }

    col *= 0.84 + 0.18 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
