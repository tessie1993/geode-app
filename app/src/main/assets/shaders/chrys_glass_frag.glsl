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

// Chrysanthemum Glass: the rose window, now a cathedral you walk down. The
// 2D style was one window seen face on: the flower cut into panes of
// coloured glass held in black lead, six rings of petals (6, 6, 12, 12, 24,
// 24, the counts a glazier would pick) over polar lozenges, a round boss and
// a stone border pierced with round foils, the light strongest in the middle
// as if the sun stood right behind it. Here that same window is glazed into
// every bay of an endless Gothic nave, one in each side wall, set back in a
// deep round embrasure. Half-round piers stand between the bays and carry
// pointed transverse ribs over a pointed barrel vault.
//
// The sun stands outside the left wall. Its light comes through the left
// windows and lays each one's flower on the floor and the far wall as a pool
// of coloured light (the window pattern projected along the sun, lead shadows
// and all), and it draws coloured shafts through the dust of the nave. The
// right windows glow dimmer, lit by the sky. Far down the nave, at infinity,
// a great west rose turns very slowly in the haze: the 2D window itself,
// always straight ahead down the travel axis, so the flight has a goal.
//
// ---- the camera -------------------------------------------------------------
//
// The shared rig, camFly: the eye rides the path down +z, held inside the
// nave (|x| < width - 1, above the floor, below the vault). Every z term has a
// period dividing 1024: the bays (8), each window's hash (128 windows a side
// per 1024), the floor flags and wall courses (1 unit) and the dust (16 / k).
// So the eye is rebased by mod 1024, which divides the rig's 8192 wrap and
// keeps highp z precise far down the travel.
//
// ---- audio ------------------------------------------------------------------
//
// As in 2D, pitch moves the colour. uKeyHue (the detected key, eased round
// the hue circle) WALKS every pane of every window together as the harmony
// moves, gated by uKeyStrength. Each pitch change (SND_HELD_NOTE_CHANGE) sends
// a hue ripple out from each boss to its border as the envelope decays, so a
// melody leaves rings turning colour all down the nave, and the coloured
// pools on the floor follow it. Spectral presence (sndSpectrum(0.80)) swings
// the sun a little, which tilts the shafts and slides the pools. Brightness
// follows SND_INTENSITY only: hits never light the glass.
// Knobs: k0 PULSE the ripple, k1 TEXTURE the streaks in the glass, k2 FLOW
// the dust in the shafts, k3 TONE the hue, k4 BUILD the sun, k5 SPACE the
// nave's width.
//
// ---- macros -----------------------------------------------------------------
//
// Kept from 2D. Speed: uTime (already Speed-integrated) turns the west rose.
// Complexity: uMorph adds up to two more petal rings. Chaos: uTurbulence +
// uWarp warp the lead into hand-cut, uneven panes; uTwist curls the petal
// rings into a pinwheel. Energy: uIntensity scales the pitch ripple. Colour:
// pal() for every pane, pool, shaft and spill (lead and stone are fixed).
// Symmetry: uSymmetry is the glazier's base count. Detail: uSteps.
//
// ---- why the estimate is safe ----------------------------------------------
//
// The nave's air is the intersection of two circles (the pointed vault, an
// exact distance to each, with y clamped up to the springing line so below
// it they become the two straight walls; clamping keeps it 1-Lipschitz) and
// the half-space above the floor. The window embrasures are exact capped
// cylinders unioned into the air. The piers are exact vertical cylinders and
// the ribs are tubes round the vault's own level set, 1-Lipschitz because the
// vault term is. The bay fold uses |z - nearest pier| and z - window centre,
// both continuous across the fold (the embrasure is far from the fold plane,
// so its two sides agree there). min and max of bounds are bounds, so the
// march never overshoots. A sphere round the eye is carved out of everything.
// The lead, the panes and the light pools are shading only, never geometry.
//
// Budget: 96-step ceiling broken on uSteps, 4-tap normals, one AO tap, one
// full window evaluation per glass or lit pixel (the pools reuse it), and ten
// shaft samples that read a cheap pane tint, never the full window.

