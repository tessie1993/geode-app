#version 300 es
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

// Supershape: the Gielis superformula as a living body. A translucent,
// iridescent membrane shaped by the spherical product of two superformula
// curves hangs in a dark void, a solid supershape core turns inside it, three
// smaller supershapes orbit it, and shells of motes far behind give the
// camera something to parallax against. Raymarched, seen with camAround.
//
//     r(u) = ( |cos u|^n2 + |sin u|^n3 )^(-1/n1),   u = m * theta / 4
//
// n1 = n2 = n3 = 2 is a sphere for any m. n1 below 1 pinches every lobe to a
// point, n2 / n3 fatten the cosine / sine lobes. Every path between two sets
// of exponents is a morph, never a cut, which is why the music drives the
// exponents rather than a zoom: a change in the music is a change in WHAT IS
// DRAWN.
//
// The body is star-shaped about its centre. The radius in direction d is
// r2(lat) * sqrt(r1(az)^2 cos^2 lat + sin^2 lat): the length the classic
// parametric supershape reaches at that latitude, with the azimuth curve
// fading out toward the poles so the poles close without a tear. Each curve
// is divided by its own maximum (sampled once per pixel in main, it does not
// depend on m), so the body never outgrows its bounding sphere.
//
// This used to be driven from a C++ SuperShape driver through lib_superformula;
// both are gone. All the formula lives here and reads the sound engine directly.
//
// ---- audio ------------------------------------------------------------------
//
// Each sound moves a different thing, and almost all of them move SHAPE:
//
// - Note clock (SND_NOTE_TIME): each new note morphs the azimuth to its next
//   lobe count m (even counts 4..12, hashed per note), eased over the clock's
//   own ~150 ms glide, so the symmetry follows the melody without a pop.
// - Flatness (SND_FLATNESS): sharpness n1. A tonal pad sits round, noise and
//   a drum break sit spiky.
// - Kick (SND_KICK_ENV): pinches n1 further on the envelope, stabbing every
//   lobe outward, and punches the body a few percent.
// - Centroid (SND_CENTROID): the n2 / n3 balance. Dark sound fattens the
//   cosine lobes, bright sound the sine ones, so the lobes alternate fat/thin.
// - Flux (sndLevel(SND_FLUX)): the WRITHE. A latitude wave twists the azimuth,
//   so a changing spectrum sets the whole membrane squirming while a held
//   chord lets it hang still. The wave travels on the mid clock.
// - Snare (SND_SNARE_ENV): snaps every lobe pattern one full period round and
//   lets it settle. A period is the curve's own symmetry, so the snap starts
//   from a pose identical to the last.
// - Onset (SND_ONSET_ENV): a bulge rolls over the membrane pole to pole as the
//   envelope falls, lighting a ring as it goes.
// - Surge (SND_HELD_SURGE): the body and core swell on the envelope's bump and
//   the gathered glow brightens.
// - Hard cut (SND_HELD_HARD_CUT with SND_CUT_COUNT): re-seeds the section: the
//   elevation curve's m and exponents, the core's lobe count, the satellites'
//   orbits and lobe counts, and the hue, all crossfaded over the held cut.
// - Mid clock (SND_MID_TIME): the body's spin and the writhe wave's phase.
// - Bass clock (SND_BASS_TIME): the satellites' orbits.
// - Treble clock (SND_TREBLE_TIME): the core's counter-spin, the motes flowing
//   along the membrane's parameter lines, their twinkle.
// - Hats (SND_HAT_ENV): some membrane motes flare white.
// - Stereo width (SND_STEREO_WIDTH): how far out the satellites orbit.
// - Stereo pan (SND_STEREO_PAN): leans the body's pole toward the louder side.
// - Fast density (SND_DENSITY_FAST): twists the core about its pole.
// - Attenuated bass / treble (SND_BASS_ATT, SND_TREBLE_ATT): the core's own
//   n2 / n3, so the core breathes with the low and high bands separately.
// - Pitch class (SND_PITCH_CLASS, gated by SND_PITCH_CONFIDENCE): the core's
//   hue, a complement of the membrane's, nudged by the note.
// - Spectrum (sndSpectrum): rings of latitude on the membrane, each lit by its
//   own slice of the log spectrum, bass at the south pole, air at the north.
// - Energy trend (SND_ENERGY_TREND): overall brightness, gently.
//
// Every motion runs on a clock (the SND_*_TIME clocks and uFlowPhase), so it
// slows and speeds with the music and never runs backwards. Quiet music: the
// clocks barely advance and the flux writhe is near zero, so the body hangs
// still. Busy music: everything squirms.
//
// ---- the march ----------------------------------------------------------------
//
// The field |p| - R * rho(p / |p|) is not a true distance where the lobes are
// sharp, so it is scaled by a bound that grows as n1 falls and the writhe
// rises (gLip). Outside each form's bounding sphere the sphere's own distance
// is used, so empty space is crossed at full speed. The membrane is marched
// first (with the core and satellites); a membrane hit then marches INSIDE it
// (a second loop, capped at 32 per lib_sdf3) to the core or the far wall, and
// the membrane is composited over what that finds.

