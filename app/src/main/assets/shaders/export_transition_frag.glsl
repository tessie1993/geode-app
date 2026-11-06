#version 300 es
// A Studio render's GL Transition between two clips (core/viz/frame/ExportPasses, TransitionPass): from
// the outgoing clip's last frame (uFrom) to the incoming clip's picture (uTo). The harness is
// export/GlTransitionEffect.kt's, verbatim; TransitionCatalog::spliceInto defines MV_TRANSITION and puts
// the transition's own GLSL (gl_transitions.json) at the marker, as it does for composite_frag.glsl. Without
// it (tools/check_shaders.py compiles the file as it is) a plain cross-fade stands in.
precision highp float;
in vec2 vUv;
out vec4 fragColor;
uniform sampler2D uFrom;
uniform sampler2D uTo;
uniform float progress;
uniform float ratio;
vec4 getFromColor(vec2 uv) { return texture(uFrom, clamp(uv, 0.0, 1.0)); }
vec4 getToColor(vec2 uv) { return texture(uTo, clamp(uv, 0.0, 1.0)); }
#ifdef MV_TRANSITION
// __GL_TRANSITION_SOURCE__
#else
vec4 transition(vec2 uv) { return mix(getFromColor(uv), getToColor(uv), progress); }
#endif
void main() { fragColor = transition(vUv); }
