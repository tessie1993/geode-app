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

// Glass Lens: magnifying lenses drifting over an LED wall that is playing the
// music. The wall is a grid of LEDs, each made of three narrow red, green and
// blue emitters; columns light to the height of the live spectrum (lib_sound's
// 64-point log spectrum), coloured along the palette, with a peak-hold dot on
// top. From a distance it is a glowing bar graph; under a lens the magnified
// LEDs resolve into their RGB triads, the moment a viewer recognises "glass".
// Each lens is a thick convex profile: a magnification that rises from the
// centre to a barrel at the rim, read once per channel at a slightly
// different power (dispersion, three reads of the wall), a polished bevel that
// mirrors the studio, a Fresnel rim, and the soft shadow and focused hot spot
// a lens throws on the wall below.
//
// ---- audio ------------------------------------------------------------------
//
//   spectrum       the wall itself;
//   bass           the magnification, 1.6 -> 2.6: the lenses lean in on low notes;
//   centroid       chromatic aberration at the rims;
//   snare          flips one lens on its axis (SND_SNARE_TOGGLE eases 0 <-> 1 per
//                  hit), squashing it edge-on and back;
//   kick           jostles the lenses (an eased bump, never a jump);
//   pan            the drift leans toward the louder side;
//   flatness       frosts the glass: the magnified image goes milky and soft.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the drift. Complexity adds up to two more lenses. Chaos wobbles
// each lens's profile so the magnified image swims. Energy scales every
// reaction. The rig's look nudges the lenses (parallax over the wall).
//
// ---- cost -------------------------------------------------------------------
//
// One wall read off the lenses; three on a lens, plus one studio lookup for the
// bevel. Up to five lenses, analytic.

#define LN_MAX 5
#define LN_COLS 48.0

/** The LED wall at point `p` (wall units, about -2..2 tall). `frost` blurs it toward each LED's average. */
vec3 wall(vec2 p, float hue, float frost) {
    vec2 g = p * (LN_COLS / 4.0);
    vec2 cell = floor(g);
    vec2 f = fract(g) - 0.5;
    float x = clamp((cell.x + LN_COLS * 0.5) / LN_COLS, 0.0, 1.0);
    float level = clamp(sndSpectrum(x * 0.92 + 0.02) * 1.15, 0.0, 1.0);
    float rows = LN_COLS * 0.5;
    float row = (cell.y + rows * 0.5) / rows;
    float lit = step(row, level);
    float peak = (1.0 - smoothstep(0.0, 1.5 / rows, abs(row - level - 0.6 / rows))) * 0.9;
    vec3 c = pal(hue + x * 0.7 + row * 0.15);
    float on = max(lit, peak) * (0.25 + 0.75 * step(row, 1.0)) * step(0.0, row);
    // The three emitters of one LED, side by side, and the dark mask between them.
    float sub = f.x * 3.0 + 1.5;
    vec3 tri = vec3(1.0 - smoothstep(0.25, 0.42, abs(sub - 0.5)), 1.0 - smoothstep(0.25, 0.42, abs(sub - 1.5)), 1.0 - smoothstep(0.25, 0.42, abs(sub - 2.5)));
    tri *= 1.0 - smoothstep(0.32, 0.45, abs(f.y));
    vec3 sharp = c * tri * 3.0;
    vec3 soft = c * 1.3 * exp(-dot(f, f) * 9.0);
    vec3 led = mix(sharp, soft, clamp(frost, 0.0, 1.0));
    vec3 offc = vec3(0.012, 0.012, 0.016) * (1.0 - smoothstep(0.3, 0.48, max(abs(f.x), abs(f.y))));
    return led * (0.05 + 0.95 * on) + offc;
}

