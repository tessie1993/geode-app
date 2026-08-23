package dev.geode.render.compute

import dev.geode.engine.gl.GlImageFormat
import dev.geode.engine.gl.WorkGroupSize

/**
 * How simulation state is stored, in the four terms every generated shader needs.
 *
 * Derived once from `FormatPlan.simulationState`, which resolved it from probed behaviour. A
 * simulation never sees this: it is the whole point of the layer that a step body is written
 * against `vec4` and has no idea whether those four floats spent the frame as packed uints, as
 * halves, or as pre-scaled bytes.
 */
internal data class SimStateEncoding(
    val format: GlImageFormat,
    /**
     * True when the four floats are stored as their own bit patterns in an `RGBA32UI` texture.
     * This is the ES 3.0 baseline's state format and the one the plan commits to: unlike every
     * float format it needs no `EXT_color_buffer_float`, so it is a core-spec guarantee rather
     * than a hope. It costs blending and filtering, which a state ping-pong never wanted.
     */
    val packed: Boolean,
    /** Whether the hardware can interpolate between texels, proven by the format probe. */
    val filterable: Boolean,
    /**
     * The range an `RGBA8` fallback divides by on write and multiplies by on read. 1.0 for the
     * packed and half-float encodings, which carry their own range. Same convention as the
     * existing `uStateScale` uniform in the hand-written field shaders, so a body ported from
     * one of those means the same thing by it.
     */
    val stateScale: Float,
)

/**
 * Generates the shader source around a simulation's authored step body.
 *
 * ### The contract a step body is written to
 *
 * Stated for scene authors on [SimSpec], which is the public half of this: a body defines
 * `vec4 simStep(ivec2 texel, ivec2 size, vec4 prev)` and may call `simLoad`, `simSample` and
 * `simUv`. That is the entire surface, and it is the reason a scene needs no `if (hasCompute)`.
 * The two paths differ in their `main()`, their output mechanism and their sampling function —
 * all three of which are generated here and none of which appear in the body.
 *
 * ### Why the layer owns the header
 *
 * `precision highp float; precision highp int;` is the plan's header rule, and the reason it
 * matters here is sharper than "highp is safer": **the two stages have different defaults.**
 * A fragment shader has no default float precision at all (omitting the line is a compile
 * error) and defaults `int` to `mediump`; a compute shader defaults both to `highp`. So the
 * same body, compiled once per path without the header, would be a hard error on one path and
 * 16-bit integer arithmetic on the other — the two paths silently disagreeing is precisely the
 * failure this layer exists to make impossible. With the packed encoding that matters most: a
 * mediump uint is only guaranteed 16 bits, so any bit manipulation a body does between
 * `simLoad` and its return truncates, with no diagnostic anywhere.
 *
 * The same argument covers `#version`: the fragment path is `300 es` and the compute path is
 * `310 es`, so a body that carried its own version directive could only ever be right for one
 * of them.
 *
 * ### Why nothing here rounds the result to match the other path
 *
 * Because there is nothing to match it to. Both paths write the **same texture in the same
 * format** — a colour attachment on one, an `imageStore` on the other — so the hardware applies
 * the format's own rounding to both, and a half-float state quantises to halves whichever path
 * produced it. A generated step therefore cannot drift from its twin by storage precision at
 * all, which matters more for these families than anywhere else in the engine: state is a
 * feedback loop, so a last-bit difference is not a last-bit difference for long, it is the seed
 * of a different picture a few hundred frames later.
 *
 * A hand-written compute kernel that picks its own format does not get this for free, and the
 * usual repair — round the fp32 result through `packHalf2x16`/`unpackHalf2x16` before storing —
 * is a patch over a mismatch rather than the absence of one. One generator emitting both paths
 * from one encoding is what makes the patch unnecessary.
 *
 * What can still differ is arithmetic: `sin`, `exp`, `pow`, `normalize` and friends are
 * specified only to a few ulp in ESSL, and no driver promises its fragment and compute stages
 * share an implementation of them. A step dense in transcendentals will part ways with itself
 * over a few seconds of feedback even with identical inputs and identical rounding. The
 * fragment path is the reference, because it is the one that ships everywhere.
 */
internal object SimGlsl {
    /** Texture unit the state is sampled from. Scene textures start at [FIRST_SCENE_TEXTURE_UNIT]. */
    const val STATE_TEXTURE_UNIT = 0

