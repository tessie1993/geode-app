#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_nebula

// Nebula Horizon: a black hole with a bright accretion disk, seen almost edge
// on, in front of a nebula its gravity bends. A black shadow; a thin photon
// ring hugging it; the disk crossing in front of it, brighter on the side
// turning toward us; the far half of the disk lensed up over the top of the
// shadow into an arch; and behind it all, stars and gas pulled into a ring
// around the hole.
//
// Nothing is marched. The background is a thin gravitational lens, analytic:
// a pixel at radius r sees the sky at radius r - k / r along the same
// direction, which makes an Einstein ring at r = sqrt(k), turns the sky
// inside it over, and magnifies it near the ring. The disk is a flat annulus
// in an inclined plane. Its texture turns at the Keplerian rate, fast inside
// and slow outside; a disk sheared for ever winds into threads, so the
// texture is two copies on staggered cycles cross-faded (the flow-map trick),
// which keeps the shear bounded with no visible restart. Rotating the SAMPLE
// point needs no angle, so there is no atan in the disk at all.
//
// ---- audio ------------------------------------------------------------------
//
// The disk's brightness FOLLOWS THE SWELL (uSwell): loud passages feed the
// hole and the disk and arch blaze, quiet ones let them dim to embers. The
// swell is the slowest signal there is, so this breathes over seconds and
// never flashes. Bass (uBassSmooth) tightens the photon ring's glow. The
// hats twinkle the stars. The key turns the outer disk's colour.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the disk's spin and the background drift.
// Complexity: uMorph adds octaves to the disk and the gas (nebOctaves).
// Chaos: uWarp/uTurbulence curl the background before it is lensed
// (nebWarp2). uTwist via view(). Colour: disk ramp, arch and gas are pal().
// Energy: uIntensity scales how far the swell drives the disk. Space: view().

#define HZ_SHADOW 0.13
#define HZ_LENS 0.06
#define HZ_COS_INC 0.24
#define HZ_RIN 0.2
#define HZ_ROUT 0.95
#define HZ_CYCLE 12.0

/** Disk texture at disk-plane point d: two staggered Keplerian copies, cross-faded. */
float diskTex(vec2 d, float rho, float t, float oct) {
    float omega = 0.5 / pow(max(rho, HZ_RIN), 1.5);
    float ph = t / HZ_CYCLE;
    float fa = fract(ph);
    float fb = fract(ph + 0.5);
    float wa = 1.0 - abs(2.0 * fa - 1.0);
    float na = nebFbm2(nebRot(omega * fa * HZ_CYCLE) * d * 4.0, oct);
    float nb = nebFbm2(nebRot(omega * fb * HZ_CYCLE) * d * 4.0 + vec2(7.7, 3.1), oct);
    return mix(nb, na, wa);
}

/** Disk colour and brightness at disk radius rho, with Doppler beaming from the line-of-sight side. */
vec3 diskLight(float rho, float side, float tex, float key) {
    float prof = smoothstep(HZ_RIN, HZ_RIN * 1.3, rho) * (1.0 - smoothstep(HZ_ROUT * 0.5, HZ_ROUT, rho));
    prof *= pow(HZ_RIN / max(rho, HZ_RIN), 1.4);
    float heat = pow(HZ_RIN / max(rho, HZ_RIN), 0.8);
    float beam = clamp(1.0 + 0.6 * side * sqrt(HZ_RIN / max(rho, HZ_RIN)), 0.3, 1.8);
    float bands = 0.75 + 0.25 * sin(rho * 38.0 + tex * 3.0);
    vec3 c = nebEmission(0.06 + 0.35 * (1.0 - heat) + key * (1.0 - heat), heat);
    return c * prof * beam * beam * (0.35 + 0.9 * tex) * bands;
}

void main() {
    vec2 uv = view();
    float t = nebTime();
    float e = nebEnergy();
    float swell = clamp(uSwell, 0.0, 1.5);
    float bass = clamp(uBassSmooth, 0.0, 1.3) * e;
    float hats = clamp(uHits.z, 0.0, 1.0);
    float oct = nebOctaves(3.0);
    float key = nebKeyShift(0.12);
    float feed = 0.45 + 0.6 * clamp(swell * e, 0.0, 1.6);

    float r = max(length(uv), 1e-3);
    vec2 dir = uv / r;

    // The lensed sky.
    vec2 src = dir * (r - HZ_LENS / r);
    float mag = min(1.0 / max(abs(1.0 - (HZ_LENS * HZ_LENS) / (r * r * r * r)), 1e-3), 3.0);
    vec2 sp = nebWarp2(src, t);
    float gas = nebFbm2(sp * 1.3 + vec2(t * 0.004, 0.0), oct);
    vec3 col = nebEmission(0.6 + key, 0.0) * smoothstep(0.45, 0.85, gas) * 0.12;
    col += pal(0.7) * 0.02;
    col += nebStars(src, 8.0, 2, vec2(0.0), 1.0, 0.22, hats, t) * mag;
    float shadow = smoothstep(HZ_SHADOW * 0.92, HZ_SHADOW * 1.08, r);
    col *= shadow;

    // The disk plane: screen y stretched by the inclination.
    vec2 dp = vec2(uv.x, uv.y / HZ_COS_INC);
    float rho = length(dp);
    float tex = diskTex(dp, rho, t, oct);
    vec3 disk = diskLight(rho, -dp.x / max(rho, 1e-3), tex, key) * feed;

    // Far half: behind the hole, hidden by the shadow.
    float farHalf = smoothstep(-0.01, 0.01, uv.y);
    col += disk * farHalf * shadow;

    // The far disk lensed over the top (and faintly under) the shadow.
    float arcR = HZ_RIN + max(r - HZ_SHADOW * 1.15, 0.0) * 5.0;
    float arcTex = nebValue2(dir * 6.0 + vec2(t * 0.15, 0.0));
    float arcW = mix(0.25, 1.0, smoothstep(-0.4, 0.5, dir.y)) * smoothstep(HZ_SHADOW * 1.05, HZ_SHADOW * 1.25, r);
    col += diskLight(arcR, -dir.x, arcTex, key) * arcW * feed * 0.8;

    // The photon ring.
    col += nebEmission(0.08 + key, 0.8) * exp(-pow((r - HZ_SHADOW * 1.12) / (0.006 + 0.004 * bass), 2.0)) * 0.6 * feed;

    // Near half, in front of everything.
    float nearHalf = 1.0 - farHalf;
    float alpha = clamp(length(disk) * 1.4, 0.0, 1.0) * nearHalf;
    col = col * (1.0 - 0.7 * alpha) + disk * nearHalf;

    fragColor = vec4(grade(col), 1.0);
}