#define CG_MAX_STEPS 96
#define CG_FAR 48.0
#define CG_TAU 6.2831853
/** Bay length along z: a pier, then a window, every 8 units (divides 1024). */
#define CG_BAY 8.0
#define CG_ZWRAP 1024.0
/** Windows per side per 1024 units of z. */
#define CG_WINDOWS 128.0
#define CG_FLOOR -2.4
/** Where the walls stop and the vault springs. */
#define CG_SPRING 2.8
/** Radius of each of the vault's two arcs; larger than the half-width, so the vault is pointed. */
#define CG_VAULT_R 6.0
#define CG_WIN_Y 0.8
#define CG_WIN_R 1.6
/** How deep each window sits in its embrasure. */
#define CG_DEPTH 0.35
#define CG_PIER 0.42
#define CG_RIB 0.2
#define CG_BUBBLE 0.6
#define CG_SHAFT_STEPS 10
/** The 2D window's radius in its own units: the panes end here. */
#define RW_WINDOW 0.9
#define RW_LEAD 0.0065

/** Half-width of the nave (k5 SPACE). */
float gW;
/** The eye, z rebased into 0..1024. */
vec3 gEye;
// The window's per-frame state, set once in main.
float gRings;
float gBase;
float gChaos;
float gPitch;
float gHue;
float gRipple;
float gStreak;
/** Twist's sweep of the window's petals. */
float gCurl;

// ---- the window ---------------------------------------------------------------

vec3 glassCol(float h) {
    vec3 g = pal(h);
    return g * g * 1.25 + 0.04;
}

/**
 * The 2D rose window at `p` (its own units, the panes inside r = 0.9, the
 * stone border with its foils out to 0.9), for window `wid`. `aa` is the
 * pixel's footprint in those units: lines never go thinner than it and the
 * fine streaks and bubbles fade before they alias. Returns the colour of the
 * light through the glass, before the sun's strength is applied.
 */
