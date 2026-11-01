// lib_bloom: the note blooms (core/viz/NoteResponse, drawn by NoteBloomPass). Four looks, picked per style
// (viz/Catalogue BloomLook, SceneParams::bloomLook): the paradoxical fractal flower below, then a soft
// glow, a ripple ring and a burst of sparks at the end of the file.
//
// One flower per sounding note, drawn in its own unit disc (radius 1 = the bloom's radius). What makes it
// paradoxical, and what each piece costs per pixel inside the disc:
//
//  - Petals that hold the flower again. Each level folds the point into one petal's sector (a mod, no
//    loop over petals), measures the petal (an ellipse from the heart outward), then maps the petal's own
//    ellipse back onto the unit disc and repeats: the next level is the whole flower squeezed into that
//    petal, its petals hold it again, and so on to the bloom's depth (2..4). Clipped to the parent petal,
//    so each level reads as the flower inside its own petal, not a pattern on top of it.
//  - Escher's nested rotation. Every level turns the other way and a little faster than the one around
//    it, so the eye follows a petal inward and finds the flower turning against itself.
//  - A heart that never ends: a logarithmic spiral rosette, the same petals winding inward forever (one
//    cos of log r and angle: the Droste spiral without the second lookup).
//  - Layered translucent iridescent neon: a faint back whorl half a petal round in the previous note's
//    colour, then the front petals, each a thin fill, a hot rim and filigree (contour rings and a vein),
//    the hue swept by a thin-film term (distance to the edge, radius, level) between the note's colour
//    and the last notes' colours (uNoteHues), so one flower is already many colours.
//
// Cost inside a disc: one back whorl, the heart, and one whorl per level (2..4), each an atan, a sincos
// and a handful of exp/pow: roughly 250 ALU instructions a pixel at depth 4, nothing outside the disc.
//
// Original code for this app. It never reads the scene below: NoteBloomPass screens it over the finished
// frame, so it lays over every style (shader, MilkDrop, fluid) the same way, live and in an export.

const float BLOOM_TAU = 6.2831853;