#define SS_MAX_STEPS 112
#define SS_IN_STEPS 32
#define SS_TAU 6.2831853
#define SS_PI 3.14159265
#define SS_SATS 3
#define SS_CAM_R 4.8
/** Everything solid sits inside this sphere about the origin. */
#define SS_SCENE_R 3.0

// ---- per-frame shape state, shared by map, normals and shading ----------------

float gR;
mat3 gBodyRot;
vec3 gN;
float gNorm;
vec3 gNe;
float gNormE;
/** Azimuth m from, m to, blend, elevation m. */
vec4 gBodyM;
float gLip;
float gSnap;
float gWrithe;
float gWritheP;
float gWave;
float gWaveLat;

float gRc;
mat3 gCoreRot;
vec3 gNc;
float gNormC;
vec4 gCoreM;
float gCoreTwist;
float gLipC;

vec3 gSatPos[SS_SATS];
mat3 gSatRot[SS_SATS];
float gSatSize[SS_SATS];
vec4 gSatM[SS_SATS];

/** What mapOuter / mapInner found nearest: material, its shape angles, its seed. */
float gMat;
vec2 gAng;
float gCoreD;

// ---- the formula ------------------------------------------------------------------

/** The raw superformula at u = m * theta / 4 with a = b = 1; n = (n1, n2, n3). */
float sfRaw(float u, vec3 n) {
    float s = pow(max(abs(cos(u)), 1e-4), n.y) + pow(max(abs(sin(u)), 1e-4), n.z);
    return pow(max(s, 1e-5), -1.0 / n.x);
}

/** The curve's maximum over a quarter period, which covers every u. Independent of m. */
float sfNorm(vec3 n) {
    float mx = 1e-4;
    for (int k = 0; k <= 8; k++) mx = max(mx, sfRaw(float(k) * (SS_PI / 16.0), n));
    return mx;
}

/** An even lobe count 4..12 from a 0..1 hash. Even, so the azimuth closes at +-pi for any n2 != n3. */
float evenM(float h) {
    return 4.0 + 2.0 * min(floor(h * 5.0), 4.0);
}

/**
 * Signed field of one supershape of size R in its own frame q (z = pole).
 * m = (azimuth m from, m to, blend, elevation m). `wave` is the onset bulge.
 * Returns the bounding sphere's distance when well outside it.
 */
