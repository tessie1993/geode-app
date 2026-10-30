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
//#include lib_lucid

// Lucid Ink: suminagashi, Japanese floating-ink marbling, in one colour on
// warm paper. The plane is first combed - drawn through by a rake of sine
// teeth the way a marbler pulls a stylus across the bath - and then read
// through the deepest nest in the family: five levels of ridged noise (three
// on a low Detail budget). Ridged noise is what gives the ink its rings: the
// creases nest inside one another like the concentric drops of a real
// suminagashi bath.
//
// The only Lucid style on a light ground. The recolour is monochrome by
// design: an ink density, dark ink lines where the ridges crest, paler wash
// where they spread, and the palette only as a tint in the ink and paper.
//
// ---- audio ------------------------------------------------------------------
//
// Spectral FLUX (uBandsHi.w) is the GRAIN: busy, changing spectra make the
// pigment granulate - it settles into the paper's tooth in a fine speckle and
// the paper's own fibre shows through. The grain pattern is fixed to the
// paper, so flux changes how much of it shows, never where it is, and its
// mean is zero so the average tone of the page does not move. uSwell pulls
// the rake deeper (comb amplitude). uMidSmooth spreads the wash. Overall
// paper brightness is constant; only the ink moves.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock, the rake and the bath's drift. Complexity:
// uMorph adds rake teeth (continuous) and, past 0.5, an octave (the nest is
// already at the 5-level cap). Chaos: uTurbulence/uWarp raise warp strength
// and jitter the bath; uTwist swirls it. Color: the ink is pal(0.6) taken
// nearly to black, the wash pal(0.3), the paper tinted by pal(0.1). Energy:
// uIntensity scales the grain and the swell-driven rake. Space: uZoom/
// uRotation through lucidView. The user's kaleidoscope folds the bath.

void main() {
    vec2 uv = lucidView(true);
    float t = uTime * uSpeed;
    float flow = uFlowPhase * uSpeed;
    float flux = clamp(uBandsHi.w, 0.0, 1.0) * uIntensity;
    float swell = clamp(uSwell, 0.0, 1.5);
    float mid = clamp(uMidSmooth, 0.0, 1.5);

    // Five levels is the family's deepest; two octaves keep it affordable.
    int depth = lucidBudget(5, 3);
    int oct = lucidDetail(2, 1);

    // The rake: teeth across x pull the bath along y, and back.
    vec2 p = uv * 1.3;
    float rake = 0.18 + 0.16 * swell * uIntensity;
    float teeth = 5.0 + 4.0 * clamp(uMorph, 0.0, 1.0);
    p.y += rake * sin(p.x * teeth + t * 0.11);
    p.x += 0.35 * rake * sin(p.y * 2.3 - t * 0.07);
    p += vec2(0.0, flow * 0.08);
    p += lucidJitter(p, t);

    vec4 n = lucidNest(p, depth, 1.05 * lucidChaos(), t * 0.12, LUCID_RIDGED, oct);

    float ridge = 0.5 + 0.5 * n.z;
    float lines = smoothstep(0.72, 0.95, ridge);
    float wash = smoothstep(0.25 - 0.1 * clamp(mid, 0.0, 1.0), 0.8, ridge) * (0.35 + 0.25 * n.w);
    float ink = clamp(max(lines, wash), 0.0, 1.0);

    // Granulation: a fixed speckle tied to the paper, revealed by flux.
    vec2 fibre = vUv * uResolution / 3.0;
    float speck = lucidValue(fibre) * 0.5 + lucidValue(fibre * 2.7 + 9.1) * 0.5;
    float grain = clamp(flux, 0.0, 1.5) * 0.25;
    ink = clamp(ink * (1.0 + grain * speck * 1.6), 0.0, 1.0);

    vec3 paper = mix(vec3(0.86, 0.82, 0.74), pal(0.1), 0.12) * (0.95 + 0.05 * speck * (0.3 + grain));
    vec3 inkCol = pal(0.6) * 0.06 + vec3(0.02, 0.02, 0.03);
    vec3 washCol = mix(inkCol, pal(0.3) * 0.5, 0.25);
    vec3 col = mix(paper, washCol, wash * (1.0 - lines));
    col = mix(col, inkCol, lines);
    col = mix(col, inkCol, (ink - max(lines, wash)) * 0.6);

    col *= 0.78;
    fragColor = vec4(grade(col), 1.0);
}
