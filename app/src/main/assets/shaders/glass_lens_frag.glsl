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

// Glass Lens: a flight low over an endless LED floor that is playing the
// music, through a gallery of magnifying lenses hanging in the air above it.
// The 2D style was lenses drifting over a flat wall; here the wall is a floor
// you travel along and the lenses are real bodies of glass you pass.
//
// The floor is the family's FRACTAL world laid out flat (wrapped round a
// cylinder in direction space, so it repeats every 32 units along z without a
// seam), lifted in bands by the live spectrum, low notes at the left. It is
// built of LEDs: each one three narrow red, green and blue emitters. Seen
// directly from the eye's height the emitters are smaller than a pixel and
// average into the picture; under a lens they resolve into their RGB triads,
// the moment the eye recognises "glass".
//
// Each lens is a true biconvex body, the intersection of two balls, held in a
// thin brass ring. A ray that hits one refracts in by Snell's law, crosses the
// body, refracts out of the far face (both exits analytic: the far
// intersection of the two balls), and goes on to read the floor or the dome:
// once per channel at a slightly different index (dispersion, three reads of
// the floor). Floating a little inside its focal length above the floor, each
// lens shows an upright, magnified image, and it throws a soft shadow with a
// focused hot spot on the floor below, toward the viewer, from the family's
// back key light. Some lenses are kaleidoscopes: what they magnify folds into a
// turning six-fold mandala, as the 2D second lens did.
//
// ---- the camera -------------------------------------------------------------
//
// The shared rig, camFly: the eye rides the path down +z, above the lens
// layer's middle. Lenses hang in rows every 4 units in a left and a right
// lane, tilted up and back toward the eye and in toward the path, plus (with
// Complexity) a low centre lane lying almost flat under the path. Rows hash
// with a period of 256 (1024 units) and the floor picture and LED grid repeat
// every 32 units (512 LEDs of 1/16), so the eye is rebased by mod 1024, which
// divides the rig's 8192 wrap and keeps highp z precise far down the travel.
// Everything else animates on time, never on z.
//
// ---- audio ------------------------------------------------------------------
//
//   spectrum       lifts the floor's glow in bands, low notes at the left;
//   bass           thickens every lens (its centre bulges, 0.31 -> 0.56 of its
//                  radius), so its focal length shortens and the image under it
//                  swells from about 1.6x to 2.6x: the lenses lean in on low notes;
//   centroid       the dispersion: brighter music splits the colours at the rims;
//   snare          flips a third of the lenses (SND_SNARE_TOGGLE eases 0 <-> 1 per
//                  hit) about their vertical diameter, edge-on and round again,
//                  and their shadows and hot spots narrow and return with them;
//   kick           jostles the lenses (an eased bump, never a jump);
//   pan            the lanes lean toward the louder side;
//   flatness       frosts the glass: grain on the faces, the magnified image milky;
//   hi-hat         glints on the faces;
//   key            uKeyHue walks the palette, gated by uKeyStrength.
//
// Knobs (each the identity at 0.5): k0 PULSE the kick's jostle, k1 TEXTURE the
// LED triads and the frost grain, k2 FLOW the lenses' drift, k3 TONE the hue,
// k4 BUILD the floor's glow, k5 SPACE the lanes' width.
//
// ---- macros -----------------------------------------------------------------
//
// Kept from 2D. Speed: uTime (already Speed-integrated) drives the drift.
// Complexity: uMorph fills more of the lanes and opens the centre lane.
// Chaos: uWarp and uTurbulence widen the lenses' wobble and ripple their faces
// (glassWobble), so the magnified image swims. Energy: uIntensity scales every
// reaction (through glassListen). Colour: pal(). Detail: uSteps.
//
// ---- why the estimate is safe ----------------------------------------------
//
// The march sees only the lenses; the floor is a plane hit analytically, and
// the march stops at it. A lens's glass is the max of two exact ball
// distances, a bound on their intersection; its ring is an exact torus. Each
// row measures only its own three lenses, and every lens stays at least 0.45
// inside its 4-unit row along z (drift, jostle and radius are all bounded
// below), so the distance is clamped to the row boundary plus 0.35: a step
// can cross into the next row but never reach its lenses. Far from a lens a
// bounding ball stands in for it. Above the lens layer and outside the lanes
// the plane bounds take over, and a sphere round the eye is carved out of
// everything. min and max of bounds are bounds, so the march never overshoots.
//
// ---- cost -------------------------------------------------------------------
//
// A 96-step ceiling broken on uSteps; the three lenses of a row are computed
// once per row the ray crosses (a small cache), not per step. Off the lenses
// one floor or dome read (one fractal); on a lens three refracted reads and one
// reflection, as the 2D style spent. Normals are analytic. Shadows test the
// six lenses of the two rows that can shade a point, once per pixel.

