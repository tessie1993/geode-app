#version 300 es
// One damped-Jacobi pass of the pressure solve, on any level of the multigrid
// V-cycle (FluidSim::solvePressure; the plan is FluidMath planPressureSolve).
// Solves lap p = rhs with the 5-point Laplacian at this level's cell size h,
// Neumann walls (an outside tap reads the edge cell, as the old Jacobi pass's
// clamped sampling did): p += omega * ((L + R + B + T - h^2 rhs) / 4 - p).
//
// The weight is 4/5, not 1. Undamped Jacobi leaves the checkerboard mode
// untouched, which is harmless as a whole solver and fatal as a smoother: the
// coarse grids cannot see that mode, so it would survive every cycle. Damped,
// a pass removes high frequencies fast and the coarse grids take the rest.
//
// texelFetch, not texture: every level has its own size, so the pass reads by
// integer cell and the base vertex shader's uv taps are not used.
precision highp float;
precision highp sampler2D;
uniform sampler2D uP;      // pressure (fine) or correction (coarse)
uniform sampler2D uRhs;    // divergence (fine) or restricted residual (coarse)
uniform float uH2;         // this level's cell size squared
uniform float uOmega;
out vec4 fragColor;

float p(ivec2 c, ivec2 hi) { return texelFetch(uP, clamp(c, ivec2(0), hi), 0).x; }

void main() {
    ivec2 c = ivec2(gl_FragCoord.xy);
    ivec2 hi = textureSize(uP, 0) - 1;
    float centre = p(c, hi);
    float sum = p(c - ivec2(1, 0), hi) + p(c + ivec2(1, 0), hi) + p(c - ivec2(0, 1), hi) + p(c + ivec2(0, 1), hi);
    float j = (sum - uH2 * texelFetch(uRhs, c, 0).x) * 0.25;
    float v = centre + uOmega * (j - centre);
    // Pressure is never advected: a NaN here would persist through the warm start.
    if (isnan(v) || isinf(v)) v = 0.0;
    fragColor = vec4(v, 0.0, 0.0, 1.0);
}
