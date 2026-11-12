#version 300 es
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP (range [-2,2), ~8
// fraction bits). uSoundTex is R32F; on GPUs honoring sampler precision
// (Mali) every read is clamped and quantized.
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

// Morph, Warp and Turbulence shape the sun itself (ray count, streamer twist,
// granulation depth), so view() must not bend the picture with them as well.
#define VIEW_OWNS_SHAPE
//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Solar: the pulsing sun, now a body in space. A boiling photosphere (a
// sphere displaced by granulation noise, with dark lanes between bright
// cells, sunspots and limb darkening) hangs in a volumetric corona whose
// streamers are the old style's rays, carried round the sun in longitude.
// Three prominence loops arch off the surface, and a shock shell runs out
// through the corona on every kick. Stars sit far behind.
//
// The camera circles the sun on the shared rig (camAround, one turn per 1024
// world units of travel, so the rig's 8192 wrap is a whole number of turns
// and never shows). Nothing in the world depends on z, so there is no other
// wrap to fit. The eye sits at SO_CAM_R and the dolly can pull it in to 0.65
// of that, 3.25 world units; the photosphere's outermost point is
// SO_R + pulse + displacement, under 1.25, so the eye is never inside the
// sun. It can sit inside the corona, which is emission only and never marched
// for a hit.
//
// ---- audio ------------------------------------------------------------------
//
// As in the flat style: the low band (SND_LOW) lifts the whole corona, the EQ
// curve (sndEq, read along the streamer's longitude) lengthens each streamer,
// and the high band (SND_HIGH) makes the photosphere flicker. New in 3D:
//
//   kick (SND_KICK_ENV)          the photosphere swells (the pulse) and a shock
//                                shell runs outward through the corona
//   drop (SND_HELD_SURGE)        the prominences rise high and settle, on a
//                                0-1-0 bump so they glide instead of snapping
//   pitch (SND_HELD_NOTE_CHANGE) nudges the hue, and the key hue tints it
//   SND_ENERGY_TREND             overall level
//
// Knobs (uKnob, exactly 0.5 = the tuned look): k0 PULSE the kick swell, k1
// TEXTURE the granule size, k2 FLOW the boil rate, k3 TONE where the
// streamers stand (a high melody lifts them off the equator into plumes at the
// poles, a low one gathers them into a tight equatorial belt; plus a small hue
// offset), k4 BUILD the prominence height, k5 SPACE the corona's reach.
//
// ---- why the estimate is safe ----------------------------------------------
//
// The photosphere is f(p) = |p| - R - A * h(p), h = fbm3 (3 octaves) of the
// spun point times F. vnoise3's quintic gradient is at most 1.875 per lattice
// unit; fbm3's normalized octave sum (lacunarity 2.02) bounds |grad h| by
// 1.74 * 1.875 * F < 3.3 F. So |grad f| <= 1 + 3.3 A F, and the march steps
// f / (1 + 3.3 A F), a true lower bound on the distance. The march starts at
// the analytic bounding sphere R + A and gives up once it leaves it, so the
// loop is bounded by uSteps under a constant SO_MAX_STEPS.

#define SO_MAX_STEPS 64
#define SO_CORONA_SAMPLES 14
#define SO_TAU 6.2831853
#define SO_PI 3.14159265
/** Photosphere radius at rest. */
#define SO_R 1.0
/** Camera orbit radius; the dolly pulls it in to 0.65 of this. */
#define SO_CAM_R 5.0
/** How far out the corona is sampled, beyond the surface. */
#define SO_CORONA 2.6

/** Sun's spin, applied to every surface and corona lookup. */
mat3 gSpin;
float gR;
float gA;
float gFreq;
float gStepK;
vec3 gBoil;
/** fbm value at the last map() point: 0..1, granule height. */
float gGran;

float sunMap(vec3 p) {
    vec3 q = gSpin * p;
    float h = fbm3(q * gFreq + gBoil, 3);
    gGran = h;
    return (length(p) - gR - gA * h) * gStepK;
}

vec3 sunNormal(vec3 p) {
    const float e = 0.004;
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * sunMap(p + k.xyy * e) + k.yyx * sunMap(p + k.yyx * e)
                   + k.yxy * sunMap(p + k.yxy * e) + k.xxx * sunMap(p + k.xxx * e));
}

