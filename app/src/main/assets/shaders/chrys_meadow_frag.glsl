#version 300 es
// The family spends Morph, Turbulence and Warp on the flowers themselves (see
// below), so view() must not bend the rays a second time.
#define VIEW_OWNS_SHAPE
precision highp float;
// GLSL ES 3.00 defaults fragment sampler2D to LOWP; uSoundTex is R32F.
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

// Chrysanthemum Meadow: a night field of small chrysanthemums, now a real
// field you glide over. The old style drew each flower as a flat rosette
// painted on a ground plane; here every grid cell of the ground grows one
// flower on a thin stem, a head of two cupped petal rings (the family's
// chrysPetal profile, given a thickness and bent up into a bowl) around a
// round centre. A closed flower is a bud, its petals short and cupped
// steeply upward; an open one spreads them out nearly flat. Moonlight from
// above, a little self-light in the petals, a mist that swallows the far
// rows into the sky, and stars overhead.
//
// ---- camera ------------------------------------------------------------------
//
// camFly: the eye rides the rig's path over the field (its glide path wanders
// about +-1.2 around y = 0; the ground lies at CM_GROUND, below every path but
// a wide spiral, and a sphere around the eye is carved out of everything, so a
// low pass skims through the flowers instead of into them). Forward travel is
// the rig's, so the old uFlowPhase glide is now the Camera tab's fly speed.
// The field tiles along z with period CM_ZWRAP (256, the cell hash wraps
// there), the eye's z is rebased by it, and 256 divides the rig's 8192 wrap,
// so neither the rebase nor the wrap ever shows.
//
// ---- audio -------------------------------------------------------------------
//
// The meadow is still a spectrum analyser. Each flower reads the spectrum
// (sndSpectrum) at the frequency of where its centre lands across the screen,
// bass at the left edge and air at the right, and opens that far: a kick
// blooms a drift of flowers down the left of the field, a hi-hat flickers the
// right, and every row shows the whole spectrum, near rows large, far rows
// small. Opening is geometry (petal length and cup) plus a faint glow of the
// petals with it. The centres take uKeyHue when the key is confident.
// Brightness follows 0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE) only.
//
// ---- macros and knobs --------------------------------------------------------
//
// Speed: uTime (already Speed-integrated) drives the sideways drift, the
// flowers' slow turning and the star twinkle. Complexity: uMorph plants the
// field denser (from about half the cells to every cell). Chaos: uTurbulence +
// uWarp vary petal lengths and fray their edges; uTwist curls every flower.
// Energy: uIntensity scales how far the spectrum opens them. Colour: pal()
// throughout. Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE the
// opening gain, k1 TEXTURE the fray, k2 FLOW the drift and turning, k3 TONE the
// hue, k4 BUILD the stem height, k5 SPACE the mist (positive clears it).
//
// ---- why the distance estimate is safe ----------------------------------------
//
// Each cell measures only its own flower. Every flower stays within CM_REACH
// (0.46) of its cell centre (petal reach clamped to 0.38 past a 0.06 base,
// jitter 0.04), so a neighbour's flower is at least the distance to the
// shared cell edge plus 0.04 away; and nothing rises above gTop, so above it
// the vertical gap is a bound too, and the march skips straight down to it.
// The petal distance is the family's 2D bound (exact on the centreline,
// drifting toward the edges); the bowl bends the sheet with slope at most
// ~0.9, the curl adds up to 0.4 per unit of |twist| and the fray up to ~0.6
// per unit of chaos, so the petal field is divided by gLip, which sums them.
// The centre is a sphere squashed 1.4x in y, divided by 1.4. The stem is an
// exact capsule, the ground an exact plane, and all are lower bounds.

