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

// Ringed Planet: a banded gas giant with a tilted, gapped ring system, one or
// two moons and a starfield, circled on the shared rig (camAround).
//
// Nothing is marched. The planet and moons are analytic ray-sphere hits and
// the rings an analytic ray-plane hit inside an annulus, so the frame costs a
// handful of noise lookups per pixel and never reads uSteps. The planet's
// spin axis is the ring normal, so the bands always run parallel to the rings.
// Shadows are analytic too: the rings shadow the planet (a ray to the sun
// against the ring plane) and the planet shadows the rings and moons (a soft
// closest-approach test against the sphere).
//
// The eye sits at RP_CAM_R, and the dolly can pull it in to 0.65 of that,
// 4.9 world units; the outermost moon reaches 4.55, so the camera is never
// inside anything whatever the rig's look and travel do.
//
// ---- audio ------------------------------------------------------------------
//
// Each sound moves a DIFFERENT property, so no two events read alike:
//
//   kick (uHits.x)         a bright wave runs outward through the rings
//   snare (uHits.y)        lightning flickers at storm sites in the clouds
//   hats (uHits.z)         ring particles glint and half the stars twinkle
//   onset (uHits.w)        the storm vortex spins up a step (continuous, via
//                          uHitCount, so it winds on rather than jumping);
//                          the onset count also moves the lightning sites
//   low bands (uBandsLo)   band turbulence: the cloud belts shear and wobble
//   high bands (uBandsHi)  polar aurora ovals and a halo over the poles
//   drop (uEvents.y)       the rings tip over and back, the sun swings round
//                          and the palette turns, all on a 0-1-0 bump
//   pitch change (uEvents.w) and the key hue: the atmosphere's hue
//   section (uSectionCount) which planet palette, ring gaps and moons; the
//                          change eases over the section envelope
//
// Overall level follows uSwell only.

#define RP_CAM_R 7.5
#define RP_R 1.5
#define RP_RIN 1.95
#define RP_ROUT 3.4
#define RP_TAU 6.2831853

/** Ring normal (= the planet's spin axis), and two axes in the ring plane. */
vec3 gN;
vec3 gT;
vec3 gB;
/** Unit direction to the sun. */
vec3 gL;
float gAtmHue;
float gRingHue;
float gMorph;
float gTurb;
float gHi;
vec2 gGaps;

/** Nearest positive hit of the ray with a sphere, or -1. The eye is always outside. */
float sphereHit(vec3 ro, vec3 rd, vec3 c, float r) {
    vec3 oc = ro - c;
    float b = dot(oc, rd);
    float h = b * b - (dot(oc, oc) - r * r);
    if (h < 0.0) return -1.0;
    float t = -b - sqrt(h);
    return t > 0.0 ? t : -1.0;
}

/** 1 in sunlight, 0 in the planet's shadow, soft at the edge. `p` is outside the planet. */
float planetShadow(vec3 p) {
    float tc = -dot(p, gL);
    if (tc <= 0.0) return 1.0;
    float b = length(p + gL * tc);
    return smoothstep(RP_R * 0.96, RP_R * 1.04, b);
}

/** Ring optical density at radius r: ringlets, two gaps (section-chosen) and soft edges. */
float ringDensity(float r) {
    float x = (r - RP_RIN) / (RP_ROUT - RP_RIN);
    if (x <= 0.0 || x >= 1.0) return 0.0;
    float edge = smoothstep(0.0, 0.05, x) * smoothstep(1.0, 0.97, x);
    float bands = 0.55 + 0.22 * sin(r * 47.0) + 0.13 * sin(r * 113.0 + 1.3) + 0.1 * sin(r * 19.0 + 0.4);
    float gapA = smoothstep(0.012, 0.035, abs(x - gGaps.x));
    float gapB = smoothstep(0.004, 0.014, abs(x - gGaps.y));
    // The inner ring is the faint, dusty one.
    float inner = mix(0.45, 1.0, smoothstep(0.12, 0.22, x));
    return clamp(bands, 0.0, 1.0) * edge * gapA * gapB * inner;
}