float formField(vec3 q, float R, vec3 n, float norm, vec4 m, vec3 ne, float normE,
                float writhe, float wave, float lip, out vec2 ang) {
    ang = vec2(0.0);
    float r = length(q);
    float bound = r - R * 1.2;
    if (bound > 0.25) return bound;
    vec3 d = q / max(r, 1e-5);
    float lat = asin(clamp(d.z, -1.0, 1.0));
    // atan(0, 0) is undefined; a pole hit straight on would be NaN on some GPUs.
    float th = atan(d.y, abs(d.x) < 1e-6 ? 1e-6 : d.x) + writhe * sin(3.0 * lat + gWritheP);
    float ua = m.x * 0.25 * th + gSnap * SS_PI;
    float ub = m.y * 0.25 * th + gSnap * SS_PI;
    float r1;
    if (m.z <= 0.001) {
        r1 = sfRaw(ua, n);
    } else if (m.z >= 0.999) {
        r1 = sfRaw(ub, n);
    } else {
        r1 = mix(sfRaw(ua, n), sfRaw(ub, n), m.z);
    }
    r1 /= norm;
    float r2 = sfRaw(m.w * 0.25 * lat, ne) / normE;
    float cl = cos(lat);
    float sl = d.z;
    float rho = r2 * sqrt(r1 * r1 * cl * cl + sl * sl);
    float dl = lat - gWaveLat;
    rho *= 1.0 + wave * exp(-dl * dl * 18.0);
    ang = vec2(th, lat);
    return max((r - R * rho) * lip, bound);
}

float bodyField(vec3 p, out vec2 ang) {
    return formField(gBodyRot * p, gR, gN, gNorm, gBodyM, gNe, gNormE, gWrithe, gWave, gLip, ang);
}

float coreField(vec3 p, out vec2 ang) {
    vec3 q = gCoreRot * p;
    q.xy = rot2(gCoreTwist * q.z) * q.xy;
    // The twist stretches space by up to |k| * radius; divide it back out.
    return formField(q, gRc, gNc, gNormC, gCoreM, vec3(1.4, 1.4, 1.4), 1.0, 0.0, 0.0, gLipC, ang)
           / (1.0 + abs(gCoreTwist) * gRc * 1.2);
}

/** The outside: membrane, core where it presses through, satellites. Materials 0, 1, 2+i. */
float mapOuter(vec3 p) {
    vec2 a;
    float d = bodyField(p, a);
    gMat = 0.0;
    gAng = a;
    float dc = coreField(p, a);
    if (dc < d) {
        d = dc;
        gMat = 1.0;
        gAng = a;
    }
    for (int i = 0; i < SS_SATS; i++) {
        vec3 q = gSatRot[i] * (p - gSatPos[i]);
        float ds = formField(q, gSatSize[i], gN, gNorm, gSatM[i], gNe, gNormE, gWrithe * 0.6, 0.0, gLip, a);
        if (ds < d) {
            d = ds;
            gMat = 2.0 + float(i);
            gAng = a;
        }
    }
    return d;
}

/** The inside of the membrane: the core, or the far wall (the body's field, negated). */
float mapInner(vec3 p) {
    vec2 a;
    float wall = -bodyField(p, a);
    gMat = 0.0;
    gAng = a;
    float dc = coreField(p, a);
    gCoreD = dc;
    if (dc < wall) {
        gMat = 1.0;
        gAng = a;
        return dc;
    }
    return wall;
}

vec3 normalOuter(vec3 p, float eps) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * mapOuter(p + k.xyy * eps) + k.yyx * mapOuter(p + k.yyx * eps)
                   + k.yxy * mapOuter(p + k.yxy * eps) + k.xxx * mapOuter(p + k.xxx * eps));
}

// ---- shading --------------------------------------------------------------------

/** 1 on the integer lines of x, 0 between them; `w` is the half-width as a fraction of a cell. */
float lineAt(float x, float w) {
    return smoothstep(0.5 - w, 0.5, abs(fract(x) - 0.5));
}

