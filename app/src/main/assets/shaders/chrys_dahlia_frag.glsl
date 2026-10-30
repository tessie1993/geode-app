#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_chrys

// Chrysanthemum Dahlia: a spiral cactus-dahlia filling the frame. Eight rings
// of quilled petals, spaced geometrically so each ring is 1.36 times the one
// inside it, every petal swept along a spiral so the whole head reads as arms
// winding into the centre. The petals are rolled tubes, not flat blades: each
// is shaded as a cylinder across its width (a light from the upper left, a
// specular streak along the roll), so the flower has relief without a march.
// Two-tone: a deep base climbing to a pale, almost white tip.
//
// ---- audio ------------------------------------------------------------------
//
// The mid band CURLS the flower: uMidSmooth (slew-limited) blended with the
// low-mid/mid regions sets how far each petal sweeps around its spiral, so a
// vocal or a lead line visibly winds the arms tighter and a sparse passage
// lets them straighten into a starburst. Inner rings curl more than outer
// ones, so the winding has a centre. The head turns on uDrift. Treble
// (uTrebleSmooth) moves the specular streak along the roll (a geometry of the
// light, not its level). Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the turn and hue clocks. Complexity: uMorph adds up to
// three more, smaller rings inside the head (11 at most), denser at the
// heart. Chaos: uTurbulence + uWarp vary petal lengths and fray the quills;
// uTwist adds to the spiral curl. Energy: uIntensity scales how far the mid
// band winds the arms. Colour: pal() throughout. Space: uZoom via view().

#define DH_MAX_RINGS 11

void main() {
    vec2 uv = view();
    float swell = clamp(uSwell, 0.0, 1.5);
    float t = uTime * uSpeed;
    float mid = clamp((0.6 * uMidSmooth + 0.4 * mix(uBandsLo.z, uBandsLo.w, 0.6)) * uIntensity, 0.0, 1.6);
    float chaos = clamp(uTurbulence + uWarp, 0.0, 2.0);
    float extra = floor(3.0 * clamp(uMorph, 0.0, 1.0) + 0.5);
    float rings = 8.0 + extra;
    float aa = 3.0 / uResolution.y;
    float r = length(uv);
    float hue = 0.92 + 0.03 * sin(t * 0.04);
    vec2 lightDir = normalize(vec2(-0.6, 0.8));

    vec3 col = pal(hue + 0.4) * 0.025 * (1.0 + exp(-r));
    vec2 p0 = chrysTurn(uDrift * 0.25 + t * 0.015) * uv;

    for (int i = DH_MAX_RINGS - 1; i >= 0; i--) {
        float k = float(i);
        if (k >= rings) continue;
        // Ring index counted from the outside, so extra rings go in at the heart.
        float kk = k - extra;
        float f = clamp(kk / 7.0, 0.0, 1.0);
        float r0 = 0.045 * pow(1.36, kk);
        float len = r0 * 1.15 + 0.04;
        float count = max(6.0 + 3.0 * kk, 4.0);
        float curl = (0.6 + 2.6 * mid) * mix(2.4, 0.7, f) / (0.4 + r0 * 3.0) + uTwist;
        vec2 p = chrysTurn(kk * CHRYS_GOLDEN) * p0;
        float pid = floor((atan(p.y, p.x) - curl * (length(p) - r0)) / (CHRYS_TAU / count) + 0.5);
        float lenJ = len * (1.0 + 0.16 * chaos * chrysWobble(mod(pid, count) + 3.0 * k, t));
        vec4 pet = chrysPetal(p, count, r0, lenJ, 0.42, curl, 0.7, 0.85);
        pet.x += chrysFray(p, chaos);
        col *= 1.0 - 0.6 * smoothstep(0.06, -0.01, pet.x);
        float m = smoothstep(aa, -aa, pet.x);
        // Cylinder shading across the roll: v is the position across, so the
        // normal tilts sideways by v and the light is the petal's own frame
        // turned back into screen space.
        float v = clamp(pet.z, -1.0, 1.0);
        float ang = atan(p.y, p.x);
        vec2 across = vec2(-sin(ang), cos(ang));
        vec3 n = normalize(vec3(across * v, sqrt(max(1.0 - v * v, 0.0)) + 0.2));
        float diff = clamp(dot(n, normalize(vec3(lightDir, 0.9))), 0.0, 1.0);
        float streakAt = 0.35 - 0.5 * clamp(uTrebleSmooth, 0.0, 1.0);
        float streak = exp(-pow((v - streakAt) * 4.0, 2.0)) * smoothstep(0.1, 0.8, pet.y);
        vec3 base = pal(hue + 0.05 * f) * 0.35;
        vec3 tipc = mix(pal(hue + 0.08 + 0.05 * f), vec3(1.0, 0.95, 0.92), 0.45);
        vec3 pc = mix(base, tipc, smoothstep(0.05, 1.0, pet.y)) * (0.35 + 0.75 * diff);
        pc += vec3(0.9, 0.9, 1.0) * streak * 0.16;
        pc *= smoothstep(-0.05, 0.3, pet.y) * 0.5 + 0.5;
        col = mix(col, pc, m);
    }
    // A dark, tight heart: the youngest petals are still curled shut.
    col *= smoothstep(0.0, 0.07, r) * 0.7 + 0.3;
    col += pal(hue + 0.5) * 0.05 * chrysCore(r, 0.06);

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
