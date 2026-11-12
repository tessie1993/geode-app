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

// Nebula Cluster: a dense globular star cluster, now a ball of stars you
// circle, seen through a lens focused on its heart. Thousands of point stars
// crowd toward the centre on a King-like profile inside the faint glow of the
// stars too small to resolve; the brightest few dozen are drawn as soft, flat
// bokeh discs with a brighter rim, and because they now have depth the lens
// treats them as a lens would: a bright star near the focal plane is a small
// disc, one in front of or behind it a wider, fainter one.
//
// ---- the scene ---------------------------------------------------------------
//
// Three things along each ray, all inside a sphere of radius CL_R about the
// origin (nebSphere trims every ray to it):
//
//   THE GLOW. The unresolved stars, a short emissive march (CL_GLOW_MAX samples,
//   a quarter of the Detail budget) through the King profile 1 / (1 + r^2/rc^2)
//   with a fine 3D grain (nebFbm3). Emissive only: a cluster blocks nothing.
//
//   THE POINT STARS. One star per cubic cell of CL_CELL, found by walking the
//   ray through the grid cell by cell (a 3D DDA, at most CL_MAX_CELLS cells,
//   fewer at low Detail), so a star costs four hashes and a closest-approach
//   distance - nothing is ray-marched. The chance a cell holds a star is the
//   King profile read at the cell's CENTRE, as in 2D.
//
//   THE BOKEH. The same walk on a coarser grid (CL_BCELL), one disc per cell,
//   its radius growing with the circle of confusion |t / focus - 1|.
//
// Behind it all, a sparse field of foreground and background stars on the sky.
//
// ---- the camera ------------------------------------------------------------------
//
// The rig's object-centred camera (camAround) at CL_CAM_R, turning about +y at
// two CAM_SPIN_UNITs per world unit of travel, an integer multiple, so the rig's
// 8192 travel wrap lands on a whole number of turns and never shows. Nothing in
// the scene reads world z or travel, so there is no other wrap to fit. The
// rig's look, bank, dolly and FOV come through; the dolly can pull the eye in
// to 0.65 * CL_CAM_R = 2.73, still outside CL_R.
//
// ---- why nothing cracks (there is no distance estimate) -------------------------
//
// No step is ever taken by a distance bound, so no estimate has to be safe.
// What has to hold instead is the cell rule nebStars keeps in 2D, lifted to 3D:
// every star, halo and disc lies wholly inside its own cell, at its largest
// (the jitter shrinks as the radius, the Chaos wander and an ignition grow). A
// ray that comes within a star's radius passes through that ball, so it enters
// that cell, so the walk visits it: the field is continuous across every cell
// face with no neighbour search. Both walks and the glow march are bounded by
// constants, and break earlier on uSteps.
//
// ---- audio ------------------------------------------------------------------
//
// Every onset IGNITES A HANDFUL OF STARS: the onset counter (SND_ONSET_COUNT)
// re-hashes which bokeh cells flare, so each hit picks a new random dozen;
// those discs swell and take a palette hue as the onset envelope
// (SND_ONSET_ENV) rises, then settle back. Only those discs change. The kick
// (SND_KICK_ENV) racks the focus a little deeper, so the discs breathe in size,
// not in light. The hats twinkle the point stars. The key turns the glow's hue.
// The core glow follows SND_LEVEL gently.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: already in uTime (twinkle, the glow's shimmer, the Chaos wander).
// Complexity: uMorph adds octaves to the glow's grain (nebVolOctaves).
// Chaos: uWarp/uTurbulence swirl the grain (nebWarp3) and set the stars
// wandering inside their cells; uTwist via view(). Colour: the glow and the
// ignition hues are pal(). Energy: uIntensity scales how far an ignition
// swells and the focus pull. Space: the rig (camAround).
//
// Knobs (offsets from 0.5, so off is the tuned look): k0 PULSE how far an
// ignition swells, k1 TEXTURE the grain's depth, k2 FLOW the stars' wander,
// k3 TONE the hue, k4 BUILD the white heat of the core, k5 SPACE the lens
// aperture (how wide the out-of-focus discs open).

