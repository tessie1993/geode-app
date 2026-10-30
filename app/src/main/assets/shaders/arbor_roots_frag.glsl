#version 300 es
precision highp float;
precision highp sampler2D;

in vec2 vUv;
out vec4 fragColor;

//#include lib_scene_uniforms
//#include lib_scene_motion
//#include lib_react
//#include lib_palette
//#include lib_scene_grade
//#include lib_sdf3
//#include lib_arbor

// Arbor Roots: a cut through the earth below a tree. A thin strip of night
// sky and grass on top, then soil strata, pebbles and glowing threads of
// mycelium, and the tree's root system branching down through it all with two
// smaller plants' roots beside it. Flat 2D, a cross-section: no march.
//
// The roots are arborTree2 grown DOWNWARD with a high per-branch jitter
// hashed from the branch id, because roots wander around stones where
// branches reach for light. The sap pulse reads gArbLevel and gArbAlong, the
// fold's own record of how deep along the tree a pixel is, so a front can
// travel from the trunk to the root tips without any per-branch bookkeeping.
//
// ---- audio ------------------------------------------------------------------
//
// Sub (uBandsLo.x) SPLAYS the roots: it widens every fork angle, so a deep
// sub-bass passage fans the whole system out sideways through the soil and a
// quiet one draws it back into a narrow tap. The beat (uEvents.x) sends a sap
// front down the roots, trunk to tips, as a change of HUE along the root (amber
// to the palette's cool end) at near-constant lightness, not a flash. Swell
// (uSwell) gently brightens the mycelium threads.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the mycelium drift, the grass and the roots' slow
// creep. Complexity: uMorph adds a fork level to every root system
// (arbLevels2). Chaos: uTurbulence and uWarp raise the per-branch wander, the
// roots kinking harder around stones. Colour: soil, roots, sap and threads
// from pal(). Energy: uIntensity scales the sub splay and the sap front.
// Space: the view zoom and pan.

#define RT_SURFACE 0.58
#define RT_LEVELS 9.0

void main() {
    vec2 uv = arbView();
    float px = max(length(fwidth(uv)), 1e-4);
    float t = uTime * uSpeed;
    float energy = uIntensity;
    float chaos = arbChaos();
    float swell = clamp(uSwell, 0.0, 1.5);
    float sub = clamp(uBandsLo.x, 0.0, 1.0) * energy;
    float beat = clamp(uEvents.x * energy, 0.0, 1.0);

    float hue = 0.07 + 0.02 * sin(t * 0.03);

    // ---- soil -----------------------------------------------------------------
    float n = fbm3(vec3(uv * 3.0, 1.7), 3);
    float strata = fract(uv.y * 2.6 + n * 0.5);
    vec3 soilA = mix(vec3(0.16, 0.10, 0.06), pal(hue) * 0.22, 0.4);
    vec3 soilB = mix(vec3(0.09, 0.06, 0.04), pal(hue + 0.05) * 0.12, 0.4);
    vec3 col = mix(soilA, soilB, smoothstep(0.3, 0.7, strata));
    col *= 0.75 + 0.35 * n;
    col *= mix(0.55, 1.0, smoothstep(-1.2, RT_SURFACE, uv.y));
    // Pebbles: one jittered stone per cell, some cells empty.
    vec2 sg = uv * 7.0;
    vec2 sc = floor(sg);
    float sh = hash13(vec3(sc, 9.0));
    vec2 so = vec2(hash13(vec3(sc, 10.0)), hash13(vec3(sc, 11.0))) * 0.6 + 0.2;
    float stone = length((fract(sg) - so) * vec2(1.0, 1.4)) - 0.13 * sh;
    vec3 stoneCol = mix(vec3(0.32, 0.30, 0.28), pal(hue + 0.5) * 0.3, 0.3) * (0.7 + 0.3 * sh);
    col = mix(col, stoneCol, arbInk(stone / 7.0, px) * step(0.6, sh));
    // Mycelium: thin glowing threads along a noise field's zero set.
    float m = vnoise3(vec3(uv * 9.0, t * 0.04)) - 0.5;
    float thread = 1.0 - smoothstep(0.0, 0.025, abs(m));
    col += pal(hue + 0.45) * thread * (0.10 + 0.06 * swell) * smoothstep(RT_SURFACE, RT_SURFACE - 0.2, uv.y);

    // ---- roots ------------------------------------------------------------------
    float levels = arbLevels2(RT_LEVELS, 1.0);
    float splay = 0.22 * sub;
    float wander = 0.70 + 0.5 * chaos;
    float creep = 0.02 * sin(t * 0.2);
    ArborForm2 main0 = ArborForm2(vec2(0.50 + splay, 0.42 + splay), vec2(0.74, 0.68), creep, wander, 0.25, 0.11, 0.70);
    vec2 rp = -(uv - vec2(0.0, RT_SURFACE + 0.02));
    float d = arborTree2(rp, main0, levels, 5.0);
    float lvl = gArbLevel;
    float along = gArbAlong;

    ArborForm2 small = ArborForm2(vec2(0.65 + splay, 0.55 + splay), vec2(0.70, 0.64), -creep, wander + 0.2, 0.12, 0.10, 0.70);
    float dl = arborTree2(-(uv - vec2(-0.36, RT_SURFACE + 0.01)), small, levels - 2.0, 21.0);
    float dr = arborTree2(-(uv - vec2(0.38, RT_SURFACE + 0.01)), small, levels - 2.0, 37.0);
    float side = min(dl, dr);

    // Sap front: depth along the tree is level + along; the front runs from
    // the trunk (0) to the tips while the beat envelope falls.
    float depth = lvl + along;
    float front = (1.0 - beat) * (levels + 1.5);
    float sap = exp(-pow((depth - front) * 1.4, 2.0)) * smoothstep(0.0, 0.15, beat);
    vec3 rootCol = mix(vec3(0.62, 0.46, 0.30), pal(hue + 0.02), 0.45);
    vec3 sapCol = pal(hue + 0.5) * 0.85;
    vec3 rc = mix(rootCol, sapCol, sap);
    float spine = smoothstep(-0.001, -0.01, d);
    col = mix(col, rc * (0.6 + 0.4 * spine), arbInk(d, px));
    col += sapCol * sap * exp(-max(d, 0.0) * 60.0) * 0.12;
    col = mix(col, rootCol * 0.75, arbInk(side, px));

    // ---- surface: sky strip, grass and the trunk --------------------------
    float grassTop = RT_SURFACE + 0.025 + 0.02 * abs(sin(uv.x * 90.0 + 0.6 * sin(t * 0.8 + uv.x * 4.0)));
    vec3 sky = mix(pal(hue + 0.6) * 0.06 + vec3(0.01, 0.015, 0.04), vec3(0.0), smoothstep(RT_SURFACE, 1.2, uv.y));
    float above = smoothstep(RT_SURFACE - px, RT_SURFACE + px, uv.y);
    col = mix(col, sky, above);
    float grass = arbInk(uv.y - grassTop, px) * above;
    col = mix(col, vec3(0.02, 0.035, 0.02), grass);
    float trunk = max(abs(uv.x) - 0.045 - 0.02 * smoothstep(RT_SURFACE + 0.15, RT_SURFACE, uv.y), RT_SURFACE - uv.y);
    col = mix(col, vec3(0.03, 0.022, 0.018), arbInk(trunk, px));

    col *= 0.9 + 0.12 * swell;
    col *= 0.8 + 0.2 * smoothstep(2.0, 0.3, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