/** The membrane at a surface point: thin-film colour, motes, spectrum rings. Alpha is its cover. */
vec3 membrane(vec3 n, vec3 rd, vec3 L, vec2 ang, float hue, float trebC, float hat, float seed, out float alpha) {
    float ndv = clamp(dot(n, -rd), 0.0, 1.0);
    float fres = pow(1.0 - ndv, 3.0);
    float diff = max(dot(n, L), 0.0);
    float lat01 = ang.y / SS_PI + 0.5;

    // Thin film: the hue walks with the viewing angle, so every lobe wears its own rainbow at the rim.
    float film = 0.55 * (1.0 - ndv) + 0.14 * ang.y + 0.06 * sin(ang.x * 3.0 + ang.y * 5.0);
    vec3 irA = pal(hue + film);
    vec3 irB = pal(hue + film + 0.33);
    vec3 col = irA * (0.025 + 0.09 * diff) + mix(irA, irB, fres) * fres * 1.15;
    col += vec3(1.0, 0.96, 0.92) * pow(max(dot(reflect(rd, n), L), 0.0), 48.0) * 0.55;

    // Motes on the parameter lines, hashed per cell so the lattice never reads as a grid,
    // flowing round on the treble clock. The cell index rides with the flow, so a mote keeps
    // its identity as it travels.
    const float NT = 36.0;
    const float NP = 18.0;
    vec2 g = vec2(ang.x / SS_TAU * NT + trebC * 0.35, lat01 * NP);
    vec2 c = floor(g);
    float h = hashCell(ivec3(int(mod(c.x, NT)), int(c.y), int(seed)));
    vec2 f = fract(g) - 0.5 - (vec2(fract(h * 17.3), fract(h * 29.1)) - 0.5) * 0.45;
    float sz = 0.10 + 0.20 * fract(h * 7.7);
    float mote = step(0.30, h) * smoothstep(sz, sz * 0.25, length(f));
    float tw = 0.55 + 0.45 * sin(trebC * (2.0 + 4.0 * fract(h * 3.3)) + h * 50.0);
    float flare = step(0.62, fract(h * 11.1)) * hat;
    col += mix(pal(hue + 0.5 + 0.2 * h), vec3(1.0), 0.25 + 0.6 * flare) * mote * (0.30 * tw + 1.6 * flare);

    // Spectrum rings: each latitude line lit by its own slice of the log spectrum.
    float spec = sndSpectrum(0.06 + 0.86 * lat01);
    col += pal(hue + 0.25 + 0.4 * lat01) * lineAt(lat01 * NP, 0.07) * spec * spec * 0.9;

    // The onset's rolling ring.
    float dl = ang.y - gWaveLat;
    col += pal(hue + 0.5) * exp(-dl * dl * 60.0) * gWave * 6.0;

    alpha = clamp(0.20 + 0.75 * fres + 0.6 * mote, 0.0, 1.0);
    return col;
}

/** The solid core: lit, with veins crawling pole to pole on the treble clock. */
vec3 coreShade(vec3 n, vec3 rd, vec3 L, vec2 ang, float hue, float trebC, float ao) {
    float ndv = clamp(dot(n, -rd), 0.0, 1.0);
    float diff = max(dot(n, L), 0.0);
    vec3 base = pal(hue);
    vec3 col = base * (0.05 + 0.55 * diff) * ao;
    col += vec3(1.0, 0.95, 0.9) * pow(max(dot(reflect(rd, n), L), 0.0), 32.0) * 0.45;
    col += pal(hue + 0.1) * pow(1.0 - ndv, 2.0) * 0.35;
    float veins = max(lineAt(ang.y * 2.5 - trebC * 0.25, 0.05), lineAt(ang.x * gCoreM.y / SS_TAU, 0.04));
    col += mix(pal(hue + 0.18), vec3(1.0), 0.3) * veins * 0.7;
    return col;
}

/** A satellite: a small opaque jewel with a thin-film rim. */
vec3 satShade(vec3 n, vec3 rd, vec3 L, vec2 ang, float hue, float h) {
    float ndv = clamp(dot(n, -rd), 0.0, 1.0);
    float diff = max(dot(n, L), 0.0);
    float fres = pow(1.0 - ndv, 3.0);
    vec3 col = pal(hue + 0.3 * h + 0.1 * ang.y) * (0.06 + 0.45 * diff);
    col += pal(hue + 0.4 * (1.0 - ndv) + h) * fres * 1.2;
    col += vec3(1.0) * pow(max(dot(reflect(rd, n), L), 0.0), 64.0) * 0.6;
    return col;
}

