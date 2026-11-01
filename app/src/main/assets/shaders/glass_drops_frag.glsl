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

// Glass Drops: a rainy window at night. Behind the misted pane the city is a
// field of bokeh - six-bladed aperture shapes, warm sodium low down, the
// palette higher up, each fringed red outside and blue inside the way a real
// lens's longitudinal chromatic aberration fringes out-of-focus highlights.
// On the pane, raindrops: each is a tiny ball lens holding a SHARP, inverted,
// wide-angle image of the city (three reads, one per wavelength, so its edge
// is fringed), dark toward its rim, with a crown highlight and a bright
// caustic crescent at its foot. Where a drop has slid it has wiped the mist,
// leaving a clear streak. The mist itself is a fine condensation of
// micro-droplets that scatter the city into a soft glow.
//
// Cells: each drop and each light sits inside its own grid cell with room to
// spare, so nothing it draws crosses a cell border: one lookup per layer, no
// loop over drops.
//
// ---- audio ------------------------------------------------------------------
//
//   onsets       slide the drops a notch down the glass (glassGlide), each column
//                at its own pace;
//   snare        a fresh drop lands in a few cells: it swells in and settles;
//   hat          the city twinkles: lights flicker individually;
//   flatness     the mist: noise fogs the pane, tones clear it;
//   centroid     the drops' dispersion and the bokeh fringes;
//   bass         swells the drops;
//   pan          the city drifts toward the louder side.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the creep and the flicker. Complexity adds the third, finest
// drop layer. Chaos wobbles the drops' refraction. Energy scales every
// reaction. The rig's look shifts the city behind the pane (parallax).
//
// ---- cost -------------------------------------------------------------------
//
// Two bokeh layers per city read; a pixel reads the city once (blurred) off a
// drop and three times (sharp) on one. Analytic, no loop over objects.

/** Hexagonal distance: a six-bladed aperture's outline. */
float hexDist(vec2 p) {
    p = abs(p);
    return max(dot(p, vec2(0.8660254, 0.5)), p.y);
}

/** One layer of bokeh `cell` units apart: aperture discs of radius growing with `blur`, fringed by `fringe`. */
vec3 bokeh(vec2 x, float cell, float blur, float fringe, float hue, float seed, float twinkle) {
    vec2 id = floor(x / cell);
    vec2 f = x / cell - id - 0.5;
    float h = glassHash(id + seed);
    if (h < 0.35) return vec3(0.0);
    vec2 o = (glassHash2(id + seed * 1.7) - 0.5) * 0.3;
    float low = 1.0 - smoothstep(-1.0, 0.4, x.y);
    vec3 c = mix(pal(hue + h * 0.6), vec3(1.0, 0.62, 0.25), low * 0.75);
    float r = 0.06 + 0.24 * blur;
    float d = hexDist(f - o) - r;
    float w = 0.012 + 0.07 * blur;
    float flick = 1.0 - twinkle * step(0.6, fract(h * 13.0 + glassTime() * 1.7)) * 0.8;
    float body = 1.0 - smoothstep(-w, w, d);
    float rimR = 1.0 - smoothstep(0.0, w * 2.0, abs(d + fringe * 0.04));
    float rimB = 1.0 - smoothstep(0.0, w * 2.0, abs(d + w * 2.0 + fringe * 0.02));
    vec3 col = c * body * (0.5 + 0.5 * (h - 0.35) / 0.65) * 0.06 / (r * r + 0.02);
    col += (vec3(1.0, 0.3, 0.2) * rimR + vec3(0.2, 0.4, 1.0) * rimB) * fringe * body * 0.35;
    return col * flick;
}

GlassLook gLk;

/** The world behind the pane (the FRACTAL world, with bokeh lights over it), at defocus `blur` 0 (sharp) .. 1. */
vec3 city(vec2 p, float blur, float fringe, float hue, float twinkle) {
    vec3 sky = glassFractal2(p * 0.8, gLk, blur * 0.18) * (1.0 - 0.45 * blur);
    vec3 col = sky + 0.5 * bokeh(p, 0.32, blur, fringe, hue, 1.0, twinkle);
    col += 0.35 * bokeh(p + vec2(0.13, 0.07), 0.19, blur, fringe, hue + 0.3, 5.0, twinkle);
    return col;
}

