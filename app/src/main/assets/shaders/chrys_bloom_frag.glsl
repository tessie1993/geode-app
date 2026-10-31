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

// Chrysanthemum Bloom: the family's front door. One flat flower seen from
// above, five rings of petals stacked on a glowing core, each ring turned
// into the gaps of the one beneath it and each casting a soft shadow on it,
// so a flat drawing reads as a flower with depth. Outer rings are many and
// slender, inner rings few and cupped, the way a real incurved bloom tightens
// toward its heart. Cheap on purpose: five petal evaluations per pixel, no
// noise, no march - this is the free style and it has to run everywhere.
//
// ---- audio ------------------------------------------------------------------
//
// Kick (SND_KICK_ENV) OPENS the flower: every ring is pushed outward, the inner
// rings by a higher power of the envelope so they lag the outer ones and the
// bloom ripples out from the centre instead of jumping as a block. The rings
// counter-rotate slowly on uDrift (an integrated angle, so it never reverses
// with a hit). (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) sets how open the flower sits between kicks and lifts
// the core a little. Hue drifts on time alone. Nothing here brightens on a
// hit: the kick moves petals, the swell is the only light.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the spin and hue clock. Complexity: uMorph adds up to
// two more rings (7 at most). Chaos: uTurbulence + uWarp give each petal its
// own length and a frayed edge; uTwist curls the rings into a spiral.
// Energy: uIntensity scales the kick push. Colour: pal() throughout. Space:
// uZoom through view(). (view() also applies uMorph, uTurbulence, uWarp and
// uTwist to the whole frame; the terms here are the flower's own.)

#define BLOOM_MAX_RINGS 7

void main() {
    vec2 uv = view();
    float t = uTime * uSpeed;
    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV * uIntensity, 0.0, 1.5);
    float chaos = clamp(uTurbulence + uWarp, 0.0, 2.0);
    float rings = 5.0 + floor(2.0 * clamp(uMorph, 0.0, 1.0) + 0.5);
    float aa = 3.0 / uResolution.y;

    float r = length(uv);
    float hue = t * 0.006;
    float rest = 0.78 + 0.12 * swell;

    // Background: a dark wash tinted off the palette, a faint halo the flower sits in.
    vec3 col = pal(hue + 0.55) * (0.035 + 0.05 * exp(-r * 1.6));

    // Outer ring first, so each inner ring is painted over (and shadows) the one below.
    for (int i = BLOOM_MAX_RINGS - 1; i >= 0; i--) {
        float k = float(i);
        if (k >= rings) continue;
        float f = k / (rings - 1.0);                      // 0 inner .. 1 outer
        float count = 7.0 + 4.0 * k * 4.0 / (rings - 1.0);
        count = floor(count);
        float push = 0.09 * pow(min(kick, 1.0), 1.0 + 1.5 * (1.0 - f)) * max(kick, 1.0);
        float tipR = (0.22 + 0.72 * f) * (rest + push);
        float len = tipR * (0.62 - 0.12 * f);
        float dir = mod(k, 2.0) < 0.5 ? 1.0 : -1.0;
        float spin = dir * (uDrift * 0.15 + t * 0.02) + chrysRingOffset(k, count);
        vec2 p = chrysTurn(spin) * uv;
        float cup = mix(0.85, 0.25, f);
        // Chaos: a per-petal length, from the petal's id.
        float sector = CHRYS_TAU / count;
        float id = floor((atan(p.y, p.x) - uTwist * (length(p) - tipR + len)) / sector + 0.5);
        float lenJ = len * (1.0 + 0.14 * chaos * chrysWobble(mod(id, count) + 17.0 * k, t));
        vec4 pet = chrysPetal(p, count, tipR - len, lenJ, mix(0.55, 0.42, f), uTwist, cup, mix(0.2, 0.55, f));
        pet.x += chrysFray(p, chaos);

        // Shadow cast on the layer below, then the petal itself.
        col *= 1.0 - 0.55 * smoothstep(0.05, -0.01, pet.x);
        float m = smoothstep(aa, -aa, pet.x);
        float shade = chrysPetalShade(pet, cup);
        vec3 base = pal(hue + 0.08 * k);
        vec3 tipc = mix(pal(hue + 0.08 * k + 0.12), vec3(1.0), 0.25);
        vec3 pc = mix(base * 0.55, tipc, smoothstep(0.1, 1.0, pet.y)) * shade;
        // A thin bright rim so neighbouring petals separate.
        pc += tipc * 0.18 * smoothstep(aa * 2.5, 0.0, abs(pet.x)) * m;
        col = mix(col, pc, m);
    }

    // The core the flower opens out of.
    float core = chrysCore(r, 0.09 * rest);
    col = mix(col, mix(pal(hue + 0.6), vec3(1.0, 0.92, 0.75), 0.5), core * 0.9);
    col += pal(hue + 0.6) * 0.12 * chrysCore(r, 0.3) * (0.7 + 0.3 * swell);

    col *= 0.85 + 0.15 * swell;
    col *= smoothstep(2.0, 0.6, r) * 0.25 + 0.75;
    fragColor = vec4(grade(col), 1.0);
}
