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

// Chrysanthemum Ink: a sumi-e brush painting of a spider chrysanthemum on
// warm paper - the one light-ground style in the family. Long quill petals of
// uneven length, their tips hooked by chrysHook, are laid down in three rings
// of wet ink; where strokes cross the ink compounds and darkens, exactly as
// layered washes do. Each stroke is dark and wet at its root and breaks into
// dry-brush streaks toward the tip, and wet ink pools darker along the edge
// it bled to. A curved stem and two grey-wash leaves carry the flower off the
// bottom of the sheet; the palette appears only twice, as a pale wash in the
// flower's heart and in the red seal stamped in the corner.
//
// ---- audio ------------------------------------------------------------------
//
// The low-mid band (sndSpectrum(0.41)) is how WET the ink is: it widens the bleed
// past every stroke's edge and the ragged, paper-fibre fringe with it, so a
// warm, full passage soaks the flower and a thin one leaves it crisp.
// Spectral flux (sndLevel(SND_FLUX)) flicks splatter drops off the brush around the
// flower, their places re-seeded by SND_ONSET_COUNT so every onset throws a new
// pattern. The flower itself turns slowly on uDrift. Brightness never
// follows the music: this is paper, and paper is always the same white.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the flower's slow sway. Complexity: uMorph adds a
// fourth ring of short, tight strokes in the heart. Chaos: uTurbulence +
// uWarp make the brush rougher (a raggeder bleed, wilder petal lengths);
// uTwist hooks every quill harder. Energy: uIntensity scales the bleed and
// the splatter. Colour: pal() tints the ink, the heart wash and the seal;
// the paper is a fixed warm white. Space: uZoom via view().

#define INK_FLOWER vec2(0.22, 0.14)

float gBleed;
float gChaos;

// Wet ink of one stroke from its signed distance; ragged at the bleed edge,
// darker where it pooled. Returns coverage 0..1.
float inkStroke(float d, vec2 fibre, float tone) {
    float n = motionNoise(fibre * 70.0) - 0.5;
    float dn = d + (gBleed * 1.4 + 0.012 * gChaos) * n;
    float body = smoothstep(gBleed, -gBleed * 0.3 - 0.002, dn);
    float pool = exp(-abs(dn) / (gBleed * 0.5 + 0.0015)) * smoothstep(gBleed, 0.0, dn);
    return clamp(body * tone + pool * 0.35, 0.0, 1.0);
}