/** The gas giant's surface colour at hit point P. */
vec3 shadePlanet(vec3 P, vec3 rd, float snare, float onset) {
    vec3 n = P / RP_R;
    vec3 lp = vec3(dot(n, gT), dot(n, gN), dot(n, gB));
    float lat = lp.y;
    // Differential rotation: belts at different latitudes turn at different
    // rates, which is what shears the clouds into a gas giant's streaks. In
    // 3D about the spin axis, so there is no longitude seam.
    float rotA = uTime * (0.05 + 0.04 * sin(lat * 5.0)) + uFlowPhase * 0.004;
    vec2 cs = rot2(rotA) * lp.xz;
    vec3 q = vec3(cs.x, lat, cs.y);

    // The storm rides with its belt. Its swirl angle winds on by uHitCount, so
    // each onset spins it up a step and it holds there.
    vec3 sC = normalize(vec3(0.78, -0.38, 0.5));
    float sr = length(q - sC);
    float swirl = (uTime * 0.3 + 2.4 * gMorph) * exp(-sr * sr * 26.0);
    vec3 qs = rotAxis(sC, swirl) * q;

    float n1 = fbm3(qs * vec3(2.2, 7.0, 2.2) + vec3(0.0, 0.0, uTime * 0.02), 4);
    float n2 = vnoise3(qs * 14.0 + 3.1);
    float latW = lat + gTurb * (n1 - 0.5) * 0.3 + 0.015 * (n2 - 0.5);
    float band = 0.5 + 0.5 * sin(latW * 19.0 + 1.7 * sin(latW * 6.0 + 0.5));
    float fine = 0.5 + 0.5 * sin(latW * 47.0 + n1 * 3.0);
    float bt = band * 0.7 + fine * 0.3;
    vec3 base = mix(pal(gAtmHue + 0.05), pal(gAtmHue + 0.32), bt);
    base = mix(base, vec3(0.92, 0.86, 0.76), 0.3 * band * fine);
    // The storm: its own colour, a dark eye, a little brighter just after an onset.
    float sm = exp(-sr * sr * 90.0);
    base = mix(base, pal(gAtmHue + 0.55) * 1.1, sm * 0.75);
    base *= 1.0 - 0.5 * exp(-sr * sr * 900.0);
    base += pal(gAtmHue + 0.55) * sm * onset * 0.35;

    // Light: a soft terminator, the rings' shadow, limb darkening.
    float ndl = dot(n, gL);
    float day = smoothstep(-0.12, 0.35, ndl);
    float rs = 0.0;
    float den = dot(gL, gN);
    if (abs(den) > 1e-3) {
        float ts = -dot(P, gN) / den;
        if (ts > 0.0) rs = ringDensity(length(P + gL * ts)) * 0.8;
    }
    float mu = clamp(dot(n, -rd), 0.0, 1.0);
    vec3 col = base * (0.025 + 1.1 * day * (1.0 - rs));
    col *= 0.55 + 0.45 * pow(mu, 0.4);
    col += pal(gAtmHue + 0.12) * pow(1.0 - mu, 3.0) * (0.12 + 0.8 * day);

    // Snare: lightning at three storm sites, which the onset count re-seeds.
    float bolt = 0.0;
    float hc = floor(uHitCount);
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float hs = hash11(fi * 7.31 + hc * 1.73 + 0.11);
        float hs2 = hash11(fi * 3.17 + hc * 2.91 + 0.53);
        vec3 site = normalize(vec3(cos(hs * RP_TAU), (hs2 - 0.5) * 1.2, sin(hs * RP_TAU)));
        float dd = length(q - site);
        float flick = step(0.45, hash11(floor(uTime * 24.0) + fi * 13.0));
        bolt += (exp(-dd * dd * 700.0) + 0.35 * exp(-dd * dd * 60.0)) * flick;
    }
    col += vec3(0.7, 0.82, 1.0) * bolt * snare * (0.5 + 0.5 * (1.0 - day)) * 1.4;

    // High bands: the auroral ovals, fixed to the spin axis (lp, not q).
    float al = abs(lat);
    float oval = smoothstep(0.8, 0.9, al) * smoothstep(0.995, 0.93, al);
    float lon = atan(lp.z, lp.x + 1e-7);
    float cur = 0.5 + 0.5 * sin(lon * 9.0 + uTime * 0.9 + 2.5 * sin(lon * 2.0 - uTime * 0.3));
    vec3 auroraCol = mix(pal(gAtmHue + 0.4), vec3(0.35, 1.0, 0.6), 0.35);
    col += auroraCol * oval * (0.35 + 0.65 * cur) * gHi * 1.3 * (0.4 + 0.6 * (1.0 - day));
    return col;
}

/** A moon's surface: cratered grey, tinted by the section palette, shadowed by the planet. */
vec3 shadeMoon(vec3 P, vec3 c, float rad, float h) {
    vec3 nm = (P - c) / rad;
    float cr = fbm3(nm * 3.0 + h * 17.0, 3);
    vec3 base = mix(vec3(0.55, 0.52, 0.5), pal(gRingHue + 0.6 + h * 0.3), 0.3) * (0.55 + 0.7 * cr);
    float diff = smoothstep(-0.05, 0.45, dot(nm, gL));
    return base * (0.015 + 1.05 * diff * planetShadow(P));
}