/** Shells of hashed motes behind the scene, at three radii, so the orbiting eye sees parallax. */
vec3 moteShells(vec3 ro, vec3 rd, float hue, float trebC) {
    vec3 acc = vec3(0.0);
    float b = dot(ro, rd);
    float c0 = dot(ro, ro);
    for (int s = 0; s < 3; s++) {
        float fs = float(s);
        float R = 7.0 + 4.5 * fs;
        float t = -b + sqrt(max(b * b - (c0 - R * R), 0.0));
        vec3 d = rotY(uFlowPhase * 0.004 * (1.0 + fs)) * ((ro + rd * t) / R);
        vec2 a = vec2(atan(d.z, d.x), asin(clamp(d.y, -1.0, 1.0)));
        float N = 44.0 + 28.0 * fs;
        vec2 g = vec2(a.x / SS_TAU * N, a.y / SS_PI * N * 0.5);
        vec2 cell = floor(g);
        float h = hashCell(ivec3(int(mod(cell.x, N)), int(cell.y), s + 7));
        vec2 f = fract(g) - 0.5 - (vec2(fract(h * 13.7), fract(h * 23.9)) - 0.5) * 0.6;
        float sz = 0.05 + 0.12 * fract(h * 31.3);
        float m = step(0.80, h) * smoothstep(sz, sz * 0.2, length(f));
        float tw = 0.6 + 0.4 * sin(trebC * (1.0 + 3.0 * fract(h * 7.7)) + h * 40.0);
        acc += pal(hue + 0.5 + 0.35 * fract(h * 5.1)) * m * tw * (0.45 / (1.0 + 0.8 * fs));
    }
    return acc;
}

/** One satellite's orbit for section `sec`: position, size factor and lobe count. */
vec3 satOrbit(int i, float sec, float bassC, float width, out float sizeH, out float m) {
    float fi = float(i);
    float h0 = hash11(fi * 7.13 + sec * 1.71 + 0.3);
    float h1 = hash11(fi * 3.31 + sec * 2.97 + 5.0);
    float h2 = hash11(fi * 9.77 + sec * 0.61 + 11.0);
    sizeH = h1;
    m = evenM(h2);
    // The rate is the satellite's own, not the section's: a rate that changed with
    // the section would jump the angle by (rate change) * the whole clock.
    float a = bassC * (0.30 + 0.11 * fi) + h0 * SS_TAU;
    float rad = 1.75 + 0.55 * width + 0.35 * h1;
    vec3 o = vec3(cos(a), 0.0, sin(a)) * rad;
    return rotZ((h2 - 0.5) * 1.4) * rotX((h0 - 0.5) * 0.9) * o;
}

