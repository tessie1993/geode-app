#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3

// Prism Rain: a flight through a falling rain of glass prisms under a dark
// sky with one bright light in it. Each prism splits that light into its
// colours: the three channels refract at slightly different indices, so the
// light seen through the glass fans out into a rainbow, and the facets catch
// rainbow rims.
//
// The camera is the shared rig (camFly). Space is cut into PR_CELL cubes,
// one prism per cube at most. Columns are hashed on x and z only, and every
// per-prism property along a column is a function of the prism's WORLD
// height. That makes the rain's fall a pure scroll with period PR_CELL: any
// whole-cell jump is invisible, which is how the fall phase wraps without a
// seam and how the kick can speed the fall without a counter. A prism tumbles
// and sways as a function of its height, so falling turns it.
//
// ---- audio ------------------------------------------------------------------
//
// Each sound moves a different thing:
//
// - Kick (uHits.x) grows every prism and throws the rain one extra cell down
//   over the envelope, fast then easing, so the fall surges on each kick.
// - Snare (uHits.y) steps every prism a third of a turn about its long axis,
//   snapping and settling. A third is the triangle's own symmetry (and the
//   hexagon's), so the step starts from a pose identical to the last.
// - Hats (uHits.z) light dispersion sparkles that ride on the facets.
// - An onset (uHits.w) morphs every prism between a triangular and a
//   hexagonal section; the parity of uHitCount picks which, so the shape
//   holds until the next onset.
// - The low bands (sub, bass) set how dense the rain is: prisms grow in and
//   shrink out, they do not pop.
// - The high bands (high-mid, presence, air) set how far the light disperses.
// - A drop (uEvents.y) changes the light's colour to its complement and sends
//   a shockwave out from the eye that shoves the prisms aside as it passes.
// - A pitch change (uEvents.w) slides the hue of the sky and the glass.
// - The section count (uSectionCount) moves the light: a hashed direction per
//   section, eased across the 1.5 s of a section change.
//
// Overall level rides uSwell and uEnergySmooth.
//
// ---- why the estimate is safe ----------------------------------------------
//
// Each cell measures only its own prism, which stays inside the cell with
// PR_MARGIN to spare (sway 0.35 + shove 0.4 + bounding radius 1.02 = 1.77 of
// a 2.0 half-cell). So the distance is clamped to the cell boundary plus that
// margin, and a neighbour is never stepped over. Prisms near the eye shrink to
// nothing and a sphere around the eye is carved out as well.

#define PR_MAX_STEPS 96
#define PR_FAR 52.0
#define PR_TAU 6.2831853
#define PR_CELL 4.0
#define PR_HALF 2.0
/** Cells along z before the hash repeats: 256 * 4 = 1024 world units, which divides 8192. */
#define PR_ZWRAP 256.0
#define PR_MARGIN 0.2
#define PR_BUBBLE 0.8

/** The eye, z rebased into 0..1024 (see main). */
vec3 gEye;
float gFall;
float gKick;
float gSpin;
float gMorph;
float gDensity;
float gDrop;
float gFront;

/** The hit prism's own hash, its local coordinates and the shockwave on it. */
float gH;
vec3 gQ;
float gWave;

/** Triangular prism along z: circumradius r (inradius r/2), half-length hl. */
float prTri(vec3 p, float r, float hl) {
    vec3 q = abs(p);
    return max(q.z - hl, max(q.x * 0.8660254 + p.y * 0.5, -p.y) - r * 0.5);
}

