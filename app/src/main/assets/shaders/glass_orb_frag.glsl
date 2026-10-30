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

// Glass Orb: one solid glass sphere hovering just above the studio floor, the
// family's free style and its cheapest. Everything is analytic: one ray-sphere
// quadratic, one ray-plane, one path through the ball (refract in, cross the
// chord, refract out) and two looks into the backdrop - no march, no loop, no
// dispersion. The orb is a lens: the floor grid and the light panels behind it
// show through upside down and pulled into its rim, the key panel overhead
// sits on its crown as a soft window highlight, and the Fresnel curve brightens
// its edge into a thin ring of reflected room. Under it the floor carries the
// shadow every glass ball casts - dark around the outside, a bright focused
// spot at the heart where the lens gathered the light.
//
// ---- audio ------------------------------------------------------------------
//
// Bass (uBandsLo.y, steadied with uBassSmooth) swells the orb's IOR from 1.38
// toward 1.58: the world inside it bulges, the inverted image shrinks and
// sharpens, and the caustic spot under it tightens, on every low note, then
// relaxes. The key (uKeyHue, weighted by uKeyStrength) drifts the palette.
// Brightness follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the slow bob and the palette drift. Complexity (uMorph)
// thickens the glass's tint. Chaos (uWarp, uTurbulence) wobbles the surface
// normal (glassWobble), as if the ball were still molten. Energy (uIntensity)
// scales the bass swell. Colour is pal() throughout; Space is the rig
// (camAround, no spin).

#define OR_CAM_R 3.6
#define OR_RADIUS 1.0
#define OR_FLOOR -1.0

void main() {
    vec2 uv = view();
    float t = glassTime();
    float swell = clamp(uSwell, 0.0, 1.5);
    float bass = clamp(0.6 * uBandsLo.y + 0.4 * uBassSmooth, 0.0, 1.2);
    float ior = 1.38 + 0.17 * bass * glassEnergy();
    float hue = 0.04 * sin(t * 0.023) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);

    vec3 ro;
    vec3 rd;
    camAround(uv, OR_CAM_R, 0.0, ro, rd);
    vec3 c = vec3(0.0, OR_FLOOR + OR_RADIUS + 0.08 + 0.05 * sin(t * 0.37), 0.0);

    vec3 col = glassBackdrop(rd, hue);

    // The floor: the backdrop's own grid, with the orb's shadow and its focus.
    float tf = ro.y > OR_FLOOR ? glassPlane(ro, rd, vec3(0.0, 1.0, 0.0), OR_FLOOR) : -1.0;
    if (tf > 0.0) {
        vec3 fp = ro + rd * tf;
        vec2 rel = fp.xz - c.xz - vec2(0.12, 0.1);
        float r2 = dot(rel, rel);
        vec3 fl = glassFloorGrid(fp.xz, tf, hue);
        float lift = c.y - (OR_FLOOR + OR_RADIUS);
        fl *= mix(0.35, 1.0, smoothstep(0.1, 1.3, r2));
        float spot = exp(-r2 * (16.0 + 30.0 * (ior - 1.38))) * (0.35 - lift);
        fl += pal(hue + 0.08) * spot + pal(hue + 0.6) * exp(-r2 * 2.5) * 0.03;
        col = mix(col, fl, exp(-tf * 0.07));
    }

    vec2 hs = glassSphere(ro, rd, c, OR_RADIUS);
    if (hs.x > 0.0) {
        vec3 p = ro + rd * hs.x;
        vec3 n = glassWobble(normalize(p - c), p, glassChaos());
        float cosI = max(dot(n, -rd), 0.0);
        float fr = glassFresnel(cosI, ior);
        float chord;
        vec3 ex = glassThroughSphere(p, n, rd, c, OR_RADIUS, ior, chord);
        vec3 tint = mix(vec3(1.0), pal(hue + 0.5), 0.25 + 0.3 * glassComplexity());
        vec3 refr = glassBackdrop(ex, hue) * glassBeer(tint, chord, 0.55);
        vec3 refl = glassBackdrop(reflect(rd, n), hue);
        col = mix(refr, refl, fr);
        col += pal(hue + 0.15) * pow(1.0 - cosI, 4.0) * 0.1;
    }

    col *= 0.85 + 0.18 * swell;
    col *= 0.78 + 0.22 * smoothstep(2.2, 0.3, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
