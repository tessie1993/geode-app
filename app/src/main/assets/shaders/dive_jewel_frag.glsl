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
//#include lib_dive

// Dive Jewel: a straight dive through a string of cut-gem chambers. Each
// cell is a cavity faceted like a stone - an octagonal girdle, crown facets
// tapering to a round doorway into the next chamber, star facets on the
// corners - with a crystal set in every wedge of the girdle. Every other
// chamber is turned half a facet, so the cuts never line up down the tunnel.
//
// ---- audio ------------------------------------------------------------------
//
// Hats (SND_HAT_ENV) make the facets GLINT: on every hat a hashed third of the
// facets (re-chosen per onset through floor(SND_HIT_TIME)) catches a sharp highlight
// that falls with the hat envelope. Only small facets light, never the frame.
// The palette switches on sections and every 16 onsets (lib_dive); the
// facets' inner colour turns with the view, like fire in a stone.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock. Complexity: uMorph cuts a second ring of
// star facets. Chaos: uWarp/uTurbulence bend the cut. Energy: uIntensity
// scales the glints. Colour: diveColor. Space: the rig.

void main() {
    vec2 uv = view();
    diveSetup(DIVE_JEWEL);
    float e = diveEnergy();

    vec3 ro;
    vec3 rd;
    diveCamera(DIVE_CAM_STRAIGHT, uv, ro, rd);

    float used;
    float hitT = diveMarch(ro, rd, used);
    vec3 col = diveBackdrop(rd);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = diveNormal(p, diveEps(hitT));
        float id = gDiveHitId;
        vec3 refl = reflect(rd, n);
        float fres = pow(1.0 - max(dot(n, -rd), 0.0), 4.0);
        vec3 base = diveMaterial(gDiveHitMat, id, hitT) * 0.7;
        col = diveShade(n, rd, hitT, used, base, 64.0);
        // Fire: the colour inside the stone turns with the reflected direction.
        vec3 fire = diveColor(id + 1.3 * dot(refl, vec3(0.3, 0.5, 0.8)));
        float fog = exp(-0.055 * hitT);
        col += fire * (0.12 + 0.4 * fres) * fog;
        float pick = step(0.66, dive_hash11(id * 91.0 + fract(floor(SND_HIT_TIME) * 0.618) * 37.0));
        float glint = pick * clamp(SND_HAT_ENV * e, 0.0, 1.0) * pow(max(dot(refl, -rd), 0.0), 6.0);
        col += mix(vec3(1.0), diveColor(id + 0.2), 0.3) * glint * 0.7 * fog;
    }

    col *= 0.85 + 0.2 * clamp(SND_INTENSITY, 0.0, 1.5);
    fragColor = vec4(grade(col), 1.0);
}