/** Stars, a faint galactic band and the sun. */
vec3 sky(vec3 rd, float hats) {
    vec3 col = vec3(0.004, 0.005, 0.01);
    vec3 sc = rd * 120.0;
    float h = hashCell(ivec3(floor(sc)));
    float s = step(0.985, h) * smoothstep(0.35, 0.0, length(fract(sc) - 0.5));
    float tw = 0.5 + 0.5 * sin(uTime * (3.0 + 9.0 * fract(h * 31.0)) + h * 80.0);
    float twinkles = step(0.5, fract(h * 57.0));
    col += mix(vec3(1.0), pal(fract(h * 13.0)), 0.3) * s * (0.45 + 0.15 * tw + hats * twinkles * 1.6 * tw);
    float gal = exp(-abs(dot(rd, normalize(vec3(0.3, 1.0, 0.2)))) * 5.0);
    col += pal(gAtmHue + 0.6) * 0.035 * gal * (0.5 + 0.5 * vnoise3(rd * 6.0));
    float ca = max(dot(rd, gL), 0.0);
    col += vec3(1.0, 0.92, 0.8) * (3.0 * smoothstep(0.9993, 0.9997, ca) + 0.25 * pow(ca, 64.0) + 0.04 * pow(ca, 6.0));
    return col;
}

