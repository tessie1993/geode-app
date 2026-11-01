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

// Glass Bubbles: soap bubbles drifting up through a dark room strung with neon,
// so every bubble carries the neon's coloured arcs wrapped round its surface,
// banded by interference. A bubble is a film a few hundred nanometres thick: it
// barely bends light, it colours it. Each bubble is an analytic sphere hit at
// both walls. The near wall reflects the room through glassFilm's two-beam
// interference; the far wall, seen from inside, adds a second, mirrored set.
// The film is not uniform: water drains down, so it is thick and banded at the
// foot, thins toward the crown and goes black (the patch just before a bubble
// pops) at the very top; and it swirls, slow marbled currents carried round the
// surface. They rise in a slow vortex round the room's axis, the inner ones
// circling faster. Bubbles composite as transmission and added reflection, which is
// order free, so no sort.
//
// ---- audio ------------------------------------------------------------------
//
//   pitch height     the film's thickness, 280 -> 720 nm: the band colours roll
//                    from gold-magenta through to green-blue as the melody climbs;
//   snare            a shiver runs over every bubble: a travelling ripple on the
//                    surface normals, so the reflected neon wobbles;
//   bass             the bubbles breathe (radius);
//   hat              glints on the films;
//   pan, width       the neon ring's yaw and how far apart its bars stand;
//   flatness         a frost on the reflections (the films go milky in noise).
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the rise and swirl. Complexity adds up to five more bubbles.
// Chaos wobbles the surfaces (glassWobble). Energy scales every reaction.
//
// ---- cost -------------------------------------------------------------------
//
// Per bubble a quadratic and, on its pixels, two world lookups and two film
// evaluations. Up to eleven bubbles; a pixel usually meets one or two.

#define BB_CAM_R 5.0
#define BB_MAX 11

void main() {
    vec2 uv = view();
    float t = glassTime();
    GlassEar ear = glassListen();
    float hue = 0.05 * sin(t * 0.017) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    GlassLook lk = glassLook(GLASS_ENV_NEON, hue, ear);
    float baseNm = 280.0 + 440.0 * ear.height;
    int count = 6 + int(floor(glassComplexity() * 5.0 + 0.5));

    vec3 ro;
    vec3 rd;
    camAround(uv, BB_CAM_R, 0.0, ro, rd);
    vec3 col = glassEnv(rd, lk);

    for (int i = 0; i < BB_MAX; i++) {
        if (i >= count) break;
        float fi = float(i);
        float h1 = glassHash1(fi + 0.3);
        float h2 = glassHash1(fi + 7.1);
        float h3 = glassHash1(fi + 13.9);
        // Rise on a 9-unit loop, swaying; size grows a little as it rises and fades at both ends.
        float life = fract(t * (0.035 + 0.025 * h2) + h1);
        float y = -3.2 + 6.4 * life;
        // A slow vortex: each bubble circles the axis as it rises, faster near the middle.
        float orbitR = 0.8 + 2.0 * h1;
        float ang = h2 * GLASS_TAU + t * 0.35 / orbitR + 0.1 * SND_CURVED_TIME;
        vec3 c = vec3(cos(ang) * orbitR * 1.3, y, sin(ang) * orbitR * 0.8);
        float r = (0.42 + 0.38 * h3) * (0.9 + 0.1 * clamp(ear.bass, 0.0, 1.2)) * smoothstep(0.0, 0.08, life) * (1.0 - smoothstep(0.9, 1.0, life));
        if (r < 0.02) continue;
        vec2 hs = glassSphere(ro, rd, c, r);
        if (hs.x <= 0.0) continue;

        for (int w = 0; w < 2; w++) {
            float tw = w == 0 ? hs.x : hs.y;
            vec3 p = ro + rd * tw;
            vec3 local = (p - c) / r;
            vec3 n = w == 0 ? local : -local;
            // The shiver: a ripple running down the bubble on the snare.
            float rip = sin(local.y * 14.0 - t * 9.0 + fi) * ear.snare * 0.12;
            n = normalize(n + rip * vec3(local.x, 0.0, local.z));
            n = glassWobble(n, local * 2.0, glassChaos());
            float cosI = abs(dot(n, rd));
            // Thickness: drained toward the top, marbled by a slow swirl round the vertical.
            float ang = atan(local.z, local.x);
            float swirl = glassNoise(vec2(ang * 1.6 + t * 0.25 + local.y * 2.0, local.y * 3.0 - t * 0.15 + fi * 4.0));
            float drain = 0.25 + 0.75 * smoothstep(-1.0, 0.85, -local.y);
            float nm = baseNm * drain * (1.0 + 0.35 * swirl);
            nm *= 1.0 - smoothstep(0.7, 0.96, local.y);
            vec3 film = glassFilm(nm, cosI, 1.33);
            float fr = mix(0.16, 1.0, pow(1.0 - cosI, 3.0));
            vec3 refl = glassEnvRough(reflect(rd, n), lk, ear.frost * 0.05) + pal(hue + 0.3 + 0.2 * n.y) * 0.1;
            float k = w == 0 ? 1.0 : 0.6;
            col = col * (1.0 - fr * 0.3 * k) + refl * film * fr * 2.6 * k;
            if (w == 0) col += vec3(1.0) * glassGlint(local * 1.4 + fi, 4.0, ear.hat) * 0.6;
        }
    }

    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col)), 1.0);
}
