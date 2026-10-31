#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_palette
//#include lib_scene_grade
//#include lib_chrys

// Chrysanthemum Lotus: a lotus seen from the side at dusk, standing on still
// water. Depth comes from layering, not from a camera: a wide back fan of
// dark outer petals, a middle fan, an upright inner cup, a bud ring in the
// heart, and in front of everything a pair of petals lying out across the
// water toward the viewer. Each fan is a polar repetition about the flower's
// base with the TWO nearest petals drawn (outer first), so neighbours overlap
// and shadow each other the way real petals do. Below the waterline the upper
// layers are drawn again, mirrored and rippled, as the reflection.
//
// ---- audio ------------------------------------------------------------------
//
// The snare (SND_SNARE_ENV) LIFTS the front: the petals lying on the water rise
// toward upright and the bud ring in the heart stands up out of the inner
// cup, so each snare reads as the flower drawing itself up. The bass band
// (sndLevel(SND_BASS_ATT)) sets how hard the water ripples (the reflection's distortion,
// a geometry). uBarOsc sways the stem a few degrees, phase-locked to the bar,
// so the flower rocks in time. Brightness follows SND_LEVEL only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the ripple, sky and hue clocks. Complexity: uMorph
// adds a wider fan of eleven petals behind the flower, then a second bud
// ring. Chaos: uTurbulence + uWarp tilt each petal off its slot and fray the
// edges; uTwist bends every petal sideways along its length. Energy:
// uIntensity scales the snare lift and the bass ripple. Colour: pal() for
// the petal flush, sky and water; the cream base is a fixed accent. Space:
// uZoom via view().

#define LO_BASE vec2(0.0, -0.3)
#define LO_WATER -0.36

float gHue;
float gT;
float gChaos;

void lotusLayer(vec2 p, float count, float spread, float len, float width, float dim, float hueOff, inout vec3 col) {
    float aa = 3.0 / uResolution.y;
    vec2 q = p - LO_BASE;
    float phi = atan(q.x, q.y);
    float h = 0.5 * (count - 1.0);
    float stepA = spread / max(h, 0.5);
    float f = phi / stepA;
    float j = clamp(floor(f + 0.5), -h, h);
    float j2 = clamp(j + (f > j ? 1.0 : -1.0), -h, h);
    float ja = abs(j) > abs(j2) ? j : j2;
    float jb = abs(j) > abs(j2) ? j2 : j;
    for (int s = 0; s < 2; s++) {
        float jj = s == 0 ? ja : jb;
        float a = jj * stepA + 0.12 * gChaos * chrysWobble(jj + count * 3.0, gT);
        float lj = len * (1.0 - 0.16 * abs(a));
        vec2 dir = vec2(sin(a), cos(a));
        vec2 lq = vec2(dot(q, vec2(dir.y, -dir.x)), dot(q, dir));
        lq.x -= uTwist * lq.y * lq.y;
        vec4 pet = chrysLeaf(lq, lj, width, 0.3, 0.95);
        pet.x += chrysFray(lq, gChaos);
        col *= 1.0 - 0.4 * smoothstep(0.04, -0.01, pet.x);
        float m = smoothstep(aa, -aa, pet.x);
        float v = clamp(pet.z, -1.0, 1.0);
        float u = clamp(pet.y, 0.0, 1.0);
        // Cream at the base flushing to the palette at the tip, with fine
        // lengthwise veins and a crease down the middle.
        vec3 cream = vec3(0.95, 0.9, 0.86);
        vec3 pc = mix(cream, pal(gHue + hueOff), smoothstep(0.25, 1.0, u));
        float veins = 0.92 + 0.08 * cos(v * 14.0);
        float crease = 1.0 - 0.25 * exp(-abs(v) * 10.0);
        float rim = 0.8 + 0.35 * v * v;
        pc *= veins * crease * rim * dim * mix(0.6, 1.0, u);
        col = mix(col, pc, m);
    }
}

