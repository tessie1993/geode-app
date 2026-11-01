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

// Glass Caustic: hovering under a sunlit sea, looking down and ahead at rippled
// sand where a glass sphere lies half sunk. Across the sand runs the caustic
// net of the waves overhead in three wavelengths (glassCausticRGB): the lines
// crawl, re-knot and split into thin rainbows where they fold. The sphere adds
// its own caustic - a focused, dispersed spot on the sand on the side away
// from the sun - and shows the sea through it upside down, refracted with
// two bounces. Between eye and floor, slanting shafts of light fall from the
// surface through a blue haze that swallows the distance (a few taps along
// the ray, more at higher quality), and specks of marine snow drift past.
// Looking up finds Snell's window in the DEEP world.
//
// The eye holds still and the sand drifts under it with the current; the
// rig's look turns the view, pitched down toward the floor.
//
// ---- audio ------------------------------------------------------------------
//
//   kick         a surge: the swell lifts and the net stretches, eased;
//   centroid     caustic sharpness: bright mixes cut fine bright lines;
//   flatness     turbidity: noise thickens the haze and softens the net;
//   pan          the sun's bearing: the shafts lean and the sphere's spot swings;
//   pitch height the sun's height: shafts steepen as the melody climbs;
//   hat          the marine snow glitters;
//   bass         the sphere's IOR, so its spot tightens on low notes.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the waves and the current. Complexity adds finer sand ripples.
// Chaos stirs a cross-current. Energy scales every reaction.
//
// ---- cost -------------------------------------------------------------------
//
// Two wave sums (floor point, surface window), 4 / 6 / 8 shaft taps, one
// sphere quadratic; on the sphere 5 DEEP lookups. Analytic.

#define CA_EYE 2.4
#define CA_BALL_R 0.7