#define CL_CAM_R 4.2
#define CL_SPIN (2.0 * CAM_SPIN_UNIT)
/** Radius of the cluster's support sphere. */
#define CL_R 2.2
/** King core radius squared, world units. */
#define CL_RC2 0.18
/** Point-star grid cell, and the walk's constant bound (a diameter is 22 cells, a diagonal chord up to ~60 steps). */
#define CL_CELL 0.2
#define CL_MAX_CELLS 72
/** Bokeh grid cell and its walk's bound. */
#define CL_BCELL 0.8
#define CL_MAX_BCELLS 20
/** Largest point-star radius: 2.5x it (the halo) stays inside half a cell. */
#define CL_STAR_RMAX 0.034
/** Largest bokeh radius, ignited and defocused: stays inside half a bokeh cell with room to jitter. */
#define CL_BOKEH_RMAX 0.34
/** Glow march samples, constant bound. */
#define CL_GLOW_MAX 24
/** Base scale of the glow's grain. */
#define CL_GRAIN 7.0

/** One step of a 3D DDA: move to the next cell the ray enters; sEnter is where (in cell units of the walk). */
void clStep(inout vec3 c, inout vec3 tMax, vec3 tDelta, vec3 st, out float sEnter) {
    if (tMax.x < tMax.y && tMax.x < tMax.z) {
        sEnter = tMax.x;
        c.x += st.x;
        tMax.x += tDelta.x;
    } else if (tMax.y < tMax.z) {
        sEnter = tMax.y;
        c.y += st.y;
        tMax.y += tDelta.y;
    } else {
        sEnter = tMax.z;
        c.z += st.z;
        tMax.z += tDelta.z;
    }
}

/** A ray direction with no zero component, so 1 / rd is finite. */
vec3 clSafeDir(vec3 rd) {
    vec3 s = vec3(rd.x >= 0.0 ? 1.0 : -1.0, rd.y >= 0.0 ? 1.0 : -1.0, rd.z >= 0.0 ? 1.0 : -1.0);
    return s * max(abs(rd), vec3(1e-5));
}

/** The King profile, 1 at the centre. */
float clKing(float r2) {
    return 1.0 / (1.0 + r2 / CL_RC2);
}

/**
 * What the field holds at a point (lib_nebula_field): the unresolved stars' King profile (their density
 * here, faded to exactly zero at the support sphere) and the glow's grain there (0.5 where it is empty).
 * The point stars are walked cell by cell (clStep), not read from a field.
 */
struct NebHit {
    float dens;
    float grain;
};

NebHit nebMap(vec3 p, float oct) {
    float r2 = dot(p, p);
    NebHit h;
    h.dens = clKing(r2) * (1.0 - smoothstep(0.55 * CL_R * CL_R, CL_R * CL_R, r2));
    h.grain = 0.5;
    if (h.dens > 0.004) {
        float t = nebTime();
        h.grain = nebFbm3(nebWarp3(p, t) * CL_GRAIN + vec3(t * 0.01, 0.0, 0.0), oct);
    }
    return h;
}

//#include lib_nebula_field