void main() {
    vec2 uv = view();
    float t = uTime * uSpeed;
    gChaos = clamp(uTurbulence + uWarp, 0.0, 2.0);
    float lowmid = clamp(sndSpectrum(0.41) * uIntensity, 0.0, 1.5);
    float flux = clamp(sndLevel(SND_FLUX) * uIntensity, 0.0, 1.5);
    float rings = 3.0 + step(0.5, uMorph);
    gBleed = 0.003 + 0.02 * lowmid;
    float hue = 0.1 + 0.02 * sin(t * 0.02);

    // Paper: warm, with fibre and slow blotches.
    vec3 paper = vec3(0.93, 0.89, 0.81);
    paper *= 0.96 + 0.04 * motionFbm(uv * 3.0) + 0.03 * (motionNoise(uv * vec2(180.0, 25.0)) - 0.5);
    paper *= 1.0 - 0.18 * smoothstep(0.8, 2.0, length(uv));

    float ink = 0.0;
    vec2 fp = chrysTurn(uDrift * 0.08 + 0.15 * sin(t * 0.03)) * (uv - INK_FLOWER);

    // Stem: a bowed stroke from the flower down off the sheet.
    float sy = INK_FLOWER.y - uv.y;
    if (sy > 0.0) {
        float sx = INK_FLOWER.x - 0.32 * sy * sy - 0.06 * sy;
        float sw = 0.012 + 0.004 * sy;
        ink = 1.0 - (1.0 - ink) * (1.0 - inkStroke(abs(uv.x - sx) - sw, uv * 1.3, 0.8));
    }
    // Two grey-wash leaves off the stem.
    for (int i = 0; i < 2; i++) {
        float k = float(i);
        float ly = INK_FLOWER.y - 0.42 - 0.3 * k;
        float lsy = INK_FLOWER.y - ly;
        vec2 base = vec2(INK_FLOWER.x - 0.32 * lsy * lsy - 0.06 * lsy, ly);
        float a = k < 0.5 ? -1.05 : 1.15;
        vec2 lq = chrysTurn(-a) * (uv - base);
        vec4 leaf = chrysLeaf(lq, 0.36 - 0.06 * k, 0.55, 0.15, 0.8);
        float vein = smoothstep(0.004, 0.0, abs(leaf.z * 0.1)) * step(leaf.x, 0.0);
        float tone = 0.32 + 0.25 * leaf.y + 0.3 * vein;
        ink = 1.0 - (1.0 - ink) * (1.0 - inkStroke(leaf.x, uv + k, tone));
    }

    // The flower: three rings, outer to inner, each petal its own length.
    for (int i = 0; i < 4; i++) {
        float k = float(i);
        if (k >= rings) break;
        float count = floor(22.0 - 5.5 * k);
        float r0 = max(0.08 - 0.025 * k, 0.012);
        float len = max(0.55 - 0.18 * k, 0.1);
        float width = min(0.16 + 0.1 * k, 0.45);
        float hook = (k < 0.5 ? 2.6 : (k < 1.5 ? -3.5 : 0.0)) * (1.0 + uTwist);
        vec2 q = chrysHook(chrysTurn(chrysRingOffset(k, count)) * fp, hook, r0);
        float id = mod(chrysPetal(q, count, r0, len, width, 0.0, 0.3, 0.9).w, count);
        float lenScale = 0.62 + 0.55 * chrysHash1(id + 13.0 * k) * (1.0 + 0.6 * gChaos);
        vec4 pet = chrysPetal(q, count, r0, len * lenScale, width, 0.0, mix(0.2, 0.85, min(k * 0.5, 1.0)), 0.95);
        float u = clamp(pet.y, 0.0, 1.0);
        // Dry brush: the stroke breaks into streaks toward the tip.
        float dry = motionNoise(vec2(pet.z * 3.5 + id * 7.0, u * 2.0));
        float tone = mix(0.95, 0.5, u) * mix(1.0, smoothstep(0.25, 0.6, dry), smoothstep(0.45, 1.0, u));
        ink = 1.0 - (1.0 - ink) * (1.0 - inkStroke(pet.x, q + k * 3.0, tone));
    }

    // Splatter: drops flicked off the brush, re-seeded on every onset.
    vec2 sp = (uv - INK_FLOWER) * 9.0;
    vec2 sc = floor(sp);
    float seed = chrysHash(sc + floor(SND_ONSET_COUNT) * vec2(0.37, 0.71));
    vec2 sCentre = sc + 0.5 + 0.35 * (vec2(seed, fract(seed * 13.7)) - 0.5);
    float sr = length(sc + 0.5) / 9.0;
    float dropR = 0.16 * flux * step(0.72, seed) * smoothstep(1.3, 0.5, sr) * smoothstep(0.25, 0.45, sr);
    ink = 1.0 - (1.0 - ink) * (1.0 - inkStroke((length(sp - sCentre) - dropR) / 9.0, uv * 2.0, 0.9) * step(0.001, dropR));

    vec3 inkCol = mix(vec3(0.05, 0.055, 0.07), pal(hue + 0.6) * 0.15, 0.2);
    vec3 col = mix(paper, inkCol, ink);

    // The colour: a pale wash in the heart of the flower...
    float heart = smoothstep(0.09, 0.02, length(fp) + 0.01 * motionNoise(fp * 40.0));
    col = mix(col, col * mix(vec3(1.0), pal(hue) * 1.2, 0.55), heart);
    // ...and the seal.
    vec2 sealC = vec2(-0.55 * uResolution.x / uResolution.y + 0.12, -0.72);
    vec2 sq = (uv - sealC) / 0.075;
    float box = max(abs(sq.x), abs(sq.y));
    float carve = step(0.45, chrysHash(floor(sq * 3.0 + 7.0))) * step(box, 0.78);
    float sealInk = smoothstep(1.0, 0.97, box) * (1.0 - carve * 0.85) * (0.85 + 0.15 * motionNoise(sq * 12.0));
    vec3 sealCol = mix(vec3(0.72, 0.12, 0.08), pal(hue + 0.95), 0.25);
    col = mix(col, sealCol, sealInk);

    fragColor = vec4(grade(col), 1.0);
}