    /**
     * Image unit the step writes through. Zero is fine alongside [STATE_TEXTURE_UNIT] — image
     * units and texture image units are separate binding namespaces, a detail that has cost
     * more than one afternoon.
     */
    const val STATE_IMAGE_UNIT = 0

    /** The first texture unit a simulation may use for its own inputs. */
    const val FIRST_SCENE_TEXTURE_UNIT = 1

    const val UNIFORM_STATE = "uSimState"
    const val UNIFORM_SIZE = "uSimSize"

    /**
     * The fullscreen triangle for the fragment path, attribute-less via `gl_VertexID`.
     *
     * Deliberately not `R.raw.quad_vert`, even though that file is the same six lines. The
     * generated fragment source and the vertex stage it runs with have to agree about the
     * varyings between them, and the agreement here is that there are none — the step derives
     * its coordinate from `gl_FragCoord` so that `simUv` means the same thing in both paths. A
     * vertex shader loaded from elsewhere could grow a varying the generator does not know
     * about; this one cannot.
     */
    val FULLSCREEN_VERTEX =
        """
        #version 300 es
        void main() {
            vec2 pos = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
            gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
        }
        """.trimIndent()

    /**
     * The ES 3.0 fragment ping-pong step: a fullscreen triangle over the write target, one
     * invocation per fragment, the result going out through the colour attachment.
     */
    fun fragmentStep(
        encoding: SimStateEncoding,
        body: String,
    ): String =
        buildString {
            appendLine("#version 300 es")
            appendPreamble(encoding, sampledInFragmentStage = true)
            appendLine("out ${encoding.format.texelType} simOut;")
            appendLine()
            appendLine(body.trim())
            appendLine()
            appendLine("void main() {")
            appendLine("    ivec2 texel = ivec2(gl_FragCoord.xy);")
            appendLine("    simOut = ${storeExpression(encoding, "simStep(texel, $UNIFORM_SIZE, simLoad(texel))")};")
            appendLine("}")
        }

    /**
     * The ES 3.1 compute step: one invocation per state texel, the result scattered through an
     * image store. No rasterization, no framebuffer bind, no tile resolve per step.
     */
    fun computeStep(
        encoding: SimStateEncoding,
        localSize: WorkGroupSize,
        body: String,
    ): String =
        buildString {
            appendLine("#version 310 es")
            appendPreamble(encoding, sampledInFragmentStage = false)
            appendLine(localSize.layoutQualifier)
            appendLine()
            // The layout qualifier, the texture's internal format and the `format` argument of
            // glBindImageTexture must all name the same thing. GlImageFormat holds all three so
            // they cannot drift; a mismatch here is undefined behaviour, not an error, and
            // shows up as a simulation that is subtly wrong rather than one that fails.
            appendLine(
                "layout(${encoding.format.layoutQualifier}, binding = $STATE_IMAGE_UNIT) " +
                    "writeonly uniform ${encoding.format.imageType} simOut;",
            )
            appendLine()
            appendLine(body.trim())
            appendLine()
            appendLine("void main() {")
            appendLine("    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);")
            // The dispatch rounds the group count up, so the last group in each axis over-runs
            // the grid. An out-of-range imageStore is a defined no-op in ES 3.1, so this guard
            // is not what makes the write safe - it is what stops the body from running at all
            // on a texel that does not exist, which matters the moment a step does anything
            // beyond writing its own texel.
            appendLine("    if (any(greaterThanEqual(texel, $UNIFORM_SIZE))) return;")
            appendLine(
                "    imageStore(simOut, texel, " +
                    "${storeExpression(encoding, "simStep(texel, $UNIFORM_SIZE, simLoad(texel))")});",
            )
            appendLine("}")
        }

    /**
     * A display fragment shader over the same state: the scene's own body, with the state
     * declarations and decode helpers already in scope. The display pass is where the log
     * curve, the palette and the tone response belong — never in the step, because a deposit
     * field has to accumulate linearly and `log(a) + log(b)` is `log(a * b)`, not `log(a + b)`.
     */
    fun displayShader(
        encoding: SimStateEncoding,
        body: String,
    ): String =
        buildString {
            appendLine("#version 300 es")
            appendPreamble(encoding, sampledInFragmentStage = true)
            appendLine(body.trim())
        }

