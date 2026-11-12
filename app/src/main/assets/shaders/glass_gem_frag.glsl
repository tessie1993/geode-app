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
//#include lib_glass

// Glass Gem: a round brilliant floating in the dark, turning on its axis, its
// fire thrown out in coloured flecks. A brilliant is a convex polyhedron - a
// table, eight crown facets, a sixteen-sided girdle and eight pavilion facets -
// so a ray meets it EXACTLY by clipping against its 33 planes: no march, no
// epsilon creep along edges, and every internal bounce is one more pass over
// the planes. That is what lets this style afford what a gem is: light enters
// the table, hits the pavilion past the critical angle, reflects totally,
// crosses, reflects again, and leaves through the crown bent per wavelength.
// Up to four internal bounces (glassQuality), each leaking its Fresnel share
// out through the facet it meets with three-wavelength dispersion; what is
// left after the last continues as one dim lookup.
//
// The cut's proportions are a real brilliant's: crown 35 degrees, pavilion 41
// (just past the critical angle at IOR 2, so the pavilion mirrors), table about
// 60% of the girdle.
//
// ---- audio ------------------------------------------------------------------
//
//   centroid (sharp)      fire: the dispersion widens from 0.02 to 0.07, so a
//                         bright mix sprays wider rainbows;
//   pitch height          raises the key light: the play of light walks across
//                         the crown as the melody climbs;
//   (no sound turns the stone or the key round it: its slow spin is time
//                         alone, owner 2026-11-12, no style spins to the music);
//   bass                  the IOR 1.95 -> 2.25: deeper stones sparkle harder;
//   hat                   glints on the facets, and flickers the starburst of
//                         spectral rays the stone throws into the dark round it.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the spin. Complexity tints the stone (colourless to sapphire).
// Chaos wobbles the facet normals. Energy scales every reaction. Knobs
// (lib_glass's roles, offsets from 0.5 so off is the tuned look): k0 PULSE the
// emission, k1 TEXTURE the fire's rays' width, k2 FLOW added to Chaos's
// wobble, k3 TONE the palette position, k4 BUILD the world's glow, k5 SPACE
// the fire's reach.
//
// ---- cost -------------------------------------------------------------------
//
// 33 plane tests per bounce, 2 to 4 bounces, 3 world lookups per bounce plus
// one reflection: about 8 to 14 VOID lookups on the stone's pixels (the stone
// covers a fifth of the frame). No march: the Detail control sets bounces.

#define GM_N 33
#define GM_CAM_R 3.5
#define GM_SCALE 1.15

vec4 gP[GM_N];
mat3 gRot;

void buildCut() {
    float s35 = sin(0.611);
    float c35 = cos(0.611);
    float s41 = sin(0.716);
    float c41 = cos(0.716);
    gP[0] = vec4(0.0, 1.0, 0.0, 0.31);
    for (int i = 0; i < 8; i++) {
        float az = float(i) * GLASS_TAU / 8.0;
        vec3 n = vec3(s35 * cos(az), c35, s35 * sin(az));
        gP[1 + i] = vec4(n, s35 + 0.05 * c35);
        float ap = az + GLASS_TAU / 16.0;
        vec3 m = vec3(s41 * cos(ap), -c41, s41 * sin(ap));
        gP[9 + i] = vec4(m, s41 + 0.05 * c41);
    }
    for (int i = 0; i < 16; i++) {
        float az = float(i) * GLASS_TAU / 16.0 + GLASS_TAU / 32.0;
        gP[17 + i] = vec4(cos(az), 0.0, sin(az), 1.0);
    }
}

/** Entry into the stone from outside: (tNear, tFar), the entry facet's normal in `n`. tNear > tFar is a miss. */
vec2 enterCut(vec3 ro, vec3 rd, out vec3 n) {
    float tn = -1e4;
    float tf = 1e4;
    n = vec3(0.0, 1.0, 0.0);
    for (int i = 0; i < GM_N; i++) {
        vec3 pn = gP[i].xyz;
        float den = dot(rd, pn);
        float num = gP[i].w - dot(ro, pn);
        if (abs(den) < 1e-6) {
            if (num < 0.0) return vec2(1.0, -1.0);
            continue;
        }
        float t = num / den;
        if (den < 0.0) {
            if (t > tn) {
                tn = t;
                n = pn;
            }
        } else {
            tf = min(tf, t);
        }
    }
    return vec2(tn, tf);
}

/** From a point inside, the distance to the wall the ray leaves through, its outward normal in `n`, its index in `id`. */
float exitCut(vec3 p, vec3 rd, out vec3 n, out float id) {
    float tf = 1e4;
    n = vec3(0.0, 1.0, 0.0);
    id = 0.0;
    for (int i = 0; i < GM_N; i++) {
        vec3 pn = gP[i].xyz;
        float den = dot(rd, pn);
        if (den > 1e-6) {
            float t = (gP[i].w - dot(p, pn)) / den;
            if (t < tf) {
                tf = t;
                n = pn;
                id = float(i);
            }
        }
    }
    return max(tf, 0.0);
}

