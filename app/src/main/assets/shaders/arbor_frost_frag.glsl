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
//#include lib_sdf3
//#include lib_arbor

// Arbor Frost: one great snow crystal seen face-on, growing its dendrites out
// of a hexagonal plate, while smaller flakes drift down behind it. Flat 2D,
// top-down and radial: no march, so it ignores uSteps.
//
// The arms are arborDendrite2, the three-way fork (straight on plus a mirrored
// pair of side arms) that is how ice actually branches, and the whole plane is
// folded into the twelve mirror wedges of a hexagon (D6), so one arm is drawn
// and the crystal's symmetry is exact. Side arms that reach the wedge edge
// meet their neighbours' mirror images there, the way real dendrites fuse.
//
// ---- audio ------------------------------------------------------------------
//
// Treble is GROWTH. uTrebleSmooth (slew-limited: the air end of the spectrum,
// without a frame of jitter) sets how many levels of the dendrite exist,
// fractionally, so a bright passage grows the crystal out to its fine
// feathered tips and a dull one melts it back to a plate with stubs. Each
// section (uSectionCount) picks a new crystal HABIT, side-arm angle and the
// two length ratios, hashed from the count and morphed in over the section
// envelope so it never snaps. Kick (uHits.x) pulses the central plate's size.
// Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the crystal's turn, the drifting flakes and the
// sparkle. Complexity: uMorph adds a level of growth (arbLevels2) and the
// background flakes' detail. Chaos: uTurbulence and uWarp etch a fine
// irregular warp into the crystal's edges. Colour: ice, glow and sky from
// pal(). Energy: uIntensity scales the treble growth and the kick pulse.
// Space: the view zoom and pan.

#define FR_ARMS 6.0
#define FR_LEN 0.12

vec3 habit(float s) {
    float h = hash11(s * 3.7 + 1.0);
    float h2 = hash11(s * 5.1 + 2.0);
    return vec3(mix(0.85, 1.25, h), mix(0.72, 0.80, h2), mix(0.38, 0.60, fract(h * 7.0 + h2)));
}

/** Fold the plane into one mirror wedge of the hexagon, arm along +x. */
vec2 hexFold(vec2 p) {
    float w = ARB_TAU / FR_ARMS;
    float a = atan(p.y, p.x);
    a = abs(mod(a + 0.5 * w, w) - 0.5 * w);
    return length(p) * vec2(cos(a), sin(a));
}

void main() {
    vec2 uv = arbView();
    float px = max(length(fwidth(uv)), 1e-4);
    float t = uTime * uSpeed;
    float energy = uIntensity;
    float chaos = arbChaos();
    float swell = clamp(uSwell, 0.0, 1.5);
    float treb = clamp(uTrebleSmooth, 0.0, 1.5) * energy;
    float kick = clamp(uHits.x * energy, 0.0, 1.0);

    float morphIn = smoothstep(0.0, 1.0, 1.0 - clamp(uEvents.z, 0.0, 1.0));
    vec3 hab = mix(habit(uSectionCount - 1.0), habit(uSectionCount), morphIn);

    float hue = 0.58 + 0.04 * sin(t * 0.03);

    // ---- sky and drifting flakes ----------------------------------------
    vec3 col = mix(pal(hue) * 0.10 + vec3(0.01, 0.015, 0.04), pal(hue + 0.1) * 0.03, smoothstep(-1.0, 1.0, uv.y));
    {
        const float cellSize = 0.32;
        vec2 bp = uv * 1.0 + vec2(0.07 * sin(t * 0.2), t * 0.06);
        vec2 cell = floor(bp / cellSize);
        float h = hash13(vec3(cell, 4.0));
        vec2 lp = (fract(bp / cellSize) - 0.5) * cellSize;
        lp -= (vec2(hash13(vec3(cell, 5.0)), hash13(vec3(cell, 6.0))) - 0.5) * cellSize * 0.5;
        lp = rot2(t * (0.3 + h) + h * 6.0) * lp;
        float sz = 0.12 + 0.12 * h;
        vec2 fq = hexFold(lp / sz);
        float fd = arborDendrite2(vec2(fq.y, fq.x - 0.08), 1.05, 0.75, 0.5, 0.18, 0.12,
                                  arbLevels2(3.0, 1.0)) * sz;
        float show = step(0.55, h);
        col += pal(hue + 0.05) * 0.35 * arbInk(fd, px) * show;
    }

    // ---- the crystal ------------------------------------------------------
    vec2 p = rot2(t * 0.03) * uv;
    p += chaos * 0.012 * vec2(sin(p.y * 47.0 + t), cos(p.x * 43.0 - t * 0.7));
    vec2 q = hexFold(p);
    float plateR = 0.075 + 0.025 * kick;
    float plate = q.x - plateR;
    float grow = arbLevels2(2.5 + 4.5 * clamp(treb, 0.0, 1.2), 1.0);
    float arm = arborDendrite2(vec2(q.y, q.x - plateR * 0.6), hab.x, hab.y, hab.z, FR_LEN, 0.10, grow);
    float tip = gArbTip;
    float lvl = gArbLevel;
    float d = min(plate, arm);

    float inside = arbInk(d, px);
    // Ice: a pale body, a brighter spine along every limb's centre and a
    // crisp edge; the plate shows faint concentric growth rings.
    vec3 ice = mix(vec3(0.85, 0.95, 1.0), pal(hue + 0.02), 0.35);
    float spine = smoothstep(-0.002, -0.012, d);
    float rings = 0.5 + 0.5 * sin(length(p) * 260.0);
    vec3 body = ice * (0.45 + 0.35 * spine + 0.08 * rings * step(plate, arm));
    float edge = 1.0 - smoothstep(0.0, 2.0 * px, abs(d));
    col = mix(col, body, inside * 0.9);
    col += ice * edge * 0.35;
    col += pal(hue + 0.08) * exp(-max(d, 0.0) * 55.0) * 0.18 * (0.8 + 0.3 * swell);

    // Sparkle on the outermost tips, each twinkling on its own clock.
    float spark = (1.0 - smoothstep(0.0, 0.006, tip)) * step(grow - 2.0, lvl);
    spark *= 0.5 + 0.5 * sin(t * 3.0 + hash11(gArbPath * 0.37) * 40.0);
    col += vec3(1.0) * spark * 0.5;

    col *= 0.88 + 0.15 * swell;
    col *= 0.8 + 0.2 * smoothstep(2.0, 0.3, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
