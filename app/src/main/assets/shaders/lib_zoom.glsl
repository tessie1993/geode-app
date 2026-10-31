// Endless zoom: the picture drawn as two layers an octave apart, both zooming
// in, the nearer fading out as the farther fades in (core/viz/EndlessZoom.hpp
// has the law and the reasons). One magnification could not do it: a sawtooth
// popped at every wrap, and the triangle that replaced it zoomed back out.
//
// Include it after `out vec4 fragColor;` and before the style's main().
// lib_scene_uniforms does that for every style that uses it. It renames the
// style's main() to geodeStyleMain() and calls it once per layer; view() reads
// the layer's magnification from gZoomMag and marks gZoomRead, so a style whose
// picture never goes through view() still draws once.

/** x: the first layer's magnification, y: the second's, z: the second's weight (0 draws one layer). */
uniform vec3 uZoomLayers;
float gZoomMag = 1.0;
bool gZoomRead = false;

void geodeStyleMain();

void main() {
    // A scene that never uploads the layers reads zeros: draw it once, at 1x.
    gZoomMag = uZoomLayers.x > 0.0 ? uZoomLayers.x : 1.0;
    geodeStyleMain();
    if (uZoomLayers.z <= 0.0 || !gZoomRead) return;
    vec4 first = fragColor;
    gZoomMag = uZoomLayers.y;
    geodeStyleMain();
    fragColor = mix(first, fragColor, uZoomLayers.z);
}

#define main geodeStyleMain
