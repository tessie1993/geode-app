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
//#include lib_truchet

// Truchet Relief: the quarter-arc tiling carved into a stone floor and flown
// over at a low angle. Every arc is a rounded groove; the two regions the
// arcs divide the floor into stand at two slightly different heights, so the
// paths read as channels cut between terraces. A light hovers ahead of the
// eye and drifts from side to side, raking along the grooves so their walls
// catch and lose it. The family's one raymarched style.
//
// THE FIELD. The floor is a heightfield, h(x, z), built from one tile lookup:
// a cosine groove of half-width W and depth D about each arc, plus a terrace
// step that changes across the groove (inside it, so it never makes a cliff).
// Its steepest slope is D * pi / (2W), so the march steps by the height above
// the floor divided by sqrt(1 + slope^2) - the Lipschitz bound - and never
// steps through a groove wall. The march only runs inside the slab the floor
// can occupy, entered at its top and abandoned below its bottom.
//
// THE CAMERA. The eye rides the rig's path (uCamPos x and z; height fixed
// above the floor) and looks along the rig's view (uCamBasis) pitched down,
// so Space - path, look, shake, FOV - all apply. The rig's travel wraps at
// 8192 along z; the tiling's ids wrap at 64 cells there (64 divides 8192,
// and is even, so the parity that sets the terraces survives the wrap).
//
// ---- audio ------------------------------------------------------------------
//
// Onsets turn a scattered third of the tiles a quarter turn, so the channels
// re-route under the viewer on the hits. The bass (uBassSmooth) deepens the
// grooves (the march's step bound follows the depth). The key leans the stone's
// hue. Brightness follows uSwell only; the light moves on its own clock.
//
// ---- macros -----------------------------------------------------------------
//
// Speed: uSpeed scales the light's drift. Complexity: uMorph past 0.5 makes
// the tiles two thirds the size (more channels per stride). Chaos: uWarp/uTurbulence bend the floor's
// plan before it is tiled, so the channels meander (that bend is not periodic
// in z, so with Chaos up the rig's 8192-unit travel wrap can show once). Energy: uIntensity scales
// the bass deepening. Colour: stone, terrace and light are pal(). Space: the
// camera rig.

#define TR_MAX_STEPS 96
#define TR_TOP 0.05
#define TR_EYE 1.05
#define TR_FAR 26.0
#define TR_W 0.17

float gDepth;
float gHits;
float gScale;
float gT;
float gTone;

float floorHeight(vec2 xz) {
    vec2 p = truchetWarp(xz * gScale, gT);
    vec2 id = floor(p);
    id.y = mod(id.y, 64.0);
    TruchetArc a = truchetSquareAt(id, fract(p) - 0.5, gHits, uHits.w, 3.0);
    float g = clamp(abs(a.d) / TR_W, 0.0, 1.0);
    float groove = 0.5 + 0.5 * cos(TRUCHET_PI * g);
    gTone = mix(1.0 - a.cap, a.cap, 1.0 - smoothstep(-TR_W * 0.8, TR_W * 0.8, a.d));
    return TR_TOP * gTone - gDepth * groove;
}