/** One drop layer: returns the drop's coverage; `q` is the point in the drop's unit frame, `centre` its centre, `clear` the wipe of its trail. */
float drops(vec2 p, float cell, float t, float slide, float swell, float splash, out vec2 q, out vec2 centre, out float clear) {
    float col = floor(p.x / cell);
    float rate = 0.2 + 0.8 * glassHash(vec2(col, cell * 7.0));
    float y = p.y / cell + t * 0.05 * rate + slide * rate;
    vec2 id = vec2(col, floor(y));
    vec2 f = vec2(fract(p.x / cell), fract(y)) - 0.5;
    float h = glassHash(id + 3.3);
    float fresh = splash * step(0.8, glassHash(id + floor(SND_ONSET_COUNT) * 0.13));
    float r = (0.12 + 0.16 * h) * (1.0 + 0.15 * swell) * (1.0 + 0.35 * fresh);
    if (h < 0.55) r = 0.0;
    vec2 c = vec2((glassHash(id + 8.1) - 0.5) * 0.4, -0.15);
    // A drop is a little taller below its widest point: squash the top half.
    vec2 d = f - c;
    d.y *= d.y > 0.0 ? 1.25 : 0.92;
    q = d / max(r, 1e-3);
    centre = (vec2(col, id.y) + 0.5 + c) * cell;
    // The trail above the drop: a clear streak fading upward.
    float tw = max(r * 0.55, 1e-3);
    clear = (1.0 - smoothstep(tw * 0.6, tw, abs(f.x - c.x))) * step(c.y, f.y) * (1.0 - smoothstep(c.y, 0.5, f.y)) * step(0.55, h);
    return r > 0.0 ? 1.0 - smoothstep(0.92, 1.0, length(q)) : 0.0;
}

void main() {
    vec2 uv = view();
    float t = glassTime();
    GlassEar ear = glassListen();
    float hue = 0.05 * sin(t * 0.02) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    gLk = glassLook(GLASS_ENV_FRACTAL, hue, ear);
    vec2 cp = uv + uCamLook.xy * 0.2 + vec2(0.2 * ear.pan, 0.0);
    float slide = glassGlide(SND_ONSET_COUNT, SND_ONSET_ENV) * 0.35 * min(glassEnergy(), 1.5);
    float mist = 0.55 + 0.45 * ear.frost;
    float fringe = 0.4 + 0.8 * ear.sharp;
    float spread = 0.02 + 0.05 * ear.sharp;

    vec3 blurred = city(cp, 1.0, fringe, hue, ear.hat);
    // The mist: micro-droplets scatter the blurred city into a glow, with a fine grain.
    float grain = glassNoise(uv * 140.0) * 0.5 + 0.5;
    vec3 col = blurred * (1.0 - 0.25 * mist) + (blurred * 0.6 + pal(hue + 0.6) * 0.012) * mist * (0.6 + 0.4 * grain);

    float clearAll = 0.0;
    vec3 dropCol = vec3(0.0);
    float dropCov = 0.0;
    for (int layer = 0; layer < 3; layer++) {
        if (layer == 2 && glassComplexity() < 0.5) break;
        float cell = layer == 0 ? 0.5 : (layer == 1 ? 0.24 : 0.11);
        vec2 q;
        vec2 centre;
        float clear;
        float cov = drops(uv + vec2(float(layer) * 0.37, 0.0), cell, t, slide, ear.bass, ear.snare, q, centre, clear);
        clearAll = max(clearAll, clear);
        if (cov <= 0.0) continue;
        float rl = length(q);
        q += 0.06 * glassChaos() * vec2(sin(q.y * 7.0 + t), cos(q.x * 6.0 - t));
        // A ball lens: inverted, wide angle. Each channel at a slightly different power.
        vec2 look = centre - vec2(float(layer) * 0.37, 0.0) + uCamLook.xy * 0.2 + vec2(0.2 * ear.pan, 0.0);
        float k = 0.3;
        vec3 img = vec3(
            city(look - q * k * (1.0 + spread), 0.55, fringe, hue, ear.hat).r,
            city(look - q * k, 0.55, fringe, hue, ear.hat).g,
            city(look - q * k * (1.0 - spread), 0.55, fringe, hue, ear.hat).b);
        img = img * 1.5 + pal(hue + 0.6) * 0.05;
        img *= 1.0 - 0.7 * smoothstep(0.6, 1.0, rl);
        // Crown highlight, foot caustic.
        img += vec3(1.0, 0.97, 0.92) * 1.6 * exp(-dot(q - vec2(-0.3, 0.45), q - vec2(-0.3, 0.45)) * 40.0);
        img += mix(vec3(1.0, 0.8, 0.5), pal(hue), 0.3) * 0.5 * smoothstep(0.55, 0.85, rl) * smoothstep(0.1, 0.7, -q.y) * (1.0 - smoothstep(0.85, 1.0, rl));
        img += vec3(1.0) * glassGlint(vec3(q * 0.8, centre.x * 9.0), 1.0, ear.hat * 0.4);
        dropCol = mix(dropCol, img, cov);
        dropCov = max(dropCov, cov);
    }
    // Where a drop has run the mist is gone: the city shows sharper.
    if (clearAll > 0.0) col = mix(col, city(cp, 0.35, fringe, hue, ear.hat), clearAll * 0.8);
    col = mix(col, dropCol, dropCov);

    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col)), 1.0);
}