vec3 lotusUpper(vec2 p, float lift, float swell) {
    vec3 sky = mix(pal(gHue + 0.55) * 0.05, pal(gHue + 0.08) * 0.2, smoothstep(-0.4, 1.1, p.y));
    sky += pal(gHue + 0.12) * 0.14 * chrysCore(length(p - vec2(0.0, 0.1)), 0.55) * (0.8 + 0.2 * swell);
    vec3 col = sky;
    float detail = clamp(uMorph, 0.0, 1.0);
    if (detail > 0.01) lotusLayer(p, 11.0, 1.55, 0.66 * smoothstep(0.0, 0.5, detail), 0.48, 0.42, 0.04, col);
    lotusLayer(p, 9.0, 1.3, 0.62, 0.5, 0.55, 0.02, col);
    lotusLayer(p, 7.0, 0.85, 0.56, 0.52, 0.75, 0.0, col);
    lotusLayer(p, 5.0, 0.42, 0.5, 0.55, 0.95, -0.03, col);
    // The bud ring: barely there at rest, standing up on a snare.
    lotusLayer(p, 3.0, 0.22, 0.42 * (0.25 + 0.75 * lift), 0.6, 1.05, -0.06, col);
    if (detail > 0.5) lotusLayer(p, 3.0, 0.5, 0.3 * smoothstep(0.5, 1.0, detail) * (0.3 + 0.7 * lift), 0.65, 1.1, -0.09, col);
    return col;
}

void main() {
    vec2 uv = view();
    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float lift = clamp(SND_SNARE_ENV * uIntensity, 0.0, 1.2);
    float bass = clamp(sndLevel(SND_BASS_ATT) * uIntensity, 0.0, 1.5);
    float aa = 3.0 / uResolution.y;
    gT = uTime * uSpeed;
    gChaos = clamp(uTurbulence + uWarp, 0.0, 2.0);
    gHue = 0.95 + 0.02 * sin(gT * 0.03);

    // The whole flower rocks about its base with the bar.
    float sway = 0.05 * (uBarOsc - 0.5);
    vec2 p = LO_BASE + chrysTurn(sway) * (uv - LO_BASE);

    vec3 col;
    if (uv.y > LO_WATER) {
        col = lotusUpper(p, lift, swell);
    } else {
        // Reflection: mirror about the waterline, rippled by rings spreading
        // from the stem, squashed by the low viewing angle.
        vec2 q = uv;
        float ringR = length((q - vec2(0.0, LO_WATER)) * vec2(1.0, 4.0));
        float amp = 0.004 + 0.014 * bass;
        q.x += amp * sin(ringR * 38.0 - gT * 2.2);
        q.y = 2.0 * LO_WATER - q.y + amp * 0.6 * cos(ringR * 31.0 - gT * 1.7);
        vec2 rp = LO_BASE + chrysTurn(sway) * (q - LO_BASE);
        col = lotusUpper(rp, lift, swell) * vec3(0.45, 0.55, 0.65);
        float depth = clamp((LO_WATER - uv.y) * 1.6, 0.0, 1.0);
        col = mix(col, pal(gHue + 0.6) * 0.03, depth * 0.6);
        // Glints on the ripple crests.
        col += pal(gHue + 0.1) * 0.05 * pow(max(sin(ringR * 38.0 - gT * 2.2), 0.0), 12.0) * (1.0 - depth);
        col *= 1.0 - 0.5 * smoothstep(aa * 2.0, 0.0, abs(uv.y - LO_WATER));
    }

    // The front petals lie out over the water toward the viewer; a snare
    // swings them up toward upright. Drawn last, over the reflection too.
    vec2 fp = vec2(abs(p.x), p.y);
    float lean = 2.25 - 0.75 * lift;
    for (int i = 0; i < 2; i++) {
        float a = lean - 0.42 * float(i);
        vec2 q = fp - LO_BASE;
        vec2 dir = vec2(sin(a), cos(a));
        vec2 lq = vec2(dot(q, vec2(dir.y, -dir.x)), dot(q, dir));
        // Foreshortened: lying flat they point at the camera, so they read shorter.
        float len = 0.5 * mix(0.55, 0.9, (2.25 - a) / 1.2);
        lq.x -= uTwist * lq.y * lq.y;
        vec4 pet = chrysLeaf(lq, len, 0.55, 0.3, 0.95);
        pet.x += chrysFray(lq, gChaos);
        col *= 1.0 - 0.45 * smoothstep(0.05, -0.01, pet.x);
        float m = smoothstep(aa, -aa, pet.x);
        float u = clamp(pet.y, 0.0, 1.0);
        float v = clamp(pet.z, -1.0, 1.0);
        vec3 pc = mix(vec3(0.95, 0.9, 0.86), pal(gHue + 0.03), smoothstep(0.3, 1.0, u));
        pc *= (0.92 + 0.08 * cos(v * 14.0)) * (0.75 + 0.3 * v * v) * mix(0.55, 1.05, u);
        col = mix(col, pc, m);
    }

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
