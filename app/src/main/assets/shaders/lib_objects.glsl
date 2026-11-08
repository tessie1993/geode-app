// Scene objects' shapes (core/scene/object, #515): the six SDF primitives an object can be, its
// transform, and the Burst and Collapse looks. Read by scene_objects_frag (the overlay pass,
// core/viz/frame/ObjectOverlayPass); kept apart so a family world or a later kind can place the same
// shapes.
//
// The primitives are the textbook exact distance functions (Hart 1996, "Sphere tracing"; the standard
// catalogue of closed forms), each centred on the origin in the object's own frame, with what
// shape.size means for each matching core/scene/object/Object.hpp's ShapeType:
//   0 Sphere      x = radius
//   1 Box         x, y, z = half extents
//   2 Torus       x = ring radius, y = tube radius (ring in the XZ plane)
//   3 Capsule     x = radius, y = half the segment (along Y)
//   4 Cylinder    x = radius, y = half height (along Y)
//   5 Octahedron  x = centre-to-vertex (the bound form: |x|+|y|+|z| scaled, never over the distance)
// Written for Geode; no code was taken from any published shader.

/** Rotates v by the unit quaternion q (x, y, z, w): v + 2 q.xyz × (q.xyz × v + w v). */
vec3 objRotate(vec4 q, vec3 v) {
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

/** The inverse rotation (q's conjugate). */
vec3 objUnrotate(vec4 q, vec3 v) { return objRotate(vec4(-q.xyz, q.w), v); }

float objSphere(vec3 p, float r) { return length(p) - r; }

float objBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float objTorus(vec3 p, float ring, float tube) { return length(vec2(length(p.xz) - ring, p.y)) - tube; }

float objCapsule(vec3 p, float r, float h) {
    p.y -= clamp(p.y, -h, h);
    return length(p) - r;
}

float objCylinder(vec3 p, float r, float h) {
    vec2 d = abs(vec2(length(p.xz), p.y)) - vec2(r, h);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

float objOctahedron(vec3 p, float s) {
    p = abs(p);
    return (p.x + p.y + p.z - s) * 0.57735027;
}

/** The shape `type` (0..5) of half extents `size`, rounded by `rnd`, at p in its own unit frame. */
float objShape(float type, vec3 p, vec3 size, float rnd) {
    int t = int(type + 0.5);
    float d;
    if (t == 1) {
        d = objBox(p, max(size - vec3(rnd), vec3(0.0)));
    } else if (t == 2) {
        d = objTorus(p, size.x, max(size.y - rnd, 0.0));
    } else if (t == 3) {
        d = objCapsule(p, max(size.x - rnd, 0.0), size.y);
    } else if (t == 4) {
        d = objCylinder(p, max(size.x - rnd, 0.0), max(size.y - rnd, 0.0));
    } else if (t == 5) {
        d = objOctahedron(p, max(size.x - rnd, 0.0));
    } else {
        d = objSphere(p, max(size.x - rnd, 0.0));
    }
    return d - rnd;
}

/** A radius that bounds the shape at unit scale (for the pass's ray-sphere cull). */
float objBound(float type, vec3 size, float rnd) {
    int t = int(type + 0.5);
    if (t == 1) return length(size) + rnd;
    if (t == 2) return size.x + size.y + rnd;
    if (t == 3) return size.x + size.y + rnd;
    if (t == 4) return length(size.xy) + rnd;
    return size.x + rnd;
}

/**
 * Burst (0..1): the shape shatters along a lattice of crack planes in its own frame, the gaps widening
 * as it is thrown out (the drawn scale already grows by life.burstSpread on the CPU). Collapse (0..1):
 * it folds in on itself, a twist about Y and a pinch toward the centre while the CPU shrinks it.
 * Returns the distance at p, in the object's unit frame.
 */
float objLife(float type, vec3 p, vec3 size, float rnd, float burst, float collapse) {
    if (collapse > 0.001) {
        float a = collapse * 3.0 * p.y;
        float c = cos(a);
        float s = sin(a);
        p.xz = mat2(c, -s, s, c) * p.xz;
        p *= 1.0 + 0.6 * collapse * (1.0 - smoothstep(0.0, 0.5, length(p)));
    }
    float d = objShape(type, p, size, rnd);
    if (burst > 0.001) {
        float k = 6.0;
        vec3 c = abs(fract(p * k) - 0.5);
        float plane = (0.5 - max(c.x, max(c.y, c.z))) / k;  // distance to the nearest crack plane
        d = max(d, burst * 0.06 - plane);
    }
    return d;
}
