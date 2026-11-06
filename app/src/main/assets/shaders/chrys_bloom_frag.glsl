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

// Chrysanthemum Bloom: the family's front door, now a real incurved bloom
// with depth. The flat style drew five rings of petals stacked on a glowing
// core and faked the stacking with a shadow per ring; here the rings ARE
// stacked. Each ring is a ring of thin petal blades in 3D: its 2D outline is
// the same chrysPetal fold the flat style used (same counts, radii, widths,
// cups and tips, so it still reads as the family's flower), and its blade
// rises from a base height toward the tip on a parabola, the inner rings set
// higher and curled steeply upward, the outer ones low and nearly flat, so
// the head is a dome that tightens toward its heart. A cupped blade also
// lifts at its edges. The petals cast real soft shadows on the rings below
// (a short shadow march toward the key light) and glow through when lit from
// behind. The core is a squat glowing dome of Vogel florets.
//
// ---- camera -------------------------------------------------------------------
//
// The rig, object-centred like camAround, but with the eye raised 54 degrees
// above the flower's plane, so the orbit circles the bloom from above (its
// identity is a flower seen from above) and the depth shows as the view
// swings. Orbit rate is an integer multiple of CAM_SPIN_UNIT, so the 8192
// travel wrap never shows; nothing in the field depends on z, so there is no
// world period to fit. The dolly pulls the eye in, the rig's look tilts it.
//
// ---- audio --------------------------------------------------------------------
//
// Kick (SND_KICK_ENV) OPENS the flower, as before: every ring is pushed
// outward and its blades flatten, the inner rings by a higher power of the
// envelope so they lag the outer ones and the bloom ripples out from the
// centre. The rings counter-rotate on uDrift (an integrated angle, so a hit
// never reverses them). (0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)) sets how
// open the flower rests between kicks and lifts the core a little.
// SND_ENERGY_TREND rides the overall level. Hue drifts on time alone. Nothing
// brightens on a hit: the kick moves petals, the swell is the only light.
//
// ---- macros and knobs ------------------------------------------------------------
//
// Speed: uTime (spin and hue). Complexity: uMorph adds up to two more rings
// (7 at most). Chaos: uTurbulence + uWarp give each petal its own length and
// a frayed edge; uTwist curls the rings into a spiral. Energy: uIntensity
// scales the kick. Colour: pal(). Space: uZoom through view() and the rig.
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE the kick's
// opening, k1 TEXTURE the fray and the rims, k2 FLOW the ring spin, k3 TONE
// the hue, k4 BUILD the core's glow, k5 SPACE the orbit radius.
//
// ---- budget -------------------------------------------------------------------
//
// One bounding-sphere test first, so rays that miss the bloom never march.
// 80-step ceiling broken on uSteps (Detail); below Detail 48 the shadow march
// is skipped. Per ring the field costs one fold and one profile; the angle is
// shared by every ring, the ring parameters are worked out once per pixel in
// main, and a ring whose bounding band is further than the best distance so
// far is skipped. The fray noise is evaluated once, for the nearest ring.
//
// ---- why the distance estimate is safe ----------------------------------------
//
// A ring's distance is max(outline, height): the outline is chrysPetal's
// bound (angle times radius across, exact along the radius), the height term
// is the gap to the blade's surface over BL_LIP. The blade's slope along the
// radius is 2 * lift * u <= 1.7. Across it the cup term reads |across|
// clamped to the petal's half-width h, and h <= 0.55 * 0.86 * len (the widest
// profile), so its slope is 1.8 * cup * h / max(len, 0.15) <= 0.72. The
// height field's gradient is under sqrt(1 + 1.7^2 + 0.72^2) < 2.1, inside
// BL_LIP = 2.3. The curl (uTwist, clamped to 1) and the fray (<= 0.04)
// loosen the outline a little more, which the 0.6 step factor absorbs. The
// ring skip compares a true lower bound (radial band and height band, both at
// most the Euclidean distance) against an estimate that is itself below the
// true distance, so it can only skip rings that are really further away.

