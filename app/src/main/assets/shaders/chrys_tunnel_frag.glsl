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
//#include lib_chrys

// Chrysanthemum Tunnel: flying down the inside of a flower turned inside
// out. The tunnel wall is shingled with petals like an artichoke heart or a
// pine cone: rows of petal scales, each row offset half a column from the
// last, rooted at their far end with their tips pointing back toward the
// viewer, the nearer row lying over the tips of the row beyond. At the far
// end of the tunnel a small flower glows at the vanishing point.
//
// Not a march: the wall is a cylinder, so each ray finds it with one
// quadratic, and the scales are a 2D pattern on the unrolled wall (two petal
// evaluations, the row and the row beyond it). Flown with camFly; the scale
// rows repeat every 32 world units, which divides the rig's 8192 travel wrap,
// so the wrap never shows.
//
// ---- audio ------------------------------------------------------------------
//
// A section change is hidden inside a BLOOM. On SND_HELD_HARD_CUT the flower at the
// vanishing point opens out until its petals fill the whole view, then
// closes again (chrysBump over the 1.5 s envelope, so it starts from rest).
// The tunnel's palette steps to a new hash of SND_CUT_COUNT exactly when the
// envelope crosses its midpoint - the moment the bloom covers the screen - so
// the old tunnel goes in behind the petals and a new one comes out. The bloom
// is drawn at the scene's own light level; it is a change of shape and hue,
// not a flash. The sub band (sndSpectrum(0.10)) lifts the scale tips off the wall,
// deepening the shadow each casts on the row below. Brightness follows SND_LEVEL
// only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the far flower's turn and the hue drift (travel is the
// rig's). Complexity: uMorph adds columns of scales round the tunnel (14 up
// to 22). Chaos: uTurbulence + uWarp give the scales uneven lengths and torn
// edges; uTwist winds the rows into a helix. Energy: uIntensity scales the
// section bloom and the sub lift. Colour: pal() for the scales, the far
// flower and the fog. Camera: the rig, via camFly.

#define TU_RADIUS 3.4
#define TU_ROW 0.5

float gT;
float gChaos;

// The scale pattern at wall point (column coord, row coord). Returns colour.
vec3 tunnelScales(vec2 w, float cols, float hue, float lift, float aa, out float shade) {
    vec3 col = pal(hue + 0.5) * 0.03;
    shade = 0.0;
    float row = floor(w.y);
    // The row beyond first (its tips reach into this row), then this row on top.
    for (int s = 0; s < 2; s++) {
        float rr = row + (s == 0 ? 1.0 : 0.0);
        float off = 0.5 * mod(rr, 2.0);
        float cx = floor(w.x - off) + 0.5 + off;
        float id = mod(rr, 64.0) * 37.0 + mod(floor(w.x - off), cols);
        float h = chrysHash1(id);
        // Petal frame: base at the top of row rr, pointing toward lower rows.
        vec2 lq = vec2(w.x - cx, (rr + 1.0) - w.y);
        float len = 1.6 * (1.0 + 0.2 * gChaos * chrysWobble(id, gT));
        vec4 pet = chrysLeaf(lq, len, 0.33, 0.35, 0.6);
        pet.x += chrysFray(lq * 0.25, gChaos) * 3.0;
        // Shadow cast down the wall, longer when the tips lift.
        col *= 1.0 - 0.6 * smoothstep(0.1 + 0.4 * lift, -0.02, pet.x + 0.15 * lift * clamp(pet.y, 0.0, 1.0));
        float m = smoothstep(aa, -aa, pet.x);
        float u = clamp(pet.y, 0.0, 1.0);
        vec3 pc = mix(pal(hue + 0.06 * h) * 0.3, mix(pal(hue + 0.08 + 0.06 * h), vec3(1.0), 0.2), u);
        pc *= chrysPetalShade(pet, 0.35);
        col = mix(col, pc, m);
        shade = mix(shade, u * lift, m);
    }
    return col;
}