void main() {
    vec2 uv = view();
    glassSetup();
    float t = glassTime();
    float kPulse = gGlassIn.pulse - 0.5;
    float kTexture = gGlassIn.texture - 0.5;
    float kFlow = gGlassIn.flow - 0.5;
    float kTone = gGlassIn.tone - 0.5;
    float kBuild = gGlassIn.build - 0.5;
    float kSpace = gGlassIn.space - 0.5;
    gGlassFlow = kFlow;
    GlassEar ear = glassListen();
    float hue = 0.05 * sin(t * 0.02) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue) + 0.6 * kTone;
    GlassLook lk = glassLook(GLASS_ENV_FRACTAL, hue, ear);
    lk.glow *= 1.0 + kBuild;
    lk.rough *= 0.5;
    float ior = glassIor(1.95 + 0.3 * clamp(ear.bass, 0.0, 1.0));
    float spread = glassSpread(0.02 + 0.05 * ear.sharp);
    int bounces = 2 + int(floor(glassQuality() * 2.0 + 0.5));

    buildCut();
    float turn = t * 0.18;
    gRot = glassEuler(vec3(0.42 + 0.05 * sin(t * 0.3), turn, 0.08 * sin(t * 0.23)));

    vec3 ro;
    vec3 rd;
    camAround(uv, GM_CAM_R, 0.0, ro, rd);
    // Into the stone's frame (rotation is orthonormal: its transpose undoes it).
    mat3 inv = transpose(gRot);
    vec3 o = inv * ro / GM_SCALE;
    vec3 d = inv * rd;

    vec3 col = glassEnv(rd, lk) * 0.7;
    // The stone's fire thrown into the dark: spectral rays from its centre, turning with it,
    // longer on bright mixes, flickering on the hat.
    float ra = atan(uv.y, uv.x);
    float rl = length(uv);
    // k1 TEXTURE widens the rays (a lower power), k5 SPACE carries them further.
    float wide = 1.0 + kTexture;
    float rays = pow(abs(sin(ra * 7.0 + turn * 2.0)), 30.0 / wide) + 0.6 * pow(abs(sin(ra * 11.0 - turn * 3.0 + 1.0)), 40.0 / wide);
    float reach = (0.6 + 0.9 * ear.sharp) * (1.0 + 0.4 * kSpace);
    vec3 fireCol = glassSpectrum(fract(ra * 1.1 + turn * 0.5 + rl * 0.8));
    col += fireCol * rays * exp(-rl / reach) * smoothstep(0.25, 0.45, rl) * (0.35 + 0.8 * ear.hat) * 1.2;
    vec3 n;
    vec2 hit = enterCut(o, d, n);
    if (hit.x < hit.y && hit.y > 0.0 && hit.x > 0.0) {
        vec3 p = o + d * hit.x;
        n = glassWobble(n, p, glassChaos());
        vec3 tint = mix(vec3(1.0), pal(hue + 0.6), 0.6 * glassComplexity());
        float cosI = max(dot(n, -d), 0.0);
        float fr = glassFresnel(cosI, ior);
        vec3 acc = glassEnv(gRot * reflect(d, n), lk) * fr;
        vec3 thr = vec3(1.0 - fr);
        vec3 dir = glassRefract(d, n, 1.0 / ior);
        vec3 iors = glassIors(ior, spread);
        float facet = 0.0;
        for (int b = 0; b < 4; b++) {
            if (b >= bounces) break;
            vec3 nx;
            float id;
            float tt = exitCut(p, dir, nx, id);
            p += dir * tt;
            thr *= glassBeer(tint, tt, 0.9);
            float c = max(dot(dir, nx), 0.0);
            vec3 out0 = refract(dir, -nx, ior);
            if (dot(out0, out0) > 1e-6) {
                float f = glassFresnel(c, ior);
                vec3 er = glassRefract(dir, -nx, iors.x);
                vec3 eg = out0;
                vec3 eb = glassRefract(dir, -nx, iors.z);
                acc += glassEnv3(gRot * er, gRot * eg, gRot * eb, lk) * thr * (1.0 - f) * 1.8;
                thr *= f;
            }
            if (b == 0) facet = id;
            dir = reflect(dir, nx);
        }
        acc += glassEnv(gRot * dir, lk) * thr * 0.6;
        // Facet sparkle on the hat: each crown facet flares on its own phase.
        float fh = glassHash1(facet + 3.0);
        acc += vec3(1.0) * ear.hat * pow(max(sin(t * 2.0 + fh * 30.0), 0.0), 12.0) * 0.6;
        acc += vec3(1.0) * glassGlint(p, 7.0, ear.hat * 0.7);
        col = acc;
    }

    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col * (1.0 + 0.5 * kPulse))), 1.0);
}