#define LN_STEPS 96
#define LN_FAR 40.0
#define LN_FLOOR -3.0
/** Rows of lenses along z, one every LN_CELL. 1024 / 4 = 256 rows before the hash repeats. */
#define LN_CELL 4.0
#define LN_ROWS 256.0
#define LN_ZWRAP 1024.0
/** The floor picture and the LED grid repeat every 32 units (it divides 1024). */
#define LN_LOOP 32.0
/** One LED: 32 / 512, so the grid also tiles the loop exactly. */
#define LN_PITCH 0.0625
/** Half-width of the lit floor; it fades out beyond. */
#define LN_HALF 6.0
#define LN_TUBE 0.045
#define LN_BUBBLE 0.55
/** Nothing of any lens rises above this (heights, bob, jostle and radius summed). */
#define LN_LENS_TOP 0.2
/** Nor reaches further out in x. */
#define LN_LANE_MAX 5.5
#define LN_FOG 0.045

GlassLook gLk;
GlassEar gEar;
float gHue;
float gDrift;
float gFlow;
float gPulse;
float gTexture;
float gBuild;
float gSpace;
float gComplex;
float gThick;
float gMag;
float gFlipC;
float gFlipS;
float gPixAng;
vec3 gEye;
vec3 gL;

// The row cache: the march's current row and its three lenses.
float gRow;
vec3 gLC[3];
vec3 gLA[3];
float gLap[3];
float gLon[3];
float gLkale[3];
float gLR[3];
float gLo[3];
/** The lens the last map() call found nearest (-1 none), and whether it was the glass (1) or the ring (2). */
int gLane;
int gPart;

// The shadow the green channel's floor read found, reused by red and blue.
float gShMul;
vec3 gShAdd;

/**
 * Lens `lane` of row `row`: centre, axis (unit), aperture radius, whether it is
 * there, and whether it is a kaleidoscope. Pure in row and the frame's globals.
 */
void lensParams(float row, int lane, out vec3 C, out vec3 A, out float ap, out float on, out float kale) {
    float rw = mod(row, LN_ROWS);
    float fl = float(lane);
    float h1 = glassHash(vec2(rw * 1.13 + 0.7, fl * 7.1 + 1.3));
    float h2 = glassHash(vec2(rw * 0.77 + 4.7, fl * 3.3 + 2.9));
    float h3 = glassHash(vec2(fl * 5.9 + 9.1, rw * 0.71 + 3.3));
    float h4 = glassHash(vec2(rw * 1.91 + 2.2, fl * 1.7 + 6.4));
    float side = lane == 0 ? -1.0 : (lane == 1 ? 1.0 : 0.0);
    float t = gDrift;
    float thresh = lane == 2 ? 0.15 + 0.55 * gComplex : 0.62 + 0.38 * gComplex;
    on = step(h4, thresh);
    kale = step(0.72, h3);
    ap = 0.6 + 0.22 * h1;
    // z: at most 0.3 + 0.18 + 0.21 off the row's middle; with the radius that
    // leaves 0.45 to the row boundary (the estimate's clamp needs it).
    float zc = (row + 0.5) * LN_CELL + 0.6 * (h3 - 0.5) + 0.18 * sin(t * (0.05 + 0.03 * h2) + h2 * 7.0);
    float x = side * (2.1 + 0.5 * h1) * gSpace + (lane == 2 ? 0.8 * (h2 - 0.5) : 0.0)
            + 0.3 * gFlow * sin(t * (0.07 + 0.04 * h1) + h1 * 9.0) + 0.3 * gEar.pan;
    float y = LN_FLOOR + (lane == 2 ? 0.95 + 0.3 * h2 : 1.25 + 0.65 * h2)
            + 0.12 * gFlow * sin(t * (0.11 + 0.05 * h3) + h3 * 6.0);
    C = vec3(x, y, zc) + (vec3(h1, h2, h3) - 0.5) * (0.28 * min(gEar.kick, 1.0) * gPulse);
    // Tilted up and back toward the eye, and in toward the path; Chaos widens the wobble.
    float w = 0.18 + 0.2 * glassChaos();
    float s1 = sin(t * (0.06 + 0.03 * h3) + h1 * 5.0);
    float s2 = sin(t * (0.05 + 0.02 * h1) + h3 * 4.0);
    A = normalize(vec3(-side * 0.45 + w * s1, 1.0, -(lane == 2 ? 0.45 : 0.8) - w * s2));
    if (h2 < 0.4) {
        // The snare flip: about the lens's own diameter E, the travel axis
        // projected into its plane, so at a quarter turn it stands edge-on to a
        // viewer looking down +z. A biconvex lens is symmetric, so a half turn is
        // the same lens again.
        vec3 E = normalize(vec3(-A.x * A.z, -A.y * A.z, 1.0 - A.z * A.z));
        A = A * gFlipC + cross(E, A) * gFlipS;
    }
}

