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
//#include lib_sdf3
//#include lib_foam

// Foam Orb: the packing cut into a ball and seen from outside, orbited by the
// rig's object camera and backlit, so its silhouette is a halo. The ball's
// surface shows the circles it cuts from every shell; the wedge fold works
// about the vertical axis, so the orb is a kaleidoscope turned on the mid clock.
//
// ---- audio ------------------------------------------------------------------
//
// lib_foam's defaults, plus: the ball breathes on uBreath (bass and the bar
// oscillator, already slewed) and swells on the drop's bump; the ring train
// becomes shells of light expanding from the core on every hit.

#define ORB_FAR 8.0

void main() {
    vec2 uv = view();
    foamSetup();
    gFoamShape = FOAM_BALL;
    gFoamRadius = 1.25 * clamp(uBreath, 0.9, 1.1) * (1.0 + 0.15 * gFoamSurgeBump);
    vec3 ro;
    vec3 rd;
    foamOrbitCamera(uv, 3.4, ro, rd);
    FoamHit h = foamMarch(ro, rd, ORB_FAR);
    // Backlit: the halo is the end light, placed behind the ball.
    vec3 col = foamBackdrop(rd) * 0.8;
    if (h.t > 0.0) {
        FoamSurf s = foamSurface(ro, rd, h);
        col = foamPearl(s) + 0.4 * foamNeon(s);
        col += foamColor(0.62) * s.fres * foamSat(dot(s.n, -gFoamFwd)) * 0.5;
    }
    col = foamAtmosphere(col, h, 0.05, 0.9);
    fragColor = foamFinish(col);
}
