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
//#include lib_nebula

// Nebula Horizon: a black hole with a bright accretion disk, now a body in
// space you circle a little above its disk. A black shadow; a thin photon ring
// hugging it; the near half of the disk crossing in front of it, brighter on
// the side turning toward us; the far half lensed up over the top of the
// shadow into an arch (and faintly under it); a haze of hot gas over the disk;
// and behind it all, gas and stars the hole's gravity pulls into a ring.
//
// None of that is drawn as a shape any more. Each pixel's ray is BENT: it is
// stepped through the hole's field, in units where the horizon radius is 1,
// with the photon orbit equation x'' = -1.5 h^2 x / r^5 (h = |x cross x'|,
// constant along the ray), semi-implicit Euler, so it falls in, slingshots
// round or escapes exactly as light does. The arch, the shadow, the ring and
// the lensed sky all come out of that one integration. The disk is a thin
// sheet on y = 0: each time the bent ray crosses the plane between two steps
// the crossing point is interpolated and the disk is sampled there, front to
// back, so the second and third crossings (the arch, the inner ring images)
// layer correctly behind the first. Its texture turns at the Keplerian rate,
// fast inside and slow outside, as in the flat style: two copies on staggered
// cycles cross-faded so the shear stays bounded, the SAMPLE point rotated so
// there is no atan and no seam. Doppler beaming and gravitational redshift are
// the real formulas (orbital speed sqrt(1 / 2r)), softened and clamped.
//
// ---- camera -----------------------------------------------------------------
//
// The shared rig, object-centred: camAround at HZ_CAM_R, one turn per 1024
// world units of travel (CAM_SPIN_UNIT), so the rig's 8192 wrap is a whole
// number of turns and never shows. Nothing in the world depends on z or on
// travel. The eye and its rays are then turned together about the hole by
// HZ_TILT, up out of the disk plane, which is the near edge-on view the flat
// style drew; the rig's own pitch and roll still act on top. The dolly pulls
// the eye in to 0.65 * HZ_CAM_R = 16.9, still outside the disk (at most
// 12.1) and its haze.
//
// ---- audio ------------------------------------------------------------------
//
// The disk's brightness FOLLOWS THE SWELL (SND_ENERGY_TREND): loud passages
// feed the hole and the disk and arch blaze, quiet ones let them dim to
// embers. The swell is the slowest signal there is, so this breathes over
// seconds and never flashes. Bass (SND_BASS_PRESENCE) widens and lifts the
// photon ring's glow. The hats twinkle the stars. The key turns the outer
// disk's colour.
//
// ---- macros and knobs -------------------------------------------------------
//
// Speed is already in uTime (nebTime): the disk's spin and the gas drift.
// Complexity: uMorph adds octaves to the disk and the gas (nebOctaves).
// Chaos: uWarp/uTurbulence curl the disk texture (nebWarp2) and the sky's gas
// before it is lensed (nebWarp3). uTwist via view(). Colour: disk ramp, ring
// and gas are pal(). Energy: uIntensity scales how far the swell drives the
// disk (nebEnergy). Space: the rig.
//
// Knobs (uKnob, exactly 0.5 = the tuned look): k0 PULSE the photon ring's
// glow, k1 TEXTURE the disk noise scale, k2 FLOW the disk's spin rate, k3 TONE
// the disk's hue, k4 BUILD the disk's brightness, k5 SPACE the disk's outer
// radius.
//
// ---- why the march is safe --------------------------------------------------
//
// There is no distance estimate: the ray is integrated, not sphere-traced, at
// a step that grows with radius (0.11 r, at least HZ_DT_MIN, lengthened when
// Detail is short), in one loop bounded by NEB_VOL_MAX and broken on uSteps.
// It ends inside the horizon (black), outside HZ_ESCAPE heading out (the sky
// along its final direction), or when the disk has made it opaque. EVERY ray,
// in every direction, starts at the eye under the same rule, so where a ray
// stops is continuous across the screen and the lensed sky has no seam at a
// bounding sphere. The acceleration has its 1 / r^5 pole only inside the
// horizon, where the ray has already stopped. A ray the budget cuts short
// near the photon sphere takes the shadow if it is still close in, else the
// sky along its current heading.

#define HZ_CAM_R 26.0
/** The eye's elevation over the disk plane, radians. */
#define HZ_TILT 0.25
/** Disk inner edge (the innermost stable orbit, 3 horizon radii) and outer edge. */
#define HZ_RIN 3.0
#define HZ_ROUT 10.5
/** Rays heading out past this radius have done their bending. */
#define HZ_ESCAPE 42.0
#define HZ_DT_MIN 0.05
#define HZ_CYCLE 12.0
/** The photon sphere, 1.5 horizon radii. */
#define HZ_PHOTON 1.5

float gT;
float gOct;
float gTex;
float gSpin;
float gRout;
float gKey;

