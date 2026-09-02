// The particle shading proper, FRAGMENT STAGES ONLY - it antialiases with
// fwidth(), which does not exist in a vertex shader. Include
// lib_particle_common.glsl first: every helper and constant used here comes
// from it, and the include resolver is one level deep on purpose, so a library
// cannot pull in its own dependency.
//
// The look, and where it comes from:
//   * shapes are signed distance fields (Inigo Quilez's 2D primitives,
//     iquilezles.org/articles/distfunctions2d) antialiased against one screen
//     pixel, so silhouettes stay crisp at any sprite size;
//   * an inverse-square aura around the sprite - a point-spread function, not
//     a cutoff - so overlapping particles sum into a real glow. Keyed on
//     radius rather than the SDF deliberately: an aura that plateaus across a
//     filled shape is what turns a few thousand sprites into a white wash;
//   * the interior falloff carries most of the brightness, so a sprite reads
//     as a luminous point with a defined edge rather than a flat disc;
//   * hot cores desaturate toward white and the aura disperses slightly in
//     hue, the way emissive sources actually photograph;
//   * ACES filmic tone mapping (Narkowicz's fit) so bright cores roll off to
//     white instead of clipping flat, plus a 1-LSB dither that kills the
//     banding an 8-bit target puts across wide soft gradients.

/**
 * The whole look, in one call. [p] is the sprite-space coordinate (the quad
 * edge is +-1), [fade] the sub-pixel dimming, [stretch] the billboard factor.
 * Returns a PREMULTIPLIED colour with a coverage alpha that counts the body
 * and core only - the aura must not occlude what is behind it, so it lands as
 * pure added light.
 */
vec4 ptShade(
    vec2 p,
    float hue,
    float energy,
    float seed,
    float fade,
    float stretch,
    float shape,
    float sat,
    float bright,
    float contrast,
    float gamma,
    float glowAmount,
    float time,
    vec2 fragCoord
) {
    float d = ptShapeField(p, shape);
    // Analytic AA: one screen pixel measured in shape space. Constant-width
    // edges at every sprite size, which a fixed smoothstep cannot give.
    float aa = max(fwidth(d), 1e-4);
    float body = 1.0 - smoothstep(-aa, aa, d);
    float r = length(p);
    float falloff = 1.0 / (1.0 + 16.0 * r * r);
    float aura = falloff * sqrt(falloff);
    // Interior falloff: deepest inside the shape is hottest. Thin shells
    // (Ring, Bubble) have almost no interior and so stay rim-lit, which is
    // exactly right for them.
    float core = pow(clamp(-d / (PT_SHAPE_R * 0.9), 0.0, 1.0), 1.5);

    float e = clamp(energy, 0.0, 1.0);
    // Star and Spark get diffraction arms; every other shape stays clean.
    float spikeAmt = ((shape > 1.5 && shape < 2.5) || (shape > 3.5 && shape < 4.5)) ? 0.55 : 0.0;
    if (spikeAmt > 0.0) aura += ptSpikes(p) * spikeAmt * (0.35 + 0.65 * e);
    // Bubble is a shell plus a faint fill and an off-centre highlight - the
    // specular pin is what separates it from Ring at a glance.
    if (shape > 5.5) {
        body += smoothstep(PT_SHAPE_R, 0.0, r) * 0.12;
        body += smoothstep(PT_SHAPE_R * 0.40, 0.0, length(p - vec2(-0.40, 0.40) * PT_SHAPE_R)) * 0.5;
    }

    float glow = aura * glowAmount;
    float weight = body + glow + core;
    if (weight < 1e-4) return vec4(0.0);

    float s = clamp(sat * (0.98 - 0.30 * e), 0.0, 1.0);
    vec3 bodyCol = ptHsv2rgb(vec3(hue, s, 1.0));
    vec3 auraCol = ptHsv2rgb(vec3(fract(hue + 0.045), min(s * 1.15, 1.0), 1.0));
    vec3 coreCol = mix(bodyCol, vec3(1.0), 0.10 + 0.45 * e);
    // Graded unpremultiplied, then weighted - grading the premultiplied value
    // would push the contrast pedestal into fully transparent pixels and box
    // every sprite in a faint square.
    vec3 tint = (bodyCol * body + auraCol * glow + coreCol * core) / weight;
    tint = (tint - 0.5) * contrast + 0.5;
    tint = pow(max(tint, 0.0), vec3(1.0 / max(gamma, 0.05)));

    // Emission. Stretching spreads one particle's light over a longer quad, so
    // divide it back out or fast particles would read as brighter ones.
    float twinkle = 0.88 + 0.12 * sin(time * 5.3 + seed * 6.2831853);
    float amp = (0.32 + 0.68 * e) * fade * twinkle / sqrt(max(stretch, 1e-3));
    vec3 color = ptAces(tint * (body * 0.40 + glow * 0.90 + core * 1.50) * amp * bright);
    color += (ptHash12(fragCoord + seed) - 0.5) / 255.0;

    float alpha = clamp(body * 0.85 + core * 0.55, 0.0, 1.0) * (0.32 + 0.68 * e) * fade;
    return vec4(max(color, 0.0), alpha);
}