#define CM_MAX_STEPS 96
#define CM_FAR 44.0
/** The ground plane, below the rig's paths. */
#define CM_GROUND -2.2
/** Cells along z before the hash repeats: 256 world units, which divides 8192. */
#define CM_ZWRAP 256.0
/** Every flower lies inside this radius of its own cell centre (< 0.5 - margin). */
#define CM_REACH 0.46
/** Longest petal, world units: 0.06 base + 0.38 reach + 0.04 jitter = CM_REACH. */
#define CM_PETAL_MAX 0.38
#define CM_THICK 0.016
#define CM_BUBBLE 0.6

vec3 gEye;
float gAspect;
float gOpenGain;
float gChaos;
float gTwist;
float gKeep;
float gStemK;
float gDriftX;
float gTurnK;
float gLip;
/** The highest point any flower can reach. */
float gTop;

/** Set by map() for the shading: material (0 ground, 1 stem, 2 outer ring, 3 inner ring, 4 centre). */
float gMat;
vec4 gPet;
float gH1;
float gH2;
float gOpen;
float gHeadY;

/** How far this flower opens: the spectrum at its centre's place across the screen. */
float openAt(vec3 centre) {
    vec3 v = centre - gEye;
    float sz = max(dot(v, uCamBasis[2]), 0.25);
    float sx = dot(v, uCamBasis[0]);
    float freq = clamp(0.5 + 0.5 * (sx / sz) * uCamFocal / gAspect, 0.0, 1.0);
    return 0.22 + 0.78 * clamp(sndSpectrum(freq) * gOpenGain, 0.0, 1.15);
}