float map(vec3 p) {
    return p.y - floorHeight(p.xz);
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

void main() {
    vec2 uv = view();
    float t = truchetTime();
    float e = truchetEnergy();
    float swell = clamp(uSwell, 0.0, 1.5);
    gT = t;
    gHits = floor(uHitCount);
    // 1 or 1.5 cells per world unit: either way 8192 * scale is a multiple
    // of the 64-cell id wrap, so the rig's travel wrap stays invisible.
    gScale = truchetMorph() >= 0.5 ? 1.5 : 1.0;
    gDepth = (0.14 + 0.07 * clamp(uBassSmooth, 0.0, 1.5) * e) / gScale;
    // Slope bound in world units: the groove is TR_W / gScale wide.
    float slope = gDepth * TRUCHET_PI * gScale / (2.0 * TR_W) + TR_TOP * gScale / (1.6 * TR_W);
    // Chaos bends the plan first; its stretch multiplies the slope.
    slope *= 1.0 + 0.25 * clamp(uWarp, 0.0, 2.0) + 0.21 * clamp(uTurbulence, 0.0, 2.0);
    float lip = 1.0 / sqrt(1.0 + slope * slope);

    vec3 ro = vec3(uCamPos.x, TR_EYE, uCamPos.z);
    // The rig's view, pitched down toward the floor.
    vec3 v = vec3(uv, uCamFocal);
    const float pitch = 0.42;
    v = vec3(v.x, v.y * cos(pitch) - v.z * sin(pitch), v.y * sin(pitch) + v.z * cos(pitch));
    vec3 rd = normalize(uCamBasis * v);

    float hue = 0.04 * sin(t * 0.02) + 0.1 * uKeyStrength * sin(TRUCHET_TAU * uKeyHue);
    vec3 fwd = normalize(vec3(uCamBasis[2].x, 0.0, uCamBasis[2].z) + vec3(0.0, 0.0, 1e-4));
    vec3 side = vec3(fwd.z, 0.0, -fwd.x);
    vec3 lightPos = ro + fwd * 3.2 + side * (1.8 * sin(t * 0.37)) + vec3(0.0, -0.55, 0.0);

    vec3 sky = mix(pal(hue + 0.6) * 0.05, pal(hue + 0.5) * 0.14, smoothstep(-0.3, 0.4, rd.y));
    vec3 col = sky;

    float hitT = -1.0;
    float used = 0.0;
    float tone = 0.0;
    if (rd.y < -0.002) {
        // Enter the slab at its top, give up past its bottom.
        float t0 = (ro.y - TR_TOP) / -rd.y;
        float t1 = min((ro.y + gDepth + 0.02) / -rd.y, TR_FAR);
        float tm = t0;
        for (int i = 0; i < TR_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (tm > t1) break;
            used = float(i);
            float d = map(ro + rd * tm);
            float eps = 0.0007 * tm + 0.0004;
            if (d < eps) {
                hitT = tm;
                break;
            }
            tm += max(d * lip, eps * 0.5);
        }
        // Below the deepest groove a ray must already be in the floor.
        if (hitT < 0.0 && tm >= t1 && t1 < TR_FAR) hitT = t1;
    }

    if (hitT > 0.0) {
        vec3 p = ro + rd * hitT;
        float h = floorHeight(p.xz);
        tone = gTone;
        vec3 n = normalAt(p, 0.0012 * hitT + 0.0008);
        vec3 toL = lightPos - p;
        float dl = length(toL);
        vec3 l = toL / dl;
        float atten = 1.0 / (1.0 + 0.12 * dl * dl);
        float dif = max(dot(n, l), 0.0);
        float spec = pow(max(dot(reflect(rd, n), l), 0.0), 24.0);
        // Groove floors sit in shadow: occlusion from how deep the point is,
        // and from the march's own step count.
        float cavity = clamp((h + gDepth) / (gDepth + TR_TOP), 0.0, 1.0);
        float ao = (0.35 + 0.65 * cavity) * clamp(1.0 - used / max(uSteps, 1.0) * 0.8, 0.4, 1.0);
        vec3 stone = mix(pal(hue + 0.58) * 0.35, pal(hue + 0.08) * 0.7, tone);
        vec3 lightC = mix(vec3(1.0, 0.93, 0.82), pal(hue + 0.15), 0.35);
        col = stone * (0.06 + 1.6 * dif * atten) * ao * lightC;
        col += lightC * spec * atten * 0.6 * ao;
        col += stone * sky * 0.4 * max(n.y, 0.0);
        col = mix(col, sky, 1.0 - exp(-hitT * 0.12));
    }
    // The light itself, a soft glow hanging over the floor.
    vec3 toLight = lightPos - ro;
    float along = max(dot(toLight, rd), 0.0);
    float miss = length(toLight - rd * along);
    if (hitT < 0.0 || along < hitT) col += pal(hue + 0.15) * 0.06 / (1.0 + miss * miss * 60.0);

    col *= 0.85 + 0.12 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