    private fun StringBuilder.appendPreamble(
        encoding: SimStateEncoding,
        sampledInFragmentStage: Boolean,
    ) {
        appendLine("precision highp float;")
        appendLine("precision highp int;")
        appendLine()
        appendLine("uniform ${encoding.format.samplerType} $UNIFORM_STATE;")
        appendLine("uniform ivec2 $UNIFORM_SIZE;")
        // A compile-time constant rather than a uniform: it is fixed for the life of the
        // program, and folding the divide costs the driver nothing to do and us nothing to set.
        appendLine("const float SIM_STATE_SCALE = ${encoding.stateScale};")
        appendLine()
        appendLine("vec2 simUv(ivec2 texel) {")
        appendLine("    return (vec2(texel) + 0.5) / vec2($UNIFORM_SIZE);")
        appendLine("}")
        appendLine()
        appendLine("vec4 simLoad(ivec2 texel) {")
        // Clamped, and not optionally. texelFetch outside the texture is undefined in GLSL ES,
        // and a NaN that enters a ping-pong stays there for the life of the scene - the same
        // reason the hand-written field shaders sanitise every sample. Clamping also makes the
        // manual interpolation below behave exactly like the GL_CLAMP_TO_EDGE wrap mode the
        // hardware path gets, so the two paths agree at the border instead of near it.
        appendLine("    ivec2 at = clamp(texel, ivec2(0), $UNIFORM_SIZE - ivec2(1));")
        appendLine("    return ${loadExpression(encoding, "texelFetch($UNIFORM_STATE, at, 0)")};")
        appendLine("}")
        appendLine()
        appendSampler(encoding, sampledInFragmentStage)
        appendLine()
    }

    private fun StringBuilder.appendSampler(
        encoding: SimStateEncoding,
        sampledInFragmentStage: Boolean,
    ) {
        appendLine("vec4 simSample(vec2 uv) {")
        if (encoding.filterable) {
            // texture() takes its level of detail from screen-space derivatives. Those exist
            // only in the fragment stage; outside it the implicit LOD is undefined, and a
            // compiler will not say so - the call compiles either way. The state textures are
            // allocated with a single level, so naming LOD 0 explicitly is the same fetch with
            // the undefined part removed.
            val fetch =
                if (sampledInFragmentStage) {
                    "texture($UNIFORM_STATE, uv)"
                } else {
                    "textureLod($UNIFORM_STATE, uv, 0.0)"
                }
            appendLine("    return ${loadExpression(encoding, fetch)};")
        } else {
            // Integer textures cannot be filtered at all - LINEAR on a usampler2D leaves the
            // texture incomplete and every fetch returns zero - so the packed encoding buys its
            // core-spec guarantee at the price of doing the interpolation by hand. Four clamped
            // loads and two mixes; this is the "manual interpolation" the format policy's own
            // fallback sentence promises.
            appendLine("    vec2 p = uv * vec2($UNIFORM_SIZE) - 0.5;")
            appendLine("    vec2 f = fract(p);")
            appendLine("    ivec2 b = ivec2(floor(p));")
            appendLine("    vec4 s00 = simLoad(b);")
            appendLine("    vec4 s10 = simLoad(b + ivec2(1, 0));")
            appendLine("    vec4 s01 = simLoad(b + ivec2(0, 1));")
            appendLine("    vec4 s11 = simLoad(b + ivec2(1, 1));")
            appendLine("    return mix(mix(s00, s10, f.x), mix(s01, s11, f.x), f.y);")
        }
        appendLine("}")
    }

    private fun loadExpression(
        encoding: SimStateEncoding,
        fetch: String,
    ): String =
        when {
            encoding.packed -> "uintBitsToFloat($fetch)"
            encoding.stateScale != 1f -> "$fetch * SIM_STATE_SCALE"
            else -> fetch
        }

    private fun storeExpression(
        encoding: SimStateEncoding,
        value: String,
    ): String =
        when {
            encoding.packed -> "floatBitsToUint($value)"
            // No clamp: writes to a normalised fixed-point target are clamped to [0, 1] by the
            // pipeline, for a colour attachment and for an image store alike.
            encoding.stateScale != 1f -> "($value) / SIM_STATE_SCALE"
            else -> value
        }
}