float map(vec3 p) {
    float dg = p.y - CM_GROUND;
    float bubble = CM_BUBBLE - length(p - gEye);
    gMat = 0.0;
    // Far above the flowers: only the vertical gap matters.
    if (p.y > gTop + 0.3) return max(min(p.y - gTop, dg), bubble);

    vec2 w = vec2(p.x + gDriftX, p.z);
    vec2 cell = floor(w);
    vec2 f = w - cell - 0.5;
    vec2 hc = vec2(cell.x, mod(cell.y, CM_ZWRAP));
    float h1 = chrysHash(hc);
    float h2 = chrysHash(hc + 19.7);
    float h3 = chrysHash(hc + vec2(7.3, 41.1));
    gH1 = h1;
    gH2 = h2;

    // Neighbours keep inside their own cells (see the header).
    float toEdge = 0.5 - max(abs(f.x), abs(f.y));
    float d = max(toEdge + (0.5 - CM_REACH), p.y - gTop);

    if (h3 < gKeep) {
        vec2 jitter = (vec2(h1, h2) - 0.5) * 0.08;
        vec2 q = f - jitter;
        float open = openAt(vec3(cell.x + 0.5 + jitter.x - gDriftX, CM_GROUND + 0.6, cell.y + 0.5 + jitter.y));
        gOpen = open;
        float bloom = clamp(open, 0.0, 1.0);
        float headY = CM_GROUND + (0.45 + 0.5 * h2) * gStemK + 0.12 * bloom;
        gHeadY = headY;

        float count = 8.0 + floor(h1 * 9.0);
        float len = 0.3 * open;
        // A bud cups steeply; an open flower lies nearly flat.
        float cup = mix(2.4, 0.5, bloom);
        vec2 lp = chrysTurn(h2 * CHRYS_TAU + uTime * 0.05 * (h1 - 0.5) * gTurnK) * q;
        float r = length(lp);
        float pid = floor((atan(lp.y, lp.x) - gTwist * (r - 0.06)) / (CHRYS_TAU / count) + 0.5);
        float lenJ = min(len * (1.0 + 0.12 * gChaos * chrysWobble(mod(pid, count) + h1 * 50.0, uTime)), CM_PETAL_MAX);

        vec4 pet = chrysPetal(lp, count, 0.06, lenJ, 0.5, gTwist, 0.35, 0.45);
        pet.x += chrysFray(lp * 0.5, gChaos);
        float d1 = max(pet.x, abs(p.y - (headY + cup * r * r)) - CM_THICK);
        vec4 pet2 = chrysPetal(chrysTurn(CHRYS_PI / count) * lp, count, 0.04, lenJ * 0.7, 0.55, gTwist, 0.6, 0.3);
        float d2 = max(pet2.x, abs(p.y - (headY + 0.03 + 1.4 * cup * r * r)) - CM_THICK);
        d1 /= gLip;
        d2 /= gLip;

        float dc = (length(vec3(q.x, (p.y - headY - 0.025) * 1.4, q.y)) - 0.065) / 1.4;
        float sy = clamp(p.y, CM_GROUND, headY);
        float ds = length(vec3(q.x, p.y - sy, q.y)) - 0.02;

        float best = d1;
        float mat = 2.0;
        gPet = pet;
        if (d2 < best) {
            best = d2;
            mat = 3.0;
            gPet = pet2;
        }
        if (dc < best) {
            best = dc;
            mat = 4.0;
        }
        if (ds < best) {
            best = ds;
            mat = 1.0;
        }
        if (best < d) {
            d = best;
            gMat = mat;
        }
    }

    if (dg < d) {
        d = dg;
        gMat = 0.0;
    }
    return max(d, bubble);
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

vec3 skyColour(vec3 rd, float hue) {
    float up = clamp(rd.y, -1.0, 1.0);
    vec3 col = mix(pal(hue + 0.05) * 0.12, pal(hue + 0.6) * 0.02, smoothstep(0.0, 0.9, up));
    // A few fixed stars, each a few pixels, twinkling on the Speed clock.
    vec3 sp = rd * 120.0;
    float s = hashCell(ivec3(floor(sp)));
    float star = (1.0 - smoothstep(0.0, 0.3, length(fract(sp) - 0.5))) * step(0.995, s);
    col += vec3(0.8, 0.85, 1.0) * star * 0.35 * smoothstep(0.03, 0.25, up)
         * (0.6 + 0.4 * sin(uTime * (1.0 + 3.0 * fract(s * 97.0)) + s * 300.0));
    return col;
}

void main() {
    vec2 uv = view();
    gAspect = uResolution.x / uResolution.y;

    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    float swell = clamp(0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE), 0.0, 1.5);
    float t0 = uTime;  // uTime already integrates Speed (ShaderScene::update)
    float hue = 0.75 + 0.02 * sin(t0 * 0.02) + 0.12 * kTone;
    float keyHue = mix(hue + 0.12, uKeyHue, clamp(uKeyStrength, 0.0, 1.0) * 0.8);

    gOpenGain = uIntensity * (1.0 + kPulse);
    gChaos = clamp(uTurbulence + uWarp + 1.2 * kTexture, 0.0, 2.0);
    gTwist = 3.0 * clamp(uTwist, -1.0, 1.0);
    gKeep = 0.5 + 0.5 * clamp(uMorph, 0.0, 1.0);
    gStemK = 1.0 + 0.8 * kBuild;
    gTurnK = 1.0 + 1.4 * kFlow;
    gDriftX = 1.7 * sin(t0 * 0.031) * (1.0 + kFlow);
    gLip = 1.6 + 0.4 * abs(gTwist) + 0.6 * gChaos;
    // Stem top plus the bloom lift, the bowl's rise and the centre.
    gTop = CM_GROUND + 0.95 * gStemK + 0.12 + 0.25;

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 256, so this shift is exact
    // and invisible, and z keeps its precision far down the travel.
    ro.z = mod(ro.z, CM_ZWRAP);
    gEye = ro;

    vec3 sky = skyColour(rd, hue);
    vec3 col = sky;

    // Skip the empty air above the flowers; a ray rising from up there is sky.
    float t = 0.03;
    bool march = true;
    if (ro.y > gTop) {
        if (rd.y > -1e-3) {
            march = false;
        } else {
            t = max(t, (ro.y - gTop) / -rd.y);
        }
    }

    float hitT = -1.0;
    float used = 0.0;
    if (march && t < CM_FAR) {
        for (int i = 0; i < CM_MAX_STEPS; i++) {
            // uSteps is the user's Detail; this loop's own bound is the constant.
            if (float(i) >= uSteps) break;
            used = float(i);
            vec3 p = ro + rd * t;
            // Climbing out above the flowers: nothing left to hit.
            if (rd.y > 0.0 && p.y > gTop) break;
            float d = map(p);
            float eps = 0.0008 * t + 0.0008;
            if (d < eps) {
                hitT = t;
                break;
            }
            t += max(d * 0.85, eps);
            if (t > CM_FAR) break;
        }
    }

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, max(0.0015, 0.001 * hitT));
        float occ = 0.0;
        float sc = 1.0;
        for (int k = 0; k < 2; k++) {
            float hk = 0.06 + 0.12 * float(k);
            occ += (hk - map(p + n * hk)) * sc;
            sc *= 0.6;
        }
        float ao = clamp(1.0 - 2.0 * occ, 0.3, 1.0) * clamp(1.0 - used / max(uSteps, 1.0) * 0.5, 0.5, 1.0);
        // Restore the hit cell's globals after the normal and AO taps.
        map(p);

        vec3 L = normalize(vec3(-0.3, 0.85, 0.45));
        float ndl = dot(n, L);
        float ndv = clamp(dot(n, -rd), 0.0, 1.0);
        vec3 ground = pal(hue + 0.36) * 0.035 * (0.7 + 0.6 * gH2);
        // World size of one pixel at this depth: petal detail fades before it shimmers.
        float foot = hitT * 2.0 / (uResolution.y * uCamFocal);
        float detail = smoothstep(0.05, 0.015, foot);
        vec3 c;
        if (gMat < 0.5) {
            c = ground * (0.6 + 0.6 * max(ndl, 0.0)) * ao;
        } else if (gMat < 1.5) {
            c = pal(hue + 0.36) * 0.09 * (0.4 + 0.6 * max(ndl, 0.0)) * ao;
        } else if (gMat < 3.5) {
            bool outer = gMat < 2.5;
            float cupS = outer ? 0.35 : 0.6;
            vec3 base = pal(hue + 0.35 * gH1 + (outer ? 0.0 : 0.06)) * (outer ? 1.0 : 0.85);
            vec3 petCol = base * mix(0.55, 1.0, clamp(gPet.y, 0.0, 1.0)) * chrysPetalShade(gPet, cupS);
            // Thin petals: lit from either side, the far side a little dimmer.
            float lit = 0.3 + 0.55 * abs(ndl) + 0.15 * max(ndl, 0.0);
            c = petCol * lit * ao
              + petCol * (0.12 + 0.3 * clamp(gOpen, 0.0, 1.0))
              + base * pow(1.0 - ndv, 3.0) * 0.12;
            vec3 avg = mix(ground, pal(hue + 0.35 * gH1) * 0.6, 0.25 + 0.4 * clamp(gOpen, 0.0, 1.0));
            c = mix(avg, c, detail);
        } else {
            vec3 core = pal(keyHue);
            float spec = pow(max(dot(reflect(rd, n), L), 0.0), 24.0);
            c = core * (0.35 + 0.5 * max(ndl, 0.0)) * ao + core * 0.25 * clamp(gOpen, 0.0, 1.0) + vec3(spec) * 0.15;
            c = mix(mix(ground, core * 0.6, 0.5), c, detail);
        }
        // Mist toward the horizon; SPACE thins or thickens it.
        float mist = 0.07 * (1.0 - 0.8 * kSpace);
        col = mix(sky, c, exp(-hitT * mist));
    }

    col *= 0.85 + 0.15 * swell;
    fragColor = vec4(grade(col), 1.0);
}
