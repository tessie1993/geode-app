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
//#include lib_glass

// Glass Lens: a handful of round magnifying lenses drifting slowly over a
// printed sheet - a halftone field of dots whose sizes swell in a slow wave,
// over fine diagonal hatching. The sheet itself is out of focus, and so are
// most of the lenses; one lens at a time is pin-sharp, magnifying the dots
// beneath it to crisp discs. Each lens is a thick convex profile: the centre
// magnifies about two and a half times and the rim pulls the image round in a
// barrel; at the rim the three channels are read at slightly different
// magnifications, so its edge is fringed with colour (the family's one
// allowed multi-tap, three reads of the sheet). Each lens throws a soft
// shadow down-right with a bright point of focused light at its heart.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets swap the focus: every onset (uHitCount) picks a lens, and as the
// onset envelope (uHits.w) falls the old lens blurs out and the new one comes
// sharp - a rack focus on the beat, eased, never a cut. The key (uKeyHue,
// uKeyStrength) drifts the ink's palette. Brightness follows uSwell.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the lenses' drift and the halftone wave. Complexity
// (uMorph) adds up to two more lenses. Chaos (uWarp, uTurbulence) wobbles each
// lens's profile so the magnified image swims. Energy (uIntensity) deepens the
// magnification. Colour: ink and paper from pal(). Space is the rig's flat
// projection.

#define LN_MAX 7

/** The printed sheet, softened by `blur` (in dot-cell units). */
vec3 sheet(vec2 x, float blur, float hue, float t) {
    vec2 g = x * 5.0;
    vec2 cell = floor(g);
    vec2 f = fract(g) - 0.5;
    float wave = 0.5 + 0.5 * sin(dot(cell, vec2(0.41, 0.27)) + t * 0.5);
    float soft = 0.015 + blur;
    float dotMask = 1.0 - smoothstep(-soft, soft, length(f) - (0.1 + 0.28 * wave));
    float hatch = 0.5 + 0.5 * cos((x.x - x.y) * 22.0);
    hatch = mix(hatch, 0.5, clamp(blur * 5.0, 0.0, 1.0));
    vec3 paper = (mix(vec3(0.9, 0.88, 0.82), pal(hue + 0.5) * 0.5 + 0.4, 0.25)) * (0.16 + 0.05 * hatch);
    vec3 ink = pal(hue + 0.03 * (cell.x + cell.y) + 0.2 * wave);
    return mix(paper, ink * 0.85, dotMask);
}

void main() {
    vec2 uv = view();
    float t = glassTime();
    float energy = glassEnergy();
    float swell = clamp(uSwell, 0.0, 1.5);
    float hue = 0.05 * sin(t * 0.017) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    float chaos = glassChaos();

    int count = 5 + int(floor(glassComplexity() * 2.0 + 0.5));
    float fc = float(count);
    float pickNew = floor(glassHash1(uHitCount) * fc);
    float pickOld = floor(glassHash1(uHitCount - 1.0) * fc);
    float e = clamp(uHits.w, 0.0, 1.0);
    e = e * e * (3.0 - 2.0 * e);

    // Find the top lens over this pixel; gather every lens's shadow and focus spot.
    float shade = 1.0;
    float spot = 0.0;
    float hitR = 2.0;
    vec2 hitQ = vec2(0.0);
    vec2 hitC = vec2(0.0);
    float hitFocus = 0.0;
    float hitSeed = 0.0;
    for (int i = 0; i < LN_MAX; i++) {
        if (i >= count) break;
        float fi = float(i);
        float h = glassHash1(fi * 3.7 + 1.3);
        vec2 c = vec2(1.25 * sin(t * (0.045 + 0.02 * h) + fi * 2.13),
                      0.62 * sin(t * (0.037 + 0.015 * h) + fi * 1.37 + 0.6));
        float rad = 0.25 + 0.14 * glassHash1(fi * 5.1 + 2.2);
        vec2 sq = (uv - c - vec2(0.07, -0.09)) / rad;
        float sr2 = dot(sq, sq);
        shade *= 1.0 - 0.38 * (1.0 - smoothstep(0.75, 1.3, sr2));
        spot += exp(-sr2 * 28.0) * 0.5;
        vec2 q = (uv - c) / rad;
        float r = length(q);
        if (r < 1.0) {
            hitR = r;
            hitQ = q;
            hitC = c;
            hitSeed = fi;
            hitFocus = mix(fi == pickNew ? 1.0 : 0.0, fi == pickOld ? 1.0 : 0.0, e);
        }
    }

    vec3 col = sheet(uv, 0.2, hue, t) * shade + pal(hue + 0.1) * spot * 0.25;

    if (hitR < 1.0) {
        float r2 = hitR * hitR;
        float wob = chaos * 0.06 * sin(hitQ.x * 5.0 + t * 0.8 + hitSeed) * sin(hitQ.y * 4.0 - t * 0.6);
        float mag = (0.42 - 0.08 * (energy - 1.0)) + 0.32 * r2 + wob;
        float blur = mix(0.22, 0.008, hitFocus);
        vec3 sc = mag + vec3(-1.0, 0.0, 1.0) * 0.035 * r2;
        vec3 lc = vec3(sheet(hitC + (uv - hitC) * sc.r, blur, hue, t).r,
                       sheet(hitC + (uv - hitC) * sc.g, blur, hue, t).g,
                       sheet(hitC + (uv - hitC) * sc.b, blur, hue, t).b);
        lc *= 1.1 + 0.1 * hitFocus;
        float rim = smoothstep(0.8, 1.0, hitR);
        lc = mix(lc, lc * 0.45 + pal(hue + 0.55) * 0.06, rim * 0.6);
        lc += vec3(1.0, 0.97, 0.92) * smoothstep(0.92, 0.975, hitR) * (1.0 - smoothstep(0.975, 1.0, hitR)) * (0.35 + 0.2 * hitFocus);
        float hl = smoothstep(0.45, 0.9, dot(hitQ, vec2(-0.6, 0.8))) * (1.0 - smoothstep(0.86, 1.0, hitR));
        lc += vec3(0.95, 0.97, 1.0) * hl * 0.2;
        col = mix(col, lc, (1.0 - smoothstep(0.985, 1.0, hitR)));
    }

    col *= 0.85 + 0.15 * swell;
    col *= 0.8 + 0.2 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
