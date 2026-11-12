// The palette every fullscreen scene colours itself with.
//
// This used to be a `pal()` copied byte-for-byte into all twenty scene
// shaders. It is one file now because it grew a second kind of palette that
// none of them could have carried alone.
//
// TWO FAMILIES, ONE FUNCTION
//
// The built-in palettes are procedural: a cosine ramp around the hue wheel
// from a base and a span (SceneParams.PALETTES). They are cheap, they wrap
// seamlessly - hue is circular by construction - and every one of them is a
// user control away from any other.
//
// What they cannot be is perceptually uniform. A cosine hue ramp swings
// wildly in lightness (yellow is far lighter than blue at the same
// saturation), so a smooth field painted with one grows bright and dark bands
// that are in the PALETTE, not in the data. On a spectrum or a wave field
// that reads as structure the music never played.
//
// uPalLutMix > 0 selects a scientific colour map instead: measured ramps with
// even perceptual steps and no lightness artefacts, and specifically the
// CYCLIC ones, whose two ends join - so they can carry a circular quantity
// (phase, angle, pitch class) with no seam. Their wrap gap is smaller than
// one ordinary step between neighbouring entries, which is what makes them
// safe to use where a linear ramp would show a hard edge at the wrap.
// GLSL ES 3.00 defaults fragment sampler2D to LOWP; declare highp before
// the LUT sampler so an including shader that forgot its own precision
// statement still reads full-precision texels on Mali.
precision highp sampler2D;
uniform sampler2D uPalLut;
/** 0 = the procedural palette, 1 = the colour map on uPalLutRow. */
uniform float uPalLutMix;
/** Which ramp of the atlas, as a texture row centre. */
uniform float uPalLutRow;

// Your colours (Customize › Look): up to five stops the user picked, packed by ShaderScene into
// linear 0..1 RGB. uPalStopCount 0 keeps the cosine ramp; one or more replace the first slot's
// ramp with the stops spaced evenly round a loop, each blending into the next and the last back into
// the first, so a wrapped t shows no seam (viz/Colour.hpp's ramp() is the same maths for the UI).
uniform vec3 uPalStops[5];
uniform float uPalStopCount;

vec3 palStops(float t) {
    int n = int(uPalStopCount + 0.5);
    float x = fract(t * uHueRange + uColorShift) * float(n);
    int i = min(int(x), n - 1);
    return mix(uPalStops[i], uPalStops[(i + 1) % n], x - float(i));
}

// The look's own palette when no stack or colour map draws: your colours, else the cosine ramp. The old
// second palette blended over it here is retired; the palette stack draws more than one palette.
vec3 palProcedural(float t) {
    return uPalStopCount > 0.5 ? palStops(t)
        : 0.5 + 0.5 * cos(6.2831 * (uPalBase + uColorShift + t * uPalRange * uHueRange + vec3(0.0, 0.33, 0.67)));
}

vec3 pal(float t) {
    vec3 procedural = palProcedural(t);
    if (uPalLutMix <= 0.001) return procedural;
    // Hue shift and the colour cycle stay meaningful on a colour map: they
    // rotate the position along the ramp rather than the hue wheel, which for
    // a CYCLIC map is the same gesture and lands back where it started.
    //
    // uPalRange is deliberately NOT in this expression, unlike in
    // palProcedural above. It is the built-in palette's hue SPAN - table data
    // (SceneParams.PALETTES), not a control - and a colour map has no hue span
    // to narrow: its entries are measured colours, not points on a wheel.
    // Multiplying by it made the map a hostage of whichever built-in happened
    // to be selected, so "Mono" (span 0.02) or "Cyan"/"Cherry" (0.08) swept 2%
    // to 8% of a 256-entry ramp and painted the whole frame one flat tone -
    // the colour map read as broken while the fault was two chip rows above
    // it. uHueRange stays: that one IS the user's "Hue range" control.
    float u = fract(t * uHueRange + uColorShift);
    vec3 mapped = texture(uPalLut, vec2(u, uPalLutRow)).rgb;
    return mix(procedural, mapped, clamp(uPalLutMix, 0.0, 1.0));
}