#define BL_MAX_RINGS 7
#define BL_MAX_STEPS 80
#define BL_SHADOW_STEPS 12
#define BL_BOUND 1.3
/** The flower's plane sits this far below the orbit centre, so the dome's mass is centred. */
#define BL_DROP 0.16
/** Petal blade half-thickness at the base (it halves toward the tip). */
#define BL_THICK 0.012
/** Gradient bound of the blade height term (see the header). */
#define BL_LIP 2.3
/** Eye elevation above the flower's plane, radians. */
#define BL_ELEV 0.95

/** Per ring: (r0 base radius, len, petal count, spin). */
vec4 gRingA[BL_MAX_RINGS];
/** Per ring: (base height, lift, cup, width). */
vec4 gRingB[BL_MAX_RINGS];
float gRingTip[BL_MAX_RINGS];
float gRings;
float gChaos;
float gCurl;
float gT;
float gCoreY;
float gCoreR;
/** Set by map(): 0 core, 1 + ring index on a petal; the petal's u, v and id. */
float gMat;
float gU;
float gV;

float map(vec3 p) {
    vec3 q = vec3(p.x, p.y + BL_DROP, p.z);
    float r = length(q.xz);
    // atan(0, 0) is undefined in GLSL ES; the axis is inside the core anyway.
    float a = r > 1e-5 ? atan(q.z, q.x) : 0.0;

    vec3 cq = q - vec3(0.0, gCoreY, 0.0);
    float best = (length(vec3(cq.x, cq.y / 0.6, cq.z)) - gCoreR) * 0.6;
    gMat = 0.0;
    gU = 0.0;
    gV = 0.0;
    float bestA = 0.0;

    for (int i = 0; i < BL_MAX_RINGS; i++) {
        float k = float(i);
        if (k >= gRings) break;
        vec4 A = gRingA[i];
        vec4 B = gRingB[i];
        // A true lower bound on the distance to this ring: its radial band
        // and its height band, both at the longest petal chaos can make
        // (gChaos reaches 3 with Texture up, a 42 % stretch).
        float stretch = 1.0 + 0.14 * gChaos;
        float top = B.x + A.y * stretch * B.y + 0.03;
        float lb = max(max(A.x - r, r - A.x - A.y * stretch), max(B.x - BL_THICK - q.y, q.y - top - BL_THICK));
        if (lb > best) continue;

        float ak = a - A.w - gCurl * (r - A.x);
        float sector = CHRYS_TAU / A.z;
        float id = floor(ak / sector + 0.5);
        float across = (ak - id * sector) * r;
        float len = A.y;
        if (gChaos > 0.001) len *= 1.0 + 0.14 * gChaos * chrysWobble(mod(id, A.z) + 17.0 * k, gT);
        float u = (r - A.x) / max(len, 1e-4);
        float h = B.w * len * chrysProfile(u, B.z, gRingTip[i]);
        h = max(min(h, 0.49 * sector * r), 1e-4);
        float d2 = max(abs(across) - h, (abs(u - 0.5) - 0.5) * len);

        float uc = clamp(u, 0.0, 1.0);
        float ac = clamp(across, -h, h);
        float yc = B.x + len * B.y * uc * uc + 0.9 * B.z * ac * ac / max(len, 0.15);
        float dy = (abs(q.y - yc) - BL_THICK * (1.0 - 0.5 * uc)) / BL_LIP;
        float d = max(d2, dy);
        if (d < best) {
            best = d;
            gMat = 1.0 + k;
            gU = uc;
            gV = across / h;
            bestA = ak;
        }
    }
    // The frayed edge, once, for the nearest ring only.
    if (gChaos > 0.001 && gMat > 0.5) best += chrysFray(r * vec2(cos(bestA), sin(bestA)), gChaos);
    return best;
}

vec3 normalAt(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * eps) + k.yyx * map(p + k.yyx * eps)
                   + k.yxy * map(p + k.yxy * eps) + k.xxx * map(p + k.xxx * eps));
}