/** Ray against a sphere at the origin: (entry, exit), or (-1, -1) on a miss. */
vec2 sphereSpan(vec3 ro, vec3 rd, float r) {
    float b = dot(ro, rd);
    float h = b * b - (dot(ro, ro) - r * r);
    if (h < 0.0) return vec2(-1.0);
    h = sqrt(h);
    return vec2(-b - h, -b + h);
}

/** Distance from p to a prominence: a ring through the surface, its upper arc standing in the corona. */
float loopDist(vec3 p, vec3 n, vec3 ax, float lift, float a) {
    vec3 rel = p - n * (gR - 0.15 + lift);
    float w = dot(rel, ax);
    vec3 v = rel - ax * w;
    return length(vec2(length(v) - a, w));
}

vec3 starfield(vec3 rd) {
    vec3 col = vec3(0.004, 0.003, 0.008);
    vec3 sc = rd * 120.0;
    float h = hashCell(ivec3(floor(sc)));
    float s = step(0.986, h) * (1.0 - smoothstep(0.0, 0.35, length(fract(sc) - 0.5)));
    float tw = 0.75 + 0.25 * sin(uTime * (2.0 + 6.0 * fract(h * 31.0)) + h * 80.0);
    col += mix(vec3(1.0), pal(fract(h * 13.0)), 0.3) * s * 0.5 * tw;
    return col;
}

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camAround(uv, SO_CAM_R, 1.0 * CAM_SPIN_UNIT, ro, rd);

    float low = sndShaped(SND_LOW);
    float high = sndShaped(SND_HIGH);
    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float drop = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float dropBump = 4.0 * drop * (1.0 - drop);
    float pitch = clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0);
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);

    float hue = 0.06 + 0.1 * (uKnob[3] - 0.5) + 0.08 * pitch
              + 0.05 * sin(SO_TAU * uKeyHue) * clamp(uKeyStrength, 0.0, 1.0);
    float ext = 1.0 + 0.6 * (uKnob[5] - 0.5);
    float tonePush = clamp(1.4 * (uKnob[3] - 0.5), 0.0, 0.7);
    float tonePull = clamp(0.5 - uKnob[3], 0.0, 0.5);

    // ---- the photosphere's shape for this frame -------------------------------
    gSpin = rotY(uTime * 0.04);
    gR = SO_R + 0.05 * (0.4 + 1.2 * uKnob[0]) * kick;
    gA = 0.03 + 0.04 * clamp(uTurbulence, 0.0, 1.5);
    gFreq = 6.0 * (0.75 + 0.5 * uKnob[1]);
    gStepK = 1.0 / (1.0 + 3.3 * gA * gFreq);
    float boil = uTime * 0.12 * (0.5 + uKnob[2]);
    gBoil = vec3(boil, -0.6 * boil, 0.35 * boil);

    // ---- march the photosphere inside its bounding shell ----------------------
    float hitT = -1.0;
    vec2 shell = sphereSpan(ro, rd, gR + gA + 0.01);
    if (shell.y > 0.0) {
        float t = max(shell.x, 0.0);
        for (int i = 0; i < SO_MAX_STEPS; i++) {
            // uSteps is the user's Detail; this loop's own bound is the constant.
            if (float(i) >= uSteps) break;
            float d = sunMap(ro + rd * t);
            if (d < 0.0015) {
                hitT = t;
                break;
            }
            t += d;
            if (t > shell.y) break;
        }
    }

    vec3 col = starfield(rd);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = sunNormal(p);
        sunMap(p);
        vec3 s = gSpin * normalize(p);
        // Bright cells, dark lanes where the granule field crosses its middle.
        float cell = smoothstep(0.0, 0.6, abs(2.0 * gGran - 1.0));
        float mu = clamp(dot(n, -rd), 0.0, 1.0);
        float limb = 0.4 + 0.6 * pow(mu, 0.5);
        float spot = smoothstep(0.64, 0.74, vnoise3(s * 2.6 + vec3(11.0, 3.0, 7.0) + boil * 0.1));
        float flicker = 0.9 + 0.1 * sin(uTime * 9.0 + gGran * 20.0) * high;
        vec3 surf = pal(hue + 0.03 * gGran) * (0.75 + 0.55 * cell);
        surf = mix(surf, vec3(1.0, 0.94, 0.82), 0.22 * cell * mu);
        col = surf * 1.1 * limb * flicker * (1.0 - 0.75 * spot);
    }

    // ---- the corona: emission sampled along the ray ---------------------------
    float rOut = gR + SO_CORONA * ext;
    vec2 span = sphereSpan(ro, rd, rOut);
    if (span.y > 0.0) {
        float t0 = max(span.x, 0.0);
        float t1 = hitT > 0.0 ? hitT : span.y;
        float dt = max(t1 - t0, 0.0) / float(SO_CORONA_SAMPLES);
        float jit = hash13(vec3(gl_FragCoord.xy, 3.0));

        // Ray count: Morph picks it as in the flat style, crossfaded between
        // whole counts so the longitude never seams and the slider never pops.
        float nRays = 8.0 + uMorph * 10.0;
        float nLo = floor(nRays);
        float nMix = nRays - nLo;

        // Prominences: three loops riding the spin, raised by the drop and BUILD.
        float lift = max(0.15 * dropBump + 0.3 * (uKnob[4] - 0.5) + 0.05 * low, -0.05);
        float loopA = 0.28 + 0.3 * dropBump;
        vec3 ln0 = normalize(vec3(0.8, 0.35, 0.3));
        vec3 ln1 = normalize(vec3(-0.5, -0.2, 0.85));
        vec3 ln2 = normalize(vec3(-0.3, 0.7, -0.6));
        vec3 la0 = normalize(cross(ln0, vec3(0.0, 1.0, 0.0)));
        vec3 la1 = normalize(cross(ln1, vec3(0.3, 1.0, 0.1)));
        vec3 la2 = normalize(cross(ln2, vec3(1.0, 0.0, 0.2)));
        float shockR = gR + 2.2 * ext * (1.0 - kick);

        vec3 glow = vec3(0.0);
        for (int i = 0; i < SO_CORONA_SAMPLES; i++) {
            vec3 p = ro + rd * (t0 + (float(i) + jit) * dt);
            float r = length(p);
            if (r < gR) continue;
            vec3 q = gSpin * p;
            vec3 dir = q / r;
            float lon = atan(dir.z, dir.x);
            // Warp twists the streamers with latitude.
            float ph = lon + uWarp * 1.5 * dir.y + uTime;
            float rayLo = pow(max(0.0, sin(ph * nLo)), 3.0);
            float rayHi = pow(max(0.0, sin(ph * (nLo + 1.0))), 3.0);
            // abs(lon) / pi runs 0..1..0 round the sun, so the EQ read has no seam.
            float rays = mix(rayLo, rayHi, nMix) * 0.5 * sndEq(abs(lon) / SO_PI);
            // TONE moves the streamers in latitude; at rest this is 1 - y^2.
            float lat = dir.y * dir.y;
            float belt = mix(pow(1.0 - lat, 1.0 + 3.0 * tonePull), lat, tonePush);
            rays *= 0.45 + 0.55 * belt;
            float x = (r - gR) / ext;
            float dens = exp(-x * 3.5) * (0.35 + rays + low);

            float dl = min(loopDist(q, ln0, la0, lift, loopA),
                       min(loopDist(q, ln1, la1, lift, loopA * 0.8), loopDist(q, ln2, la2, lift, loopA * 1.15)));
            float prom = exp(-dl * dl * 260.0) * (0.8 + 0.6 * dropBump);
            float sd = r - shockR;
            float shock = kick * exp(-sd * sd * 30.0) * exp(-x * 0.8);

            vec3 c = pal(hue + 0.078 * r / SO_R) * dens
                   + pal(hue - 0.02) * prom * 2.0
                   + pal(hue + 0.12) * shock * 0.9;
            glow += c;
        }
        col += glow * dt * 0.6;
    }

    col *= 0.85 + 0.2 * swell;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
