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
//#include lib_truchet

// Truchet Tri: a flight down a corridor of stained glass. The corridor is a
// prism of equilateral triangle section - a floor and two walls leaning in to
// meet overhead - and all three faces are glazed with the triangle-grid
// Truchet set: every triangle's edges crossed at the same two points, each
// tile pairing its six points with arcs round its corners (three small ones,
// or one corner's nested pair with a half circle on the facing edge), so the
// lead lines run on unbroken into whatever the neighbour holds and close into
// rounded panes of every size. The lead is real came now: a rounded ridge
// standing a few centimetres proud of the glass, catching a soft sheen from
// the eye. The panes take the curves' two-colouring (two palette tones half
// the wheel apart), drifting in hue down the corridor, lit from behind by a
// slow light that circles outside the glass ahead of the eye, and each pane is
// brightest at its middle and dims into the lead like real glass.
//
// THE GLAZING. The three faces are one strip unrolled: a point on face k at
// u across it (corner to corner) sits at lattice y = (k + 1/2 + u / side) *
// Yp / 3, and z runs along lattice x. Yp is a whole number of rows pairs
// (M * sqrt3, 2M rows of sqrt3 / 2), so the strip closes on itself at a
// lattice line whose vertices land where the first row's do; since any
// triangle tile meets any neighbour, the lead runs on round all three
// corners, the closing one included. The faces fold the glass along the
// prism's edges like a lantern's.
//
// ---- camera -----------------------------------------------------------------
//
// camFly: the eye rides the rig's path down the corridor's axis, its x/y
// eased into the inner prism (the glide path's wander and the orbit's circle
// are both pulled inside, never through the glass). The eye's z is rebased by
// TT_ZWRAP = 1024, which divides the rig's 8192 travel wrap. Lattice x is z
// times TT_SZ levels that are multiples of 1/16, so 1024 world units is a
// whole number of 64-cell id periods; tile ids are taken mod 64 along x, and
// the warp and the hue field have periods of 64 cells along x and Yp around,
// so neither the rebase nor the rig's wrap ever shows.
//
// ---- audio ------------------------------------------------------------------
//
// Onsets (SND_HIT_TIME, SND_ONSET_ENV) re-roll a scattered third of the
// triangles; the old came sinks into the glass as the new one rises, over the
// onset envelope, so panes split and merge on the hits, and the came itself
// stands taller for the length of the envelope (geometry, never brightness).
// A section change (SND_HELD_HARD_CUT, SND_CUT_COUNT) turns the whole
// corridor's colours to a new place on the palette, eased across the section
// envelope. The key (uKeyHue, weighted by uKeyStrength) leans the hue. The bass
// (sndShaped(SND_LOW)) breathes the walls in toward the eye. The back light
// follows (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uTime drives the circling back light and the hue drift. Complexity:
// uMorph packs in smaller triangles, in five steps (the scale must stay a
// multiple of 1/16 for the z wrap). Chaos: uWarp/uTurbulence bend the glazing
// before it is tiled, on wrap-safe periods; uTwist in view(). Energy:
// uIntensity scales the bass breathing. Colour: every pane is pal(). Space:
// the camera rig. Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE
// how tall the came stands on an onset, k1 TEXTURE the came's width, k2 FLOW
// the back light's strength, k3 TONE the hue, k4 BUILD the glow at the far
// end, k5 SPACE the corridor's width.
//
// ---- why the distance estimate is safe --------------------------------------
//
// Inside a convex prism the distance to its boundary is the least distance to
// its three face planes, R - dot(p.xy, n_k), exactly. Each face carries a
// heightfield, the came's cosine ridge of height LH and half-width w (lattice
// units) about the nearest curve, so its slope is LH * pi / (2w) times the
// lattice scale, times the warp's stretch; the march steps by the height above
// the relief over sqrt(1 + slope^2), the Lipschitz bound, with LH and every
// scale taken from this frame's values. A re-roll mixes two such ridges, which
// keeps the bound. Only the nearest face's relief is read, unless the second
// face's bare plane minus LH comes nearer, and then both are; the third is
// farther than both. The march starts where the ray leaves the inner prism no
// came can reach and ends at the bare faces, and the eye is kept inside that
// inner prism, so it never starts inside the glass.

