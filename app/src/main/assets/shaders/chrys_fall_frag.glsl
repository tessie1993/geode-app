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
//#include lib_sdf3
//#include lib_chrys

// Chrysanthemum Fall: a flight through a slow fall of loose petals in dusk
// air, toward a great out-of-focus bloom. The flat style stacked four layers
// of petal cells to fake depth; now the air is a real volume. Space is cut
// into 2-unit cubes and each cube holds at most one petal: a thin, cupped
// slab whose outline is the family's petal (chrysLeaf), tumbling in 3D. It
// turns in its plane, pitches, and rolls about its own length, so seen edge
// on it narrows to a sliver and then opens again, showing its paler
// underside on the way round. The far petals sink into the dusk haze, and
// the bloom behind hangs in the sky at infinity, drawn with the same huge
// edge softness as before, as a lens would leave it. Petals between the eye
// and the bloom are lit through from behind: they glow where they cross it.
//
// ---- camera -----------------------------------------------------------------
//
// camFly: the eye rides the rig's path down +z through the falling air; the
// petals fall across the flight. The eye's z is rebased by PF_ZWRAP (256 =
// 128 cells); every z term (the cell hash and the wind's period of 64) has a
// period dividing it, and 256 divides the rig's 8192 travel wrap, so neither
// the rebase nor the wrap shows. The bloom is fixed in world direction, so it
// stays put at infinity while the camera banks and looks about.
//
// ---- audio ------------------------------------------------------------------
//
// No sound turns a petal: the hi-hat's flip is gone (owner 2026-11-12, no
// style spins to the music), so the petals tumble on time alone. The air band (sndSpectrum(0.93))
// runs a thin shimmer of light along the petal rims (an edge highlight, never
// a full-frame lift). The fall rides uFlowPhase, each column of cells at its
// own rate, so loudness sets the speed and never the direction. Brightness
// follows SND_ENERGY_TREND only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed is already in uTime (tumbling, shimmer travel). Complexity: uMorph
// fills more cells, up to roughly twice the petals. Chaos: uTurbulence +
// uWarp blow a gustier wind (a wider sway of the whole fall, a per-petal
// gust) and tear the petal edges (chrysFray); uTwist curls every petal along
// its length. Energy: uIntensity scales the air shimmer.
// Colour: pal() for petals, bloom and sky. Space: uZoom via view().
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE was the hat
// flip and is spent nowhere now, k1 TEXTURE the rim shimmer, k2 FLOW the wind sway, k3 TONE the hue, k4
// BUILD the bloom and its light through the petals, k5 SPACE the haze (more
// space, thinner air).
//
// ---- why the distance estimate is safe --------------------------------------
//
// Each cell measures only its own petal, and the result is clamped to the
// cell's walls (+0.02 to step across), so the march never jumps a neighbour.
// That holds because every petal stays 0.02 inside its cell: it fits a 0.7
// sphere about its centre (half-length 0.55, half-width 0.31, cup and curl at
// most 0.28 out of plane), the centre jitters by 0.1 and gusts by 0.16, and
// 0.7 + 0.1 + 0.16 < 0.98 of the 1.0 half-cell. Outside that sphere the
// petal is not evaluated at all: length - 0.7 is an exact lower bound.
// Inside, the outline (chrysLeaf with cup 0.9, tip 1.0, whose half-width
// slopes at most ~1.1 along the petal) plus the fray has gradient < 1.75, and
// the cup and curl offsets of the slab's height are taken on clamped
// coordinates so its gradient is < 1.25; both are divided out and the slab
// scaled by 0.85 for the two combined. The per-column fall is a pure
// translation of the column (the walls between columns bound it), and the
// wind is an x shift that depends on z with slope < 0.33, so the domain
// stretches by < 1.06, inside the march's 0.9 step factor. A sphere around
// the eye is carved out, so no petal ever cuts across the lens.

#define PF_MAX_STEPS 96
#define PF_FAR 40.0
#define PF_TAU 6.2831853
/** World units per cell edge. */
#define PF_CELL 2.0
/** Rebase period of the eye's z: 128 cells, divides 8192. */
#define PF_ZWRAP 256.0
/** Cells along y before the hash repeats; the fall offset wraps at PF_YCELLS * PF_CELL. */
#define PF_YCELLS 64.0
#define PF_LEN 1.1
#define PF_WIDTH 0.7
#define PF_THICK 0.018
/** Bounding sphere of a petal about its centre (see the header). */
#define PF_BOUND 0.7
/** Fall speed, world units per flow second, before each column's own rate. */
#define PF_FALL 0.55
#define PF_BUBBLE 0.5

