// A simulation presented in 3D, the second half: the part of lib_field3d that calls the present's
// fieldColour and fieldGlow, which the present defines between the two includes. The distance to the
// surface, its normal, the march and the shading.
//
// Why a second file: no shader declares a function ahead of its body (style_reads_test:
// noStyleDeclaresAFunctionAhead), so the march that calls the present's colours comes after them, as
// lib_flame_march, lib_opal_march and lib_hollow_march do for their families.
//
// INCLUDE ORDER: after lib_field3d and after the present's fieldColour and fieldGlow.

/**
 * The distance from p to the surface (a bound: steps take gFieldStride of it), and the field where p
 * meets it. `fp` is the ray's footprint in field texels.
 */
float fieldDist(vec3 p, float fp, out vec4 f) {
    vec3 q = fieldBend(p);
    vec4 at = fieldPlace(q);
    float h = fieldHeight(fieldWarp(at.xy), fp, f);
    float lift = (gFieldRelief * h + gFieldRipple * sin(at.z - uTime * FIELD_RATE(2100))) * at.w;
    if (gFieldSurface == FIELD_TERRAIN) return q.y - lift;
    if (gFieldSurface == FIELD_TORUS) {
        vec2 m = vec2(length(q.xz) - uFieldGeom.z, q.y);
        return length(m) - (uFieldGeom.w + lift);
    }
    return length(q) - (uFieldGeom.z + lift);
}

/** The surface's normal at p, unit length; straight back along the ray where the field is flat. */
vec3 fieldNormal(vec3 p, vec3 rd, float eps, float fp) {
    vec2 e = vec2(1.0, -1.0) * eps;
    vec4 f;
    vec3 g = e.xyy * fieldDist(p + e.xyy, fp, f) + e.yyx * fieldDist(p + e.yyx, fp, f) +
        e.yxy * fieldDist(p + e.yxy, fp, f) + e.xxx * fieldDist(p + e.xxx, fp, f);
    float l = length(g);
    return l > 1.0e-9 ? g / l : -rd;
}

/** How far along rd the surface is, and whether the ray met it; f is the field there. */
float fieldMarch(vec3 ro, vec3 rd, out vec4 f, out bool hit) {
    hit = false;
    f = vec4(0.0);
    float t0 = 0.0;
    float t1 = gFieldFar;
    if (gFieldSurface == FIELD_TERRAIN) {
        // Nothing rises above the top: a ray that does not come down through it is sky, and the march
        // starts where it does.
        float top = uFieldShape.w;
        if (ro.y > top) {
            if (rd.y > -1.0e-4) return t1;
            t0 = (ro.y - top) / -rd.y;
        }
    } else {
        // The bounding sphere: a ray that misses it is the void beside the object.
        float bound = uFieldGeom.z + uFieldGeom.w + uFieldShape.w;
        float b = dot(ro, rd);
        float disc = b * b - (dot(ro, ro) - bound * bound);
        if (disc < 0.0) return t1;
        float sq = sqrt(disc);
        t0 = max(-b - sq, 0.0);
        t1 = min(-b + sq, t1);
    }
    float n = FIELD_STEPS;
    float t = t0 + 0.02 * fieldDither();
    for (int i = 0; i < FIELD_MAX_STEPS; i++) {
        if (float(i) >= n || t > t1) break;
        float d = fieldDist(ro + rd * t, t * gFieldLod, f);
        float eps = 0.0012 * (1.0 + t);
        // Under the surface counts as met: a step that overshot a thin crest lands just inside it.
        if (d < eps) {
            hit = true;
            break;
        }
        t += max(d * gFieldStride, 0.25 * eps);
    }
    return min(t, gFieldFar);
}

/** The surface at p lit in the palette's colours, young matter in one material and old in the other. */
vec3 fieldShade(vec3 p, vec3 rd, float t, vec4 f) {
    float fp = t * gFieldLod;
    vec3 n = fieldNormal(p, rd, max(0.5 * gFieldTexel, 0.0015 * (1.0 + t)), fp);
    float soma = smoothstep(0.5, 0.95, f.b) * smoothstep(0.3, 0.7, f.r);
    vec4 ma = mix(uFieldLook[0], uFieldLook[2], soma);
    vec4 mb = mix(uFieldLook[1], uFieldLook[3], soma);
    vec3 albedo = fieldColour(f);

    float la = uTime * FIELD_RATE(60) + 0.8;
    vec3 l = normalize(vec3(0.55 * cos(la), 0.7, 0.55 * sin(la)));
    vec3 v = -rd;
    float ndl = dot(n, l);
    float ndv = clamp(dot(n, v), 0.0, 1.0);
    float diff = clamp((ndl + 0.25) / 1.25, 0.0, 1.0);
    vec3 hv = l + v;
    hv = dot(hv, hv) > 1.0e-8 ? normalize(hv) : n;
    float spec = pow(clamp(dot(n, hv), 0.0, 1.0), max(ma.z, 1.0));
    float rim = 1.0 - ndv;
    float fres = rim * rim * rim;
    float k = 6.0 * gFieldTexel;
    vec4 fo;
    float occ = clamp(fieldDist(p + n * k, fp, fo) / k, 0.0, 1.0);

    vec3 col = albedo * (0.05 + ma.x * diff * (0.35 + 0.65 * occ));
    col += pal(gFieldTone + 0.2 + 0.3 * f.g) * ma.y * spec;
    col += pal(gFieldTone + 0.45 + 0.3 * f.g) * ma.w * fres * (0.4 + 0.6 * occ);
    col += fieldSky(reflect(rd, n)) * mb.x * 6.0 * (0.25 + 0.75 * fres);
    col += albedo * mb.y * f.r * f.r;
    col += pal(gFieldTone + 0.25 + 0.6 * rim) * mb.w * rim * rim;
    // Contour lines on the height, every twelfth, as wide as TEXTURE says; gone into the distance.
    float ch = f.r * 12.0;
    float line = 1.0 - smoothstep(0.0, gFieldLines, min(fract(ch), 1.0 - fract(ch)));
    col *= 1.0 - mb.z * 0.5 * line * exp(-0.08 * t);
    // New matter glows (BUILD).
    col += fieldGlow(f) * f.a * gFieldGlow;
    return col;
}

/** The light along one ray: marched to the surface, shaded, and faded into the void with distance on the terrain. */
vec3 fieldTrace(vec3 ro, vec3 rd) {
    vec3 sky = fieldSky(rd);
    vec4 f;
    bool hit;
    float t = fieldMarch(ro, rd, f, hit);
    if (!hit) return sky;
    vec3 col = fieldShade(ro + rd * t, rd, t, f);
    float air = gFieldSurface == FIELD_TERRAIN ? exp(-2.4 * t / max(gFieldFar, 1.0)) : 1.0;
    return mix(sky, col, air);
}
