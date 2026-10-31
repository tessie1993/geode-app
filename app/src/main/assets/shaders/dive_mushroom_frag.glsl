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

// Dive Mushroom: a straight dive down a tunnel lined with a forest of
// mushrooms - eight round the wall per ring, a ring every two units, each a
// tapering pale stem growing in from the wall and a domed cap. The caps'
// undersides glow softly (bioluminescent gills); the wall is dark moss.
//
// ---- audio ------------------------------------------------------------------
//
// Kick (SND_KICK_ENV) PULSES THE CAPS: every cap in the tunnel swells on the kick
// and settles as the envelope falls. The glow under the caps is constant;
// only the geometry moves on a hit. Palette switching as in lib_dive.
// Brightness follows SND_LEVEL mildly.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the clock. Complexity: uMorph grows a second, smaller
// mushroom between each pair along the ring. Chaos: uWarp/uTurbulence bend
// the stems. Energy: uIntensity scales the cap pulse. Colour: diveColor.
// Space: the rig.

void main() {
    vec2 uv = view();
    diveSetup(DIVE_MUSHROOM);

    vec3 ro;
    vec3 rd;
    diveCamera(DIVE_CAM_STRAIGHT, uv, ro, rd);

    float used;
    float hitT = diveMarch(ro, rd, used);
    vec3 col = diveBackdrop(rd);
    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        vec3 n = diveNormal(p, diveEps(hitT));
        float mat = gDiveHitMat;
        float id = gDiveHitId;
        vec3 capCol = diveColor(0.1 + 0.6 * id);
        vec3 base = mat > 1.5 ? mix(vec3(0.78, 0.72, 0.62), capCol, 0.2)
                  : (mat > 0.5 ? capCol : diveColor(0.75) * 0.22);
        col = diveShade(n, rd, hitT, used, base, 16.0);
        if (mat > 0.5 && mat < 1.5) {
            // Gills: the underside of a cap faces back out toward the wall.
            float under = smoothstep(0.0, 0.8, dot(n.xy, normalize(p.xy + 1e-5)));
            col += capCol * under * 0.4 * exp(-0.05 * hitT);
        }
    }

    col *= 0.85 + 0.2 * clamp(SND_LEVEL, 0.0, 1.5);
    fragColor = vec4(grade(col), 1.0);
}
