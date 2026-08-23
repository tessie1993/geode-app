package dev.geode.render.compute

import android.opengl.GLES30
import android.util.Log
import dev.geode.engine.gl.ComputePass
import dev.geode.engine.gl.ComputeProgram
import dev.geode.engine.gl.ComputeReader
import dev.geode.engine.gl.ComputeSupport
import dev.geode.engine.gl.GlImageFormat
import dev.geode.engine.gl.GlProfile
import dev.geode.engine.gl.TexelEncoding
import dev.geode.engine.gl.WorkGroupSize
import dev.geode.render.scene.GlUtil

/**
 * Sets the uniforms of one simulation step, without knowing which path it is setting them on.
 *
 * Both paths are ordinary GL programs, so the uniform calls are identical; the only thing this
 * hides is the location cache and the texture-unit allocation. Texture unit 0 belongs to the
 * state, so [sampler] hands out units from 1 upward by name — a simulation never picks a unit
 * and therefore never collides with the state binding.
 */
class SimUniforms internal constructor(
    private val cache: GlUtil.UniformCache,
    private val bindTexture: (Int, Int) -> Unit,
) {
    private val units = HashMap<String, Int>()

    fun float(
        name: String,
        value: Float,
    ) = GLES30.glUniform1f(cache.loc(name), value)

    fun int(
        name: String,
        value: Int,
    ) = GLES30.glUniform1i(cache.loc(name), value)

    fun bool(
        name: String,
        value: Boolean,
    ) = GLES30.glUniform1i(cache.loc(name), if (value) 1 else 0)

    fun vec2(
        name: String,
        x: Float,
        y: Float,
    ) = GLES30.glUniform2f(cache.loc(name), x, y)

    fun vec3(
        name: String,
        x: Float,
        y: Float,
        z: Float,
    ) = GLES30.glUniform3f(cache.loc(name), x, y, z)

    fun vec4(
        name: String,
        x: Float,
        y: Float,
        z: Float,
        w: Float,
    ) = GLES30.glUniform4f(cache.loc(name), x, y, z, w)

    fun ivec2(
        name: String,
        x: Int,
        y: Int,
    ) = GLES30.glUniform2i(cache.loc(name), x, y)

    /**
     * Binds [texture] for the sampler called [name] and points the uniform at it.
     *
     * The unit is chosen here, on first use of the name, and stays put for the life of the
     * pass. Allocation happens on the first frame only; after that this is a map lookup.
     */
    fun sampler(
        name: String,
        texture: Int,
    ) {
        val unit = units.getOrPut(name) { SimGlsl.FIRST_SCENE_TEXTURE_UNIT + units.size }
        bindTexture(unit, texture)
        GLES30.glUniform1i(cache.loc(name), unit)
    }

    /**
     * Sets the first [count] elements of a `vec4[]` from [values], four floats per element.
     *
     * [declared] is the array length the shader source writes, and it is a fallback, not the
     * count: a linker is free to shrink an array whose tail is never read, and uploading more
     * elements than the linked program has is `GL_INVALID_OPERATION` — which silently drops the
     * whole upload, not just the tail. So the true length is asked of the program once and
     * cached, exactly as the hand-written passes do it.
     */
    fun vec4Array(
        name: String,
        values: FloatArray,
        count: Int,
        declared: Int,
    ) = GLES30.glUniform4fv(cache.loc(name), minOf(count, cache.arrayCount(name, declared)), values, 0)
}

/**
 * How a step reads the state it is stepping — the one thing about the encoding a simulation
 * cannot be blind to, and therefore the only thing it declares about it.
 *
 * Everything else the layer hides: whether the four floats spend the frame as packed uints, as
 * halves or as pre-scaled bytes, and whether the step runs as a dispatch or as a fragment. This
 * cannot be hidden because it decides which *format* the state can live in at all, and the two
 * answers are not a preference between equals — each is wrong for the other's simulation.
 */