vec3 bloomHsv(float h, float s, float v) {
    vec3 k = clamp(abs(fract(h + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
    return v * mix(vec3(1.0), k, s);
}

// Neon: saturated, with the white-hot core a glow tube has where it is brightest.
vec3 bloomNeon(float h, float heat) {
    vec3 c = bloomHsv(h, 0.88, 1.0);
    return mix(c, vec3(1.0), 0.35 * heat * heat);
}

mat2 bloomRot(float a) {
    float c = cos(a);
    float s = sin(a);
    return mat2(c, s, -s, c);
}

// A whorl of `petals` ellipses from the heart outward, the point folded into the nearest one's sector
// (a mod, no loop over petals). Returns the signed distance to that petal's edge (in disc units) and, in
// e, the point in the petal's own ellipse (|e| = 1 on the edge), which is where the next level lives.
float bloomPetal(vec2 p, float turn, float petals, float centre, float halfLen, float widthK, out vec2 e, out float which,
                 out float fa) {
    float r = length(p);
    float a = atan(p.y, p.x) + turn;
    float sector = BLOOM_TAU / petals;
    which = floor(a / sector + 0.5);
    fa = a - which * sector;
    vec2 fp = r * vec2(cos(fa), sin(fa));
    float halfWid = halfLen * clamp(widthK * sin(0.5 * sector) + 0.12, 0.2, 0.7);
    e = (fp - vec2(centre, 0.0)) / vec2(halfLen, halfWid);
    return (length(e) - 1.0) * halfWid;
}

// One flower. p: the point in the bloom's unit disc. shape = (petals, depth, open, angle);
// look = (hue, seed, tension, hue ring offset). Returns premultiplied colour and coverage.
vec4 bloomFlower(vec2 p, vec4 shape, vec4 look, float hues[6]) {
    float petals = shape.x;
    float depth = shape.y;
    float open = shape.z;
    float angle = shape.w;
    float hue0 = look.x;
    float seed = look.y;
    float tension = look.z;
    int ring = int(look.w);

    vec3 col = vec3(0.0);
    float cover = 0.0;
    float r0 = length(p);
    float a0 = atan(p.y, p.x);

    // The back whorl: a second, larger ring of petals half a petal round, faint and in the previous note's
    // colour, so the flower is layered glass rather than one cut-out.
    {
        vec2 e;
        float which;
        float fa;
        float d = bloomPetal(p, -0.6 * angle + 3.14159 / petals, petals, mix(0.3, 0.58, open), mix(0.2, 0.34, open), 1.5, e,
                             which, fa);
        float inside = smoothstep(0.015, -0.015, d);
        float rim = exp(-abs(d) * 55.0);
        float lace = pow(abs(cos(e.y * 9.0 + e.x * 3.0 + seed * 4.0)), 18.0) * inside;
        float h = hues[ring] + 0.18 * cos(BLOOM_TAU * (d * 6.0 + r0 * 1.3 + which * 0.21));
        float k = open * (inside * 0.1 + rim * 0.35 + lace * 0.18);
        col += bloomNeon(h, rim) * k;
        cover += k * 0.5;
    }

    // The heart: a log-spiral rosette, the petals winding inward forever.
    {
        float lr = log(max(r0, 1e-3));
        float spiral = pow(abs(cos(0.5 * petals * a0 + 0.77 * lr * petals - 2.0 * angle)), 6.0);
        float heart = smoothstep(0.26, 0.04, r0) * smoothstep(0.0, 0.03, r0) * open;
        float h = hue0 + 0.12 * lr + 0.5 * hues[ring];
        float k = heart * (0.25 + 0.75 * spiral);
        col += bloomNeon(h, spiral) * k * 0.8;
        cover += k * 0.6 * (1.0 - cover);
    }

    float mask = 1.0;
    for (int level = 0; level < 4; level++) {
        float fl = float(level);
        if (fl >= depth) break;
        // Each level appears as the flower opens: the inner flowers unfurl after the outer petals.
        float reveal = smoothstep(fl * 0.2, fl * 0.2 + 0.35, open);
        if (reveal <= 0.0 || mask <= 0.004) break;
        float r = length(p);
        // Escher: every level turns against the one around it, a little faster.
        float dir = (level == 1 || level == 3) ? -1.35 : 1.0;
        vec2 e;
        float which;
        float fa;
        float d = bloomPetal(p, angle * dir * (1.0 + 0.3 * fl), petals, mix(0.22, 0.52, open), mix(0.16, 0.36, open), 1.15, e,
                             which, fa);
        float el = length(e);
        // Tension frays the edge; resolution smooths it back.
        d += tension * 0.018 * sin(fa * 41.0 + r * 57.0 + seed * 31.0 + fl * 2.1);
        float inside = smoothstep(0.012, -0.012, d);
        float rim = exp(-abs(d) * 70.0);
        // Filigree: contour rings inside the petal and a central vein.
        float rings = pow(abs(cos(el * 15.0 - fl * 1.7 + seed * 6.0)), 28.0) * inside;
        float vein = exp(-abs(e.y) * 22.0) * smoothstep(1.0, 0.2, el) * inside;
        // Iridescence: a thin-film sweep of the hue with the distance to the edge, the radius and the level,
        // between the note's colour and the colour of an earlier note.
        int hi = ring + level + 1;
        hi = hi - 6 * (hi / 6);
        float hueL = hue0 + fract(hues[hi] - hue0 + 0.5) - 0.5;
        float film = 0.16 * cos(BLOOM_TAU * (d * 9.0 + r * 1.7 + fl * 0.29 + which * 0.13 + seed));
        float h = mix(hue0, hueL, 0.25 + 0.25 * fl) + film;
        float layer = mask * reveal * (inside * 0.2 + rim * 0.85 + rings * 0.4 + vein * 0.3);
        // Deeper levels are finer and dimmer, so the eye reads depth rather than clutter.
        layer *= 1.0 / (1.0 + 0.35 * fl);
        col += bloomNeon(h, rim + 0.5 * vein) * layer;
        cover += layer * (1.0 - cover);
        // Into the petal: the whole flower again, squeezed into this petal's ellipse.
        mask *= inside;
        p = bloomRot(0.5 * BLOOM_TAU / petals + seed) * (e * 1.04);
    }

    // Keep the flower inside its disc.
    float edge = smoothstep(1.0, 0.86, r0);
    return vec4(col * edge, clamp(cover * edge, 0.0, 1.0));
}

// ---- The other three looks (SceneParams::bloomLook, viz/Catalogue BloomLook) ---------------------------
// Same arguments and result as bloomFlower, so the pass picks one per frame (uBloomLook) and nothing else
// changes. Each keeps the flower's language: the note's hue swept toward the last notes' colours, a hot
// neon rim, tension fraying the edges, the petal count as the look's own symmetry. All cheaper than the
// flower: roughly 60 (glow), 90 (ripple) and 70 (burst) ALU a pixel inside the disc.

float bloomHash(float x) { return fract(sin(x * 91.3458 + 17.17) * 43758.5453); }

// A soft glow: an orb of light with a halo ring and faint star rays, one per petal, that turn slowly.
// For gas, glass, branches and soft styles, where a hard flower would cut the picture.
vec4 bloomGlow(vec2 p, vec4 shape, vec4 look, float hues[6]) {
    float petals = shape.x;
    float open = shape.z;
    float angle = shape.w;
    float hue0 = look.x;
    float tension = look.z;
    int ring = int(look.w);
    float r = length(p);
    float a = atan(p.y, p.x);
    float core = exp(-r * r * mix(30.0, 9.0, open));
    float haloR = mix(0.2, 0.62, open) + tension * 0.02 * sin(a * 17.0 + look.y * 40.0);
    float halo = exp(-pow((r - haloR) * 16.0, 2.0)) * open;
    float rays = pow(abs(cos(0.5 * petals * a + angle)), 24.0) * exp(-r * 3.2) * open;
    float h = mix(hue0, hues[ring], smoothstep(0.0, 0.7, r)) + 0.08 * cos(BLOOM_TAU * (r * 1.5 + look.y));
    vec3 col = bloomNeon(h, core) * core * 0.9 + bloomNeon(h + 0.06, halo) * halo * 0.6 + bloomNeon(hue0, rays) * rays * 0.45;
    float edge = smoothstep(1.0, 0.8, r);
    return vec4(col * edge, clamp((core + halo * 0.5 + rays * 0.3) * edge, 0.0, 1.0));
}

// A ripple: rings spreading from the note like a drop on still water, three of them a step apart, each in
// an earlier note's colour, their brightness rippled by the petal count so they interfere like waves.
vec4 bloomRipple(vec2 p, vec4 shape, vec4 look, float hues[6]) {
    float petals = shape.x;
    float open = shape.z;
    float angle = shape.w;
    float hue0 = look.x;
    float seed = look.y;
    float tension = look.z;
    int ring = int(look.w);
    float r = length(p);
    float a = atan(p.y, p.x);
    vec3 col = vec3(0.0);
    float cover = 0.0;
    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float ri = open * 0.95 - fi * 0.2;
        if (ri <= 0.02) break;
        ri += tension * 0.025 * sin(a * 23.0 + fi * 2.0 + seed * 30.0);
        float width = mix(26.0, 60.0, fi / 2.0);
        float band = exp(-pow((r - ri) * width, 2.0));
        // Interference: the ring brightens where the note's symmetry crosses it.
        band *= 0.7 + 0.3 * cos(petals * a + angle * (fi + 1.0) + seed * 6.0);
        // Each ring fades as it spreads, as a wave on water loses height.
        band *= (1.0 - ri) * (1.0 - 0.25 * fi);
        int hi = ring + i;
        hi = hi - 6 * (hi / 6);
        float h = mix(hue0, hues[hi], 0.5 * fi) + 0.06 * cos(BLOOM_TAU * (r * 3.0 + seed));
        col += bloomNeon(h, band) * band;
        cover += band * (1.0 - cover);
    }
    // A faint caustic sheen inside the front ring.
    float inner = smoothstep(open * 0.95, open * 0.6, r) * open * (1.0 - open);
    float caustic = pow(abs(cos(r * 18.0 - angle * 3.0) * cos(a * petals * 0.5 + r * 6.0)), 6.0) * inner;
    col += bloomNeon(hue0 + 0.1, caustic) * caustic * 0.35;
    float edge = smoothstep(1.0, 0.9, r);
    return vec4(col * edge, clamp((cover + caustic * 0.2) * edge, 0.0, 1.0));
}

// A burst: sparks thrown out from the note, three per petal, each a short streak with a hot head and a
// cooling tail, at its own angle and speed; a bright heart flashes at the start and gives way to them.
vec4 bloomBurst(vec2 p, vec4 shape, vec4 look, float hues[6]) {
    float petals = shape.x;
    float open = shape.z;
    float angle = shape.w;
    float hue0 = look.x;
    float seed = look.y;
    float tension = look.z;
    int ring = int(look.w);
    float r = length(p);
    float a = atan(p.y, p.x) + 0.3 * angle;
    float sparks = petals * 3.0;
    float sector = BLOOM_TAU / sparks;
    float idx = floor(a / sector);
    float jitter = bloomHash(idx + seed * 13.0);
    float speed = 0.55 + 0.45 * bloomHash(idx * 1.7 + seed * 7.0);
    float sa = (idx + 0.5 + 0.6 * (jitter - 0.5)) * sector;
    // Distance across the spark's ray, and along it from the head back to the tail.
    vec2 q = r * vec2(cos(a - sa), sin(a - sa));
    float head = open * speed * 0.95;
    float len = 0.12 + 0.25 * open + tension * 0.1;
    float along = (head - q.x) / max(len, 1e-3);
    float across = abs(q.y) + tension * 0.004 * sin(q.x * 90.0 + seed * 20.0);
    float streak = smoothstep(1.0, 0.0, along) * step(0.0, along) * exp(-across * mix(160.0, 90.0, open));
    streak *= 1.0 - 0.6 * open;
    float tail = clamp(along, 0.0, 1.0);
    int hi = ring + int(mod(idx, 6.0));
    hi = hi - 6 * (hi / 6);
    float h = mix(hue0, hues[hi], 0.4 * tail + 0.3 * jitter);
    vec3 col = bloomNeon(h, (1.0 - tail) * streak) * streak;
    // The heart: bright at the strike, gone as the sparks fly.
    float heart = exp(-r * r * 60.0) * (1.0 - open);
    col += bloomNeon(hue0, heart) * heart;
    float edge = smoothstep(1.0, 0.88, r);
    return vec4(col * edge, clamp((streak + heart) * edge, 0.0, 1.0));
}

// One bloom in the frame's look: 1 flower (and anything unset), 2 glow, 3 ripple, 4 burst.
vec4 bloomDraw(int lookId, vec2 p, vec4 shape, vec4 look, float hues[6]) {
    if (lookId == 2) return bloomGlow(p, shape, look, hues);
    if (lookId == 3) return bloomRipple(p, shape, look, hues);
    if (lookId == 4) return bloomBurst(p, shape, look, hues);
    return bloomFlower(p, shape, look, hues);
}

// The disc turned in 3D: a screen point mapped back onto the flower's tilted plane (radians about the
// screen's x and y axes), with a touch of perspective so the near edge reads larger than the far one.
vec2 bloomUntilt(vec2 p, vec2 tilt) {
    vec2 c = max(cos(tilt), vec2(0.35));
    vec2 q = vec2(p.x / c.y, p.y / c.x);
    float w = 1.0 + 0.3 * (q.x * sin(tilt.y) + q.y * sin(tilt.x));
    // Never below 0.8, so the tilted disc never reaches past 1.25 radii on screen (the pass's reach).
    return q * clamp(w, 0.8, 1.4);
}
