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

// Glass Gem: one round-cut brilliant turning slowly on its point, tipped
// toward the viewer so the table and crown face up. The stone is a convex
// polytope - the intersection of half-spaces: a table, eight bezel and eight
// upper-girdle facets on the crown, eight star facets round the table, a
// sixteen-sided girdle, and eight main and eight lower-girdle facets on the
// pavilion - so its distance field is the largest of the plane distances,
// exact inside, a safe underestimate outside. The point is folded into one
// 45-degree wedge by mirrors first (no atan, no seam), so only the facets of
// that wedge are measured, and the wedge and facet the maximum came from
// name the facet the ray landed on.
//
// The ray marches to the stone, refracts in at a dense IOR, marches the
// interior once and leaves through the far facet three times - red, green and
// blue at three IORs (glassExitDispersed) - so every facet is split into
// spectral fringes. At an IOR this high many inner faces send the light back
// by total internal reflection; those rays take the mirror direction into the
// room and stop, which is what makes the pavilion read as a pattern of bright
// and dark facets rather than a window.
//
// ---- audio ------------------------------------------------------------------
//
// Hats (uHits.z) make facets sparkle: each hat lights a handful of facets,
// chosen anew for every onset (uHitCount), in spectral colours, and they fade
// as the hat envelope falls. Only those facets light; the frame does not.
// The key (uKeyHue, uKeyStrength) tints the room and the stone. Brightness
// follows uSwell only.
//
// ---- macros -----------------------------------------------------------------
//
// Speed (uSpeed) scales the stone's turn. Complexity (uMorph) widens the
// dispersion. Chaos (uWarp, uTurbulence) ripples the facets
// (glassSurfaceWarp). Energy (uIntensity) scales how many facets a hat lights.
// Colour: the room, a light body tint and the sparkle from pal(). Space is the
// rig (camAround, no spin).

#define GM_CAM_R 3.4
#define GM_BOUND 1.12
#define GM_IOR 2.0

mat3 gOrient;
float gFacet;

/** Raise d to plane value e, remembering which facet it was. */
void facet(inout float d, inout float id, float e, float k) {
    if (e > d) {
        d = e;
        id = k;
    }
}

float gemDE(vec3 p) {
    float sector = step(0.0, p.x) + 2.0 * step(0.0, p.z);
    vec3 q = vec3(abs(p.x), p.y, abs(p.z));
    float swapped = step(q.x, q.z);
    if (q.z > q.x) q.xz = q.zx;
    sector += 4.0 * swapped;
    const vec2 A0 = vec2(1.0, 0.0);
    const vec2 A1 = vec2(0.92388, 0.38268);
    const vec2 A2 = vec2(0.70711, 0.70711);
    float r0 = dot(q.xz, A0);
    float r1 = dot(q.xz, A1);
    float r2 = dot(q.xz, A2);
    float d = q.y - 0.33;
    float id = 0.0;
    facet(d, id, r0 * 0.56641 + q.y * 0.82413 - 0.58289, 1.0);
    facet(d, id, r2 * 0.56641 + q.y * 0.82413 - 0.58289, 2.0);
    facet(d, id, r1 * 0.66913 + q.y * 0.74314 - 0.68399, 3.0);
    facet(d, id, r1 * 0.37461 + q.y * 0.92718 - 0.5085, 4.0);
    facet(d, id, r0 - 1.0, 5.0);
    facet(d, id, r1 - 1.0, 6.0);
    facet(d, id, r2 - 1.0, 7.0);
    facet(d, id, r0 * 0.65276 - q.y * 0.75756 - 0.66791, 8.0);
    facet(d, id, r2 * 0.65276 - q.y * 0.75756 - 0.66791, 9.0);
    facet(d, id, r1 * 0.682 - q.y * 0.73135 - 0.69663, 10.0);
    gFacet = id + 11.0 * sector;
    return d;
}

float map(vec3 p) {
    vec3 q = gOrient * p;
    return (gemDE(q) + glassSurfaceWarp(q)) / glassSurfaceLip();
}

vec3 normalAt(vec3 p, float e) {
    vec2 k = vec2(1.0, -1.0);
    return normalize(k.xyy * map(p + k.xyy * e) + k.yyx * map(p + k.yyx * e)
                   + k.yxy * map(p + k.yxy * e) + k.xxx * map(p + k.xxx * e));
}

void main() {
    vec2 uv = view();
    float t = glassTime();
    float swell = clamp(uSwell, 0.0, 1.5);
    float hat = clamp(uHits.z, 0.0, 1.0);
    float hue = 0.04 * sin(t * 0.02) + 0.12 * uKeyStrength * sin(GLASS_TAU * uKeyHue);
    float spread = 0.035 + 0.03 * glassComplexity();
    gOrient = glassEuler(vec3(0.0, t * 0.12, 0.0)) * glassEuler(vec3(0.42, 0.0, 0.0));

    vec3 ro;
    vec3 rd;
    camAround(uv, GM_CAM_R, 0.0, ro, rd);
    vec3 col = glassBackdrop(rd, hue);

    float hitT = -1.0;
    float used = 0.0;
    float fid = 0.0;
    vec2 tb = glassSphere(ro, rd, vec3(0.0), GM_BOUND);
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
                fid = gFacet;
                break;
            }
            tm += max(d, eps * 0.5);
        }
    }

    if (hitT > 0.0) {
        float eps = glassEps(hitT);
        vec3 p = ro + rd * hitT;
        vec3 n = normalAt(p, eps);
        float cosI = max(dot(n, -rd), 0.0);
        float fr = glassFresnel(cosI, GM_IOR);
        vec3 r = reflect(rd, n);
        vec3 refl = glassBackdrop(r, hue);

        vec3 d1 = glassRefract(rd, n, 1.0 / GM_IOR);
        vec3 q = p - n * eps * 3.0;
        float ti = 0.0;
        for (int i = 0; i < GLASS_MAX_INNER; i++) {
            if (float(i) >= glassInnerSteps()) break;
            float d = -map(q + d1 * ti);
            if (d < eps) break;
            ti += max(d, eps);
        }
        vec3 pe = q + d1 * ti;
        vec3 n2 = normalAt(pe, eps);
        vec3 tint = mix(vec3(1.0), pal(hue + 0.55), 0.18);
        vec3 refr = glassExitDispersed(d1, n2, GM_IOR, spread, hue) * glassBeer(tint, ti, 0.8) * 1.15;
        col = mix(refr, refl, fr);
        col += vec3(1.0, 0.96, 0.9) * pow(max(dot(r, normalize(vec3(-0.35, 1.0, -0.45))), 0.0), 60.0) * 0.6;

        // Hat sparkle: a few facets, chosen per onset, flare in spectral colour.
        float pick = glassHash(vec2(fid * 1.37, floor(uHitCount) * 0.71 + 3.0));
        float lit = smoothstep(0.86 - 0.12 * (glassEnergy() - 1.0), 0.95, pick) * hat;
        col += mix(glassSpectrum(fract(pick * 9.7)), pal(hue + pick), 0.3) * lit * 0.9;
    }

    col *= 0.85 + 0.18 * swell;
    col *= 0.78 + 0.22 * smoothstep(2.2, 0.3, length(uv));
    fragColor = vec4(grade(col), 1.0);
}