enum class SimSampling {
    /**
     * The step reads whole texels only: `simLoad` at integer coordinates, its own and its
     * neighbours'. A lattice — a cellular automaton, a reaction-diffusion grid, anything whose
     * stencil is a fixed set of offsets.
     *
     * Takes `FormatPlan.simulationState`, which prefers packed `RGBA32UI`: the one four-channel
     * state format that is core-renderable in ES 3.0 with no float extension at all. It cannot
     * be filtered, and a step that never resamples never notices.
     */
    WHOLE_TEXELS,

    /**
     * The step reads **between** texels: `simSample` at a continuous coordinate, on essentially
     * every texel of every frame. Advection — the back-traced fetch of a dye field.
     *
     * Takes `FormatPlan.advectedField`, which prefers filterable `RGBA16F`, because on this
     * access pattern the packed encoding's price comes due every texel: no hardware filtering,
     * so four loads and two mixes per fetch, out of twice the state bytes. Where the probe
     * proves the format filterable both paths get the texture unit's own bilinear — including
     * the compute path, which samples the read state through a sampler rather than an image
     * for exactly this reason.
     */
    BETWEEN_TEXELS,
}

/**
 * A simulation's per-step uniform binding, as an object rather than a lambda parameter.
 *
 * **Store your binder in a `val`.** A lambda literal written at the `step(...)` call site
 * captures the scene and allocates one object per frame, which is exactly the per-frame garbage
 * the render loop is written to avoid. Held in a property it is allocated once, at scene
 * construction, and costs nothing thereafter.
 */
fun interface SimUniformBinder {
    fun bind(uniforms: SimUniforms)
}

/**
 * What a simulation declares about its state step.
 *
 * ### The GLSL contract
 *
 * [stepBody] is a fragment of GLSL with **no `#version`, no `precision` directives, no `in` or
 * `out` declarations and no `main()`** — the layer supplies all four, differently per path, and
 * a body that brought its own could only ever be right for one of them. What the body must
 * define is one function:
 *
 * ```glsl
 * vec4 simStep(ivec2 texel, ivec2 size, vec4 prev)
 * ```
 *
 * and what it may call, identically on both paths:
 *
 * - `vec4 simLoad(ivec2 texel)` — one texel of the previous state, decoded, with the coordinate
 *   clamped to the grid. Neighbour reads go through this; a raw `texelFetch` outside the
 *   texture is undefined, and a NaN that enters a ping-pong stays for the life of the scene.
 * - `vec4 simSample(vec2 uv)` — the previous state at a continuous coordinate, decoded and
 *   interpolated. Advection goes through this. It is hardware filtering where the format proved
 *   filterable and four clamped loads plus two mixes where it did not, which is what lets one
 *   body run on the packed encoding that cannot be filtered at all.
 * - `vec2 simUv(ivec2 texel)` — that texel's centre in [0, 1].
 *
 * The body never learns whether its four floats spent the frame as packed uints, as halves or
 * as pre-scaled bytes, and it never learns whether it ran as a dispatch or as a fragment.
 *
 * ### What a body may not contain: `barrier()`, and therefore `shared`
 *
 * The generated compute `main()` returns early on an invocation outside the grid, and the
 * dispatch always carries some — the group count is rounded up, so unless the grid is an exact
 * multiple of the local size in both axes the last group in each row and column over-runs it.
 * `barrier()` is defined only when **every** invocation in the work group reaches it, so a
 * `barrier()` inside a body that some invocations return before is undefined behaviour: not a
 * hang, but a group reading half-written `shared` storage, on some drivers, sometimes.
 *
 * That is a real constraint and not a small one. A tiled Jacobi solve — stage a halo in
 * `shared`, iterate several times in registers, store once — is the kernel shape with the most
 * to gain from compute, and it cannot be expressed here. It needs a guarded store rather than
 * an early return, which is a different `main()`, which is a different layer. A step that wants
 * one is telling you it is not a `SimSpec`.
 *
 * @param label short name for logs and shader error messages.
 * @param stepBody the authored GLSL, per the contract above.
 * @param sampling how the body reads the state, which decides the format it can live in. Get
 *   this wrong toward [SimSampling.BETWEEN_TEXELS] and the state costs twice the bytes it
 *   needed; wrong toward [SimSampling.WHOLE_TEXELS] and every advection fetch is done by hand.
 * @param stateScale the range an `RGBA8` fallback packs into [0, 1]. Ignored on the packed and
 *   half-float encodings, which carry their own range. Same meaning as the `uStateScale`
 *   uniform in the hand-written field shaders.
 * @param preferredInvocations the work-group size to aim for when compute is available. Leave
 *   it alone unless the step caches a halo in `shared` memory, which is the only thing that
 *   pays for a bigger group.
 * @param resultReadBy what **the scene** does with the state after a step. The default covers
 *   the usual case: a display pass samples it, and so does the next step. The layer adds the
 *   barrier bits its own mechanism needs; this is only about what the caller does.
 */