#define TT_MAX_STEPS 80
#define TT_FAR 44.0
/** Rebase period of the eye's z; divides 8192. */
#define TT_ZWRAP 1024.0
/** Inradius of the corridor at rest, world units: M / 6 at one lattice cell per world unit, M = 15. */
#define TT_R0 2.5
/** The came's height above the glass at rest, world units. */
#define TT_LEAD 0.06
/** The came's half-width, lattice units, at rest. */
#define TT_W 0.08
#define TT_SQRT3 1.7320508

/** The faces' outward normals (floor, right, left) and their tangents, counter-clockwise. */
#define TT_N0 vec2(0.0, -1.0)
#define TT_N1 vec2(0.8660254, 0.5)
#define TT_N2 vec2(-0.8660254, 0.5)
#define TT_T0 vec2(1.0, 0.0)
#define TT_T1 vec2(-0.5, 0.8660254)
#define TT_T2 vec2(-0.5, -0.8660254)

float gR;
float gSide;
float gSz;
float gYp;
float gLH;
float gW;
float gHits;
float gEnv;
float gT;
float gLip;
/** Set by map(): the hit face's came height fraction 0..1, distance to the lead (lattice units), glass colour, lattice position. */
float gRel;
float gLeadD;
float gTone;
vec2 gLat;

/** Chaos, wrap-safe: the lib's bend with frequencies that are whole multiples of 2pi/64 along x and 2pi/Yp around. */
vec2 ttWarp(vec2 p) {
    float a = 0.8 * clamp(uWarp, 0.0, 2.0);
    float b = 0.18 * clamp(uTurbulence, 0.0, 2.0);
    if (a + b <= 1e-4) return p;
    float fx = TRUCHET_TAU / 64.0;
    float fy = TRUCHET_TAU / gYp;
    p += a * vec2(sin(p.y * fy + gT * 0.21), sin(p.x * (3.0 * fx) - gT * 0.17 + 1.3));
    p += b * vec2(sin(p.y * (5.0 * fy) - gT * 0.37 + 2.1), sin(p.x * (11.0 * fx) + gT * 0.33));
    return p;
}

/** The came's cosine ridge, 1 on the curve, 0 from w out. */
float ridge(float d) {
    float g = clamp(d / gW, 0.0, 1.0);
    return 0.5 + 0.5 * cos(TRUCHET_PI * g);
}

/** Face k's glazing at p: x = came height fraction, y = distance to the lead, z = glass colour. Also the lattice position. */
vec3 faceTile(vec3 p, float k, vec2 tn, out vec2 lat) {
    float u = dot(p.xy, tn);
    vec2 q = vec2(p.z * gSz, (k + 0.5 + u / gSide) * gYp / 3.0);
    q = ttWarp(q);
    q.y = mod(q.y, gYp);
    lat = q;
    TruchetTri tri = truchetTriCell(q);
    vec2 rid = vec2(mod(tri.id.x, 64.0) * 2.0 + tri.id.z, tri.id.y);
    vec3 roll = truchetRoll(rid, gHits, gEnv, 3.0);
    float vNew = truchetTriVariant(roll.y);
    vec3 r = truchetTriTile(tri, q, vNew);
    vec3 res = vec3(ridge(r.x), r.x, r.y);
    float vOld = truchetTriVariant(roll.x);
    if (vOld != vNew && roll.z < 1.0) {
        // The old came sinks as the new one rises: a mix of two ridges.
        vec3 o = truchetTriTile(tri, q, vOld);
        res = mix(vec3(ridge(o.x), o.x, o.y), res, roll.z);
    }
    return res;
}