/** v turned by angle a about unit axis k (Rodrigues). */
vec3 hzTurn(vec3 v, vec3 k, float a) {
    float c = cos(a);
    float s = sin(a);
    return v * c + cross(k, v) * s + k * dot(k, v) * (1.0 - c);
}

/** Disk texture at disk-plane point d: two staggered Keplerian copies, cross-faded. */
float diskTex(vec2 d, float rho) {
    float omega = 0.5 * pow(HZ_RIN / max(rho, HZ_RIN), 1.5);
    float ph = gT * gSpin / HZ_CYCLE;
    float fa = fract(ph);
    float fb = fract(ph + 0.5);
    float wa = 1.0 - abs(2.0 * fa - 1.0);
    vec2 s = d * gTex;
    float na = nebFbm2(nebWarp2(nebRot(omega * fa * HZ_CYCLE) * s, gT), gOct);
    float nb = nebFbm2(nebWarp2(nebRot(omega * fb * HZ_CYCLE) * s, gT) + vec2(7.7, 3.1), gOct);
    return mix(nb, na, wa);
}

/**
 * The disk at crossing point x seen along photon direction toward the eye
 * `toEye`: rgb is the light, a the sheet's opacity there.
 */
vec4 diskAt(vec3 x, vec3 toEye) {
    float rho = length(x.xz);
    if (rho < HZ_RIN * 0.9 || rho > gRout) return vec4(0.0);
    float prof = smoothstep(HZ_RIN * 0.9, HZ_RIN * 1.25, rho) * (1.0 - smoothstep(gRout * 0.55, gRout, rho));
    float inner = HZ_RIN / max(rho, HZ_RIN);
    prof *= pow(inner, 1.2);
    float heat = pow(inner, 0.8);
    float tex = diskTex(x.xz, rho);

    // Doppler: the gas orbits counter-clockwise seen from +y at sqrt(1 / 2r).
    float beta = sqrt(0.5 / max(rho, HZ_RIN));
    vec3 orbit = vec3(-x.z, 0.0, x.x) / rho;
    float cosA = dot(orbit, toEye);
    float dop = sqrt(1.0 - beta * beta) / (1.0 - beta * cosA);
    float beam = clamp(pow(dop, 2.5), 0.3, 2.6);
    float redshift = sqrt(max(1.0 - 1.0 / rho, 0.0));

    float bands = 0.75 + 0.25 * sin(rho * 3.1 + tex * 3.0);
    float hue = 0.06 + 0.35 * (1.0 - heat) + gKey * (1.0 - heat) + 0.1 * (dop - 1.0);
    vec3 c = nebEmission(hue, heat * clamp(dop, 0.6, 1.3));
    vec3 light = c * prof * beam * redshift * (0.35 + 0.9 * tex) * bands;
    float alpha = clamp(prof * (0.45 + 0.9 * tex) * 1.5, 0.0, 0.93);
    return vec4(light, alpha);
}

/** Field stars on the sky sphere: one 3D cell per layer, each star inside its cell, so no edge cracks. */
vec3 hzStars(vec3 d, float twinkle) {
    vec3 acc = vec3(0.0);
    float px = 2.0 / max(uResolution.y, 1.0) / max(uCamFocal, 0.3);
    for (int i = 0; i < 2; i++) {
        float fi = float(i);
        float k = 70.0 + 60.0 * fi;
        vec3 q = d * k + fi * vec3(17.3, 41.9, 8.1);
        vec3 cell = floor(q);
        vec3 f = q - cell - 0.5;
        float h = nebHash3(cell);
        float h2 = nebHash3(cell + 31.7);
        float fill = 0.16;
        float on = 1.0 - smoothstep(fill - 0.02, fill, h);
        float rel = clamp(h / fill, 0.0, 1.0);
        float bright = mix(1.0, 0.08, sqrt(rel)) / (1.0 + fi * 0.9);
        float r = min(max(mix(0.12, 0.05, rel), px * k * 0.9), 0.14);
        vec3 off = (vec3(h2, fract(h2 * 7.31), fract(h2 * 13.17)) - 0.5) * 2.0 * max(0.45 - 2.5 * r, 0.0);
        float dd = length(f - off);
        float core = max(1.0 - dd / r, 0.0);
        float halo = max(1.0 - dd / (2.5 * r), 0.0);
        float tw = 1.0 - clamp(twinkle, 0.0, 1.0) * 0.65 * (0.5 + 0.5 * sin(gT * (4.0 + 6.0 * h2) + h * 40.0));
        acc += nebStarTint(fract(h2 * 3.7)) * (core * core + 0.16 * halo * halo) * bright * on * tw;
    }
    return acc;
}