class SimSpec(
    val label: String,
    val stepBody: String,
    val sampling: SimSampling = SimSampling.WHOLE_TEXELS,
    val stateScale: Float = 1f,
    val preferredInvocations: Int = WorkGroupSize.TARGET_INVOCATIONS,
    val resultReadBy: Set<ComputeReader> = setOf(ComputeReader.TEXTURE_SAMPLE),
)

/** The outcome of building a [SimPass]. Expected failure is a value, not an exception. */
sealed interface SimBuild {
    data class Ready(
        val pass: SimPass,
    ) : SimBuild

    /**
     * Neither path could be built — the fragment step itself failed to compile. This is the
     * same condition a scene already handles by reporting "unavailable on this GPU"; a device
     * without compute never lands here, because that is not a failure.
     */
    data class Failed(
        val message: String,
    ) : SimBuild
}

/**
 * One simulation step, running as an ES 3.1 compute dispatch where the device proves it can and
 * as the ES 3.0 fragment ping-pong everywhere else — decided once, at build time.
 *
 * ### The thing this exists to delete
 *
 * The alternative is every scene growing an `if (hasCompute)` ladder around its step: two code
 * paths, two shader files, two sets of bugs, and a fallback that is exercised only on the
 * devices nobody testing has. The quality bar already fails a mega-shader with
 * `if (uSceneMode == 7)` chains and a renderer core that switches on `sceneId`, for the same
 * reason: a branch that selects between whole strategies belongs at the place the strategy is
 * chosen, not at every place it is used. So the fork lives in [build], runs once, and nothing
 * downstream can tell which arm it took. There is deliberately no `isCompute` on this
 * interface; [pathLabel] is a string for logs precisely so that reading it feels like the
 * wrong thing to do.
 *
 * ### What a device gets
 *
 * On the compute path a step is a dispatch: no fullscreen triangle, no framebuffer bind, no
 * tile resolve per step. That is a bandwidth win rather than an ALU win, and for anything that
 * scatters it is the only win that matters — the binding constraint for scattered geometry on
 * a mobile tiler is the binning pass, not the ROP. Every deposit drawn as an instanced quad
 * lands at an essentially random tile, and Mali's polygon-list build and Adreno's visibility
 * stream both write per-primitive-per-tile records to main memory for it; shrinking the quad
 * cuts fragments and does nothing at all to that cost. A dispatch emits no primitives, so it
 * never enters binning.
 *
 * ### Lifetime
 *
 * Every method needs the GL thread and a current context. [release] deletes GL objects and
 * [forget] drops them without calling GL, for a context that is already gone — they are not
 * interchangeable.
 */
interface SimPass {
    /** Short name, as given in the spec. */
    val label: String

    /** Current simulation grid width in texels, or 0 before the first successful [step]. */
    val width: Int

    /** Current simulation grid height in texels, or 0 before the first successful [step]. */
    val height: Int

    /**
     * `"compute"` or `"fragment"`, for logcat and a debug HUD. Branching on it re-creates the
     * ladder this interface exists to remove.
     */
    val pathLabel: String

    /** The texture holding the current state. Prefer [bindStateFor] over binding it by hand. */
    val stateTexture: Int