/** The two balls of a lens of aperture `ap`: their radius R and their centres' offset o along the axis. */
void lensBalls(float ap, out float R, out float o) {
    float h = gThick * ap;
    R = (ap * ap + 0.25 * h * h) / h;
    o = R - 0.5 * h;
}

void fillRow(float row) {
    gRow = row;
    for (int i = 0; i < 3; i++) {
        vec3 C;
        vec3 A;
        float ap;
        float on;
        float kale;
        lensParams(row, i, C, A, ap, on, kale);
        float R;
        float o;
        lensBalls(ap, R, o);
        gLC[i] = C;
        gLA[i] = A;
        gLap[i] = ap;
        gLon[i] = on;
        gLkale[i] = kale;
        gLR[i] = R;
        gLo[i] = o;
    }
}

float map(vec3 p) {
    float row = floor(p.z / LN_CELL);
    if (row != gRow) fillRow(row);
    float d = 1e3;
    gLane = -1;
    gPart = 0;
    for (int i = 0; i < 3; i++) {
        if (gLon[i] < 0.5) continue;
        vec3 pp = p - gLC[i];
        float ball = length(pp) - (gLap[i] + LN_TUBE + 0.02);
        if (ball > 0.25) {
            d = min(d, ball);
            continue;
        }
        vec3 A = gLA[i];
        float glass = max(length(pp + A * gLo[i]) - gLR[i], length(pp - A * gLo[i]) - gLR[i]);
        float ax = dot(pp, A);
        float rr = sqrt(max(dot(pp, pp) - ax * ax, 0.0));
        float ring = length(vec2(rr - gLap[i], ax)) - LN_TUBE;
        float dl = min(glass, ring);
        if (dl < d) {
            d = dl;
            gLane = i;
            gPart = ring < glass ? 2 : 1;
        }
    }
    float zl = p.z - gRow * LN_CELL;
    d = min(d, min(zl, LN_CELL - zl) + 0.35);
    d = max(d, p.y - LN_LENS_TOP);
    d = max(d, abs(p.x) - LN_LANE_MAX);
    d = max(d, LN_BUBBLE - length(p - gEye));
    return d;
}

vec3 fogColour() {
    return pal(gHue + 0.6) * 0.035;
}

/** The dome over the floor: the FRACTAL world, dimmed, sinking into the fog at the horizon. */
vec3 skyColour(vec3 d) {
    vec3 fc = fogColour();
    float up = smoothstep(0.0, 0.4, d.y + 0.02);
    if (up <= 0.0) return fc;
    return fc + glassEnv(d, gLk) * (0.28 * up);
}

/**
 * The picture the LED floor shows at floor point q: the FRACTAL world round a
 * cylinder (period LN_LOOP in z), lifted in bands by the spectrum, low notes
 * at the left. `rough` frosts it.
 */
