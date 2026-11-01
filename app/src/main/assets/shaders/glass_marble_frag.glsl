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

// Glass Marble: a big cat's-eye marble hanging against a sunset sea, close
// enough to fill most of the frame. Clear glass round three twisted coloured
// vanes that meet at its axis. The glass is the family's two-bounce sphere
// (glassSphereTrace): the sunset shows through upside down with dispersed
// edges and a second, fainter image from the far wall's reflection. The vanes
// are a true volume: the refracted ray is sampled along its chord, each sample
// testing three planes that twist round the axis, and composited front to back
// with a lit, translucent colour, so the vanes overlap, glow where the sun is
// behind them and show through each other. A drift of tiny air bubbles trapped
// in the glass catches the light. At the heart, between the vanes, a small
// nebula swirls: a warped glow that breathes with the bass.
//
// ---- audio ------------------------------------------------------------------
//
//   snare        twists the vanes: each hit winds them a little further, and
//                they ease back;
//   bass         swells the vanes (wider, more colour in the glass);
//   onsets       roll the marble one notch each (glassGlide);
//   flatness     fills the glass with seed bubbles (frost): noise clouds it;
//   centroid     the dispersion at the rim;
//   pitch, pan   the sun's height and bearing (glassLook), so the marble's
//                highlight and the vanes' backlight follow the melody;
//   hat          glints on the surface.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the turn. Complexity adds vanes (three to five). Chaos wobbles
// the surface. Energy scales every reaction.
//
// ---- cost -------------------------------------------------------------------
//
// 5 / 8 / 12 volume samples (Saver / Balanced / High), each three plane
// distances; 6 world lookups on the marble's pixels. Analytic, no march.

#define MB_CAM_R 4.3
#define MB_R 1.45

void main() {
    vec2 uv = view();
    float t = glassTime();
    GlassEar ear = glassListen();
    float hue = 0.04 * sin(t * 0.019) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    GlassLook lk = glassLook(GLASS_ENV_FRACTAL, hue, ear);
    float ior = 1.5;
    float spread = 0.015 + 0.035 * ear.sharp;
    int bounces = glassBounces();
    float vanes = 3.0 + floor(glassComplexity() * 2.0 + 0.5);
    float twist = 1.4 + 1.6 * ear.snare + 0.3 * sin(t * 0.2);
    float roll = t * 0.15 + 0.35 * glassGlide(SND_ONSET_COUNT, SND_ONSET_ENV) * min(glassEnergy(), 1.5);
    mat3 rot = glassEuler(vec3(0.35 * sin(t * 0.11), roll, 0.5 + 0.2 * sin(t * 0.07)));
    mat3 inv = transpose(rot);

    vec3 ro;
    vec3 rd;
    camAround(uv, MB_CAM_R, 0.0, ro, rd);
    vec3 c = vec3(0.0, 0.05 * sin(t * 0.3), 0.0);
    vec3 col = glassEnv(rd, lk);

    vec2 hs = glassSphere(ro, rd, c, MB_R);
    if (hs.x > 0.0) {
        vec3 p = ro + rd * hs.x;
        vec3 n = glassWobble(normalize(p - c), p, glassChaos());
        float cosI = max(dot(n, -rd), 0.0);
        float fr = glassFresnel(cosI, ior);
        vec3 key = glassKeyDir(lk);

        // The volume: march the refracted chord through the vanes.
        vec3 d1 = glassRefract(rd, n, 1.0 / ior);
        vec3 oc = p - c;
        float b = dot(oc, d1);
        float chord = max(-b + sqrt(max(b * b - (dot(oc, oc) - MB_R * MB_R), 0.0)), 0.0);
        float q = glassQuality();
        int samples = q < 0.3 ? 6 : (q < 0.6 ? 9 : 14);
        float dt = chord / float(samples);
        vec3 acc = vec3(0.0);
        float trans = 1.0;
        float prev = 0.0;
        vec3 prevS = vec3(0.0);
        for (int i = 0; i <= 14; i++) {
            if (i > samples) break;
            vec3 s = inv * (oc + d1 * (dt * float(i))) / MB_R;
            float th = atan(s.z, s.x) + s.y * twist;
            float g = sin(vanes * th) * length(s.xz);
            if (i > 0 && g * prev < 0.0) {
                // A vane crossed between the two samples: interpolate to it and shade it as a sheet.
                vec3 x = mix(prevS, s, prev / (prev - g));
                float rr = length(x.xz);
                float r3 = length(x);
                float edge = 1.0 - smoothstep(0.5, 0.62 + 0.08 * clamp(ear.bass, 0.0, 1.2), r3);
                if (edge > 0.0) {
                    float tx = atan(x.z, x.x) + x.y * twist;
                    float id = floor(mod(tx * vanes / GLASS_PI + 0.5, 2.0 * vanes));
                    vec3 sn = rot * vec3(-sin(tx), 0.0, cos(tx));
                    float cs = abs(dot(sn, d1));
                    vec3 vc = pal(hue + 0.18 + 0.11 * mod(id, vanes));
                    float back = 0.3 + 0.9 * pow(max(dot(d1, key), 0.0), 2.0);
                    float lit = 0.35 + 0.65 * abs(dot(sn, key));
                    float a = edge * (1.0 - exp(-0.35 / max(cs, 0.05))) * smoothstep(0.0, 0.05, rr);
                    acc += trans * a * vc * (lit + back) * 0.9;
                    trans *= 1.0 - a;
                }
            }
            // The nebula core: a slow, warped glow at the marble's heart, between the vanes.
            float r3c = length(s);
            if (r3c < 0.5) {
                vec3 w = s * 4.0;
                w += 0.6 * sin(w.zxy * 1.9 + lk.flow * 2.0);
                w += 0.4 * sin(w.yzx * 3.1 - lk.flow * 1.6);
                float neb = pow(0.5 + 0.5 * sin(w.x + w.y + w.z), 3.0) * (1.0 - smoothstep(0.1, 0.5, r3c));
                vec3 nc = pal(hue + 0.6 + 0.2 * w.y);
                acc += trans * nc * neb * dt / MB_R * 2.2 * (0.6 + 0.8 * clamp(ear.bass, 0.0, 1.2));
            }
            prev = g;
            prevS = s;
        }
        // Seed bubbles: tiny bright specks in the glass, more in noisy music.
        vec3 sp = inv * (oc + d1 * chord * 0.35) / MB_R * 9.0;
        float bub = glassHash3(floor(sp));
        float speck = step(0.975 - 0.06 * ear.frost, bub) * exp(-dot(fract(sp) - 0.5, fract(sp) - 0.5) * 40.0);
        acc += trans * vec3(1.0, 0.95, 0.9) * speck * 0.8;

        vec3 tint = mix(vec3(1.0), pal(hue + 0.55), 0.15);
        vec3 through = glassSphereTrace(p, n, rd, c, MB_R, ior, spread, tint, 0.25, bounces, lk);
        vec3 inside = through * trans + acc;
        vec3 refl = glassEnv(reflect(rd, n), lk);
        col = inside * (1.0 - fr) + refl * fr;
        col += glassRim(cosI, hue + 0.2, 0.5);
        col += vec3(1.0) * glassGlint(inv * (p - c), 6.0, ear.hat) * (0.4 + fr);
    }

    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col)), 1.0);
}