    /**
     * Sets the simulation grid size. Storage is (re)allocated lazily on the next [step], so
     * this is safe to call from `Scene.resize` before there is anything to allocate into.
     *
     * This is the **simulation** resolution, which is not the display resolution and must not
     * be derived from it by the caller's own arithmetic — a field sim with a nine-tap kernel is
     * texture-fetch-bound at native resolution on a mid-tier Mali long before it is ALU bound.
     * The number belongs to the quality profile.
     */
    fun resize(
        width: Int,
        height: Int,
    )

    /**
     * Runs one step: binds the state, lets [binder] set the simulation's own uniforms, runs the
     * step, and swaps. Returns false when storage could not be allocated, in which case the
     * caller should skip its display pass for this frame.
     */
    fun step(binder: SimUniformBinder): Boolean

    /** Zeroes the state. Called automatically on allocation; call it again to reseed. */
    fun clear()

    /**
     * Wraps a display fragment shader body with the state declarations and decode helpers, so
     * the display pass reads the state through the same `simLoad` / `simSample` the step does
     * and is equally blind to the encoding. The body brings its own `in`/`out` and `main()`.
     */
    fun displayShader(body: String): String

    /**
     * Binds the current state for a display program and sets its `uSimState` and `uSimSize`
     * uniforms. [display] is the caller's own uniform cache for that program.
     */
    fun bindStateFor(
        display: GlUtil.UniformCache,
        unit: Int,
    )

    /** Deletes every GL object. Requires a live context. */
    fun release()

    /** Drops every GL object without calling GL, for a context that is already gone. */
    fun forget()

    companion object {
        private const val TAG = "SimPass"

        /**
         * Chooses the path and builds it. **The only place in the tree that asks whether this
         * device has compute.**
         *
         * The order is deliberate: ask the device, try compute, and fall back to the fragment
         * path on *any* compute failure — an unreadable limit, a link error, a driver that
         * advertises 3.1 and then refuses `GL_COMPUTE_SHADER`. A device that lies about compute
         * costs one failed link at scene construction and then runs the path it was always
         * going to run. It never costs a black frame, which is what an `if (hasCompute)` at the
         * call site would have produced.
         *
         * @param gl the device profile. A scene gets it from
         *   `DeviceGl.profileWithCurrentContext(context)`, which memoises, so asking for it in
         *   `init()` costs three `glGetString` calls after the first scene.
         * @param onDiagnostic receives one line describing the path taken and why, plus any
         *   compile errors. Wire it to the same place a scene sends shader errors.
         */
        fun build(
            spec: SimSpec,
            gl: GlProfile,
            onDiagnostic: (String) -> Unit,
        ): SimBuild {
            // The one decision the spec makes about storage, and it is made here rather than in
            // the scene so that the scene still never names a format. Both roles come out of
            // the same probe pass, so a device that fails RGBA16F and a device that fails
            // RGBA32UI each get an answer measured on it rather than assumed for it.
            val resolved =
                when (spec.sampling) {
                    SimSampling.WHOLE_TEXELS -> gl.formats.simulationState
                    SimSampling.BETWEEN_TEXELS -> gl.formats.advectedField
                }
            val format =
                GlImageFormat.of(resolved.format) ?: return SimBuild.Failed(
                    "${spec.label}: the format policy resolved state for ${spec.sampling} sampling to ${resolved.format}, " +
                        "which has no GLSL descriptor here (${resolved.because})",
                )
            val encoding =
                SimStateEncoding(
                    format = format,
                    packed =
                        when (resolved.encoding) {
                            TexelEncoding.FLOAT_BITS_IN_UINT -> true
                            TexelEncoding.LINEAR, TexelEncoding.PRE_SCALED -> false
                        },
                    // The one place the layer overrides the plan. An integer texture cannot be
                    // filtered under any circumstances, and asking for LINEAR on one leaves it
                    // incomplete and sampling as zero - a silent black simulation. Whatever the
                    // resolved format claims, the packed encoding interpolates by hand.
                    filterable = resolved.filterable && !format.integerTexels,
                    stateScale =
                        when (resolved.encoding) {
                            TexelEncoding.PRE_SCALED -> spec.stateScale
                            TexelEncoding.LINEAR, TexelEncoding.FLOAT_BITS_IN_UINT -> 1f
                        },
                )
            val state = SimField(spec.label, format, encoding.filterable)

            when (val support = ComputeSupport.query(gl)) {
                is ComputeSupport.Unavailable ->
                    onDiagnostic("${spec.label}: fragment ping-pong — ${support.because}")

                is ComputeSupport.Available -> {
                    val pass = computePass(spec, encoding, support, onDiagnostic)
                    if (pass != null) {
                        onDiagnostic("${spec.label}: compute dispatch — ${support.because}")
                        return SimBuild.Ready(
                            ComputeSimPass(spec.label, encoding, state, pass, support.limits.maxGroupCount),
                        )
                    }
                }
            }
            return fragmentPass(spec, encoding, state, onDiagnostic)
        }

        private fun computePass(
            spec: SimSpec,
            encoding: SimStateEncoding,
            support: ComputeSupport.Available,
            onDiagnostic: (String) -> Unit,
        ): ComputePass? {
            val localSize = WorkGroupSize.forGrid(support.limits, spec.preferredInvocations)
            val source = SimGlsl.computeStep(encoding, localSize, spec.stepBody)
            val program =
                ComputeProgram.buildReporting(spec.label, source, localSize) { message ->
                    onDiagnostic("${spec.label}: compute step did not build, using fragment ping-pong — $message")
                } ?: return null
            Log.i(TAG, "${spec.label}: compute step at local size $localSize")
            return ComputePass(
                label = spec.label,
                program = program,
                // The scene's readers, plus the bit this layer's own mechanism needs. The
                // ping-pong makes each texture alternately an image store destination and a
                // sampled source, so the mask has to cover both edges: read-after-write when
                // the display pass samples what was just written, and write-after-read when the
                // step two frames later binds that same texture as its output image again.
                readers = spec.resultReadBy + ComputeReader.IMAGE_LOAD_STORE,
            )
        }

        private fun fragmentPass(
            spec: SimSpec,
            encoding: SimStateEncoding,
            state: SimField,
            onDiagnostic: (String) -> Unit,
        ): SimBuild {
            var error: String? = null
            val program =
                GlUtil.buildProgramReporting(
                    SimGlsl.FULLSCREEN_VERTEX,
                    SimGlsl.fragmentStep(encoding, spec.stepBody),
                ) { message ->
                    error = message ?: "no message"
                }
            if (program == 0) {
                state.release()
                return SimBuild.Failed("${spec.label}: fragment step did not build — $error")
            }
            return SimBuild.Ready(FragmentSimPass(spec.label, encoding, state, program))
        }
    }
}