/** The sky the hole lenses: curled gas and stars, continuous over the whole sphere. */
vec3 skyAt(vec3 d, float hats) {
    vec3 sp = nebWarp3(d * 2.4, gT) + vec3(gT * 0.004, 0.0, 0.0);
    float gas = nebFbm3(sp, min(gOct, 3.0));
    vec3 col = nebEmission(0.6 + gKey, 0.0) * smoothstep(0.45, 0.85, gas) * 0.13;
    col += pal(0.7) * 0.02;
    return col + hzStars(d, hats);
}

/**
 * What the field holds at a point (lib_nebula_field): the hot haze over the disk, its density there.
 * The disk itself is a surface, met where a bent ray crosses its plane (diskAt), not a field.
 */
struct NebHit {
    float dens;
};

NebHit nebMap(vec3 p, float oct) {
    float rhoH = length(p.xz);
    float wH = 0.35 + 0.09 * rhoH;
    NebHit h;
    h.dens = exp(-p.y * p.y / (wH * wH)) * smoothstep(HZ_RIN * 0.8, HZ_RIN * 1.6, rhoH) * exp(-rhoH * 0.28);
    return h;
}

//#include lib_nebula_field

void main() {
    vec2 uv = view();
    nebSetup();
    gT = nebTime();
    float e = nebEnergy();
    float swell = clamp(SND_ENERGY_TREND * uSoundDrive, 0.0, 1.5);
    float bass = clamp(SND_BASS_PRESENCE * uSoundDrive, 0.0, 1.3) * e;
    float hats = clamp(SND_HAT_ENV * uSoundDrive, 0.0, 1.0);
    gOct = nebOctaves(3.0);
    gKey = nebKeyShift(0.12) + 0.12 * (gNebIn.tone - 0.5);
    gTex = 0.27 * (0.8 + 0.4 * gNebIn.texture);
    gSpin = 0.7 + 0.6 * gNebIn.flow;
    gRout = HZ_ROUT * (0.85 + 0.3 * gNebIn.space);
    float feed = (0.45 + 0.6 * clamp(swell * e, 0.0, 1.6)) * (0.7 + 0.6 * gNebIn.build);

    vec3 ro;
    vec3 rd;
    camAround(uv, HZ_CAM_R, CAM_SPIN_UNIT, ro, rd);
    // Lift the eye and its rays together over the disk plane.
    vec3 axis = cross(ro, vec3(0.0, 1.0, 0.0));
    float al = length(axis);
    if (al > 1e-3) {
        axis /= al;
        ro = hzTurn(ro, axis, HZ_TILT);
        rd = hzTurn(rd, axis, HZ_TILT);
    }

    vec3 p = ro;
    vec3 v = rd;
    vec3 hv = cross(p, v);
    float h2 = dot(hv, hv);
    float budget = clamp(96.0 / max(uSteps, 1.0), 0.75, 2.5);
    float minR = 1e4;

    vec3 col = vec3(0.0);
    float trans = 1.0;
    // 0 still going, 1 escaped, 2 fell in.
    int fate = 0;
    vec3 haze = nebEmission(0.12 + gKey, 0.4);

    for (int i = 0; i < NEB_VOL_MAX; i++) {
        if (float(i) >= uSteps) break;
        float r = length(p);
        float dt = max(0.11 * r - 0.06, HZ_DT_MIN) * budget;
        vec3 acc = -1.5 * h2 * p / (r * r * r * r * r);
        v += acc * dt;
        vec3 pn = p + v * dt;
        float rn = length(pn);
        minR = min(minR, rn);

        // The hot haze over the disk: a smooth glow, lensed like everything else.
        float hz = nebField(pn, gOct).dens;
        col += trans * haze * hz * 0.05 * feed * dt;

        // Crossing the disk plane between p and pn.
        if (p.y * pn.y < 0.0) {
            vec3 x = mix(p, pn, p.y / (p.y - pn.y));
            vec4 dk = diskAt(x, -normalize(v));
            col += trans * dk.rgb * feed;
            trans *= 1.0 - dk.a;
        }

        p = pn;
        if (rn < 1.0) {
            fate = 2;
            break;
        }
        if (rn > HZ_ESCAPE && dot(p, v) > 0.0) {
            fate = 1;
            break;
        }
        if (trans < NEB_TRANS_MIN) break;
    }
    if (fate == 0) fate = length(p) < 2.6 ? 2 : 1;

    // The photon ring: rays that skimmed the photon sphere.
    float ringW = 0.05 + 0.04 * bass;
    float ring = exp(-pow((minR - HZ_PHOTON * 1.04) / ringW, 2.0));
    col += trans * nebEmission(0.08 + gKey, 0.8) * ring * (0.35 + 0.2 * bass) * feed * (0.5 + gNebIn.pulse);

    if (fate == 1) col += trans * skyAt(normalize(v), hats);

    col *= 0.75 + 0.25 * (1.0 - smoothstep(0.5, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