vec3 wallPicture(vec2 q, float rough) {
    float th = q.y * (GLASS_TAU / LN_LOOP);
    vec3 d = normalize(vec3(q.x * 0.17, cos(th), sin(th)));
    GlassLook l = gLk;
    l.rough += rough;
    float s = clamp(q.x / (2.0 * LN_HALF) + 0.5, 0.0, 1.0);
    float band = clamp(sndSpectrum(s * 0.92 + 0.02) * 1.3, 0.0, 1.2);
    float edge = 1.0 - smoothstep(LN_HALF, LN_HALF + 3.0, abs(q.x));
    return glassFractal(d, l) * (0.55 + 0.9 * band) * gBuild * edge;
}

/**
 * The LED floor at q, for a pixel whose footprint on the floor is `fp` units.
 * Near and magnified, each LED shows its three emitters; once an emitter is
 * under a pixel the triads fade into their average (0.574 of the picture: the
 * emitters' lit share), so nothing aliases into moire far away.
 */
vec3 ledFloor(vec2 q, float fp, float rough) {
    vec2 g = q / LN_PITCH;
    vec2 cell = floor(g);
    vec2 f = g - cell - 0.5;
    float vis = clamp((1.0 - smoothstep(0.12 * LN_PITCH, 0.35 * LN_PITCH, fp)) * gTexture, 0.0, 1.0);
    vec3 pic = wallPicture(mix(q, (cell + 0.5) * LN_PITCH, vis), rough);
    if (vis <= 0.0) return pic * 0.574;
    float sx = (f.x + 0.5) * 3.0;
    float k = min(floor(sx), 2.0);
    float fx = fract(sx) - 0.5;
    float m = (1.0 - smoothstep(0.28, 0.42, abs(fx))) * (1.0 - smoothstep(0.36, 0.46, abs(f.y)));
    vec3 chan = vec3(1.0 - step(0.5, k), step(0.5, k) * (1.0 - step(1.5, k)), step(1.5, k));
    return mix(pic * 0.574, pic * chan * (m * 3.0), vis);
}

/**
 * The lenses' shadows and hot spots at floor point P: for each lens of the two
 * rows the key light crosses on its way down, where the light ray through P
 * meets the lens's plane, in units of its radius. Inside the disc the light is
 * gathered into a spot (narrowing as the lens turns edge-on); the ring throws a
 * dark line. Writes gShMul and gShAdd.
 */
void lensShadows(vec3 P) {
    gShMul = 1.0;
    gShAdd = vec3(0.0);
    vec3 pm = P + gL * ((LN_FLOOR + 1.6 - P.y) / gL.y);
    float rz = pm.z / LN_CELL;
    float r0 = floor(rz);
    float r1 = r0 + (fract(rz) < 0.5 ? -1.0 : 1.0);
    for (int k = 0; k < 2; k++) {
        float row = k == 0 ? r0 : r1;
        for (int i = 0; i < 3; i++) {
            vec3 C;
            vec3 A;
            float ap;
            float on;
            float kale;
            lensParams(row, i, C, A, ap, on, kale);
            if (on < 0.5) continue;
            float den = dot(gL, A);
            if (abs(den) < 0.03) continue;
            float tl = dot(C - P, A) / den;
            if (tl <= 0.0) continue;
            float r = length(P + gL * tl - C) / ap;
            float disc = 1.0 - smoothstep(0.85, 1.08, r);
            float ring = exp(-(r - 1.0) * (r - 1.0) * 120.0);
            gShMul *= 1.0 - 0.38 * disc - 0.35 * ring;
            gShAdd += pal(gHue + 0.1) * (0.55 * exp(-r * r * 22.0) * abs(den));
        }
    }
}

/** A kaleidoscope lens's fold: the floor about kc in six mirrored wedges, turning on the music's clock. */
vec2 kaleFold(vec2 q, vec2 kc) {
    vec2 kd = glassRot(0.1 * SND_CURVED_TIME) * (q - kc);
    float len = length(kd);
    if (len < 1e-4) return q;
    float seg = GLASS_TAU / 6.0;
    float ka = abs(mod(atan(kd.y, kd.x), seg) - seg * 0.5);
    return kc + vec2(cos(ka), sin(ka)) * len + vec2(0.05 * sin(gDrift * 0.3), 0.0);
}