float map(vec3 p) {
    vec3 pd = gR - vec3(dot(p.xy, TT_N0), dot(p.xy, TT_N1), dot(p.xy, TT_N2));
    // Nearest face first, the second nearest beside it.
    float k1 = 0.0;
    float d1 = pd.x;
    vec2 t1 = TT_T0;
    float k2 = 1.0;
    float d2 = pd.y;
    vec2 t2 = TT_T1;
    if (pd.y < pd.x) {
        k1 = 1.0;
        d1 = pd.y;
        t1 = TT_T1;
        k2 = 0.0;
        d2 = pd.x;
        t2 = TT_T0;
    }
    if (pd.z < d1) {
        k2 = k1;
        d2 = d1;
        t2 = t1;
        k1 = 2.0;
        d1 = pd.z;
        t1 = TT_T2;
    } else if (pd.z < d2) {
        k2 = 2.0;
        d2 = pd.z;
        t2 = TT_T2;
    }
    vec2 lat;
    vec3 f = faceTile(p, k1, t1, lat);
    float d = d1 - gLH * f.x;
    gRel = f.x;
    gLeadD = f.y;
    gTone = f.z;
    gLat = lat;
    if (d2 - gLH < d) {
        vec2 lat2;
        vec3 f2 = faceTile(p, k2, t2, lat2);
        float dd = d2 - gLH * f2.x;
        if (dd < d) {
            d = dd;
            gRel = f2.x;
            gLeadD = f2.y;
            gTone = f2.z;
            gLat = lat2;
        }
    }
    return d * gLip;
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

/** The ray's distance to the prism of inradius r, from inside (where it leaves through the nearest face). */
float exitT(vec3 ro, vec3 rd, float r) {
    vec3 c = vec3(dot(rd.xy, TT_N0), dot(rd.xy, TT_N1), dot(rd.xy, TT_N2));
    vec3 h = r - vec3(dot(ro.xy, TT_N0), dot(ro.xy, TT_N1), dot(ro.xy, TT_N2));
    float t = 1e6;
    if (c.x > 1e-5) t = min(t, h.x / c.x);
    if (c.y > 1e-5) t = min(t, h.y / c.y);
    if (c.z > 1e-5) t = min(t, h.z / c.z);
    return t;
}

/** A smooth hue field, periodic in 64 cells along the corridor and once round it. */
float ttField(vec2 q, float t) {
    float fx = TRUCHET_TAU / 64.0;
    float fy = TRUCHET_TAU / gYp;
    float a = sin(q.x * (2.0 * fx) + 1.7 * sin(q.y * fy + t * 0.05) + t * 0.03);
    float b = sin(q.y * (2.0 * fy) - q.x * fx - t * 0.04 + 1.9);
    return 0.5 * (a + b);
}

void main() {
    vec2 uv = view();
    // One pixel in view units, taken in uniform control flow.
    float pxUv = max(length(fwidth(uv)), 1e-5);

    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    float t = truchetTime();
    gT = t;
    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float morph = truchetMorph();
    gHits = floor(SND_HIT_TIME);
    gEnv = SND_ONSET_ENV;

    // Five density levels, each a multiple of 1/16 (see the camera notes).
    gSz = 1.0 + 0.25 * floor(morph * 4.0 + 0.5);
    float m = floor(15.0 * gSz + 0.5);
    gYp = m * TT_SQRT3;
    gR = (TT_R0 + 0.8 * kSpace) * mix(1.0, truchetBreath(sndShaped(SND_LOW)), 0.6);
    gSide = 2.0 * TT_SQRT3 * gR;
    float sy = m / (6.0 * gR);
    gW = TT_W * clamp(1.0 + 0.8 * kTexture, 0.6, 1.4);
    gLH = TT_LEAD * (1.0 + 0.6 * clamp(gEnv, 0.0, 1.0) * clamp(1.0 + kPulse, 0.5, 1.5));
    float sc = max(gSz, sy);
    float slope = gLH * TRUCHET_PI / (2.0 * gW) * sc
                * (1.0 + 0.232 * clamp(uWarp, 0.0, 2.0) + 0.218 * clamp(uTurbulence, 0.0, 2.0));
    gLip = 1.0 / sqrt(1.0 + slope * slope);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    ro.z = mod(ro.z, TT_ZWRAP);
    // Ease the eye into the inner prism: its farthest reach toward any face
    // is squashed under lim, so it never meets the came.
    float lim = gR - 0.75;
    float reach = max(dot(ro.xy, TT_N0), max(dot(ro.xy, TT_N1), dot(ro.xy, TT_N2)));
    if (reach > 0.0) ro.xy /= sqrt(1.0 + (reach / lim) * (reach / lim));

    // Section colour: a new place on the wheel each section, slid into.
    float se = clamp(SND_HELD_HARD_CUT, 0.0, 1.0);
    se = se * se * (3.0 - 2.0 * se);
    float sec = mix(truchetHash(vec2(SND_CUT_COUNT, 7.0)), truchetHash(vec2(SND_CUT_COUNT - 1.0, 7.0)), se);
    float hue = 0.3 * sec + 0.12 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue) + 0.03 * sin(t * 0.02) + 0.12 * kTone;

    // The far end: the corridor's glass glows down the axis.
    float ahead = pow(max(rd.z, 0.0), 8.0);
    vec3 sky = pal(hue + 0.3) * 0.035 + pal(hue + 0.05) * (0.16 + 0.12 * swell) * (1.0 + kBuild) * ahead;
    vec3 col = sky;

    float tIn = max(exitT(ro, rd, gR - gLH - 0.02), 0.0);
    float tOut = exitT(ro, rd, gR);
    float hitT = -1.0;
    if (tIn < TT_FAR) {
        float tm = tIn;
        for (int i = 0; i < TT_MAX_STEPS; i++) {
            // uSteps is the user's Detail; this loop's own bound is the constant.
            if (float(i) >= uSteps) break;
            if (tm >= tOut) break;
            float d = map(ro + rd * tm);
            float eps = 0.0008 * tm + 0.0006;
            if (d < eps) {
                hitT = tm;
                break;
            }
            tm += max(d, eps * 0.5);
        }
        // Past the bare face a ray is in the glass; out of steps, it is in
        // the thin relief slab already, close enough to shade.
        if (hitT < 0.0) hitT = min(tm, tOut);
        if (hitT > TT_FAR) hitT = -1.0;
    }

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, 0.0015 * hitT + 0.002);
        // Restore the hit point's globals after the normal taps.
        map(p);
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        float px = pxUv * hitT / (uCamFocal * max(ndv, 0.2)) * sc;

        float field = ttField(gLat, t);
        vec3 paneA = pal(hue + 0.05 + 0.16 * field);
        vec3 paneB = pal(hue + 0.52 + 0.16 * field);
        vec3 pane = mix(paneA, paneB, gTone);

        // Back light: a soft lamp outside the glass, circling the corridor
        // ahead of the eye; the panes nearest it burn brightest.
        float ang = t * 0.13;
        vec3 lamp = vec3((gR + 1.4) * vec2(cos(ang), sin(ang)), ro.z + 6.0 + 2.5 * sin(t * 0.11));
        vec3 dl = p - lamp;
        float back = 0.45 + 0.55 * clamp(1.0 + 1.2 * kFlow, 0.2, 2.2) * exp(-dot(dl, dl) * 0.07);
        // Brightest mid-pane, dimming into the lead.
        float body = 0.55 + 0.45 * smoothstep(0.03, 0.2, gLeadD);
        vec3 glass = pane * back * body * (0.8 + 0.15 * swell) * (0.65 + 0.35 * ndv);
        float fall = 1.0 / (1.0 + 0.02 * hitT * hitT);
        glass += sky * pow(1.0 - ndv, 4.0) * 0.6 + vec3(pow(ndv, 40.0)) * 0.05 * fall;

        // Lead came: dark grey, a headlight sheen along its crown and the
        // panes' glow caught at its feet.
        float spec = pow(ndv, 18.0);
        vec3 leadC = vec3(0.05) + pal(hue + 0.3) * 0.03 + vec3(0.22) * spec * fall
                   + pane * back * 0.05 * (1.0 - gRel);
        // Coverage from the ridge itself (so a re-roll's mix fades it too),
        // a pixel of soft edge: d(ridge)/d(dist) is about 1.55 / w there.
        float edge = px * 1.55 / gW;
        float cover = smoothstep(0.42 - edge, 0.42 + edge, gRel);
        col = mix(glass, leadC, cover);

        col = mix(col, sky, 1.0 - exp(-hitT * 0.05));
    }

    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
