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

// Glass Ice: a druse of ice crystals - seven hexagonal prisms with pyramidal
// tips, fanning up and out from a frosted mound - in a cold, blue-white room.
// Each crystal is a hexagon measured by folding (two absolutes and one dot,
// the hexagon's apothem), extruded along its own axis and capped where a
// plane tilted to the hexagon meets its axis, so the tip is a true six-sided
// point. The orientations are built once per pixel. The ray marches to the
// ice, refracts in at water ice's 1.31, crosses the crystal once - collecting
// a milky haze from the frozen veins inside - and refracts out into the room.
//
// Frost is what makes it ice and not glass: the surface normal at both the
// entry and the exit is roughened with 3D value noise before it refracts, so
// the room seen through a crystal is broken and smeared like light through
// frosted glass, and the specular highlight shatters into glitter. The noise
// is read twice per pixel, never inside a march.
//
// ---- audio ------------------------------------------------------------------
//
// Highs (sndShaped(SND_HIGH), with the air band sndSpectrum(0.93)) grow the frost: the
// normal roughening deepens and the glitter thickens on bright passages, and
// clears to glassy ice in the quiet. Bass (SND_BASS_PRESENCE) grows the crystals a
// little longer, eased. The key (uKeyHue, uKeyStrength) drifts the cold
// palette, but only slightly - ice stays cold. Brightness follows SND_LEVEL only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the veins' slow drift. Complexity (uMorph) grows up to
// two more crystals. Chaos (uWarp, uTurbulence) ripples the faces
// (glassSurfaceWarp), half-melted. Energy (uIntensity) scales the frost and the
// growth. Colour: the room and the ice's tint from pal(), pulled toward blue.
// Space is the rig (camAround, no spin).

#define IC_MAX 9
#define IC_CAM_R 4.9
#define IC_IOR 1.31
#define IC_BOUND 2.8

mat3 gRot[IC_MAX];
vec2 gDim[IC_MAX];
float gCount;
const vec3 IC_BASE = vec3(0.0, -1.3, 0.0);

/** A hexagonal prism of apothem r along +y from y = -0.3, capped by a six-sided point at height L. */
float crystal(vec3 q, float r, float L) {
    vec2 a = abs(q.xz);
    float m = max(a.x * 0.8660254 + a.y * 0.5, a.y);
    float side = m - r;
    float tip = (q.y - L + m * 1.6) * 0.5300066;
    return max(max(side, tip), -q.y - 0.3);
}

