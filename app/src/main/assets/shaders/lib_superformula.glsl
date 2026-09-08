// The Gielis superformula as a shape a style can draw, mask with, or march.
//
//     r(theta) = ( |cos(m*theta/4) / a|^n2 + |sin(m*theta/4) / b|^n3 )^(-1/n1)
//
// n1 = n2 = n3 = 2 is a circle for any m. Raise n2 and n3 and it bulges into
// a rounded m-gon; drop n1 below 1 and it pinches into an m-pointed star; a,
// b weight the two terms. One family of six real numbers, every member of it
// a closed shape, and every path between two members a morph rather than a
// cut. That is why the music drives THIS rather than a zoom.
//
// The driving is done on the CPU, in viz/SuperShape, which also samples the
// two curves into uShapeTex (see "the shape tap" in lib_scene_uniforms).
// Everything here reads those rows through ashape() / ashapeElev(), so a
// style pays two texel fetches per radius, not three pow() calls - the
// difference between a silhouette that can sit inside a 128-step march and
// one that cannot.
//
// Include AFTER lib_scene_uniforms and lib_scene_motion. Nothing here reads
// the palette, so the order against lib_palette does not matter.
//
// ---- what the music moves (the mapping lives in SuperShape.cpp) -------------
//
//   lobe count m     snares walk it, a drop resets it low, a section re-rolls it
//   sharpness n1     tonal sits round, percussive sits spiky; a kick pinches it
//   lobe form n2 n3  bass fattens the cosine lobes, treble and hats the sine ones
//   weights a b      stereo pan (silhouette), stereo width (elevation)
//   uShapeSpin       energy sets the rate, a section the sense, a snare a turn

// ---- the raw parameters, for a style that wants the analytic form ------------
//
// The rows in uShapeTex are a CROSSFADE between the count on screen and the
// count these describe (uShapeAWeight.z is how far along it is), so the
// analytic curve and the sampled row agree only once a count change has
// settled. Read the rows for what is on screen; read these for the numbers.

/** Azimuth curve: (m, n1, n2, n3). */
uniform vec4 uShapeA;
/** Azimuth curve: (a, b, crossfade 0..1, the mean radius the row was normalised by). */
uniform vec4 uShapeAWeight;
/** Elevation curve: (m, n1, n2, n3). */
uniform vec4 uShapeB;
/** Elevation curve: (a, b, crossfade 0..1, mean radius). */
uniform vec4 uShapeBWeight;
/**
 * The bounds a march divides by. .x for the 2D silhouette, .y for the 3D
 * spherical product. Both clamped at 8 - see SuperShape::lipschitz for what
 * is and is not covered past the clamp.
 */
uniform vec2 uShapeLip;

/** The formula itself. `theta` in radians; a, b positive; n1 non-zero. */
float superformula(float theta, float m, float n1, float n2, float n3, float a, float b) {
    float arg = m * theta * 0.25;
    float c = abs(cos(arg) / a);
    float s = abs(sin(arg) / b);
    float sum = pow(c, n2) + pow(s, n3);
    return pow(max(sum, 1e-6), -1.0 / n1);
}

/** The azimuth curve's analytic radius, normalised like its row (mean 1). */
float shapeAnalytic(float theta) {
    return superformula(theta, uShapeA.x, uShapeA.y, uShapeA.z, uShapeA.w, uShapeAWeight.x, uShapeAWeight.y) /
           max(uShapeAWeight.w, 1e-6);
}

// ---- the silhouette, in 2D ---------------------------------------------------

/** The silhouette radius at `theta`, in the shape's own frame (spin applied), mean 1. */
float shapeRadius(float theta) {
    return ashape(theta - uShapeSpin);
}

/** How far outside (+) or inside (-) the silhouette of size `radius` the point `p` is, radially. */
float shapeGap(vec2 p, float radius) {
    return length(p) - radius * shapeRadius(atan(p.y, p.x));
}

/**
 * A marchable signed distance to the silhouette: never more than the true
 * distance outside (divided by the bound), negative inside.
 */
float sdShape(vec2 p, float radius) {
    return shapeGap(p, radius) / uShapeLip.x;
}

/** 1 inside the silhouette of size `radius`, 0 outside, feathered over `soft`. */
float shapeMask(vec2 p, float radius, float soft) {
    return 1.0 - smoothstep(-soft, soft, shapeGap(p, radius));
}

// ---- the solid, in 3D --------------------------------------------------------
//
// The spherical product: the azimuth curve around z, the elevation curve from
// pole to pole, multiplied. rho(theta, phi) = r1(theta) * r2(phi). Every
// direction has one radius, so the surface is star-shaped about the origin
// and |p| - rho(p/|p|) is a field whose zero set is the surface. Both curves
// meet at the poles, where all azimuths collapse to one point - the pinch the
// classic supershape renders show - and the bound stops holding within about
// twenty degrees of them.

/** (azimuth in the shape's frame, latitude) of the unit direction `d`, z up. */
vec2 shapeAngles(vec3 d) {
    return vec2(atan(d.y, d.x) - uShapeSpin, asin(clamp(d.z, -1.0, 1.0)));
}

/** The solid's radius in the unit direction `d`, mean about 1. */
float shapeRadius3(vec3 d) {
    vec2 ang = shapeAngles(d);
    return ashape(ang.x) * ashapeElev(ang.y);
}

/**
 * A marchable signed distance to the solid of size `radius`: never more than
 * the true distance outside (the field divided by the 3D bound), negative
 * inside. Step by it directly; there is no further factor to divide out.
 */
float sdSupershape(vec3 p, float radius) {
    float r = length(p);
    // Every row is clamped at 0.15, so the origin is inside by at least that.
    if (r < 1e-5) return -0.15 * radius / uShapeLip.y;
    return (r - radius * shapeRadius3(p / r)) / uShapeLip.y;
}
