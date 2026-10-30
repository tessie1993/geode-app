#version 300 es
#define VIEW_OWNS_SHAPE
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

// Chrysanthemum Kaleido: a bouquet seen through a kaleidoscope. In the source
// plane a handful of different flowers - each its own petal count, shape and
// colour - stream outward from the centre, growing as they go and shrinking
// away before they wrap, so none ever pops. The plane is folded into mirror
// wedges, so the stream becomes a jewelled, ever-opening rosette of whole
// flowers rather than of single petals. Thin bright seams mark the mirrors,
// as on the real toy.
//
// ---- audio ------------------------------------------------------------------
//
// The beat (uEvents.x) SWINGS THE MIRRORS: the fold seam twists away and back
// across each beat's 0.3 s envelope (chrysBump, from rest), so on a steady
// groove the rosette rocks in time while the flowers keep streaming. The bar
// oscillator (uBarOsc, phase-locked and confidence-gated) breathes the
// kaleidoscope's magnification over each bar. The high-mid band
// (uBandsHi.x) slides the seams' glint along the mirrors. The stream itself
// rides uFlowPhase (loudness sets its speed, never its direction).
// Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the slow turn of the whole scope and the hue clock.
// Complexity: uMorph adds up to four more flowers to the bouquet (nine at
// most). Chaos: uTurbulence + uWarp give the petals uneven lengths and
// ragged edges; uTwist curls every flower. Energy: uIntensity scales the
// mirror swing and the bar breathing. Colour: pal() for every flower and the
// ground. Space: uZoom via view(). Symmetry: uSymmetry (default 6) is the
// number of mirror wedges.

#define KA_MAX_FLOWERS 9

void main() {
    vec2 uv = view();
    float t = uTime * uSpeed;
    float swell = clamp(uSwell, 0.0, 1.5);
    float beat = clamp(uEvents.x, 0.0, 1.0);
    float chaos = clamp(uTurbulence + uWarp, 0.0, 2.0);
    float flowers = 5.0 + floor(4.0 * clamp(uMorph, 0.0, 1.0) + 0.5);
    float mirrors = clamp(floor(uSymmetry + 0.5), 3.0, 16.0);
    float aa = 3.0 / uResolution.y;
    float hue = 0.3 + t * 0.003;

    // Fold into mirror wedges; the beat swings the seam.
    float zoom = 1.0 + 0.08 * uIntensity * (uBarOsc - 0.5);
    float rr = length(uv) / zoom;
    float seg = CHRYS_TAU / mirrors;
    float swing = 0.3 * uIntensity * chrysBump(beat);
    float a = atan(uv.y, uv.x) + t * 0.02 + swing * (0.4 + rr);
    float wedge = mod(a, seg);
    float fa = abs(wedge - 0.5 * seg);
    vec2 p = rr * vec2(cos(fa), sin(fa));

    // Ground: a deep jewel gradient across the wedge.
    vec3 col = mix(pal(hue + 0.5) * 0.04, pal(hue + 0.62) * 0.09, smoothstep(0.0, 1.2, rr));
    col *= 0.8 + 0.2 * cos(fa * 6.0 / seg);

    for (int i = 0; i < KA_MAX_FLOWERS; i++) {
        float k = float(i);
        if (k >= flowers) break;
        float h = chrysHash1(k * 3.7 + 1.0);
        float h2 = chrysHash1(k * 5.1 + 2.0);
        // Stream outward; grow, then shrink away before the wrap.
        float life = fract(h + uFlowPhase * 0.04 * (0.6 + 0.6 * h2));
        float dist = 0.05 + 1.25 * life;
        vec2 c = dist * vec2(cos(h2 * 0.5 * seg), sin(h2 * 0.5 * seg));
        float size = (0.06 + 0.22 * dist) * sin(CHRYS_PI * life);
        if (size < 0.004) continue;
        vec2 q = chrysTurn(t * (h - 0.5) * 0.6 + h * 6.0) * (p - c);
        float count = 5.0 + floor(h * 9.0);
        float cup = h2;
        float tip = fract(h * 7.3);
        float pid = floor((atan(q.y, q.x) - uTwist * 4.0 * (length(q) - size * 0.2)) / (CHRYS_TAU / count) + 0.5);
        float len = size * 0.8 * (1.0 + 0.2 * chaos * chrysWobble(mod(pid, count) + k * 11.0, t));
        vec4 pet = chrysPetal(q, count, size * 0.2, len, 0.5, uTwist * 4.0, cup, tip);
        pet.x += chrysFray(q / max(size, 0.05) * 0.3, chaos) * size * 4.0;
        col *= 1.0 - 0.45 * smoothstep(size * 0.25, -0.005, pet.x);
        float m = smoothstep(aa, -aa, pet.x);
        float fh = hue + 0.6 * h;
        vec3 pc = mix(pal(fh) * 0.45, mix(pal(fh + 0.07), vec3(1.0), 0.2), clamp(pet.y, 0.0, 1.0));
        pc *= chrysPetalShade(pet, cup);
        col = mix(col, pc, m);
        // A contrasting heart.
        float hr = length(q) / max(size * 0.22, 1e-3);
        col = mix(col, pal(fh + 0.5) * 0.8 * (1.1 - 0.4 * hr), smoothstep(1.0 + 0.1, 0.9, hr));
    }

    // Mirror seams: a thin line along each fold, its glint sliding outward.
    float seamD = min(fa, 0.5 * seg - fa) * rr;
    float glintAt = fract(t * 0.07 + 0.8 * clamp(uBandsHi.x, 0.0, 1.0));
    float glint = exp(-pow((rr - glintAt * 1.4) * 5.0, 2.0));
    col += pal(hue + 0.15) * smoothstep(0.004, 0.0, seamD) * (0.05 + 0.18 * glint);

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