void main() {
    vec2 uv = view();
    nebSetup();
    float t = nebTime();
    float e = nebEnergy();
    float swell = clamp(SND_LEVEL * uSoundDrive, 0.0, 1.5);
    float hats = clamp(SND_HAT_ENV * uSoundDrive, 0.0, 1.0);
    float kick = clamp(SND_KICK_ENV * uSoundDrive, 0.0, 1.0);
    float kPulse = gNebIn.pulse - 0.5;
    float kTexture = gNebIn.texture - 0.5;
    float kFlow = gNebIn.flow - 0.5;
    float kTone = gNebIn.tone - 0.5;
    float kBuild = gNebIn.build - 0.5;
    float kSpace = gNebIn.space - 0.5;
    // Clamped: an ignited disc may not outgrow its cell, whatever the Energy.
    float onset = clamp(SND_ONSET_ENV * uSoundDrive * e * (1.0 + kPulse), 0.0, 1.0);
    float oct = min(nebVolOctaves(2.0), 3.0);
    float key = nebKeyShift(0.1) + 0.2 * kTone;
    float wander = clamp(nebChaos() * 0.02 + 0.02 * kFlow, 0.0, 0.03);

    vec3 ro;
    vec3 rd;
    camAround(uv, CL_CAM_R, CL_SPIN, ro, rd);
    float pxK = 2.0 / (max(uCamFocal, 0.1) * max(uResolution.y, 1.0));

    vec3 col = vec3(0.0);
    vec2 tb = nebSphere(ro, rd, vec3(0.0), CL_R);
    if (tb.y > tb.x) {
        float span = tb.y - tb.x;
        vec3 entry = ro + rd * tb.x;
        vec3 rds = clSafeDir(rd);
        vec3 st = sign(rds);
        vec3 step01 = max(st, 0.0);
        vec3 tDelta = abs(1.0 / rds);

        // ---- the glow: unresolved stars, emissive only ----
        float nGlow = clamp(floor(uSteps * 0.25), 8.0, float(CL_GLOW_MAX));
        float dt = span / nGlow;
        float tt = tb.x + dt * nebDither();
        float heat = clamp(0.6 + 0.4 * kBuild, 0.0, 1.0);
        float grainK = clamp(1.0 + 2.0 * kTexture, 0.0, 2.0);
        float glow = 0.0;
        float core = 0.0;
        for (int i = 0; i < CL_GLOW_MAX; i++) {
            if (float(i) >= nGlow) break;
            NebHit at = nebField(ro + rd * tt, oct);
            // Faded to exactly zero at the support sphere, so the trim never shows.
            float k = at.dens;
            if (k > 0.004) {
                float g = at.grain;
                glow += k * (0.16 + 0.1 * grainK * (g - 0.5) * 2.0 + 0.05) * dt;
                core += k * k * dt;
            }
            tt += dt;
        }
        vec3 glowCol = nebEmission(0.1 + key, heat * 0.6);
        col += glowCol * glow * 0.7 * (0.85 + 0.2 * swell);
        col += nebEmission(0.06 + key, heat) * core * 0.08 * (0.85 + 0.2 * swell);

        // ---- the point stars: one per cell, walked by a DDA ----
        {
            vec3 pos = entry / CL_CELL;
            vec3 c = floor(pos);
            vec3 tMax = (c + step01 - pos) / rds;
            float sEnter = 0.0;
            float sEnd = span / CL_CELL;
            float nCells = clamp(floor(uSteps * 0.75), 24.0, float(CL_MAX_CELLS));
            for (int i = 0; i < CL_MAX_CELLS; i++) {
                if (float(i) >= nCells) break;
                if (sEnter > sEnd) break;
                vec3 cc = (c + 0.5) * CL_CELL;
                float fill = 0.03 + 0.62 * clKing(dot(cc, cc)) * (1.0 - smoothstep(CL_R - 0.5, CL_R, length(cc)));
                float h = nebHash3(c + vec3(71.3, 5.1, 13.7));
                float on = 1.0 - smoothstep(fill - 0.02, fill, h);
                if (on > 0.0) {
                    vec3 j = vec3(nebHash3(c), nebHash3(c + vec3(19.1, 3.3, 7.7)), nebHash3(c + vec3(4.9, 29.3, 11.1)));
                    float rel = clamp(h / max(fill, 1e-3), 0.0, 1.0);
                    float rs = mix(0.022, 0.007, rel);
                    float bright = mix(1.0, 0.07, sqrt(rel));
                    // The wander is a slow orbit inside the cell's margin.
                    vec3 wob = wander * vec3(sin(t * 0.7 + j.x * 40.0), sin(t * 0.6 + j.y * 40.0), sin(t * 0.8 + j.z * 40.0));
                    vec3 sp0 = cc - ro;
                    float tc0 = max(dot(sp0, rd), 0.05);
                    float r = min(max(rs, tc0 * pxK * 0.9), CL_STAR_RMAX);
                    float jit = max(0.5 * CL_CELL - 2.5 * r - wander, 0.0);
                    vec3 sp = sp0 + (j - 0.5) * 2.0 * jit + wob;
                    float tc = dot(sp, rd);
                    if (tc > 0.0) {
                        float d = length(sp - rd * tc);
                        float cr = max(1.0 - d / r, 0.0);
                        float halo = max(1.0 - d / (2.5 * r), 0.0);
                        float shape = cr * cr + 0.16 * halo * halo;
                        // A star drawn wider than itself keeps its flux, not its peak.
                        float flux = (rs * rs) / (r * r);
                        float tw = 1.0 - hats * 0.65 * (0.5 + 0.5 * sin(t * (4.0 + 6.0 * j.y) + h * 40.0));
                        tw *= 0.9 + 0.1 * sin(t * (0.7 + j.x) + h * 17.0);
                        col += nebStarTint(j.y) * shape * bright * flux * on * tw * 0.9;
                    }
                }
                clStep(c, tMax, tDelta, st, sEnter);
            }
        }

        // ---- the bokeh: the brightest, out of focus by depth; onsets ignite a few ----
        {
            float seed = mod(floor(SND_ONSET_COUNT), 512.0);
            float focus = length(ro) * (1.0 + 0.1 * kick * e);
            float aperture = clamp(0.22 * (1.0 + 1.2 * kSpace), 0.0, 0.4);
            vec3 pos = entry / CL_BCELL;
            vec3 c = floor(pos);
            vec3 tMax = (c + step01 - pos) / rds;
            float sEnter = 0.0;
            float sEnd = span / CL_BCELL;
            float nCells = clamp(floor(uSteps * 0.2), 10.0, float(CL_MAX_BCELLS));
            for (int i = 0; i < CL_MAX_BCELLS; i++) {
                if (float(i) >= nCells) break;
                if (sEnter > sEnd) break;
                vec3 cc = (c + 0.5) * CL_BCELL;
                float rc = length(cc);
                float fill = (0.06 + 0.42 * clKing(dot(cc, cc) * 0.5)) * (1.0 - smoothstep(CL_R - 0.6, CL_R, rc));
                float h = nebHash3(c * 1.7 + vec3(5.0, 2.0, 9.0));
                float on = 1.0 - smoothstep(fill - 0.03, fill, h);
                float pick = nebHash3(c + vec3(seed * 0.731, 2.0, 5.0));
                float ignite = smoothstep(0.86, 0.9, pick) * onset * (1.0 - smoothstep(CL_R - 0.4, CL_R, rc));
                if (on + ignite > 0.0) {
                    vec3 j = vec3(nebHash3(c + vec3(13.0, 0.0, 0.0)), nebHash3(c + vec3(0.0, 13.0, 7.0)), nebHash3(c + vec3(3.0, 5.0, 13.0)));
                    vec3 sp0 = cc - ro;
                    float tc0 = max(dot(sp0, rd), 0.05);
                    float r0 = 0.06 + 0.035 * j.x;
                    float coc = aperture * abs(tc0 / max(focus, 0.5) - 1.0);
                    float rad = min((r0 + coc) * (1.0 + 0.4 * ignite), CL_BOKEH_RMAX);
                    // Jitter sized for the largest a disc can grow, so it swells in place.
                    vec3 sp = sp0 + (j - 0.5) * 2.0 * (0.5 * CL_BCELL - CL_BOKEH_RMAX - 0.01);
                    float tc = dot(sp, rd);
                    if (tc > 0.0) {
                        float d = length(sp - rd * tc);
                        float edge = max(0.035 * rad / 0.13, tc * pxK * 1.5);
                        float disc = 1.0 - smoothstep(rad - edge, rad, d);
                        float rim = smoothstep(rad * 0.5, rad, d) * disc;
                        float shape = disc * (0.35 + 0.55 * rim);
                        // The light spreads over a wider disc: it dims as it opens.
                        float spread = mix(1.0, (r0 * r0) / (rad * rad), 0.6);
                        vec3 tint = mix(nebStarTint(j.y), nebEmission(nebHash3(c + seed) + key, 0.3), ignite);
                        col += tint * shape * spread * (on * 0.24 + ignite * 0.4);
                    }
                }
                clStep(c, tMax, tDelta, st, sEnter);
            }
        }
    }

    // The sky around the cluster: a sparse field, faint.
    col += nebStars(nebSky(rd), 9.0, 2, vec2(0.0), 0.8, 0.08, hats, t) * 0.6;

    col *= 0.8 + 0.2 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