/**
 * The half of [SimPass] that is the same on both paths: the state pair, the grid size, the
 * decode the display shares with the step.
 *
 * Only the step itself differs, which is the measure of how much of a "compute rewrite" is
 * actually about compute — almost none of it.
 */
internal abstract class BaseSimPass(
    override val label: String,
    protected val encoding: SimStateEncoding,
    protected val state: SimField,
) : SimPass {
    private var requestedWidth = 0
    private var requestedHeight = 0

    override val width: Int get() = state.width

    override val height: Int get() = state.height

    override val stateTexture: Int get() = state.readTexture

    override fun resize(
        width: Int,
        height: Int,
    ) {
        requestedWidth = width
        requestedHeight = height
    }

    override fun clear() = state.clear()

    override fun displayShader(body: String): String = SimGlsl.displayShader(encoding, body)

    override fun bindStateFor(
        display: GlUtil.UniformCache,
        unit: Int,
    ) {
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0 + unit)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, state.readTexture)
        GLES30.glUniform1i(display.loc(SimGlsl.UNIFORM_STATE), unit)
        GLES30.glUniform2i(display.loc(SimGlsl.UNIFORM_SIZE), state.width, state.height)
    }

    override fun release() = state.release()

    override fun forget() = state.forget()

    /** Allocates the state pair at the requested size if needed. False means skip this frame. */
    protected fun ensureStorage(): Boolean = state.ensure(requestedWidth, requestedHeight)
}
