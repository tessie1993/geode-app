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

// Chrysanthemum Phyllo: a composite flower head seen face on - a disk of
// golden-angle florets (a Vogel spiral, so the 21/34/55 parastichies appear
// on their own) ringed by 34 ray petals. Each floret is a small lit dome; the
// disk darkens toward its centre where the young florets are still closed.
// chrysFloret finds the nearest floret in four tries, so the disk holds ~700
// florets for the price of one.
//
// ---- audio ------------------------------------------------------------------
//
// Every onset SEEDS a new constellation: SND_ONSET_COUNT re-hashes which florets
// are "awake" (about one in nine), and those wear the contrasting hue until
// the next onset picks another set. The onset envelope (SND_ONSET_ENV) swells the
// awake florets' domes for its 0.22 s, so each hit reads as a scatter of
// florets popping up across the disk. A slow hue wave runs outward along the
// floret index on uFlowPhase (louder passages run it faster, never backwards).
// The sub band (sndSpectrum(0.10)) loosens the spiral's spacing a few percent, and
// uBreath sways the ray petals. Brightness follows SND_ENERGY_TREND only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the sway and hue clocks. Complexity: uMorph packs the
// disk tighter, up to twice the florets (~1440) in the same disk. Chaos:
// uTurbulence + uWarp fray the ray petals and vary their lengths; uTwist
// sweeps them into a pinwheel. Energy: uIntensity scales the onset pop and
// the sub-band loosening. Colour: pal() throughout. Space: uZoom via view().

#define PH_DISK 0.62

void main() {
    vec2 uv = view();
    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float t = uTime * uSpeed;
    float onset = clamp(SND_ONSET_ENV * uIntensity, 0.0, 1.5);
    float sub = clamp(sndSpectrum(0.10) * uIntensity, 0.0, 1.5);
    float chaos = clamp(uTurbulence + uWarp, 0.0, 2.0);
    float florets = 720.0 * (1.0 + clamp(uMorph, 0.0, 1.0));
    float aa = 3.0 / uResolution.y;
    float r = length(uv);
    float hue = 0.08 + 0.02 * sin(t * 0.05);

    vec3 col = mix(pal(hue + 0.62) * 0.03, pal(hue + 0.5) * 0.08, exp(-r * 1.2));

    // Ray petals: one ring of 34, a second of 21 behind it, swaying on uBreath.
    vec2 pr = chrysTurn(t * 0.01 + 0.06 * (uBreath - 1.0)) * uv;
    for (int i = 0; i < 2; i++) {
        float k = float(i);
        float count = k < 0.5 ? 21.0 : 34.0;
        float len = k < 0.5 ? 0.42 : 0.36;
        vec2 q = chrysTurn(chrysRingOffset(k, count)) * pr;
        float pid = floor((atan(q.y, q.x) - uTwist * (length(q) - PH_DISK * 0.92)) / (CHRYS_TAU / count) + 0.5);
        len *= 1.0 + 0.18 * chaos * chrysWobble(mod(pid, count) + 5.0 * k, t);
        vec4 pet = chrysPetal(q, count, PH_DISK * 0.92, len, 0.5, uTwist, 0.15, 0.35);
        pet.x += chrysFray(q, chaos);
        col *= 1.0 - 0.5 * smoothstep(0.04, -0.01, pet.x);
        float m = smoothstep(aa, -aa, pet.x);
        float wob = chrysHash1(mod(pet.w, count) + 7.0 * k);
        vec3 pc = mix(pal(hue + 0.03 * wob) * 0.7, pal(hue + 0.06) * 1.05, pet.y) * chrysPetalShade(pet, 0.2);
        pc *= 0.75 + 0.25 * wob;
        col = mix(col, pc, m);
    }

    // The disk.
    float spacing = PH_DISK / sqrt(florets) * (1.0 + 0.06 * sub);
    if (r < PH_DISK + 0.02) {
        vec4 fl = chrysFloret(uv, spacing);
        float k = fl.y;
        float age = clamp(k / florets, 0.0, 1.0);          // 0 centre .. 1 rim
        float awake = step(0.89, chrysHash(vec2(k, floor(SND_ONSET_COUNT) * 1.618)));
        float rad = spacing * (0.62 + 0.28 * age) * (1.0 + 0.55 * onset * awake);
        float dome = clamp(1.0 - fl.x / rad, 0.0, 1.0);
        // Lit from the upper left: a dome's highlight sits off centre.
        vec2 n2 = fl.zw / rad;
        float lit = clamp(0.55 + 0.6 * dot(n2, vec2(-0.6, 0.7)) + 0.4 * sqrt(dome), 0.0, 1.3);
        float wave = 0.06 * sin(k * 0.035 * 720.0 / florets - uFlowPhase * 0.8);
        vec3 fc = mix(pal(hue + 0.92 + wave) * 0.25, pal(hue + 0.02 + wave) * 0.9, smoothstep(0.0, 0.8, age));
        fc = mix(fc, pal(hue + 0.45 + wave) * 0.95, awake);
        float m = smoothstep(0.0, 0.25, dome);
        vec3 gap = pal(hue + 0.9) * 0.04;
        vec3 disk = mix(gap, fc * lit, m);
        float edge = smoothstep(PH_DISK + aa, PH_DISK - aa, r);
        col = mix(col, disk, edge);
    }
    // A soft shadow ring where the disk meets the petals.
    col *= 1.0 - 0.35 * exp(-abs(r - PH_DISK) * 40.0);

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