/** The bloom's direction in the sky, ahead and a little up and right. */
const vec3 PF_BLOOM = vec3(0.2812, 0.2062, 0.9372);

vec3 gEye;
float gT;

float gChaos;
float gFill;
float gSway;
float gCurl;

/** The style's world at p: its distance, and in `found` what is there (at the last petal measured: a.x its hash, a.y its 2D outline distance, a.z its u, a.w its v, b.x its side (sign = top or underside)). */
float mapAt(vec3 p0, inout ChrysHit found) {
    vec3 p = p0;
    // The wind: the whole fall sways sideways, a slow wave down z (period 64).
    p.x += gSway * sin(gT * 0.31 + p.z * (PF_TAU / 64.0));
    // Each column falls at its own rate. Wrapped where the y hash repeats, so
    // the offset stays small for highp and the wrap is invisible.
    vec2 colm = floor(p.xz / PF_CELL);
    float hc = uhashUnit(uhash3(uvec3(ivec3(int(colm.x), 7, int(mod(colm.y, PF_ZWRAP / PF_CELL))))));
    p.y += mod(uFlowPhase * PF_FALL * mix(0.7, 1.3, hc), PF_YCELLS * PF_CELL);

    vec3 cell = floor(p / PF_CELL);
    vec3 f = p - (cell + 0.5) * PF_CELL;
    vec3 af = abs(f);
    float dEdge = 0.5 * PF_CELL - max(af.x, max(af.y, af.z)) + 0.02;
    float dBubble = PF_BUBBLE - length(p0 - gEye);

    uint u = uhash3(uvec3(ivec3(int(cell.x), int(mod(cell.y, PF_YCELLS)), int(mod(cell.z, PF_ZWRAP / PF_CELL)))));
    float h = uhashUnit(u);
    if (h < gFill) return max(dEdge, dBubble);
    uint u2 = uhash(u);
    float h2 = uhashUnit(u2);
    float h3 = uhashUnit(uhash(u2));

    vec3 q = f - (vec3(h, h2, h3) - 0.5) * 0.2;
    // A gust of its own for each petal.
    q.x -= gChaos * 0.08 * sin(gT * 1.3 + h * 40.0);
    float r = length(q);
    if (r > PF_BOUND + 0.1) return max(min(r - PF_BOUND, dEdge), dBubble);

    // Tumble: pitch and spin set the plane, then the roll about the petal's
    // own length, all on time alone.
    float pitch = h3 * PF_TAU + gT * (h3 - 0.5) * 0.5;
    float spin = h2 * PF_TAU + gT * (h - 0.5) * 1.2;
    float roll = gT * (0.4 + 0.8 * h) + h2 * 6.0;
    q.yz = rot2(pitch) * q.yz;
    q.xy = rot2(spin) * q.xy;
    q.xz = rot2(roll) * q.xz;

    // The petal runs base to tip along +y; centre it on the cell point.
    vec2 lq = vec2(q.x, q.y + 0.5 * PF_LEN);
    vec4 pet = chrysLeaf(lq, PF_LEN, PF_WIDTH, 0.9, 1.0);
    float d2 = pet.x;
    if (d2 < 0.1) d2 += chrysFray(lq * 0.2 + vec2(h * 13.0, h2 * 7.0), gChaos * 0.5);
    // Cupped across, curled along its length by Twist, on clamped
    // coordinates so the slope stays bounded (see the header).
    float cx = clamp(q.x, -0.4, 0.4);
    float cy = clamp(q.y, -0.6, 0.6);
    float zc = q.z - 0.5 * cx * cx - gCurl * cy * cy;
    vec2 w = vec2(d2 / 1.75, (abs(zc) - PF_THICK) / 1.25);
    float dp = 0.85 * (min(max(w.x, w.y), 0.0) + length(max(w, 0.0)));

    found.a.x = h;
    found.a.y = d2;
    found.a.z = pet.y;
    found.a.w = pet.z;
    found.b.x = zc;
    return max(min(dp, dEdge), dBubble);
}

/** The style's world at p as a hit, for lib_chrys_field. */
ChrysHit chrysMap(vec3 p) {
    ChrysHit h = chrysHit(0.0);
    float d = mapAt(p, h);
    h.d = d;
    return h;
}

//#include lib_chrys_field

