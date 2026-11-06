#version 300 es
// The non-Pro watermark's full-frame quad (core/viz/frame/ExportPasses, WatermarkPass), as the Kotlin
// overlay drew it before #442: texture coordinates count tiles across the frame (frame pixels over tile
// pixels, uTiles) and run top-down, because the tile is a top-down Android bitmap and GL textures are
// bottom-up. A fullscreen triangle (fade_vert's) instead of the old strip: the coordinates are the same
// affine function of the pixel, so every pixel samples the same texel.
uniform vec2 uTiles;
out vec2 vTexCoord;

void main() {
    vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vTexCoord = vec2(pos.x, 1.0 - pos.y) * uTiles;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