vec3 roseGlass(vec2 p, float wid, float aa) {
    float r = length(p);
    float ang = atan(p.y, p.x);
    // The default pane: polar lozenges behind the petals.
    float sector = CHRYS_TAU / 48.0;
    float band = 0.09;
    float la = (ang / sector - floor(ang / sector) - 0.5) * sector * r;
    float lr = (fract(r / band) - 0.5) * band;
    vec2 pane = vec2(0.0, floor(ang / sector) + 48.0 * floor(r / band));
    float paneT = r / RW_WINDOW;
    float lead = min(abs(abs(la) - 0.5 * sector * r), abs(abs(lr) - 0.5 * band));

    // A pane laid on top hides the lead of the panes beneath it.
    vec2 pc = p + gChaos * 0.025 * (vec2(chrysNoise(p * 6.0), chrysNoise(p * 6.0 + 9.0)) - 0.5);
    for (int i = 7; i >= 0; i--) {
        float k = float(i);
        if (k >= gRings) continue;
        float f = k * 5.0 / (gRings - 1.0);
        float count = gBase * pow(2.0, floor(f * 0.5));
        float tipR = 0.2 + 0.124 * f;
        float len = 0.16 + 0.05 * f;
        vec2 q = chrysTurn(chrysRingOffset(k, count)) * pc;
        vec4 pet = chrysPetal(q, count, tipR - len, len, 0.62, gCurl, 0.45, 0.7);
        if (pet.x < 0.0) {
            pane = vec2(k + 1.0, mod(pet.w, count));
            paneT = k / (gRings - 1.0);
            lead = abs(pet.x);
        } else {
            lead = min(lead, pet.x);
        }
    }
    // The boss.
    if (r < 0.09) {
        pane = vec2(9.0, floor(mod(ang / (CHRYS_TAU / 6.0) + 6.0, 6.0)));
        paneT = 0.0;
        lead = min(abs(r - 0.09), r < 0.035 ? 0.035 - r : 1.0);
    } else {
        lead = min(lead, abs(r - 0.09));
    }

    // Hue: the pane's own hash (and its window's), the key walk, and the
    // pitch ripple travelling outward as the envelope decays.
    float front = 1.0 - gPitch;
    float rx = (paneT - front) * 4.0;
    float ripple = exp(-rx * rx) * sqrt(chrysBump(gPitch));
    float h = gHue + 0.38 * chrysHash(pane * vec2(1.7, 3.1) + wid * vec2(0.61, 0.37)) + gRipple * ripple;
    vec3 glass = glassCol(h);
    float seed = chrysHash(pane + 11.0 + wid);
    vec2 sp = chrysTurn(seed * CHRYS_TAU) * p;
    // Hand-blown streaks and seed bubbles, faded out before they alias.
    float fine = 1.0 - smoothstep(0.004, 0.02, aa);
    float streak = 1.0 - gStreak * fine + gStreak * fine * motionNoise(sp * vec2(46.0, 5.0));
    float bubbles = step(0.985, chrysHash(floor(sp * 140.0))) * (1.0 - smoothstep(0.002, 0.006, aa));
    float sun = 0.5 + 0.5 * exp(-r * 1.8);
    vec3 col = glass * streak * sun + vec3(0.06) * bubbles * sun;

    // Lead came: dark, with a dull highlight down its middle. Thinner than
    // a pixel, it covers only its share of the pixel.
    float lw = RW_LEAD;
    float cover = min(1.0, 2.0 * lw / max(aa, 1e-4));
    float lm = (1.0 - smoothstep(lw - aa, lw + aa, lead)) * cover;
    vec3 leadCol = vec3(0.02, 0.02, 0.025) + vec3(0.03) * (1.0 - smoothstep(0.0, lw, lead));
    col = mix(col, leadCol, lm);

    // Stone border with round foils.
    if (r > 0.84) {
        float fs = CHRYS_TAU / 30.0;
        float fa = (fract(ang / fs) - 0.5) * fs;
        vec2 fq = vec2(r - 0.87, fa * 0.87);
        float foil = length(fq) - 0.018;
        float fid = floor(ang / fs);
        vec3 fglass = glassCol(gHue + 0.5 + 0.2 * chrysHash1(fid + wid)) * 0.8;
        float stoneShade = 0.05 + 0.03 * smoothstep(0.84, 0.87, r) * (1.0 - smoothstep(0.87, 0.9, r));
        vec3 stoneCol = vec3(stoneShade * 1.1, stoneShade, stoneShade * 0.85);
        vec3 border = mix(stoneCol, fglass, 1.0 - smoothstep(-aa, aa, foil));
        border = mix(border, leadCol, (1.0 - smoothstep(lw - aa, lw + aa, abs(foil))) * cover);
        col = mix(col, border, smoothstep(0.84 - aa, 0.84 + aa, r));
    }
    col *= 1.0 - 0.7 * smoothstep(RW_WINDOW - 0.01, RW_WINDOW, r);
    return col;
}

/**
 * A cheap stand-in for roseGlass for the shafts, which read the window ten
 * times a pixel: the same ring counts and pane hues by band and sector, no
 * petals and no lead. Through dust nobody can see the difference.
 */
vec3 roseTint(vec2 p, float wid) {
    float r = length(p);
    if (r > 0.84) return vec3(0.0);
    float ang = atan(p.y, p.x);
    float ring = min(floor(r * 8.0), 7.0);
    float count = gBase * pow(2.0, floor(ring * 0.5));
    float sid = mod(floor(ang / (CHRYS_TAU / count)), count);
    float h = gHue + 0.38 * chrysHash(vec2(ring + 1.0, sid) * vec2(1.7, 3.1) + wid * vec2(0.61, 0.37));
    return glassCol(h) * (0.5 + 0.5 * exp(-r * 1.8)) * 0.85;
}

// ---- the nave -------------------------------------------------------------------