void main() {
    vec2 uv = view();
    float t = glassTime();
    GlassEar ear = glassListen();
    float hue = 0.05 * sin(t * 0.02) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    GlassLook lk = glassLook(GLASS_ENV_DEEP, hue, ear);
    vec3 key = glassKeyDir(lk);
    vec3 sunDir = normalize(vec3(key.x, max(key.y, 0.4), key.z));

    // The view: the rig's ray, pitched down toward the sand.
    vec3 rd = camRayDir(uv);
    float pitch = 0.62;
    rd.yz = mat2(cos(pitch), sin(pitch), -sin(pitch), cos(pitch)) * rd.yz;
    rd = normalize(rd);
    vec3 ro = vec3(0.0, CA_EYE, 0.0);
    vec2 drift = vec2(0.05, 0.22) * t;

    vec3 water = mix(vec3(0.03, 0.3, 0.42), pal(hue + 0.5), 0.35);
    float haze = 0.12 + 0.12 * ear.frost;
    vec3 col = glassEnv(rd, lk);
    float tHit = 60.0;

    float chop = 0.75 + 0.5 * ear.kick + 0.2 * clamp(ear.bass, 0.0, 1.2);
    float sharp = mix(0.06, 0.014, ear.sharp) * (1.0 + 1.5 * ear.frost);
    vec3 ball = vec3(0.6, 0.15, 3.4);
    float ior = 1.45 + 0.15 * clamp(ear.bass, 0.0, 1.0);

    float tf = glassPlane(ro, rd, vec3(0.0, 1.0, 0.0), 0.0);
    vec2 hb = glassSphere(ro, rd, ball, CA_BALL_R);
    if (tf > 0.0) {
        tHit = tf;
        vec3 fp = ro + rd * tf;
        vec2 sp = fp.xz + drift;
        // Sand: long ripples across the current, finer ones with Complexity, a few dark stones.
        float rip = sin(sp.y * 5.0 + 0.8 * sin(sp.x * 1.3)) * 0.5 + 0.5;
        rip += glassComplexity() * 0.4 * (sin(sp.y * 13.0 + sp.x * 3.0) * 0.5 + 0.5);
        float stone = smoothstep(0.86, 0.9, glassHash(floor(sp * 1.4)) * (1.0 - length(fract(sp * 1.4) - 0.5)));
        vec3 sand = mix(vec3(0.75, 0.68, 0.52), pal(hue + 0.15), 0.2) * (0.55 + 0.25 * rip);
        sand = mix(sand, vec3(0.08, 0.07, 0.07), stone * 0.8);
        // The net: waves at the point above, shifted toward the sun so it leans with the light.
        vec3 hess;
        vec2 cross = glassChaos() * 0.3 * vec2(sin(sp.y + t), cos(sp.x - t));
        glassWaves(fp.xz * 2.2 + sunDir.xz * 1.5 + cross, t * 0.9, chop, hess);
        vec3 caus = glassCausticRGB(hess, 2.4, 0.03 + 0.04 * ear.sharp, sharp);
        // The sphere's shadow and its focused spot, cast away from the sun.
        vec2 fc = ball.xz - sunDir.xz / sunDir.y * ball.y;
        vec2 rel = fp.xz - fc - (-sunDir.xz) * 0.55;
        float r2 = dot(rel, rel);
        float shadow = 1.0 - 0.6 * exp(-dot(fp.xz - fc, fp.xz - fc) * 1.6);
        float k = 14.0 + 30.0 * (ior - 1.45);
        vec3 spot = vec3(exp(-r2 * k * 0.8), exp(-r2 * k), exp(-r2 * k * 1.25)) * 2.2;
        vec3 sun = mix(vec3(1.0, 0.95, 0.8), water * 2.0, 0.25);
        vec3 fl = sand * (0.45 + 1.8 * caus * sun * shadow + spot * sun);
        col = fl;
    }
    if (hb.x > 0.0 && hb.x < tHit) {
        tHit = hb.x;
        vec3 p = ro + rd * hb.x;
        vec3 n = glassWobble(normalize(p - ball), p, glassChaos());
        float cosI = max(dot(n, -rd), 0.0);
        float fr = glassFresnel(cosI, ior);
        vec3 tint = mix(vec3(1.0), water * 2.0, 0.3);
        vec3 through = glassSphereTrace(p, n, rd, ball, CA_BALL_R, ior, 0.02 + 0.03 * ear.sharp, tint, 0.4, glassBounces(), lk);
        vec3 rr = reflect(rd, n);
        col = through * (1.0 - fr) * 1.3 + glassEnv(rr, lk) * fr;
        col += vec3(1.0, 0.97, 0.9) * 3.0 * pow(max(dot(rr, sunDir), 0.0), 180.0);
        col += mix(water, vec3(1.0), 0.5) * pow(1.0 - cosI, 3.0) * 0.4;
    }
    // The water between: absorption, then the shafts.
    float fog = 1.0 - exp(-tHit * haze);
    col = mix(col, water * 0.12, fog);
    float q = glassQuality();
    int taps = q < 0.3 ? 4 : (q < 0.6 ? 6 : 8);
    float shafts = 0.0;
    float span = min(tHit, 12.0);
    float j = glassHash(uv * 400.0);
    for (int i = 0; i < 8; i++) {
        if (i >= taps) break;
        vec3 x = ro + rd * span * ((float(i) + j) / float(taps));
        vec2 s = x.xz - sunDir.xz / sunDir.y * (6.0 - x.y);
        float sh = glassNoise(s * 1.3 + t * 0.15) * 0.5 + 0.5;
        shafts += pow(sh, 4.0) * exp(-span * float(i) / float(taps) * 0.15);
    }
    col += mix(water, vec3(1.0), 0.4) * shafts / float(taps) * 0.9 * lk.glow;
    // Marine snow: specks in three depth layers on the screen, glittering on the hat.
    for (int l = 0; l < 3; l++) {
        float sc = 9.0 + 7.0 * float(l);
        vec2 g = uv * sc + vec2(0.0, t * (0.15 + 0.05 * float(l))) + float(l) * 7.0;
        vec2 id = floor(g);
        float h = glassHash(id);
        if (h > 0.93) {
            vec2 o = glassHash2(id) - 0.5;
            float d = length(fract(g) - 0.5 - o * 0.6);
            col += vec3(0.85, 0.95, 1.0) * exp(-d * d * 160.0) * (0.15 + 0.9 * ear.hat * step(0.6, fract(h * 17.0 + t))) * 0.6;
        }
    }

    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col)), 1.0);
}