void main() {
    vec2 uv = view();
    float spd = uSpeed;
    float midC = SND_MID_TIME * spd;
    float bassC = SND_BASS_TIME * spd;
    float trebC = SND_TREBLE_TIME * spd;

    float kick = clamp(SND_KICK_ENV, 0.0, 1.0);
    float snare = clamp(SND_SNARE_ENV, 0.0, 1.0);
    float hat = clamp(SND_HAT_ENV, 0.0, 1.0);
    float onset = clamp(SND_ONSET_ENV, 0.0, 1.0);
    float flux = clamp(sndLevel(SND_FLUX), 0.0, 1.0);
    float flatn = smoothstep(0.05, 0.6, clamp(SND_FLATNESS, 0.0, 1.0));
    float cent = smoothstep(0.25, 0.7, clamp(SND_CENTROID, 0.0, 1.0));
    float dens = clamp(SND_DENSITY_FAST, 0.0, 1.0);
    float width = clamp(SND_STEREO_WIDTH, 0.0, 1.0);
    float pan = clamp(SND_STEREO_PAN, -1.0, 1.0);
    float surge = clamp(SND_HELD_SURGE, 0.0, 1.0);
    float surgeBump = 4.0 * surge * (1.0 - surge);
    float trend = clamp(SND_ENERGY_TREND, 0.0, 1.5);
    float bassA = clamp(sndLevel(SND_BASS_ATT), 0.0, 1.0);
    float trebA = clamp(sndLevel(SND_TREBLE_ATT), 0.0, 1.0);
    float pitchHue = 0.18 * clamp(SND_PITCH_CLASS, 0.0, 1.0) * clamp(SND_PITCH_CONFIDENCE, 0.0, 1.0);

    // The section: at a hard cut the held envelope is 1 (the old section shows) and
    // falls to 0 over 2.5 s, so `fresh` carries everything from the old seed to the new.
    float sec = floor(SND_CUT_COUNT + 0.5);
    float fresh = 1.0 - smoothstep(0.0, 1.0, clamp(SND_HELD_HARD_CUT, 0.0, 1.0));
    float hsA = hash11(sec * 0.913 + 2.0);
    float hsB = hash11((sec - 1.0) * 0.913 + 2.0);
    float hs = mix(hsB, hsA, fresh);
    float hs2 = mix(hash11((sec - 1.0) * 1.37 + 9.0), hash11(sec * 1.37 + 9.0), fresh);

    float hue = 0.60 + 0.004 * midC + 0.25 * hs;

    // ---- the body ----
    // Note clock: floor is the note being left, the fraction its eased arrival at the next.
    float nt = SND_NOTE_TIME;
    float ni = floor(nt);
    gBodyM = vec4(evenM(hash11(ni * 0.731 + 3.1)), evenM(hash11((ni + 1.0) * 0.731 + 3.1)),
                  smoothstep(0.0, 1.0, fract(nt)), 2.0 + 6.0 * hs);
    float n1 = mix(2.3, 0.5, flatn) * (1.0 - 0.5 * kick);
    gN = vec3(clamp(n1, 0.3, 3.0), mix(5.0, 1.3, cent), mix(1.3, 5.0, cent));
    gNorm = sfNorm(gN);
    gNe = vec3(mix(0.9, 2.6, hs2), 1.5 + 2.5 * hs, 1.5 + 2.5 * (1.0 - hs));
    gNormE = sfNorm(gNe);
    gR = 1.0 * (1.0 + 0.05 * kick + 0.16 * surgeBump);
    gWrithe = 0.85 * flux;
    gWritheP = midC * 1.7;
    gSnap = 1.0 - snare * snare;
    gWave = 0.10 * onset;
    gWaveLat = mix(-0.5 * SS_PI, 0.5 * SS_PI, onset);
    gLip = 0.85 / (1.0 + 0.6 / min(gN.x, gNe.x) + 1.5 * gWrithe);
    // The pole leans toward the louder channel; the body turns on the mid clock.
    gBodyRot = rotZ(midC * 0.35) * rotX(-1.15 + 0.3 * pan) * rotZ(0.25 * pan);

    // ---- the core ----
    gRc = gR * 0.45 * (1.0 + 0.2 * surgeBump);
    gNc = vec3(0.9 + 0.6 * hs2, mix(1.2, 4.0, bassA), mix(1.2, 4.0, trebA));
    gNormC = sfNorm(gNc);
    gCoreM = vec4(evenM(hash11((sec - 1.0) * 2.31 + 1.0)), evenM(hash11(sec * 2.31 + 1.0)), fresh, 3.0 + 3.0 * hs2);
    gCoreTwist = 1.6 * dens;
    gLipC = 0.85 / (1.0 + 0.6 / min(gNc.x, 1.4));
    gCoreRot = rotY(-trebC * 0.45) * rotX(0.7);

    // ---- the satellites ----
    for (int i = 0; i < SS_SATS; i++) {
        float szA;
        float szB;
        float mA;
        float mB;
        vec3 pA = satOrbit(i, sec, bassC, width, szA, mA);
        vec3 pB = satOrbit(i, sec - 1.0, bassC, width, szB, mB);
        gSatPos[i] = gR * mix(pB, pA, fresh);
        gSatSize[i] = gR * 0.24 * (0.75 + 0.5 * mix(szB, szA, fresh));
        gSatM[i] = vec4(mB, mA, fresh, 2.0 + 4.0 * fract(szA * 7.0));
        gSatRot[i] = rotY(trebC * (0.5 + 0.2 * float(i)) + float(i) * 2.1) * rotX(0.9 * float(i) + 0.4);
    }

    vec3 ro;
    vec3 rd;
    camAround(uv, SS_CAM_R, 2.0 * CAM_SPIN_UNIT, ro, rd);
    vec3 L = normalize(uCamBasis * vec3(0.55, 0.65, -0.5));

    vec3 voidCol = pal(hue + 0.1) * 0.02 + vec3(0.002, 0.003, 0.008);
    vec3 bg = voidCol + moteShells(ro, rd, hue, trebC);
    vec3 glowCol = pal(hue + 0.5);
    vec3 coreGlowCol = pal(hue + 0.5 + pitchHue + 0.15);

    // ---- outside march, inside the scene sphere only ----
    float b = dot(ro, rd);
    float disc = b * b - (dot(ro, ro) - SS_SCENE_R * SS_SCENE_R);
    float hitT = -1.0;
    float used = 0.0;
    vec3 glow = vec3(0.0);
    if (disc > 0.0) {
        float sq = sqrt(disc);
        float t = max(-b - sq, 0.0);
        float tExit = -b + sq;
        for (int i = 0; i < SS_MAX_STEPS; i++) {
            // uSteps is the user's Detail; this loop's own bound is the constant.
            if (float(i) >= uSteps) break;
            if (t > tExit) break;
            used = float(i);
            float d = mapOuter(ro + rd * t);
            float eps = 0.0007 * t + 0.0005;
            if (d < eps) {
                hitT = t;
                break;
            }
            // Glow gathered along the ray: complementary light from the membrane and satellites.
            glow += (gMat == 1.0 ? coreGlowCol : glowCol) * exp(-d * 9.0) * 0.010;
            t += max(d, eps);
        }
    }

    vec3 col = bg;
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = normalOuter(p, max(0.0012 * hitT, 0.0006));
        // Restore the hit's material and angles after the normal taps.
        mapOuter(p);
        float mat = gMat;
        vec2 ang = gAng;
        float ao = clamp(1.0 - used / max(uSteps, 1.0) * 1.2, 0.25, 1.0);
        float coreHue = hue + 0.5 + pitchHue;

        if (mat == 0.0) {
            float alpha;
            vec3 front = membrane(n, rd, L, ang, hue, trebC, hat, sec, alpha);

            // ---- inside: to the core or the far wall ----
            float t2 = hitT + 0.02;
            float hit2 = -1.0;
            float innerMat = 0.0;
            vec2 ang2 = vec2(0.0);
            vec3 inGlow = vec3(0.0);
            float inBudget = min(float(SS_IN_STEPS), 6.0 + 0.3 * uSteps);
            for (int j = 0; j < SS_IN_STEPS; j++) {
                if (float(j) >= inBudget) break;
                float d = mapInner(ro + rd * t2);
                if (d < 0.0015) {
                    hit2 = t2;
                    innerMat = gMat;
                    ang2 = gAng;
                    break;
                }
                // The core lights the inside like a lantern.
                inGlow += coreGlowCol * exp(-max(gCoreD, 0.0) * 7.0) * 0.022;
                t2 += max(d, 0.004);
            }

            vec3 behind = bg;
            if (hit2 > 0.0) {
                vec3 p2 = ro + rd * hit2;
                vec3 n2 = normalOuter(p2, 0.0015);
                if (innerMat == 1.0) {
                    behind = coreShade(n2, rd, L, ang2, coreHue, trebC, 1.0);
                } else {
                    // The far wall, seen from inside: its rim faces back at us, dimmed.
                    float backA;
                    vec3 back = membrane(-n2, rd, L, ang2, hue + 0.08, trebC, hat, sec, backA);
                    behind = back * 0.45 + bg * (1.0 - 0.45 * backA);
                }
            }
            // The membrane tints what it is seen through.
            col = front + (1.0 - alpha) * (behind * mix(vec3(1.0), pal(hue + 0.05), 0.3) + inGlow);
        } else if (mat == 1.0) {
            col = coreShade(n, rd, L, ang, coreHue, trebC, ao);
        } else {
            col = satShade(n, rd, L, ang, hue + 0.2, hash11(mat * 3.7 + sec)) * ao;
        }
        // Depth fog past the body's near face.
        col = mix(col, bg, 1.0 - exp(-max(hitT - 2.5, 0.0) * 0.12));
    }
    col += glow * (0.8 + 1.5 * surgeBump);

    col *= 0.82 + 0.28 * trend;
    col *= 0.72 + 0.28 * smoothstep(2.2, 0.4, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