void main() {
    vec2 uv = view();

    float swell = clamp(uSwell, 0.0, 1.5);
    float kick = clamp(uHits.x, 0.0, 1.0);
    float snare = clamp(uHits.y, 0.0, 1.0);
    float hats = clamp(uHits.z, 0.0, 1.0);
    float onset = clamp(uHits.w, 0.0, 1.0);
    float drop = clamp(uEvents.y, 0.0, 1.0);
    // 0 at the drop's instant and at its end, 1 in between: the tilt and the
    // sun glide over and back instead of snapping.
    float dropBump = 4.0 * drop * (1.0 - drop);

    // 0 on the hit frame (an onset triggers to 0.4..1), 1 once it has fallen:
    // the storm's wind-up is continuous across the hit.
    float prog = smoothstep(0.4, 0.05, onset);
    gMorph = uHitCount - 1.0 + prog;

    // ---- section: palette, ring gaps and moons ------------------------------
    float sec = floor(uSectionCount);
    float secBlend = smoothstep(0.95, 0.15, clamp(uEvents.z, 0.0, 1.0));
    float hPrev = hash11(sec - 1.0 + 0.37);
    float hCur = hash11(sec + 0.37);
    float secHue = mix(0.25 * floor(hPrev * 4.0), 0.25 * floor(hCur * 4.0), secBlend);
    float tiltBase = mix(0.3 + 0.3 * hPrev, 0.3 + 0.3 * hCur, secBlend);
    float yaw = mix(hPrev, hCur, secBlend) * 2.0 + 0.4;
    gGaps = mix(vec2(0.5 + 0.12 * hPrev, 0.78 + 0.1 * fract(hPrev * 7.0)),
                vec2(0.5 + 0.12 * hCur, 0.78 + 0.1 * fract(hCur * 7.0)), secBlend);

    float pitch = clamp(uEvents.w, 0.0, 1.0);
    float keyHue = 0.08 * sin(RP_TAU * uKeyHue) * clamp(uKeyStrength, 0.0, 1.0);
    gAtmHue = 0.06 + secHue + 0.14 * pitch + keyHue + 0.3 * dropBump + 0.02 * sin(uTime * 0.03);
    gRingHue = 0.1 + secHue + 0.3 * dropBump;

    gTurb = 0.18 + 0.22 * clamp(uBassSmooth, 0.0, 1.5) / 1.5 + 0.25 * clamp(uBandsLo.x, 0.0, 1.0)
          + 0.15 * clamp(uBandsLo.z, 0.0, 1.0);
    gHi = clamp(0.6 * uBandsHi.y + 0.6 * uBandsHi.z, 0.0, 1.2);

    // ---- frame: ring normal (= spin axis) and the sun -----------------------
    float tilt = tiltBase * (1.0 - 2.0 * dropBump);
    gN = normalize(vec3(sin(tilt) * cos(yaw), cos(tilt), sin(tilt) * sin(yaw)));
    gT = normalize(cross(gN, vec3(0.0, 0.0, 1.0)));
    gB = cross(gT, gN);
    gL = normalize(rotY(0.6 + 1.6 * dropBump + 0.8 * mix(hPrev, hCur, secBlend)) * vec3(0.8, 0.35, -0.5));

    vec3 ro;
    vec3 rd;
    camAround(uv, RP_CAM_R, 2.0 * CAM_SPIN_UNIT, ro, rd);

    // ---- solids: the nearest of planet and moons, shaded once ----------------
    vec3 col = sky(rd, hats);
    float tP = sphereHit(ro, rd, vec3(0.0), RP_R);
    float tS = tP;
    float tMoon = -1.0;
    vec3 moonC = vec3(0.0);
    float moonR = 1.0;
    float moonH = 0.0;
    for (int i = 0; i < 2; i++) {
        float fi = float(i);
        float h = mix(hash11(sec - 1.0 + fi * 11.3 + 0.7), hash11(sec + fi * 11.3 + 0.7), secBlend);
        // The second moon comes and goes with the section, growing in or out.
        float here = i == 0 ? 1.0 : mix(step(0.45, hash11(sec - 1.0 + 5.5)), step(0.45, hash11(sec + 5.5)), secBlend);
        float rad = (0.17 + 0.13 * h) * here;
        if (rad < 0.01) continue;
        float orbR = 3.85 + 0.35 * h;
        float ang = uTime * (0.05 + 0.04 * h) * (i == 0 ? 1.0 : -1.0) + h * RP_TAU + fi * 2.1;
        vec3 c = (gT * cos(ang) + gB * sin(ang)) * orbR + gN * (h - 0.5) * 0.5;
        float tm = sphereHit(ro, rd, c, rad);
        if (tm > 0.0 && (tS < 0.0 || tm < tS)) {
            tS = tm;
            tMoon = tm;
            moonC = c;
            moonR = rad;
            moonH = h;
        }
    }
    if (tMoon > 0.0) {
        col = shadeMoon(ro + rd * tMoon, moonC, moonR, moonH);
    } else if (tP > 0.0) {
        col = shadePlanet(ro + rd * tP, rd, snare, onset);
    }

    // Atmosphere halo just outside the limb, brighter toward the sun, with the
    // aurora's glow over the poles.
    if (tS < 0.0) {
        float tc = max(-dot(ro, rd), 0.0);
        vec3 cp = ro + rd * tc;
        float b = length(cp);
        if (b > RP_R) {
            vec3 cn = cp / b;
            float hz = exp(-(b - RP_R) * 7.0);
            float lit = smoothstep(-0.3, 0.6, dot(cn, gL));
            col += pal(gAtmHue + 0.12) * hz * (0.04 + 0.3 * lit + 1.2 * pow(max(dot(rd, gL), 0.0), 6.0));
            float pole = smoothstep(0.75, 0.95, abs(dot(cn, gN)));
            col += mix(pal(gAtmHue + 0.4), vec3(0.35, 1.0, 0.6), 0.35) * pole * gHi * exp(-(b - RP_R) * 14.0) * 0.6;
        }
    }

    // ---- the rings, over whatever is behind them -----------------------------
    float den = dot(rd, gN);
    if (abs(den) > 1e-4) {
        float tr = -dot(ro, gN) / den;
        if (tr > 0.0 && (tS < 0.0 || tr < tS)) {
            vec3 Q = ro + rd * tr;
            float r = length(Q);
            float dens = ringDensity(r);
            if (dens > 0.0) {
                float x = (r - RP_RIN) / (RP_ROUT - RP_RIN);
                vec3 rc = mix(vec3(0.85, 0.78, 0.66), pal(gRingHue + x * 0.25), 0.45);
                // Lit face or seen through; forward scatter toward the sun.
                bool sameSide = dot(gN, gL) * dot(gN, -rd) > 0.0;
                float lit = abs(dot(gN, gL)) * (sameSide ? 1.0 : 0.45) + 0.05;
                lit += 0.6 * pow(max(dot(rd, gL), 0.0), 6.0);
                vec3 ringCol = rc * lit * planetShadow(Q) * 1.2;
                // Kick: a wave front travelling outward as the envelope falls.
                float front = RP_RIN - 0.15 + (RP_ROUT - RP_RIN + 0.4) * (1.0 - kick);
                float wave = kick * exp(-(r - front) * (r - front) * 80.0);
                ringCol += pal(gRingHue + 0.3) * wave * 1.4;
                // Hats: individual ring particles glint.
                float a = atan(dot(Q, gB), dot(Q, gT));
                vec2 cell = vec2(floor(r * 60.0), mod(floor((a / RP_TAU + 0.5) * 512.0), 512.0));
                float hp = hashCell(ivec3(int(cell.x), int(cell.y), 3));
                float tw = 0.5 + 0.5 * sin(uTime * 20.0 + hp * 100.0);
                ringCol += vec3(1.0, 0.95, 0.85) * step(0.95, hp) * tw * hats * 1.6;
                // Optical depth grows at grazing angles, as through a real sheet.
                float alpha = 1.0 - exp(-dens * 1.6 / max(abs(den), 0.08));
                col = mix(col, ringCol, clamp(alpha, 0.0, 0.97)) + pal(gRingHue + 0.3) * wave * 0.3 * dens;
            }
        }
    }

    col *= 0.85 + 0.2 * swell;
    col *= 0.75 + 0.25 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