void main() {
    vec2 uv = view();
    gT = uTime * uSpeed;
    gChaos = clamp(uTurbulence + uWarp, 0.0, 2.0);
    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float sect = clamp(SND_HELD_HARD_CUT, 0.0, 1.0);
    float lift = clamp(sndSpectrum(0.10) * uIntensity, 0.0, 1.5);
    float cols = 14.0 + 2.0 * floor(4.0 * clamp(uMorph, 0.0, 1.0) + 0.5);

    // The palette steps at the bloom's peak, hidden behind its petals.
    float sectionIdx = SND_CUT_COUNT - (sect > 0.5 ? 1.0 : 0.0);
    float hue = chrysHash(vec2(sectionIdx * 0.73, 4.0)) + gT * 0.002;
    float nextHue = chrysHash(vec2(SND_CUT_COUNT * 0.73, 4.0)) + gT * 0.002;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);

    // The wall: the far root of |ro.xy + t rd.xy| = R (the eye is inside).
    float qa = max(dot(rd.xy, rd.xy), 1e-5);
    float qb = dot(ro.xy, rd.xy);
    float qc = dot(ro.xy, ro.xy) - TU_RADIUS * TU_RADIUS;
    float t = (-qb + sqrt(max(qb * qb - qa * qc, 0.0))) / qa;
    vec3 p = ro + rd * t;
    float ang = atan(p.y, p.x);
    vec2 w = vec2(ang / CHRYS_TAU * cols + uTwist * p.z * 0.3, p.z / TU_ROW);
    // Pixel footprint on the wall in cell units, so far scales blur, not shimmer.
    float aa = clamp(t * 2.5 / uResolution.y * cols / (CHRYS_TAU * TU_RADIUS) / max(abs(dot(rd, -normalize(vec3(p.xy, 0.0)))), 0.08), 0.004, 0.5);
    float lifted;
    vec3 col = tunnelScales(w, cols, hue, lift, aa, lifted);
    // Headlamp from the eye, the scale tips tilting toward it as they lift.
    vec3 n = normalize(vec3(-p.xy / TU_RADIUS, -0.6 * lifted));
    float diff = max(dot(n, -rd), 0.0);
    col *= 0.35 + 0.85 * diff;

    // Fog toward the far end, which glows.
    vec3 fogCol = pal(hue + 0.12) * 0.12;
    col = mix(col, fogCol, 1.0 - exp(-t * 0.05));
    float ahead = pow(max(rd.z, 0.0), 24.0);
    col += pal(hue + 0.1) * 0.25 * ahead;

    // The flower at the vanishing point, and the bloom that hides the change.
    if (rd.z > 0.05) {
        vec2 vq = rd.xy / rd.z;
        float size = 0.07 + 3.6 * uIntensity * chrysBump(sect);
        float bh = mix(hue, nextHue, smoothstep(0.55, 0.45, sect));
        vec2 fq = chrysTurn(gT * 0.1) * vq;
        // Behind the petals, a disc that only exists during the bloom, so the
        // gaps between petals never show the palette stepping.
        float cover = smoothstep(0.15, 0.45, chrysBump(sect));
        col = mix(col, pal(bh + 0.04) * 0.14, cover * smoothstep(size * 0.8, size * 0.55, length(vq)));
        for (int i = 0; i < 2; i++) {
            float k = float(i);
            float count = 9.0 + 4.0 * k;
            vec4 pet = chrysPetal(chrysTurn(chrysRingOffset(k, count)) * fq, count, size * 0.12, size * (1.0 - 0.3 * k), 0.5, 0.4 / max(size, 0.2), 0.4, 0.5);
            float m = smoothstep(0.006 * (1.0 + size), -0.006 * (1.0 + size), pet.x);
            vec3 pc = mix(pal(bh) * 0.18, pal(bh + 0.08) * 0.42, clamp(pet.y, 0.0, 1.0)) * chrysPetalShade(pet, 0.4);
            col = mix(col, pc, m);
        }
        col = mix(col, pal(bh + 0.55) * 0.45, chrysCore(length(vq), size * 0.1));
    }

    col *= 0.85 + 0.15 * swell;
    col *= 0.75 + 0.25 * smoothstep(2.2, 0.3, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
