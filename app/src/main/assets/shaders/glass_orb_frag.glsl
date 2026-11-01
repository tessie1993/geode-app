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
//#include lib_glass

// Glass Orb: one large solid glass sphere hovering over a glossy floor,
// the family's free style. It floats in the flowing AURORA light, so what
// shows through it never stops moving. Everything is analytic: one ray-sphere quadratic,
// one ray-plane, the two-bounce path through the ball (glassSphereTrace) with
// three-wavelength dispersion on the way out, so the studio's strip lights
// show through it upside down with rainbow edges, and the share the far wall
// reflects comes round a second time as a fainter, inverted-again ghost. The
// key softbox sits on the crown; the Fresnel rim mirrors the room. Under it the
// floor carries the shadow a glass ball really casts - dark round the outside,
// a focused caustic spot at its heart, fringed red-out, blue-in - and a soft
// mirror image of the orb itself.
//
// ---- audio ------------------------------------------------------------------
//
// Five things move, none of them the frame's brightness:
//   bass (GlassEar.bass)     swells the IOR 1.36 -> 1.6: the world inside bulges and
//                            the caustic spot under the ball tightens;
//   centroid (sharp)         widens the dispersion: bright mixes fringe harder;
//   flatness (frost)         frosts the ball: the lookup roughens and grain appears;
//   pan and pitch height     walk the key softbox round and up, so the crown
//                            highlight and the floor spot follow the mix;
//   hat                      throws glints across the surface;
//   kick                     rings the ball: standing ripples run over its skin.
// The key (uKeyHue, uKeyStrength) drifts the palette.
//
// ---- macros -----------------------------------------------------------------
//
// Speed scales the bob and drift. Complexity tints the glass. Chaos wobbles the
// surface (glassWobble). Energy scales every reaction (via glassListen).
//
// ---- cost -------------------------------------------------------------------
//
// About 8 world lookups a pixel on the ball (3 dispersed, 1 second bounce, 1
// reflection, 1 floor mirror, 1 backdrop), 2 elsewhere. Saver drops the bounce.

#define OR_CAM_R 3.4
#define OR_RADIUS 1.0
#define OR_FLOOR -1.05

void main() {
    vec2 uv = view();
    float t = glassTime();
    GlassEar ear = glassListen();
    float hue = 0.04 * sin(t * 0.023) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    GlassLook lk = glassLook(GLASS_ENV_AURORA, hue, ear);
    float ior = 1.36 + 0.2 * clamp(ear.bass, 0.0, 1.2);
    float spread = 0.012 + 0.04 * ear.sharp;
    float frost = ear.frost * 0.6;
    int bounces = glassBounces();

    vec3 ro;
    vec3 rd;
    camAround(uv, OR_CAM_R, 0.0, ro, rd);
    vec3 c = vec3(0.0, OR_FLOOR + OR_RADIUS + 0.12 + 0.05 * sin(t * 0.37), 0.0);
    vec3 tint = mix(vec3(1.0), pal(hue + 0.5), 0.2 + 0.5 * glassComplexity());

    vec3 col = glassEnv(rd, lk);

    // The floor: the world's floor, darkened by the ball's shadow, lit by its focus, mirroring it.
    float tf = ro.y > OR_FLOOR ? glassPlane(ro, rd, vec3(0.0, 1.0, 0.0), OR_FLOOR) : -1.0;
    vec2 hs = glassSphere(ro, rd, c, OR_RADIUS);
    if (tf > 0.0 && (hs.x < 0.0 || tf < hs.x)) {
        vec3 fp = ro + rd * tf;
        vec3 key = glassKeyDir(lk);
        // Where the key's ray through the ball's centre lands: the shadow and the spot sit there.
        vec2 sc = c.xz - key.xz / max(key.y, 0.2) * (c.y - OR_FLOOR);
        vec2 rel = fp.xz - sc;
        float r2 = dot(rel, rel);
        float fl = 1.0 - 0.75 * exp(-r2 * 0.9);
        vec3 mr = reflect(rd, vec3(0.0, 1.0, 0.0));
        vec3 floorCol = glassEnv(rd, lk) * fl;
        vec2 hm = glassSphere(fp, mr, c, OR_RADIUS);
        if (hm.x > 0.0) {
            vec3 mp = fp + mr * hm.x;
            vec3 mn = normalize(mp - c);
            floorCol = mix(floorCol, glassEnv(reflect(mr, mn), lk) * 0.35 + pal(hue + 0.5) * 0.03, 0.55);
        }
        float k = 10.0 + 26.0 * (ior - 1.36);
        vec3 spot = vec3(exp(-r2 * k * 0.8), exp(-r2 * k), exp(-r2 * k * 1.25));
        floorCol += mix(vec3(1.0, 0.95, 0.85), pal(hue + 0.08), 0.35) * spot * 1.8 * (1.0 - frost);
        col = mix(col, floorCol, exp(-tf * 0.05));
    }

    if (hs.x > 0.0) {
        vec3 p = ro + rd * hs.x;
        vec3 n = glassWobble(normalize(p - c), p, glassChaos());
        // The kick rings the ball like struck jelly: a few standing waves on its surface, eased.
        vec3 lc = (p - c) / OR_RADIUS;
        n = normalize(n + ear.kick * 0.14 * sin(lc * vec3(7.0, 9.0, 8.0) + t * vec3(5.0, 4.0, 6.0)));
        n = glassGrain(n, p, frost);
        float cosI = max(dot(n, -rd), 0.0);
        float fr = glassFresnel(cosI, ior);
        GlassLook inner = lk;
        inner.rough += frost * 0.25;
        vec3 refr = glassSphereTrace(p, n, rd, c, OR_RADIUS, ior, spread, tint, 0.5, bounces, inner);
        vec3 refl = glassEnvRough(reflect(rd, n), lk, frost * 0.15);
        col = refr * (1.0 - fr) + refl * fr;
        col += pal(hue + 0.15) * pow(1.0 - cosI, 3.0) * 0.08;
        col += vec3(1.0) * glassGlint(p - c, 9.0, ear.hat) * (0.3 + 0.7 * fr + 0.3 * cosI);
    }

    col *= 0.8 + 0.2 * (1.0 - smoothstep(0.4, 2.2, length(uv)));
    fragColor = vec4(grade(glassTone(col)), 1.0);
}
