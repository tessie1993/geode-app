#version 300 es
#define VIEW_OWNS_SHAPE
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_camera
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_glass

// Glass Caustic: hovering a few metres under a sunlit sea, looking down and
// ahead at the sand. The floor - rippled sand with dark stones - is crossed by
// a net of bright light lines that crawl and re-knot as the waves overhead
// move: the caustic of glassWaves, the light map's folds, evaluated where each
// view ray meets the floor. The water between eye and floor is a blue haze that
// swallows the distance, and through it slant shafts of light fall from the
// surface. Look up and the surface shows Snell's window: a bright disc of sky
// inside the critical angle, the mirrored dark of the deep outside it. Specks
// of marine snow drift past. Analytic: one ray-plane for the floor, one for the
// surface, and eight cheap taps along the ray for the shafts.
//
// The eye holds still (it does not ride the rig's path; the floor drifts
// under it instead, with the current), and the rig's look turns the view, so
// this is the rig's camera pitched down toward the sand.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (uBassSmooth, with uBandsLo.y) sharpens the net: the caustic's
// contrast climbs, so the lines burn brighter and the gaps between them go
// darker, while the floor's mean light stays where it was - the net
// tightens, the frame does not flash. The current's pace rides uFlowPhase
// (loudness sets the rate, never the direction). Brightness follows uSwell.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the waves, the shafts and the current. Complexity
// (uMorph) focuses the light deeper, so the net grows finer and denser. Chaos
// (uWarp, uTurbulence) makes the surface choppier. Energy (uIntensity) scales
// the bass contrast. Colour: water, sand and light from pal(). Space is the
// rig's look.

#define CS_FLOOR -2.2
#define CS_SURF 2.8
#define CS_PITCH 0.38
#define CS_SHAFTS 8

void main() {
    vec2 uv = view();
    float t = glassTime();
    float swell = clamp(uSwell, 0.0, 1.5);
    float bass = clamp(0.65 * uBassSmooth + 0.35 * uBandsLo.y, 0.0, 1.2);
    float hue = 0.04 * sin(t * 0.02) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    float chop = 1.0 + 0.6 * glassChaos();
    float focus = 1.0 + 0.5 * glassComplexity();

    vec3 r0 = camRayDir(uv);
    float cp = cos(CS_PITCH);
    float sp = sin(CS_PITCH);
    vec3 rd = normalize(vec3(r0.x, r0.y * cp - r0.z * sp, r0.z * cp + r0.y * sp));
    vec3 ro = vec3(0.0);
    vec2 drift = vec2(0.07 * t, 0.12 * uFlowPhase * uSpeed);

    vec3 deep = pal(hue + 0.55) * 0.05 + vec3(0.0, 0.03, 0.06);
    vec3 shallow = mix(vec3(0.1, 0.45, 0.55), pal(hue + 0.5), 0.3);
    vec3 col = mix(deep, shallow * 0.5, smoothstep(-0.7, 0.9, rd.y));
    float span = 14.0;

    float tf = glassPlane(ro, rd, vec3(0.0, 1.0, 0.0), CS_FLOOR);
    if (tf > 0.0) {
        span = min(tf, span);
        vec3 fp3 = ro + rd * tf;
        vec2 fp = fp3.xz + drift;
        float rock = smoothstep(0.35, 0.6, 0.7 * glassNoise(fp * 0.45) + 0.3 * glassNoise(fp * 1.1 + 3.0));
        float ripple = 0.85 + 0.15 * sin(fp.x * 4.0 + 2.5 * glassNoise(fp * 0.7));
        vec3 sand = mix(vec3(0.78, 0.7, 0.52), pal(hue + 0.08), 0.25) * ripple;
        vec3 stone = mix(vec3(0.2, 0.22, 0.2), pal(hue + 0.35) * 0.3, 0.3);
        vec3 base = mix(sand, stone, rock);
        vec4 w = glassWaves(fp3.xz * 0.9 + drift * 0.25, t * 0.7, chop, focus);
        float net = glassCaustic(w.w, 0.1);
        net = mix(0.2, net, 0.45 + 0.55 * clamp(bass * glassEnergy(), 0.0, 1.0));
        vec3 lit = base * (0.18 + 1.8 * net) * (shallow * 1.2 + 0.15);
        col = mix(col, lit, exp(-tf * 0.13));
    } else {
        float ts = glassPlane(ro, rd, vec3(0.0, 1.0, 0.0), CS_SURF);
        if (ts > 0.0) {
            vec3 s3 = ro + rd * ts;
            vec4 w = glassWaves(s3.xz * 0.9 + drift * 0.25, t * 0.7, chop, focus);
            vec3 n = normalize(vec3(-w.y, 1.0, -w.z));
            float window = smoothstep(0.62, 0.72, dot(rd, n));
            vec3 sky = mix(vec3(0.85, 0.95, 1.0), pal(hue + 0.15), 0.3) * 1.1;
            col = mix(col, mix(deep * 1.6, sky, window), exp(-ts * 0.13));
        }
    }

    // Shafts of light: taps along the ray, each projected up to the surface
    // along the sun direction and read in a slow interference of sines.
    vec3 sun = normalize(vec3(0.25, 1.0, 0.12));
    float shaft = 0.0;
    for (int i = 0; i < CS_SHAFTS; i++) {
        float s = (float(i) + 0.5) / float(CS_SHAFTS) * span;
        vec3 q = ro + rd * s;
        vec2 sxz = q.xz + sun.xz * (CS_SURF - q.y) / sun.y + drift * 0.25;
        float g = 0.5 + 0.5 * sin(sxz.x * 1.3 + 1.6 * sin(sxz.y * 0.7 + t * 0.21) + t * 0.15);
        g *= 0.5 + 0.5 * sin(sxz.y * 1.1 - t * 0.13 + 1.2 * sin(sxz.x * 0.5));
        shaft += g * g * exp(-s * 0.16) * smoothstep(CS_FLOOR, CS_SURF, q.y);
    }
    col += shallow * shaft * span / float(CS_SHAFTS) * 0.07 * (0.85 + 0.25 * swell);
    col += shallow * fluidMotes(uv, 16.0, 0.09) * 0.12;

    col *= 0.85 + 0.15 * swell;
    col *= 0.78 + 0.22 * smoothstep(2.2, 0.3, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