/** Dusk sky and the bloom far out of focus, fixed at infinity. */
vec3 skyColour(vec3 rd, float hue, float bloomK) {
    vec3 col = mix(pal(hue + 0.55) * 0.05, pal(hue + 0.05) * 0.16, smoothstep(-1.0, 1.0, rd.y));
    float facing = dot(rd, PF_BLOOM);
    if (facing > 0.05) {
        vec3 tx = normalize(vec3(PF_BLOOM.z, 0.0, -PF_BLOOM.x));
        vec3 ty = cross(PF_BLOOM, tx);
        vec2 bp = vec2(dot(rd, tx), dot(rd, ty)) / facing * 1.4;
        bp = chrysTurn(gT * 0.01) * bp;
        vec4 bpet = chrysPetal(bp, 14.0, 0.12, 0.7, 0.55, 0.0, 0.3, 0.4);
        float bm = smoothstep(0.09, -0.09, bpet.x) * smoothstep(0.05, 0.3, facing);
        col = mix(col, pal(hue) * 0.22 * bloomK * mix(0.6, 1.0, clamp(bpet.y, 0.0, 1.0)), bm * 0.85);
        col += pal(hue + 0.1) * 0.1 * bloomK * chrysCore(length(bp), 0.18);
    }
    return col;
}

void main() {
    ChrysHit fld = chrysHit(0.0);
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 256 (power of two), so this
    // shift is exact and invisible, and highp keeps its fraction far down the
    // rig's 8192 travel.
    ro.z = mod(ro.z, PF_ZWRAP);
    // Grow and layers (lib_grow) measure from the eye in the frame the world is read in.
    growEye(ro);
    gEye = ro;
    gT = uTime;  // uTime already integrates Speed (ShaderScene::update)

    float swell = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    // The normalization shell (lib_chrys): every variable from its driver, mapped to its range here, once.
    ChrysIn N = chrysIn();
    float air = clamp(sndSpectrum(0.93) * uIntensity, 0.0, 1.5) * chrysSpan(N.width, 1.0, 0.5, 1.5);
    gChaos = chrysSpan(N.chaos, 0.0, 0.0, 2.0);
    gFill = chrysSpan(N.rings, 0.5, 0.5, 0.05);
    gSway = (0.6 + 0.8 * gChaos) * chrysSpan(N.sway, 1.0, 0.5, 1.5);
    gCurl = chrysSpan(N.curl, 0.15, -0.2, 0.5);
    float hue = 0.9 + 0.02 * sin(gT * 0.02) + chrysSpan(N.tone, 0.0, -0.15, 0.15);
    float bloomK = chrysSpan(N.glow, 1.0, 0.5, 1.5);
    float fogK = chrysSpan(N.reach, 0.06, 0.09, 0.03);

    float t = 0.05;
    float hitT = -1.0;
    for (int i = 0; i < PF_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        fld = chrysField(ro + rd * t);
        float d = fld.d;
        if (d < 0.001 + 0.0006 * t) {
            hitT = t;
            break;
        }
        t += d * 0.9;
        if (t > PF_FAR) break;
    }

    vec3 sky = skyColour(rd, hue, bloomK);
    vec3 col = sky;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = chrysNormal(p, max(0.0015, 0.0008 * hitT));
        // The hit petal again for the shading, after the normal taps.
        fld = chrysField(p);
        // A thin petal: light the face the eye sees.
        if (dot(n, rd) > 0.0) n = -n;

        float u = clamp(fld.a.z, 0.0, 1.0);
        bool under = fld.b.x < 0.0;
        vec3 top = pal(hue + 0.1 * fld.a.x) * mix(0.55, 1.0, u);
        vec3 bottom = mix(pal(hue + 0.1 * fld.a.x), vec3(1.0, 0.95, 0.95), 0.45) * 0.7;
        vec3 base = under ? bottom : top;
        float shade = chrysPetalShade(vec4(fld.a.y, fld.a.z, fld.a.w, 0.0), 0.5);

        vec3 L = normalize(vec3(-0.3, 0.8, -0.4));
        float dif = 0.35 + 0.65 * max(dot(n, L), 0.0);
        // The bloom behind shines through the petals that cross it.
        float back = pow(max(dot(rd, PF_BLOOM), 0.0), 4.0);
        float fres = pow(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 3.0);
        col = base * shade * dif * 0.7
            + pal(hue + 0.05) * back * 0.45 * bloomK * shade
            + pal(hue + 0.1) * fres * 0.12;

        // Air shimmer: a light running along the rim.
        float rim = smoothstep(0.03, 0.0, abs(fld.a.y));
        float run = pow(0.5 + 0.5 * sin(u * 18.0 - gT * 6.0 + fld.a.x * 30.0), 6.0);
        col += mix(col, vec3(1.0), 0.5) * rim * run * 0.6 * air;

        // Haze: far petals sink into the dusk.
        vec3 haze = mix(sky, pal(hue + 0.4) * 0.08, 0.5);
        col = mix(col, haze, 1.0 - exp(-hitT * fogK));
    }

    col *= 0.85 + 0.15 * swell;
    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.6, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