/** Hexagonal prism along z: apothem a, half-length hl. */
float prHex(vec3 p, float a, float hl) {
    const vec3 k = vec3(-0.8660254, 0.5, 0.57735);
    p = abs(p);
    p.xy -= 2.0 * min(dot(k.xy, p.xy), 0.0) * k.xy;
    vec2 d = vec2(length(p.xy - vec2(clamp(p.x, -k.z * a, k.z * a), a)) * sign(p.y - a), p.z - hl);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

float map(vec3 p) {
    vec2 cxz = floor(p.xz / PR_CELL);
    uint u = uhash3(uvec3(ivec3(int(cxz.x), 5, int(mod(cxz.y, PR_ZWRAP)))));
    float h0 = uhashUnit(u);
    uint u2 = uhash(u);
    float h1 = uhashUnit(u2);
    uint u3 = uhash(u2);
    float h2 = uhashUnit(u3);
    float h3 = uhashUnit(uhash(u3));

    // Each column sits at its own height phase, so rows never line up.
    float yy = p.y + gFall + h0 * PR_CELL;
    float cy = floor(yy / PR_CELL);
    float worldY = (cy + 0.5) * PR_CELL - gFall - h0 * PR_CELL;
    vec3 base = vec3((cxz.x + 0.5) * PR_CELL, worldY, (cxz.y + 0.5) * PR_CELL);
    // Sway with height: the rain drifts sideways as it falls.
    vec3 centre = base + vec3(0.25 * sin(worldY * 0.53 + h1 * PR_TAU), 0.0, 0.25 * cos(worldY * 0.47 + h2 * PR_TAU));

    // Density: a prism grows in as the low bands rise past its threshold.
    float dens = gDensity + 0.22 * sin(worldY * 0.61 + h2 * PR_TAU);
    float exist = smoothstep(h3 - 0.1, h3 + 0.1, dens);
    float near = smoothstep(1.4, 3.6, length(centre - gEye));
    float s = 0.42 * (0.7 + 0.3 * h1) * (1.0 + 0.35 * gKick) * exist * near;

    // The drop's shockwave: a shell running out from the eye that shoves
    // every prism it passes straight away from the viewer.
    vec3 away = centre - gEye;
    float r = length(away);
    float wr = (r - gFront) / 3.0;
    float wave = gDrop * exp(-wr * wr);
    centre += away / max(r, 1e-3) * 0.4 * wave;

    vec3 q = p - centre;
    // Tumble with height, plus a slow turn so a still rain still moves.
    q.xz = rot2(h1 * PR_TAU + 0.21 * worldY + 0.15 * uTime * (h2 - 0.5)) * q.xz;
    q.yz = rot2(h2 * 3.0 + 0.17 * worldY) * q.yz;
    // The snare step, about the long axis.
    q.xy = rot2(gSpin + h0 * PR_TAU) * q.xy;
    float hl = 1.5 * s;
    float dp = mix(prTri(q, s, hl), prHex(q, 0.62 * s, hl), gMorph);
    // A shrunk-away prism is no prism: without this the march would stop on
    // specks of dust.
    if (s < 0.03) dp = 1e3;

    vec2 fxz = abs(p.xz - base.xz);
    float fy = abs(yy - (cy + 0.5) * PR_CELL);
    float toEdge = PR_HALF - max(max(fxz.x, fxz.y), fy);
    float d = min(dp, toEdge + PR_MARGIN);
    d = max(d, PR_BUBBLE - length(p - gEye));

    gH = h1;
    gQ = q;
    gWave = wave;
    return d;
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** Hue to a pure rainbow colour, independent of the palette: the split light. */
vec3 spectrum(float x) {
    return clamp(abs(fract(x + vec3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0, 0.0, 1.0);
}

/** The light for section `sec`: hashed, always ahead of the eye and above the horizon. */
vec3 lightDir(float sec) {
    float h = hash11(sec * 1.37 + 0.5);
    float h2 = hash11(sec * 2.91 + 7.0);
    return normalize(vec3(1.4 * (h - 0.5), 0.12 + 0.5 * h2, 1.0));
}

vec3 skyColour(vec3 rd, vec3 L, vec3 lightCol, float hueBase) {
    vec3 col = mix(pal(hueBase + 0.62) * 0.10, pal(hueBase + 0.75) * 0.015, smoothstep(-0.4, 0.8, rd.y));
    float sun = max(dot(rd, L), 0.0);
    col += lightCol * (0.9 * pow(sun, 400.0) + 0.22 * pow(sun, 24.0) + 0.05 * pow(sun, 4.0));
    return col;
}

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 1024, so this shift is
    // invisible and exact (power of two). Without it, far down the 8192
    // travel a highp z has ~0.001 of fraction left: every sample and the
    // finite-difference normals quantise into speckle.
    ro.z = mod(ro.z, PR_ZWRAP * PR_CELL);
    gEye = ro;

    float swell = clamp(uSwell, 0.0, 1.5);
    float energy = clamp(uEnergySmooth, 0.0, 1.5);
    float kick = clamp(uHits.x, 0.0, 1.0);
    float snare = clamp(uHits.y, 0.0, 1.0);
    float hats = clamp(uHits.z, 0.0, 1.0);
    float onset = clamp(uHits.w, 0.0, 1.0);
    float drop = clamp(uEvents.y, 0.0, 1.0);
    float pitch = clamp(uEvents.w, 0.0, 1.0);
    float lowB = clamp(0.5 * (uBandsLo.x + uBandsLo.y), 0.0, 1.0);
    float highB = clamp((uBandsHi.x + uBandsHi.y + uBandsHi.z) / 3.0, 0.0, 1.0);

    gKick = kick;
    // The fall is a scroll with period PR_CELL. At rest the kick term is one
    // whole cell (invisible); a hit drops it to zero (invisible again) and the
    // falling envelope adds the cell back, fast at first: an extra cell of
    // fall per kick. Squared, so a kick on a kick jumps by only kick^2.
    gFall = mod(uFlowPhase * 1.3 + PR_CELL * (1.0 - kick * kick), PR_CELL);
    // The snare step: the same trick, by a third of a turn.
    gSpin = (PR_TAU / 3.0) * (1.0 - snare * snare);
    // Odd onset counts are hexagons, even ones triangles; the onset envelope
    // carries the morph from the last shape to this one.
    float odd = mod(floor(uHitCount + 0.5), 2.0);
    gMorph = smoothstep(0.0, 1.0, odd > 0.5 ? 1.0 - onset : onset);
    gDensity = 0.32 + 0.55 * lowB + 0.10 * swell;
    gDrop = drop;
    gFront = 1.5 + 30.0 * (1.0 - drop);

    float sec = floor(uSectionCount + 0.5);
    vec3 L = normalize(mix(lightDir(sec), lightDir(sec - 1.0), smoothstep(0.0, 1.0, clamp(uEvents.z, 0.0, 1.0))));
    float hueBase = 0.2 * pitch;
    float lightHue = hash11(sec * 0.77 + 3.0);
    vec3 lightCol = mix(vec3(1.0, 0.88, 0.70), pal(lightHue), 0.35);
    lightCol = mix(lightCol, pal(lightHue + 0.5) * 1.2 + 0.1, smoothstep(0.0, 0.4, drop));

    float t = 0.05;
    float hitT = -1.0;
    for (int i = 0; i < PR_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        float d = map(p);
        if (d < 0.0015 + 0.0008 * t) {
            hitT = t;
            break;
        }
        t += d * 0.9;
        if (t > PR_FAR) break;
    }

    vec3 sky = skyColour(rd, L, lightCol, hueBase);
    vec3 col = sky;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.001 * hitT));
        // Restore the hit prism's globals after the normal taps.
        map(p);

        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float fres = 0.04 + 0.96 * pow(1.0 - ndv, 5.0);
        float rim = (1.0 - ndv) * (1.0 - ndv);
        // Dispersion: red bends least, blue most. Entering glass (eta < 1)
        // there is no total internal reflection, so none of these is zero.
        float disp = 0.015 + 0.11 * highB;
        vec3 rr = refract(rd, n, 0.66 - disp);
        vec3 rg = refract(rd, n, 0.66);
        vec3 rb = refract(rd, n, 0.66 + disp);
        vec3 thru = vec3(pow(max(dot(rr, L), 0.0), 5.0), pow(max(dot(rg, L), 0.0), 5.0), pow(max(dot(rb, L), 0.0), 5.0));
        vec3 bow = spectrum(hueBase + 2.2 * dot(rg, L) + 0.6 * rim + 0.3 * gH);
        vec3 rf = reflect(rd, n);
        float spec = pow(max(dot(rf, L), 0.0), 60.0);

        vec3 glass = pal(hueBase + 0.55 + 0.2 * gH) * (0.05 + 0.08 * max(dot(n, L), 0.0));
        col = glass + lightCol * thru * (0.55 + 0.4 * highB)
            + skyColour(rf, L, lightCol, hueBase) * fres
            + bow * rim * (0.06 + 0.7 * highB)
            + lightCol * spec * 0.8;

        // Hat sparkles, hashed in the prism's own frame so they ride on it.
        vec3 gp = gQ * 9.0;
        float g = hashCell(ivec3(floor(gp)) + ivec3(int(gH * 977.0)));
        float tw = 0.5 + 0.5 * sin(uTime * (10.0 + 14.0 * g) + g * 70.0);
        float pt = smoothstep(0.32, 0.0, length(fract(gp) - 0.5));
        col += spectrum(g + 0.2 * uTime) * step(0.9 - 0.12 * hats, g) * pt * tw * (0.05 + 1.6 * hats);

        // The shockwave lights the glass it is shoving.
        col += pal(lightHue + 0.5) * gWave * 0.45;

        col = mix(col, sky, 1.0 - exp(-hitT * 0.045));
    }

    col *= 0.80 + 0.30 * swell + 0.08 * energy;
    col *= 0.7 + 0.3 * smoothstep(2.2, 0.5, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
