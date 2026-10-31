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

// Terrain Terraces: drifting over hills cut into rice-paddy terraces on a
// misty morning. The hills are lib_terra's soft fbm; the terraces are its
// contours made flat - twenty-two levels, each a level tread with a short steep
// riser at its outer edge (a smoothstep over the last eighth of the step, so
// it is continuous) - which is exactly how paddies follow a hillside. Each
// tread holds a thin sheet of water that mirrors the pale sky, ringed by a
// grassy bund along its lip; some paddies are planted and show green through
// the water instead. The mist is thick, so far hills fade to layered
// silhouettes.
//
// Risers are steep, so the march steps short (k 0.22).
//
// ---- audio ------------------------------------------------------------------
//
// Beats (SND_KICK_ENV) RIPPLE THE PADDIES: on every beat a ring spreads across
// each paddy's water from a point inside it and settles as the envelope falls
// - a ripple in the mirror's normal, never a flash. The mid band
// (SND_MID_PRESENCE) ripens the rice, sliding the planted paddies from young green
// toward gold. Brightness follows SND_INTENSITY only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the water's shiver and the Chaos drift. Complexity
// (uMorph) fades in an octave of hillside. Chaos (uWarp, uTurbulence, uTwist)
// bends the hills, and the terraces with them (terraChaos). Energy
// (uIntensity) scales the ripples and the ripening. Colour is pal() in the
// rice, the mist and the sky. Space is the rig.

#define TR_MAX_STEPS 128
#define TR_FAR 34.0
#define TR_EYE 2.6
#define TR_AMP 1.7
#define TR_LEVELS 22.0

float gOct;

/** The hill before it is cut: 0..1. */
float hill(vec2 xz) {
    return terraFbm(terraChaos(terraWrap(xz)), gOct);
}

float height(vec2 xz) {
    float s = hill(xz) * TR_LEVELS;
    return TR_AMP * (floor(s) + smoothstep(0.86, 1.0, fract(s))) / TR_LEVELS;
}

void main() {
    vec2 uv = view();
    float t0 = terraTime();
    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float energy = terraEnergy();
    float beat = clamp(SND_KICK_ENV * energy, 0.0, 1.0);
    float ripe = clamp(SND_MID_PRESENCE * energy, 0.0, 1.0);
    gOct = terraOctaves(3.0);

    vec3 ro;
    vec3 rd;
    terraFly(uv, TR_EYE, 0.3, ro, rd);

    vec3 sun = normalize(vec3(0.5, 0.3, 0.8));
    vec3 sunCol = vec3(1.0, 0.93, 0.80);
    vec3 zenith = mix(vec3(0.30, 0.48, 0.72), pal(0.55) * 0.7, 0.2);
    vec3 horizon = mix(vec3(0.95, 0.84, 0.70), pal(0.1), 0.12);

    float hitT = -1.0;
    float used = 0.0;
    vec2 span = terraSlab(ro, rd, TR_AMP, TR_FAR);
    if (span.x >= 0.0) {
        float t = span.x;
        float tPrev = t;
        float dyPrev = 1.0;
        for (int i = 0; i < TR_MAX_STEPS; i++) {
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
            t += min(terraStep(dy, t, 0.22), 0.7 + 0.04 * t);
        }
        // Out of budget short of the far end: a grazing ray creeping along a
        // slope. Shade the ground where it got to rather than show sky through it.
        if (hitT < 0.0 && t <= span.y) hitT = t;
    }

    vec3 col = terraSky(rd, sun, zenith, horizon, sunCol);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = TERRA_NORMAL(height, p.xz, 0.002 * hitT + 0.006);
        float s = hill(p.xz) * TR_LEVELS;
        float lvl = floor(s);
        float f = fract(s);
        float dif = max(dot(n, sun), 0.0);
        float sky = 0.5 + 0.5 * n.y;
        vec3 grass = mix(vec3(0.22, 0.42, 0.14), pal(0.28) * 0.5, 0.25);
        vec3 bank = mix(vec3(0.36, 0.30, 0.20), grass, 0.5);
        col = mix(bank, grass, (1.0 - smoothstep(0.8, 0.86, f))) * (dif * sunCol + sky * zenith * 0.6);
        // The tread: water or rice, inside the bund.
        float tread = (1.0 - smoothstep(0.74, 0.80, f)) * smoothstep(0.9, 0.97, n.y);
        if (tread > 0.0) {
            vec2 wq = terraWrap(p.xz);
            // Ripple rings: one per 2-unit cell, centred in it and dying out
            // before the cell's edge, so the pattern is continuous.
            vec2 cell = fract(wq * 0.5) - 0.5;
            float rr = length(cell);
            float ring = exp(-pow((rr - 0.08 - 0.34 * (1.0 - beat)) * 22.0, 2.0)) * beat * (1.0 - smoothstep(0.4, 0.5, rr));
            vec2 shiver = 0.012 * vec2(sin(wq.x * 9.0 + t0 * 1.7), sin(wq.y * (TERRA_TAU * 1467.0 / TERRA_PERIOD) - t0 * 1.3));
            vec2 tilt = shiver + 0.12 * ring * cell / max(rr, 1e-3);
            vec3 wn = normalize(vec3(tilt.x, 1.0, tilt.y));
            float fres = 0.06 + 0.94 * pow(1.0 - max(dot(wn, -rd), 0.0), 5.0);
            vec3 mirror = terraSky(reflect(rd, wn), sun, zenith, horizon, sunCol);
            vec3 water = mix(vec3(0.12, 0.16, 0.10), mirror, 0.45 + 0.55 * fres);
            float planted = step(0.45, terraHash(vec2(lvl, floor(wq.x * 0.25) + 7.0 * floor(wq.y * 0.25))));
            vec3 rice = mix(mix(vec3(0.35, 0.62, 0.18), pal(0.25), 0.25), vec3(0.78, 0.66, 0.28), ripe);
            rice *= 0.7 + 0.3 * terraNoise(wq * 12.0);
            vec3 tcol = mix(water, rice * (dif * sunCol + sky * zenith * 0.6), planted * 0.85);
            col = mix(col, tcol, tread);
        }
        col *= terraStepAO(used);
        col = terraFog(col, rd, hitT, 0.075, horizon, sun, sunCol);
    }

    col *= 0.84 + 0.18 * swell;
    col *= 0.82 + 0.18 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