void main() {
    vec2 uv = view();
    float t = glassTime();
    GlassEar ear = glassListen();
    float hue = 0.05 * sin(t * 0.02) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    GlassLook lk = glassLook(GLASS_ENV_STUDIO, hue, ear);
    vec2 par = uCamLook.xy * 0.25;
    float mag = 1.6 + 1.0 * clamp(ear.bass, 0.0, 1.0);
    float ab = 0.02 + 0.06 * ear.sharp;
    float frost = ear.frost * 0.7;
    int count = 3 + int(floor(glassComplexity() * 2.0 + 0.5));

    // The wall far away is a little defocused: the eye sits on the lenses.
    vec3 col = wall(uv + par, hue, 0.9);
    vec2 shadowOff = vec2(0.07, -0.09);

    for (int i = 0; i < LN_MAX; i++) {
        if (i >= count) break;
        float fi = float(i);
        float h1 = glassHash1(fi + 1.3);
        float h2 = glassHash1(fi + 4.7);
        float r = 0.42 + 0.22 * h2;
        float lane = fi - 0.5 * float(count - 1);
        vec2 c = vec2(lane * 1.15 + sin(t * (0.07 + 0.04 * h1) + h1 * 9.0) * 0.35 + 0.25 * ear.pan, -0.3 + sin(t * (0.05 + 0.03 * h2) + h2 * 7.0) * 0.5);
        c += (glassHash2(vec2(fi, 3.0)) - 0.5) * 0.1 * ear.kick;
        float flip = (i == 0 ? clamp(SND_SNARE_TOGGLE, 0.0, 1.0) : 0.0) * GLASS_PI * min(glassEnergy(), 1.0);
        float sq = max(abs(cos(flip)), 0.12);

        // Shadow and focus on the wall below.
        vec2 ds = (uv - c - shadowOff) / r;
        float dsl = length(vec2(ds.x / sq, ds.y));
        col *= 1.0 - 0.45 * (1.0 - smoothstep(0.7, 1.15, dsl));
        col += pal(hue + 0.1) * 0.5 * exp(-dsl * dsl * 30.0) * sq;

        vec2 q = (uv - c) / r;
        q.x /= sq;
        float rl = length(q);
        if (rl >= 1.0) continue;
        float rim = smoothstep(0.86, 1.0, rl);
        q += 0.04 * glassChaos() * vec2(sin(q.y * 6.0 + t), cos(q.x * 5.0 - t));
        // Magnification: strongest at the centre, rolling into a barrel at the rim.
        float m = mag * (1.0 - 0.32 * rl * rl);
        vec2 base = c + par;
        vec2 dir = (uv - c);
        vec3 seen = vec3(
            wall(base + dir / (m * (1.0 + ab * rl)), hue, frost).r,
            wall(base + dir / m, hue, frost).g,
            wall(base + dir / (m * (1.0 - ab * rl)), hue, frost).b);
        // The lens surface: a cap whose normal tilts toward the rim.
        vec3 n = normalize(vec3(q * 0.85, sqrt(max(1.0 - rl * rl * 0.72, 0.0))));
        n = vec3(n.x * sq, n.y, n.z);
        vec3 rd = vec3(0.0, 0.0, -1.0);
        float cosI = max(n.z, 0.0);
        float fr = glassFresnel(cosI, 1.5);
        vec3 refl = glassEnv(reflect(rd, n).xzy * vec3(1.0, 1.0, -1.0), lk);
        vec3 lensCol = seen * (1.0 - fr) * (0.92 - 0.25 * rim) + refl * (fr + 0.5 * rim);
        // The polished bevel and a thin bright edge line.
        float edge = smoothstep(0.93, 0.975, rl) * (1.0 - smoothstep(0.985, 1.0, rl));
        lensCol += mix(vec3(1.0), pal(hue + 0.6), 0.3) * edge * 0.6;
        lensCol += vec3(1.0) * glassGlint(vec3(q * 3.0, fi), 3.0, ear.hat) * 0.5;
        col = mix(col, lensCol, 1.0 - smoothstep(0.985, 1.0, rl));
    }

    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col)), 1.0);
}
