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

// Jelly Abyss: a slow drift through deep water past bioluminescent jellyfish,
// with marine snow and light shafts from the surface far above.
//
// Each jelly is an SDF: a hollow ellipsoid bell cut open underneath, six
// trailing tentacles (one tentacle folded into six angular sectors) and a
// frilled oral arm. They are domain-repeated in a JA_CELL grid, one per cell,
// some cells empty, so new ones keep drifting into view. The march glows as
// it goes: emission near the bells and tentacles is integrated along the ray,
// so thin tentacles still read at distance where no step lands on them.
//
// The camera is the shared rig (camFly). The cells of the corridor the rig's
// paths live in (|x| < 5 and |y| < 5: glide, spiral and orbit all stay inside
// it, orbit's 5.4 radius reaches only into a live cell's empty margin) hold no
// jelly, and every jelly stays JA_MARGIN inside its own cell, so the camera
// is never inside one. Each cell's distance is clamped to the cell boundary
// plus that margin, which keeps the repeated field a true lower bound. The
// cell hash repeats every 32 cells (256 units) along z, which divides the
// rig's 8192 travel wrap, and the march runs with z rebased into that tile.
//
// ---- audio ------------------------------------------------------------------
//
// Each sound moves a DIFFERENT property:
//
//   kick (uHits.x)         the bells contract (squash and stretch) and the
//                          jellies jet upward on a 0-1-0 bump as it falls
//   snare (uHits.y)        a bioluminescent pulse runs down every tentacle
//   hats (uHits.z)         marine-snow flakes sparkle
//   onset (uHits.w)        jellies change colour one at a time: each onset
//                          (uHitCount) recolours about one in JA_RECOLOUR,
//                          cross-fading over the envelope with a brief glow
//   low bands (uBandsLo)   tentacle sway: the current drags them further
//   high bands (uBandsHi)  the glow of the radial canals inside each bell
//   drop (uEvents.y)       the whole swarm lights up, the snow turns into a
//                          glowing swarm, and the palette swaps for its length
//   pitch change (uEvents.w) and the key hue: a hue drift
//   section (uSectionCount) the depth zone: sunlit, twilight, midnight,
//                          abyss; water, fog, shafts and jelly hues follow,
//                          eased over the section envelope
//
// Overall level follows uSwell only.

/** Compile-time ceiling. uSteps is the runtime budget. */
#define JA_MAX_STEPS 96
#define JA_FAR 48.0
#define JA_TAU 6.2831853
#define JA_CELL vec3(10.0, 10.0, 8.0)
/** Every jelly stays at least this far inside its own cell. */
#define JA_MARGIN 0.5
/** About one jelly in this many takes a new colour per onset. */
#define JA_RECOLOUR 7.0
/** 2pi / 256: the z wavenumber unit that keeps the light shafts periodic over the rebased tile. */
#define JA_KZ 0.02454369

float gContract;
float gJet;
float gSway;
float gZoneHue;
float gOnsetProg;

// What map() last found: the live cell's jelly, in jelly-local units.
float gAlive;
float gSeed;
float gLen;
vec3 gQ;
float gDB;
float gDT;

/** iq's ellipsoid bound; not exact, so its caller scales the step down. */
float sdEllipsoid(vec3 p, vec3 r) {
    float k0 = length(p / r);
    float k1 = length(p / (r * r));
    return k0 * (k0 - 1.0) / max(k1, 1e-4);
}

/** Bell and tentacles of one jelly at local q (bell centre at the origin, opening down). */
void jellyParts(vec3 q, float seed) {
    float idle = 0.12 * (0.5 + 0.5 * sin(uTime * 1.1 + seed * JA_TAU));
    float c = gContract + idle;
    vec3 r = vec3(0.95 * (1.0 - 0.28 * c), 0.72 * (1.0 + 0.3 * c), 0.95 * (1.0 - 0.28 * c));
    float e = sdEllipsoid(q, r);
    // The scalloped margin: the cut edge rises and falls around the rim.
    float cut = -(q.y + 0.08 + 0.05 * sin(q.x * 9.0 + uTime) * sin(q.z * 9.0));
    gDB = max(abs(e) - 0.045, cut);

    float len = 2.6 * (0.85 + 0.3 * fract(seed * 7.7));
    gLen = len;
    float s = clamp(-q.y / len, 0.0, 1.0);
    // The current drags the whole skirt one way, more toward the tips. Applied
    // before the sector fold so a swaying tentacle never crosses a sector edge.
    float ph = uTime * 0.9 + uFlowPhase * 0.6 + seed * JA_TAU;
    vec2 sw = gSway * s * s * vec2(sin(q.y * 1.4 + ph), 0.6 * cos(q.y * 1.1 + ph * 0.8));
    vec3 qt = vec3(q.x - sw.x, q.y, q.z - sw.y);

    float sector = JA_TAU / 6.0;
    float k = floor(atan(qt.z, qt.x + 1e-7) / sector + 0.5);
    vec2 xz = rot2(k * sector) * qt.xz;
    float wig = 0.05 * sin(q.y * 5.0 + k * 2.0 + uTime * 2.0) * s;
    vec3 off = vec3(xz.x - 0.55 * r.x * (1.0 - 0.35 * s), q.y - clamp(q.y, -len, 0.0), xz.y - wig);
    float dT = length(off) - 0.035 * (1.0 - 0.6 * s);

    // The oral arm: shorter, thicker, frilled.
    float len2 = 0.55 * len;
    float s2 = clamp(-q.y / len2, 0.0, 1.0);
    vec3 off2 = vec3(qt.x, q.y - clamp(q.y, -len2, 0.0), qt.z);
    float frill = (0.09 + 0.04 * sin(q.y * 14.0 + uTime * 2.0)) * (1.0 - 0.5 * s2);
    gDT = min(dT, length(off2) - frill);
}

