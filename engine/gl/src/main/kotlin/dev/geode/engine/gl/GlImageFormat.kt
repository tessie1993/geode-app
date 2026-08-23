package dev.geode.engine.gl

import android.opengl.GLES30
import android.opengl.GLES31

/** How a compute step may touch an image binding. Closed set; the GL enums are ES 3.1 core. */
enum class ImageAccess(
    val glAccess: Int,
) {
    /** `readonly`. */
    READ(GLES31.GL_READ_ONLY),

    /** `writeonly`. Ping-pong steps want this: the destination is never read in the same pass. */
    WRITE(GLES31.GL_WRITE_ONLY),

    /**
     * `coherent`-less read-modify-write of the *same* texel by the *same* invocation. Not a
     * licence to read a neighbour's texel: invocations in different work groups have no
     * ordering, so a step that reads any texel another invocation writes is a data race no
     * barrier can repair. That is what the ping-pong is for.
     */
    READ_WRITE(GLES31.GL_READ_WRITE),
}

/**
 * A texture format that a compute shader can bind as an image, together with the four pieces
 * of GLSL text that have to agree with it.
 *
 * ### The agreement this type exists to enforce
 *
 * Binding a state texture as an image requires **three** things to name the same format:
 *
 * 1. the texture's own internal format, from `glTexStorage2D`;
 * 2. the `format` argument of `glBindImageTexture`;
 * 3. the shader's `layout(...)` qualifier on the image uniform.
 *
 * Get any of them wrong and the result is usually not a compile error. A shader compiler
 * rejects a qualifier whose *base type* disagrees — `rgba16f` on a `uimage2D` is caught — but
 * `rgba16ui` on a texture allocated as `RGBA32UI` compiles clean and is undefined at runtime:
 * on some drivers the store is dropped, on others it writes reinterpreted bits, and the symptom
 * is a simulation that looks *almost* right. Holding all three in one enum member is the only
 * way to make the mismatch unrepresentable rather than merely unlikely.
 *
 * ### Why some formats are missing
 *
 * ES 3.1 mandates support for a specific list of image unit formats (spec table 8.27):
 * `rgba32f`, `rgba16f`, `r32f`, `rgba32ui`, `rgba16ui`, `rgba8ui`, `r32ui`, `rgba32i`,
 * `rgba16i`, `rgba8i`, `r32i`, `rgba8`, `rgba8_snorm`. **`r16f` and `rg16f` are not on it.**
 * They are perfectly good render targets — `FormatPolicy` picks `R16F` for linear accumulation
 * and `RG16F` for filterable fields on proven behaviour — but a driver is entirely within
 * spec to refuse them as images. So [of] answers null for those two, and a caller holding a
 * null has to take the fragment path. That is not a fallback for a broken device; it is the
 * correct answer for a conformant one.
 *
 * ### Precision
 *
 * Every declaration here carries `highp`, per the plan's header rule
 * (`precision highp float; precision highp int;`). For the packed state format it is not a
 * quality preference: `floatBitsToUint` round-trips only if the uint keeps all 32 bits, and a
 * mediump int is allowed to be 16.
 */
enum class GlImageFormat(
    /** For `glTexStorage2D` and the `format` argument of `glBindImageTexture`. */
    val internalFormat: Int,
    /** The GLSL `layout(...)` token. Must equal [internalFormat]'s spelling. */
    val layoutQualifier: String,
    /** How a sampler of this format is declared. */
    val samplerType: String,
    /** How an image of this format is declared. */
    val imageType: String,
    /** The GLSL type one texel loads as, before any decode. */
    val texelType: String,
    /**
     * True when texels are integers: the sampler returns `uvec4`, the target cannot be blended
     * or filtered, and clears go through `glClearBufferuiv` rather than `glClearBufferfv`.
     */
    val integerTexels: Boolean,
) {
    /**
     * The ES 3.0 baseline's state format and the one the whole encoding rests on: float bits
     * packed with `floatBitsToUint`. Core in ES 3.0 as a colour-renderable format — unlike
     * every float format, which needs `EXT_color_buffer_float` — and a required image format in
     * ES 3.1, so the same texture serves the fragment path and the compute path unchanged.
     */
    RGBA32UI(
        internalFormat = GLES30.GL_RGBA32UI,
        layoutQualifier = "rgba32ui",
        samplerType = "highp usampler2D",
        imageType = "highp uimage2D",
        texelType = "uvec4",
        integerTexels = true,
    ),

    /** Half-float state, taken when `RGBA32UI` fails its probe. Filterable where proven. */
    RGBA16F(
        internalFormat = GLES30.GL_RGBA16F,
        layoutQualifier = "rgba16f",
        samplerType = "highp sampler2D",
        imageType = "highp image2D",
        texelType = "vec4",
        integerTexels = false,
    ),

    /**
     * The core-mandated floor. Renderable and image-bindable everywhere, at the cost of 8 bits
     * per channel in [0, 1] — which is why the state that lands here is pre-scaled rather than
     * stored raw.
     */
    RGBA8(
        internalFormat = GLES30.GL_RGBA8,
        layoutQualifier = "rgba8",
        samplerType = "highp sampler2D",
        imageType = "highp image2D",
        texelType = "vec4",
        integerTexels = false,
    ),

    /**
     * One channel, not four. Present because it *is* a required image format and answering
     * null for it would be a lie, but a four-channel state cannot live here — which is why
     * `FormatPolicy` never resolves the simulation-state role to it.
     */
    R32F(
        internalFormat = GLES30.GL_R32F,
        layoutQualifier = "r32f",
        samplerType = "highp sampler2D",
        imageType = "highp image2D",
        texelType = "vec4",
        integerTexels = false,
    ),
    ;

    companion object {
        /**
         * The image format for a probed format, or null when ES 3.1 does not require the
         * format to be image-bindable — see the class doc's table 8.27 note. Exhaustive with
         * no `else`, so a new [ProbedFormat] stops compiling here instead of silently
         * resolving to "no compute".
         */
        fun of(format: ProbedFormat): GlImageFormat? =
            when (format) {
                ProbedFormat.RGBA8 -> RGBA8
                ProbedFormat.R16F -> null
                ProbedFormat.RG16F -> null
                ProbedFormat.RGBA16F -> RGBA16F
                ProbedFormat.R32F -> R32F
                ProbedFormat.RGBA32UI -> RGBA32UI
            }
    }
}