void main() {
    vec2 uv = view();
    float t = uTime;  // uTime already integrates Speed (ShaderScene::update)
    gT = t;

    float kPulse = clamp(uKnob[0], 0.0, 1.0) - 0.5;
    float kTexture = clamp(uKnob[1], 0.0, 1.0) - 0.5;
    float kFlow = clamp(uKnob[2], 0.0, 1.0) - 0.5;
    float kTone = clamp(uKnob[3], 0.0, 1.0) - 0.5;
    float kBuild = clamp(uKnob[4], 0.0, 1.0) - 0.5;
    float kSpace = clamp(uKnob[5], 0.0, 1.0) - 0.5;

    float swell = clamp((0.5 * (SND_BASS_PRESENCE + SND_MID_PRESENCE)), 0.0, 1.5);
    float trend = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float kick = clamp(SND_KICK_ENV * uIntensity * (1.0 + kPulse), 0.0, 1.5);
    gChaos = clamp(uTurbulence + uWarp, 0.0, 2.0) * clamp(1.0 + kTexture, 0.3, 1.7);
    gCurl = clamp(uTwist, -1.0, 1.0);
    gRings = 5.0 + floor(2.0 * clamp(uMorph, 0.0, 1.0) + 0.5);
    float rest = 0.78 + 0.12 * swell;
    float hue = t * 0.006 + 0.12 * kTone;

    // The rings, once per pixel: the flat style's radii and counts, given heights.
    for (int i = 0; i < BL_MAX_RINGS; i++) {
        float k = float(i);
        float f = clamp(k / (gRings - 1.0), 0.0, 1.0);  // 0 inner .. 1 outer
        float count = floor(7.0 + 16.0 * k / (gRings - 1.0));
        float open = pow(min(kick, 1.0), 1.0 + 1.5 * (1.0 - f));
        float push = 0.09 * open * max(kick, 1.0);
        float tipR = (0.22 + 0.72 * f) * (rest + push);
        float len = tipR * (0.62 - 0.12 * f);
        float dir = mod(k, 2.0) < 0.5 ? 1.0 : -1.0;
        float spin = dir * (uDrift * 0.15 * (1.0 + kFlow) + t * 0.02) + chrysRingOffset(k, count);
        float cup = mix(0.85, 0.25, f);
        // Inner rings sit high and curl up hard; the kick flattens them open.
        float base = (0.30 + 0.04 * swell) * pow(1.0 - f, 1.3);
        float lift = mix(0.85, 0.12, f) * (1.0 - 0.55 * min(open, 1.0));
        gRingA[i] = vec4(tipR - len, len, count, spin);
        gRingB[i] = vec4(base, lift, cup, mix(0.55, 0.42, f));
        gRingTip[i] = mix(0.2, 0.55, f);
    }
    gCoreY = gRingB[0].x + 0.02;
    gCoreR = 0.08 * rest * (1.0 + 0.08 * swell);

    // Object-centred orbit from above: camAround's eye, pitched down BL_ELEV.
    float orbit = uCamTravel * (4.0 * CAM_SPIN_UNIT);
    mat3 turn = mat3(cos(orbit), 0.0, -sin(orbit), 0.0, 1.0, 0.0, sin(orbit), 0.0, cos(orbit));
    float ce = cos(BL_ELEV);
    float se = sin(BL_ELEV);
    mat3 pitch = mat3(1.0, 0.0, 0.0, 0.0, ce, se, 0.0, -se, ce);
    mat3 basis = turn * pitch * uCamBasis;
    float radius = 2.5 * (1.0 + 0.4 * kSpace) * (1.0 - 0.35 * uCamDolly);
    vec3 ro = -basis[2] * radius;
    vec3 rd = normalize(basis * vec3(uv, uCamFocal));

    // Background: the flat style's dark wash and the halo the flower sits in.
    float sr = length(uv);
    vec3 bg = pal(hue + 0.55) * (0.035 + 0.05 * exp(-sr * 1.6));
    vec3 col = bg;
    float buildK = clamp(1.0 + 1.2 * kBuild, 0.2, 2.2);

    float b = dot(ro, rd);
    float disc = b * b - (dot(ro, ro) - BL_BOUND * BL_BOUND);
    if (disc > 0.0) {
        float sq = sqrt(disc);
        float tm = max(-b - sq, 0.01);
        float tEnd = -b + sq;
        float hitT = -1.0;
        float used = 0.0;
        for (int i = 0; i < BL_MAX_STEPS; i++) {
            // uSteps is the user's Detail; this loop's own bound is the constant.
            if (float(i) >= uSteps) break;
            if (tm > tEnd) break;
            used = float(i);
            float d = map(ro + rd * tm);
            float eps = 0.0006 * tm + 0.0004;
            if (d < eps) {
                hitT = tm;
                break;
            }
            tm += max(d * 0.6, eps);
        }

        if (hitT > 0.0) {
            vec3 p = ro + rd * hitT;
            vec3 n = normalAt(p, 0.0008 * hitT + 0.0005);
            vec3 L = normalize(vec3(0.35, 0.9, 0.25));

            // Soft shadow: the rings above shade the rings below.
            float sh = 1.0;
            if (uSteps >= 48.0) {
                float st = 0.02;
                for (int j = 0; j < BL_SHADOW_STEPS; j++) {
                    float d = map(p + L * st);
                    sh = min(sh, 9.0 * d / st);
                    if (sh < 0.05) break;
                    st += clamp(d, 0.015, 0.12);
                }
                sh = clamp(sh, 0.0, 1.0);
                sh = 0.3 + 0.7 * sh * sh * (3.0 - 2.0 * sh);
            }
            // Ambient occlusion: two taps along the normal.
            float o1 = map(p + n * 0.03);
            float o2 = map(p + n * 0.08);
            float ao = clamp(1.0 - 4.0 * ((0.03 - o1) + 0.5 * (0.08 - o2)), 0.35, 1.0);
            ao *= clamp(1.0 - used / max(uSteps, 1.0) * 0.6, 0.5, 1.0);
            // Restore the hit point's globals after the shading taps.
            map(p);

            // A blade has two faces; light the one we are looking at.
            vec3 nf = dot(n, rd) > 0.0 ? -n : n;
            float dif = max(dot(nf, L), 0.0);
            float through = max(dot(-nf, L), 0.0);
            float spec = pow(max(dot(reflect(rd, nf), L), 0.0), 24.0);

            if (gMat < 0.5) {
                vec3 q = vec3(p.x, p.y + BL_DROP, p.z);
                vec4 fl = chrysFloret(q.xz, 0.011);
                float dot0 = 1.0 - smoothstep(0.003, 0.008, fl.x);
                vec3 corec = mix(pal(hue + 0.6), vec3(1.0, 0.92, 0.75), 0.5);
                col = corec * (0.55 + 0.45 * dif * sh) * (0.7 + 0.3 * dot0) * 0.9;
                col += pal(hue + 0.6) * 0.15 * buildK;
            } else {
                float k = gMat - 1.0;
                float f = clamp(k / (gRings - 1.0), 0.0, 1.0);
                float cup = mix(0.85, 0.25, f);
                float shade = chrysPetalShade(vec4(0.0, gU, gV, 0.0), cup);
                vec3 base = pal(hue + 0.08 * k);
                vec3 tipc = mix(pal(hue + 0.08 * k + 0.12), vec3(1.0), 0.25);
                vec3 pc = mix(base * 0.55, tipc, smoothstep(0.1, 1.0, gU)) * shade;
                col = pc * (0.16 + 0.95 * dif * sh) * ao + pc * through * 0.45 * sh;
                // A thin bright rim so neighbouring petals separate.
                col += tipc * 0.16 * clamp(1.0 + kTexture, 0.2, 1.8) * smoothstep(0.75, 1.0, abs(gV)) * ao;
                col += mix(vec3(1.0), tipc, 0.5) * spec * 0.12 * sh;
            }
            col = mix(col, bg, smoothstep(radius, radius + 2.0, hitT) * 0.5);
        }
    }

    // The core's light, over everything, as the flat style laid it: the
    // ray's closest approach to the core centre.
    vec3 cc = vec3(0.0, gCoreY - BL_DROP, 0.0) - ro;
    float along = max(dot(cc, rd), 0.0);
    float miss = length(cc - rd * along);
    col += pal(hue + 0.6) * 0.12 * chrysCore(miss, 0.3) * (0.7 + 0.3 * swell) * buildK;

    col *= 0.85 + 0.15 * swell;
    col *= 0.9 + 0.1 * trend;
    col *= smoothstep(2.0, 0.6, sr) * 0.25 + 0.75;
    fragColor = vec4(grade(col), 1.0);
}