/**
 * The repeated field. Returns a step-safe distance and leaves the cell's jelly
 * in the g* globals for the glow and the shading.
 */
float map(vec3 p) {
    vec3 cid = floor(p / JA_CELL + 0.5);
    vec3 lc = p - cid * JA_CELL;
    vec3 db = 0.5 * JA_CELL - abs(lc);
    float dBound = max(min(db.x, min(db.y, db.z)), 0.0) + JA_MARGIN;
    float h = hashCell(ivec3(int(cid.x), int(cid.y), int(mod(cid.z, 32.0))));
    gSeed = h;
    // The rig's corridor holds no jelly, and some other cells are empty so the
    // swarm never reads as a grid.
    gAlive = (abs(cid.x) + abs(cid.y) < 0.5 || hash11(h * 3.3 + 0.2) < 0.3) ? 0.0 : 1.0;
    if (gAlive < 0.5) {
        gDB = 1e3;
        gDT = 1e3;
        return dBound;
    }
    float h1 = hash11(h * 17.0 + 1.0);
    float h2 = hash11(h * 29.0 + 2.0);
    float h3 = hash11(h * 41.0 + 3.0);
    float size = 0.7 + 0.3 * h3;
    vec3 c = vec3((h1 - 0.5) * 2.4,
                  1.5 + (h2 - 0.5) * 1.6 + 0.3 * sin(uTime * 0.35 + h * JA_TAU) + gJet * 0.8,
                  (h3 - 0.5) * 2.4);
    vec3 q = (lc - c) / size;
    jellyParts(q, h);
    gQ = q;
    gDB *= size;
    gDT *= size;
    // The bell bound and the sway's bend both overestimate; 0.6 covers them.
    return min(min(gDB, gDT) * 0.6, dBound);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** This jelly's hue. `fresh` is 1 the instant an onset recoloured it, falling to 0. */
float jellyHue(float seed, out float fresh) {
    float n = floor(uHitCount) + seed * JA_RECOLOUR;
    float idx = floor(n / JA_RECOLOUR);
    // The jellies whose count just wrapped are the ones the last onset took.
    float justNow = step(mod(n, JA_RECOLOUR), 1.0);
    float hNew = gZoneHue + 0.6 * fract(idx * 0.618034 + seed * 0.25);
    float hOld = gZoneHue + 0.6 * fract((idx - 1.0) * 0.618034 + seed * 0.25);
    fresh = justNow * (1.0 - gOnsetProg);
    return justNow > 0.5 ? mix(hOld, hNew, gOnsetProg) : hNew;
}

/** Depth zone z: water colour, and (fog density, shaft strength, jelly hue, snow amount). */
void zoneParams(float z, out vec3 water, out vec4 prm) {
    float zi = mod(z, 4.0);
    if (zi < 0.5) {
        water = vec3(0.02, 0.10, 0.14);
        prm = vec4(0.050, 1.0, 0.50, 1.0);
    } else if (zi < 1.5) {
        water = vec3(0.015, 0.03, 0.09);
        prm = vec4(0.042, 0.5, 0.72, 0.8);
    } else if (zi < 2.5) {
        water = vec3(0.006, 0.008, 0.03);
        prm = vec4(0.036, 0.15, 0.92, 0.6);
    } else {
        water = vec3(0.003, 0.012, 0.012);
        prm = vec4(0.030, 0.0, 0.30, 0.45);
    }
}

void main() {
    vec2 uv = view();

    float swell = clamp(uSwell, 0.0, 1.5);
    float kick = clamp(uHits.x, 0.0, 1.0);
    float snare = clamp(uHits.y, 0.0, 1.0);
    float hats = clamp(uHits.z, 0.0, 1.0);
    float onset = clamp(uHits.w, 0.0, 1.0);
    float drop = clamp(uEvents.y, 0.0, 1.0);
    float pitch = clamp(uEvents.w, 0.0, 1.0);
    float hiBand = clamp(0.6 * uBandsHi.x + 0.5 * uBandsHi.y, 0.0, 1.0);

    // Kick: contract at once, jet on the bump as the envelope falls (0 at the
    // hit and at rest, so the jellies glide up and settle back).
    gContract = 0.55 * kick;
    gJet = 4.0 * kick * (1.0 - kick);
    gSway = 0.15 + 0.25 * clamp(uBassSmooth, 0.0, 1.5) / 1.5 + 0.2 * clamp(uBandsLo.x, 0.0, 1.0);
    // 0 on the onset's frame (it triggers to 0.4..1), 1 once it has fallen.
    gOnsetProg = smoothstep(0.4, 0.05, onset);

    // ---- depth zone, eased across a section change ---------------------------
    float sec = floor(uSectionCount);
    float secBlend = smoothstep(0.95, 0.15, clamp(uEvents.z, 0.0, 1.0));
    vec3 waterP;
    vec3 waterC;
    vec4 prmP;
    vec4 prmC;
    zoneParams(sec - 1.0, waterP, prmP);
    zoneParams(sec, waterC, prmC);
    vec3 water = mix(waterP, waterC, secBlend);
    vec4 prm = mix(prmP, prmC, secBlend);
    float fogD = prm.x;

    // The drop swaps the palette for its length and glides back as it ends.
    float dropHue = 0.4 * smoothstep(0.0, 0.3, drop);
    float keyHue = 0.08 * sin(JA_TAU * uKeyHue) * clamp(uKeyStrength, 0.0, 1.0);
    gZoneHue = prm.z + dropHue + 0.12 * pitch + keyHue + 0.02 * sin(uTime * 0.04);
    water = mix(water, pal(gZoneHue + 0.5) * length(water) * 0.6, 0.3);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Exact (power of two) and invisible: every z term tiles with 256.
    ro.z = mod(ro.z, 256.0);

    // ---- march, integrating the jellies' glow on the way ---------------------
    float t = 0.05;
    float hitT = -1.0;
    float glowAmt = 0.0;
    vec2 hueVec = vec2(0.0);
    for (int i = 0; i < JA_MAX_STEPS; i++) {
        if (float(i) >= uSteps) break;
        if (t > JA_FAR) break;
        float d = map(ro + rd * t);
        float stepLen = max(d, 0.015 + 0.002 * t);
        if (gAlive > 0.5) {
            float fresh;
            float hue = jellyHue(gSeed, fresh);
            // Snare: a pulse front runs from the bell to the tips as it falls.
            float s = clamp(-gQ.y / gLen, 0.0, 1.0);
            float front = 1.1 * (1.0 - snare) - 0.05;
            float flash = sqrt(snare) * exp(-(s - front) * (s - front) * 40.0) * step(gQ.y, 0.0);
            float rb = exp(-gDB * 6.0) * 0.12 * (0.25 + 0.6 * kick + 1.5 * fresh + 1.6 * drop);
            float rt = exp(-gDT * 30.0) * (0.4 + 4.0 * flash + 1.2 * drop);
            float e = (rb + rt) * exp(-t * fogD) * min(stepLen, 1.0);
            glowAmt += e;
            hueVec += e * vec2(cos(JA_TAU * hue), sin(JA_TAU * hue));
        }
        if (d < 0.002 * t + 0.004) {
            hitT = t;
            break;
        }
        t += stepLen;
    }
    float tEnd = hitT > 0.0 ? hitT : JA_FAR;

    // Water: lighter looking up toward the surface.
    vec3 fogCol = water * (0.25 + 0.75 * smoothstep(-0.5, 1.0, rd.y));
    vec3 col = fogCol;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, 0.001 * hitT + 0.002);
        map(p);
        float fresh;
        float hue = jellyHue(gSeed, fresh);
        float glowMul = 1.0 + 0.6 * kick + 2.0 * fresh + 1.5 * drop;
        vec3 jc;
        if (gDB <= gDT) {
            // The bell: see-through in the middle, bright at grazing and at
            // the rim, with four radial canals that ride the high bands.
            float fr = pow(1.0 - abs(dot(n, -rd)), 2.0);
            float ang = atan(gQ.z, gQ.x + 1e-7);
            float canal = smoothstep(0.55, 1.0, 0.5 + 0.5 * cos(ang * 4.0)) * smoothstep(0.75, 0.15, length(gQ.xz)) * step(0.0, gQ.y);
            float rim = exp(-max(gQ.y + 0.08, 0.0) * 10.0);
            jc = fogCol * 0.6 + pal(hue) * (0.12 + 0.9 * fr + 0.6 * canal * (0.4 + 1.2 * hiBand) + 0.7 * rim) * glowMul;
            jc += vec3(0.8, 0.9, 1.0) * pow(max(dot(reflect(rd, n), vec3(0.0, 1.0, 0.0)), 0.0), 20.0) * 0.25 * prm.y;
        } else {
            float s = clamp(-gQ.y / gLen, 0.0, 1.0);
            float front = 1.1 * (1.0 - snare) - 0.05;
            float flash = sqrt(snare) * exp(-(s - front) * (s - front) * 40.0);
            jc = fogCol * 0.5 + pal(hue + 0.06) * (0.35 + 3.0 * flash + 1.2 * drop) * (0.6 + 0.4 * glowMul);
        }
        col = mix(fogCol, jc, exp(-hitT * fogD));
    }

    // The integrated glow, in the jellies' averaged hue.
    if (glowAmt > 1e-5 && dot(hueVec, hueVec) > 1e-12) {
        float gh = atan(hueVec.y, hueVec.x) / JA_TAU;
        col += pal(gh) * glowAmt * 2.5;
    }

    // ---- light shafts: a few fixed samples along the ray ---------------------
    if (prm.y > 0.001) {
        float sh = 0.0;
        float tPrev = 0.0;
        for (int i = 0; i < 6; i++) {
            float fi = float(i);
            float ts = 1.0 + fi * fi * 0.9 + fi * 1.5;
            if (ts > tEnd) break;
            vec3 p = ro + rd * ts;
            float u = p.x + p.y * 0.35;
            float v = p.z - p.y * 0.2;
            float a = 0.5 + 0.5 * sin(u * 0.9 + 0.6 * sin(v * (9.0 * JA_KZ) + uTime * 0.2));
            float b = 0.5 + 0.5 * sin(v * (20.0 * JA_KZ) + u * 0.3 + uTime * 0.15);
            float m = a * b;
            sh += m * m * m * exp(-ts * fogD) * smoothstep(-6.0, 8.0, p.y) * (ts - tPrev);
            tPrev = ts;
        }
        col += mix(vec3(0.6, 0.85, 1.0), pal(gZoneHue + 0.5), 0.3) * sh * prm.y * 0.035;
    }

    // ---- marine snow: flake layers every 2 units along z, world-anchored -----
    float az = abs(rd.z);
    if (az > 0.08) {
        float sgn = rd.z >= 0.0 ? 1.0 : -1.0;
        float base = sgn > 0.0 ? floor(ro.z * 0.5) + 1.0 : ceil(ro.z * 0.5) - 1.0;
        vec3 snow = vec3(0.0);
        for (int i = 0; i < 5; i++) {
            float zp = (base + sgn * float(i)) * 2.0;
            float ts = (zp - ro.z) / rd.z;
            if (ts > tEnd) break;
            vec3 p = ro + rd * ts;
            vec2 g = (p.xy + vec2(0.0, uTime * 0.12)) * 4.0;
            vec2 cell = floor(g);
            float li = mod(zp * 0.5, 128.0);
            float h = hashCell(ivec3(int(cell.x), int(cell.y), int(li)));
            vec2 jit = vec2(fract(h * 13.1), fract(h * 71.7)) - 0.5;
            float dd = length(fract(g) - 0.5 - jit * 0.6);
            float fade = exp(-ts * fogD * 1.2) * smoothstep(0.3, 1.0, ts);
            float flake = step(0.55, h) * smoothstep(0.07, 0.0, dd) * fade;
            float tw = 0.5 + 0.5 * sin(uTime * 25.0 + h * 90.0);
            float sparkle = step(0.8, fract(h * 37.0)) * hats * tw;
            snow += vec3(0.75, 0.85, 0.9) * flake * (0.35 + 1.8 * sparkle) * prm.w;
            // The drop's swarm: a share of the flakes glows in the palette.
            float swarm = step(fract(h * 5.3), 0.6 * drop) * smoothstep(0.16, 0.0, dd) * fade;
            snow += pal(gZoneHue + 0.2 + 0.3 * fract(h * 3.7)) * swarm * 1.4;
        }
        col += snow * smoothstep(0.08, 0.3, az);
    }

    col *= 0.8 + 0.25 * swell;
    col *= 0.72 + 0.28 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