float map(vec3 p) {
    vec3 b = p - IC_BASE;
    float d = (length(b * vec3(0.8, 2.2, 0.8)) - 0.75) / 2.2;
    for (int i = 0; i < IC_MAX; i++) {
        if (float(i) >= gCount) break;
        d = min(d, crystal(gRot[i] * b, gDim[i].x, gDim[i].y));
    }
    return (d + glassSurfaceWarp(p)) / glassSurfaceLip();
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

vec3 frost(vec3 n, vec3 p, float amount) {
    vec3 g = vec3(glassNoise3(p * 14.0), glassNoise3(p * 14.0 + 17.3), glassNoise3(p * 14.0 + 31.7));
    return normalize(n + amount * g);
}

void main() {
    vec2 uv = view();
    float t = glassTime();
    float energy = glassEnergy();
    float swell = clamp(SND_LEVEL, 0.0, 1.5);
    float highs = clamp(0.6 * sndShaped(SND_HIGH) + 0.4 * sndSpectrum(0.93), 0.0, 1.3);
    float frostAmt = 0.12 + 0.38 * highs * energy;
    float grow = 1.0 + 0.16 * clamp(SND_BASS_PRESENCE, 0.0, 1.2) * energy;
    float hue = 0.55 + 0.05 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    vec3 cold = vec3(0.72, 0.9, 1.12);

    gCount = 7.0 + floor(glassComplexity() * 2.0 + 0.5);
    for (int i = 0; i < IC_MAX; i++) {
        float fi = float(i);
        float h1 = glassHash1(fi * 1.7 + 0.3);
        float h2 = glassHash1(fi * 2.9 + 4.1);
        float h3 = glassHash1(fi * 4.3 + 7.7);
        float tilt = fi < 0.5 ? 0.0 : 0.3 + 0.55 * h1;
        float az = fi * 2.39996 + 0.4;
        gRot[i] = transpose(glassEuler(vec3(tilt, az, 0.0)));
        gDim[i] = vec2(0.13 + 0.09 * h2, (1.0 + 1.1 * h3) * (1.0 - 0.25 * tilt) * grow);
    }

    vec3 ro;
    vec3 rd;
    camAround(uv, IC_CAM_R, 0.0, ro, rd);
    vec3 col = glassBackdrop(rd, hue) * cold;

    float hitT = -1.0;
    float used = 0.0;
    vec3 bc = vec3(0.0, -0.15, 0.0);
    vec2 tb = glassSphere(ro, rd, bc, IC_BOUND);
    if (tb.y > 0.0) {
        float tm = max(tb.x, 0.0);
        for (int i = 0; i < GLASS_MAX_STEPS; i++) {
            if (float(i) >= uSteps) break;
            if (tm > tb.y) break;
            used = float(i);
            float d = map(ro + rd * tm);
            float eps = glassEps(tm);
            if (d < eps) {
                hitT = tm;
                break;
            }
            tm += max(d * 0.9, eps * 0.5);
        }
    }

    if (hitT > 0.0) {
        float eps = glassEps(hitT);
        vec3 p = ro + rd * hitT;
        vec3 ng = normalAt(p, eps);
        vec3 n = frost(ng, p, frostAmt);
        float cosI = max(dot(n, -rd), 0.0);
        float fr = glassFresnel(cosI, IC_IOR);
        vec3 r = reflect(rd, n);
        vec3 refl = glassBackdrop(r, hue) * cold;

        vec3 d1 = glassRefract(rd, n, 1.0 / IC_IOR);
        vec3 q = p - ng * eps * 3.0;
        float ti = 0.0;
        float milk = 0.0;
        for (int i = 0; i < GLASS_MAX_INNER; i++) {
            if (float(i) >= glassInnerSteps()) break;
            vec3 s = q + d1 * ti;
            float d = -map(s);
            if (d < eps) break;
            float adv = max(d, eps);
            float vein = sin(s.x * 6.1 + t * 0.05) * sin(s.y * 4.3 - 1.3) * sin(s.z * 5.7 + 0.7);
            milk += exp(-abs(vein) * 9.0) * min(adv, 0.25);
            ti += adv;
        }
        vec3 pe = q + d1 * ti;
        vec3 n2 = frost(normalAt(pe, eps), pe, frostAmt * 0.6);
        vec3 ex = glassRefract(d1, -n2, IC_IOR);
        vec3 tint = mix(vec3(0.75, 0.92, 1.0), pal(hue), 0.3);
        vec3 refr = glassBackdrop(ex, hue) * cold * glassBeer(tint, ti, 0.9);
        refr += mix(vec3(0.85, 0.95, 1.0), pal(hue + 0.05), 0.2) * (1.0 - exp(-milk * 1.6)) * 0.22;
        col = mix(refr, refl, fr) * glassStepAO(used * 0.5);
        // Glitter: the frosted highlight of the key panel, broken into points.
        col += vec3(0.95, 0.98, 1.0) * pow(max(dot(r, normalize(vec3(-0.35, 1.0, -0.45))), 0.0), 120.0) * (0.4 + 0.8 * highs);
    }

    col *= 0.85 + 0.18 * swell;
    col *= 0.78 + 0.22 * (1.0 - smoothstep(0.3, 2.2, length(uv)));
    fragColor = vec4(grade(col), 1.0);
}