/** The vault and walls as one air term: negative in the nave, 0 on the stone. */
float vaultAir(vec3 p) {
    float ax = abs(p.x);
    // The farther arc's centre is across the nave: its circle is the near wall's.
    float off = CG_VAULT_R - gW;
    return length(vec2(ax + off, max(p.y, CG_SPRING) - CG_SPRING)) - CG_VAULT_R;
}

/** The style's world at p: its distance, and in `found` what is there (the stone and the window's embrasure only: no slots). */
float mapAt(vec3 p, inout ChrysHit found) {
    float ax = abs(p.x);
    float zb = mod(p.z, CG_BAY);
    // Distance along z to the nearest pier plane, and offset from the window centre.
    float zp = min(zb, CG_BAY - zb);
    float zw = zb - 0.5 * CG_BAY;

    float vault = vaultAir(p);
    float air = max(vault, CG_FLOOR - p.y);
    // The window embrasure: a capped cylinder across the nave, its caps the glass.
    float rho = length(vec2(p.y - CG_WIN_Y, zw));
    vec2 dd = vec2(rho - CG_WIN_R, ax - (gW + CG_DEPTH));
    float cyl = min(max(dd.x, dd.y), 0.0) + length(max(dd, 0.0));
    air = min(air, cyl);
    float d = -air;

    float pier = length(vec2(ax - gW, zp)) - CG_PIER;
    float rib = length(vec2(vault, zp)) - CG_RIB;
    d = min(d, min(pier, rib));
    return max(d, CG_BUBBLE - length(p - gEye));
}

/** The style's world at p as a hit, for lib_chrys_field. */
ChrysHit chrysMap(vec3 p) {
    ChrysHit h = chrysHit(0.0);
    float d = mapAt(p, h);
    h.d = d;
    return h;
}

//#include lib_chrys_field

/** Window id on the side `side` (-1 left, +1 right) for the bay holding z. */
float windowId(float z, float side) {
    return mod(floor(z / CG_BAY), CG_WINDOWS) * 2.0 + (side > 0.0 ? 1.0 : 0.0);
}

/** Where the sun's ray from `p` crosses the left glass plane, in window units (r 0.9 at the edge); w is the id. */
vec3 sunWindow(vec3 p, vec3 L) {
    float tt = (-(gW + CG_DEPTH) - p.x) / L.x;
    vec3 q = p + L * tt;
    float qzw = mod(q.z, CG_BAY) - 0.5 * CG_BAY;
    return vec3(vec2(qzw, q.y - CG_WIN_Y) * (RW_WINDOW / CG_WIN_R), windowId(q.z, -1.0));
}

/** What a fully hazed ray sees: the dark nave air and, far ahead, the west rose. */
vec3 farColour(vec3 rd, vec3 fogCol, float light, float pxTan) {
    vec3 col = fogCol;
    if (rd.z > 0.05) {
        vec2 e = rd.xy / rd.z - vec2(0.0, 0.05);
        float er = length(e);
        col += pal(gHue + 0.15) * 0.06 * exp(-er * 7.0) * light;
        float sc = RW_WINDOW / 0.14;
        vec2 lp = chrysTurn(uTime * 0.004) * e * sc;
        if (length(lp) < RW_WINDOW + 0.02) {
            col += roseGlass(lp, 300.0, max(pxTan * sc * 1.25, 0.002)) * 0.7 * light;
        }
    }
    return col;
}