/**
 * What a ray from `o` along `dir` sees beyond the glass: the LED floor (with
 * the lenses' shadows, computed only when `shade` is set, else reused) or the
 * dome, fogged by the whole path `travelled` + the last leg. `fpk` scales the
 * pixel footprint (a lens magnifies it down); `kale` folds the floor about kc.
 */
vec3 seeBeyond(vec3 o, vec3 dir, float travelled, float fpk, float rough, float kale, vec2 kc, bool shade) {
    if (dir.y > -1e-3) return skyColour(dir);
    float tf = (LN_FLOOR - o.y) / dir.y;
    float tt = travelled + tf;
    float fog = 1.0 - exp(-tt * LN_FOG);
    vec3 fc = fogColour();
    if (fog > 0.97) return fc;
    vec3 P = o + dir * tf;
    vec2 q = kale > 0.5 ? kaleFold(P.xz, kc) : P.xz;
    float fp = tt * gPixAng * fpk / max(-dir.y, 0.12);
    if (shade) lensShadows(P);
    vec3 c = ledFloor(q, fp, rough) * gShMul + gShAdd;
    return mix(c, fc, fog);
}

void main() {
    vec2 uv = view();
    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 1024 (rows of 256 x 4, the floor's 32), so
    // this rebase is invisible and exact, and highp z keeps its fraction far down the travel.
    ro.z = mod(ro.z, LN_ZWRAP);
    gEye = ro;

    gEar = glassListen();
    gHue = 0.05 * sin(glassTime() * 0.02) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue)
         + 0.25 * (uKnob[3] - 0.5);
    gLk = glassLook(GLASS_ENV_FRACTAL, gHue, gEar);
    gL = glassKeyDir(gLk);
    gDrift = glassTime();
    gFlow = clamp(1.0 + 1.2 * (uKnob[2] - 0.5), 0.4, 1.6);
    gPulse = clamp(1.0 + 1.0 * (uKnob[0] - 0.5), 0.5, 1.5);
    gTexture = clamp(1.0 + 1.2 * (uKnob[1] - 0.5), 0.4, 1.6);
    gBuild = clamp(1.0 + 0.6 * (uKnob[4] - 0.5), 0.7, 1.3);
    gSpace = clamp(1.0 + 0.5 * (uKnob[5] - 0.5), 0.75, 1.25);
    gComplex = glassComplexity();
    float bass = clamp(gEar.bass, 0.0, 1.0);
    gThick = 0.31 + 0.25 * bass;
    gMag = 1.6 + 1.0 * bass;
    float flip = clamp(SND_SNARE_TOGGLE, 0.0, 1.0) * GLASS_PI * min(glassEnergy(), 1.0);
    gFlipC = cos(flip);
    gFlipS = sin(flip);
    gPixAng = 2.0 / (max(uResolution.y, 1.0) * uCamFocal);
    gRow = -1e9;
    gShMul = 1.0;
    gShAdd = vec3(0.0);
    float frost = gEar.frost * 0.7;

    // March the lenses only; the floor is a plane, so the march ends where it meets it.
    float tFloor = rd.y < -1e-4 ? (LN_FLOOR - ro.y) / rd.y : 1e4;
    float tMax = min(tFloor, LN_FAR);
    float t = 0.02;
    float hitT = -1.0;
    for (int i = 0; i < LN_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        vec3 p = ro + rd * t;
        if (p.y > LN_LENS_TOP && rd.y >= 0.0) break;
        float d = map(p);
        if (d < 0.001 + 0.0008 * t) {
            hitT = t;
            break;
        }
        t += d * 0.95;
        if (t > tMax) break;
    }

    vec3 col;
    vec3 p = ro + rd * max(hitT, 0.0);
    if (hitT > 0.0 && gLane >= 0 && length(p - gEye) > LN_BUBBLE + 0.02) {
        vec3 C = gLC[0];
        vec3 A = gLA[0];
        float ap = gLap[0];
        float kale = gLkale[0];
        float R = gLR[0];
        float o = gLo[0];
        for (int i = 1; i < 3; i++) {
            if (i == gLane) {
                C = gLC[i];
                A = gLA[i];
                ap = gLap[i];
                kale = gLkale[i];
                R = gLR[i];
                o = gLo[i];
            }
        }
        vec3 pp = p - C;
        float ax = dot(pp, A);
        vec3 radial = pp - A * ax;
        float rr = length(radial);
        vec3 rdir = rr > 1e-4 ? radial / rr : vec3(0.0);
        float fog = 1.0 - exp(-hitT * LN_FOG);
        if (gPart == 2) {
            // The brass ring: a mirror of the dome, warmed toward the palette.
            vec3 n = normalize(pp - rdir * ap);
            vec3 rf = reflect(rd, n);
            float cosI = clamp(dot(-rd, n), 0.0, 1.0);
            vec3 brass = mix(vec3(0.95, 0.72, 0.42), pal(gHue + 0.6), 0.35);
            float m = 1.0 - cosI;
            float fr = 0.6 + 0.4 * m * m * m * m * m;
            col = brass * glassEnvRough(rf, gLk, 0.04) * (0.9 * fr) + brass * 0.05
                + brass * pow(max(dot(rf, gL), 0.0), 40.0) * 0.8;
        } else {
            vec3 S1 = C - A * o;
            vec3 S2 = C + A * o;
            vec3 n = (length(p - S1) - R) > (length(p - S2) - R) ? normalize(p - S1) : normalize(p - S2);
            if (dot(n, rd) > 0.0) n = -n;
            vec3 local = pp + vec3(mod(gRow, LN_ROWS) * 3.1, float(gLane) * 5.3, 0.0);
            n = glassWobble(n, pp * 2.0, glassChaos());
            n = glassGrain(n, local, clamp(frost * gTexture, 0.0, 1.0));
            vec3 iors = glassIors(GLASS_IOR, 0.012 + 0.045 * gEar.sharp);
            vec2 kc = C.xz;
            float chord = 0.0;
            vec3 seen = vec3(0.0);
            // Green first: its floor read finds the shadows that red and blue reuse.
            for (int c = 0; c < 3; c++) {
                float ior = c == 0 ? iors.y : (c == 1 ? iors.x : iors.z);
                vec3 rt = glassRefract(rd, n, 1.0 / ior);
                vec2 h1 = glassSphere(p, rt, S1, R);
                vec2 h2 = glassSphere(p, rt, S2, R);
                float f1 = h1.y > 0.0 ? h1.y : 1e3;
                float f2 = h2.y > 0.0 ? h2.y : 1e3;
                float tx = min(f1, f2);
                if (tx > 100.0) tx = 0.0;
                vec3 pe = p + rt * tx;
                vec3 ne = f1 < f2 ? normalize(pe - S1) : normalize(pe - S2);
                vec3 out2 = glassRefract(rt, -ne, ior);
                vec3 b = seeBeyond(pe, out2, hitT + tx, 1.0 / gMag, frost * 0.084, kale, kc, c == 0);
                if (c == 0) {
                    seen.g = b.g;
                    chord = tx;
                } else if (c == 1) {
                    seen.r = b.r;
                } else {
                    seen.b = b.b;
                }
            }
            float cosI = clamp(dot(-rd, n), 0.0, 1.0);
            float fr = glassFresnel(cosI, GLASS_IOR);
            vec3 refl = glassEnvRough(reflect(rd, n), gLk, frost * 0.05) * 0.8;
            vec3 beer = glassBeer(mix(vec3(1.0), pal(gHue + 0.5), 0.15), chord, 0.6);
            float rl = rr / ap;
            col = seen * beer * (1.0 - fr) + refl * (fr + 0.06);
            col += glassRim(cosI, gHue + 0.2, 0.35);
            // The polished bevel: a thin bright line just inside the ring.
            col += mix(vec3(1.0), pal(gHue + 0.6), 0.3) * smoothstep(0.9, 0.98, rl) * 0.35;
            col += vec3(1.0) * glassGlint(local, 3.0, gEar.hat * gTexture) * 0.5;
        }
        col = mix(col, fogColour(), fog);
    } else {
        // The floor seen directly is a little defocused: the eye sits on the lenses.
        col = seeBeyond(ro, rd, 0.0, 1.0, 0.072, 0.0, vec2(0.0), true);
    }

    col *= 0.85 + 0.15 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col)), 1.0);
}