void main() {
    ChrysHit fld = chrysHit(0.0);
    vec2 uv = view();
    // The normalization shell (lib_chrys): every variable from its driver, mapped to its range here, once.
    ChrysIn N = chrysIn();
    gW = chrysSpan(N.reach, mix(3.7, 4.5, 0.5), 3.7, 4.5);
    gChaos = chrysSpan(N.chaos, 0.0, 0.0, 2.0);
    gRings = 6.0 + floor(chrysSpan(N.rings, 0.0, 0.0, 2.0) + 0.5);
    gCurl = chrysSpan(N.curl, 0.0, -1.0, 1.0);
    float rippleK = chrysSpan(N.emit, 1.0, 0.5, 1.5);
    gStreak = chrysSpan(N.width, 0.18 * (0.4 + 1.2 * 0.5), 0.18 * 0.4, 0.18 * 1.6);
    float sunK = chrysSpan(N.glow, 0.8 + 0.4 * 0.5, 0.8, 1.2);
    float driftRate = chrysSpan(N.spin, 0.1 + 0.3 * 0.5, 0.1, 0.4);
    float shaftK = chrysSpan(N.sway, 0.6 + 0.8 * 0.5, 0.6, 1.4);
    float hueShift = chrysSpan(N.tone, 0.0, -0.1, 0.1);

    vec3 ro;
    vec3 rd;
    camFly(uv, ro, rd);
    // Every z term tiles with a period dividing 1024, so this shift is
    // invisible and exact (power of two); see the header.
    ro.z = mod(ro.z, CG_ZWRAP);
    // Held inside the nave whatever the rig's wander does.
    ro.x = clamp(ro.x, -(gW - 1.0), gW - 1.0);
    ro.y = clamp(ro.y, CG_FLOOR + 0.9, CG_SPRING + 1.5);
    // Grow and layers (lib_grow) measure from the eye in the frame the world is read in.
    growEye(ro);
    gEye = ro;

    float swell = clamp(SND_INTENSITY, 0.0, 1.5);
    float light = 0.8 + 0.2 * swell;
    gPitch = clamp(SND_HELD_NOTE_CHANGE, 0.0, 1.0);
    gBase = clamp(floor(uSymmetry + 0.5), 3.0, 12.0);
    gHue = 0.6 + uKeyHue * clamp(uKeyStrength, 0.0, 1.0) + hueShift;
    gRipple = 0.22 * uIntensity * rippleK;
    // The sun outside the left wall, a little ahead; presence swings it.
    vec3 L = normalize(vec3(-0.78, 0.5, 0.3 + 0.15 * clamp(sndSpectrum(0.80), 0.0, 1.0)));

    float t = 0.05;
    float hitT = -1.0;
    for (int i = 0; i < CG_MAX_STEPS; i++) {
        // uSteps is the user's Detail; this loop's own bound is the constant.
        if (float(i) >= uSteps) break;
        fld = chrysField(ro + rd * t);
        float d = fld.d;
        if (d < 0.0015 + 0.0008 * t) {
            hitT = t;
            break;
        }
        t += d * 0.9;
        if (t > CG_FAR) break;
    }

    // World units per pixel at unit distance, for the line AA.
    float pxTan = 2.0 / (max(uCamFocal, 0.1) * uResolution.y);
    vec3 fogCol = vec3(0.012, 0.011, 0.014) + pal(gHue + 0.15) * 0.02 * light;
    vec3 farCol = farColour(rd, fogCol, light, pxTan);
    vec3 col = farCol;
    float sceneT = hitT > 0.0 ? hitT : CG_FAR;

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = chrysNormal(p, max(0.0015, 0.001 * hitT));
        float ao = clamp(1.0 - 2.0 * (0.3 - chrysField(p + n * 0.3).d), 0.35, 1.0);
        float ndv = clamp(dot(n, -rd), 0.05, 1.0);
        float ax = abs(p.x);
        float side = p.x < 0.0 ? -1.0 : 1.0;
        float zw = mod(p.z, CG_BAY) - 0.5 * CG_BAY;
        // Window-local, mirrored on the right so both walls read the same way round.
        vec2 wl = vec2(-side * zw, p.y - CG_WIN_Y);
        float sc = RW_WINDOW / CG_WIN_R;

        if (ax > gW + CG_DEPTH - 0.03 && length(wl) < CG_WIN_R + 0.02) {
            // The glass: lit from behind, the sun side bright, the sky side dimmer.
            float aa = max(hitT * pxTan / ndv * sc * 1.25, 0.002);
            vec3 g = roseGlass(wl * sc, windowId(p.z, side), aa);
            col = g * light * sunK * (side < 0.0 ? 1.0 : 0.55);
        } else {
            // Stone: floor flags, or coursed blocks on walls, piers and vault.
            // Rows offset by half a block; the row parity and the z cell
            // index both have periods dividing 1024.
            bool onFloor = p.y < CG_FLOOR + 0.03;
            vec2 bp = onFloor ? vec2(p.x, p.z) : vec2(p.z, p.y * 2.0);
            bp.x += 0.5 * mod(floor(bp.y), 2.0);
            vec2 cell = floor(bp);
            if (onFloor) {
                cell.y = mod(cell.y, CG_ZWRAP);
            } else {
                cell.x = mod(cell.x, CG_ZWRAP);
            }
            vec2 bf = abs(fract(bp) - 0.5);
            float jw = max(0.004, hitT * pxTan * 1.5);
            float joint = smoothstep(0.5 - jw - 0.02, 0.5, max(bf.x, bf.y));
            float stone = 0.035 + 0.015 * chrysHash(cell);
            vec3 base = vec3(stone * 1.1, stone, stone * 0.9) * (1.0 - 0.5 * joint);

            col = base * (0.35 + 0.65 * ao);
            // Coloured spill from the four nearest windows, continuous across the bays.
            float zc0 = floor((p.z - 0.5 * CG_BAY) / CG_BAY) * CG_BAY + 0.5 * CG_BAY;
            for (int k = 0; k < 4; k++) {
                float s = k < 2 ? -1.0 : 1.0;
                float zc = zc0 + CG_BAY * float(k - 2 * (k / 2));
                vec3 toL = vec3(s * (gW + CG_DEPTH), CG_WIN_Y, zc) - p;
                float dl = length(toL);
                float lam = max(dot(n, toL / max(dl, 1e-3)), 0.0);
                vec3 tint = pal(gHue + 0.15 + 0.1 * s);
                col += tint * tint * lam * 0.9 / (1.0 + dl * dl * 0.3) * light * (s < 0.0 ? 1.0 : 0.55) * ao;
            }
            // The sun through the left windows: each window's flower laid on
            // the stone it falls on, lead shadows and all, softened a little.
            float nl = dot(n, L);
            if (nl > 0.0) {
                vec3 sw = sunWindow(p, L);
                if (length(sw.xy) < RW_WINDOW) {
                    col += roseGlass(sw.xy, sw.z, 0.02) * nl * 0.75 * sunK * light;
                }
            }
            float spec = pow(max(dot(reflect(rd, n), L), 0.0), 24.0);
            col += vec3(0.02) * spec * ao;
        }
        col = mix(col, farCol, 1.0 - exp(-hitT * 0.05));
    }

    // Shafts: the sun through the left windows, caught by the dust in the
    // nave. Dithered samples along the ray up to what it hit, each reading
    // the cheap tint of the window it lines up with.
    vec3 shaft = vec3(0.0);
    float segT = min(sceneT, 26.0);
    float jit = chrysHash(gl_FragCoord.xy);
    float drift = uTime * driftRate;
    for (int i = 0; i < CG_SHAFT_STEPS; i++) {
        float ts = (float(i) + jit) / float(CG_SHAFT_STEPS) * segT;
        vec3 s = ro + rd * ts;
        if (abs(s.x) > gW || s.y > CG_SPRING + 4.0) continue;
        vec3 sw = sunWindow(s, L);
        float rr = length(sw.xy) / RW_WINDOW;
        if (rr > 0.93) continue;
        // Dust: sines whose z periods (16/3, 16/5) divide the 1024 wrap.
        float dust = 0.55 + 0.45 * sin(s.y * 1.9 + s.x * 1.3 + drift)
                   * sin(s.z * (CG_TAU * 3.0 / 16.0) + s.y * 0.7 - drift * 0.6)
                   + 0.2 * sin(s.z * (CG_TAU * 5.0 / 16.0) + s.x * 2.1 + drift * 0.8);
        shaft += roseTint(sw.xy, sw.z) * max(dust, 0.0) * exp(-ts * 0.07);
    }
    shaft *= segT / float(CG_SHAFT_STEPS) * 0.028 * sunK * light * shaftK;
    col += shaft;

    col *= 0.7 + 0.3 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
