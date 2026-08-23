package dev.geode.engine.gl

import android.opengl.GLES30
import android.opengl.GLES31
import android.util.Half
import android.util.Log
import dev.geode.util.bestEffort
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.CancellationException
import kotlin.math.abs

/**
 * The three strings that identify a driver build. [CapabilityCache] compares all three because
 * a driver update usually shows up only in `GL_VERSION` — vendor and renderer stay put.
 */
data class GlIdentity(
    val vendor: String,
    val renderer: String,
    val versionString: String,
)

/**
 * Fills a [GlProbeReport] from a live GL context.
 *
 * **Every function here requires a current GL context and must run on the thread that owns
 * it** — the GLSurfaceView render thread, or the throwaway thread that made
 * [EglProbeHarness]' context current. Nothing here touches Android storage or preferences;
 * turning facts into a decision and persisting them is [DeviceGl]'s job.
 *
 * Three properties this file is built around, in the order they matter:
 *
 * 1. **Behaviour, not advertisement.** §6.3's rule is "never infer support from the GLES
 *    version string alone". So a format is renderable only after a draw and a `glReadPixels`
 *    agree; vertex texture fetch is proven by fetching and reading the result back; the timer
 *    query is trusted only after it returns a plausible non-zero duration with the disjoint
 *    flag clear. "Attachable" and "correct" are different claims, and drivers lie about the
 *    second one far more often than the first.
 * 2. **It cannot take the process down.** Every step is individually guarded; a step that
 *    throws degrades to "not supported" and the app keeps running on the baseline, which is
 *    always correct. The only thing rethrown is cancellation, which is not an error.
 * 3. **It leaves no trace.** Every GL object is registered with the arena and deleted on
 *    every path, and every piece of context state is captured before and restored after. This
 *    runs before the first frame; an object leaked here is leaked for the session, and a
 *    blend mode left enabled here is a rendering bug three files away.
 *
 * This is explicitly **not** a hot path — it runs once per process, so ordinary allocation is
 * fine here and the "reuse preallocated buffers" rule that governs the render loop does not
 * apply.
 */
object GlProber {
    private const val TAG = "GlProber"

    private val UNKNOWN_IDENTITY = GlIdentity(vendor = "", renderer = "", versionString = "")

    /**
     * Probe targets are 4x4. Big enough that a driver writing only the first texel of a tile
     * would show up, small enough that the readback is a handful of bytes and the timed draw
     * measures the pipeline rather than the fill rate.
     */
    private const val PROBE_SIZE = 4

    /** The filter probe needs exactly two texels to interpolate between. */
    private const val FILTER_WIDTH = 2
    private const val FILTER_HEIGHT = 1

    /**
     * `GL_EXT_disjoint_timer_query`. The enums are not in [GLES30] because the extension is
     * not core in any ES version; the entry points are, since ES 3.0 has query objects and the
     * extension only adds a target and the 64-bit getters.
     */
    private const val TIMER_QUERY_EXTENSION = "GL_EXT_disjoint_timer_query"
    private const val GL_TIME_ELAPSED_EXT = 0x88BF
    private const val GL_GPU_DISJOINT_EXT = 0x8FBB

    /**
     * A 4x4 draw is microseconds of GPU time. Anything past half a second is not a
     * measurement, it is a driver returning a placeholder — refuse to call that "trusted".
     */
    private const val TIMER_CEILING_NS = 500_000_000L

    /** Bounded so a query that never becomes available cannot spin the GL thread forever. */
    private const val QUERY_POLL_LIMIT = 256

    /** Same reasoning: a driver stuck returning the same error must not hang startup. */
    private const val ERROR_DRAIN_LIMIT = 32

    /**
     * Reads the driver identity. Three `glGetString` calls — cheap enough to run on every
     * surface creation so [CapabilityCache] can decide whether the stored facts still apply
     * before paying for a full probe.
     */
    fun identity(): GlIdentity =
        GlIdentity(
            vendor = string(GLES30.GL_VENDOR),
            renderer = string(GLES30.GL_RENDERER),
            versionString = string(GLES30.GL_VERSION),
        )

    /**
     * Runs the whole probe pass. Never throws (except on cancellation) and never leaves GL
     * objects or state behind.
     *
     * A report in which every probe failed is still a valid report: per the ABI, a format
     * absent from the map counts as failed, so an empty report claims nothing and
     * [FormatPolicy] hands every role the core-mandated RGBA8 floor. A device therefore gets a
     * named plan rather than a black frame even if this function accomplishes nothing.
     *
     * Suppressed rather than rewritten: the instance check IS the cancellation rethrow.
     */
    @Suppress("TooGenericExceptionCaught", "InstanceOfCheckForException")
    fun probe(): GlProbeReport {
        val identity = identity()
        val guard = probeCatching("capture GL state") { GlStateGuard.captureAndNeutralise() }
        val arena = GlArena()
        return try {
            gather(identity, arena)
        } catch (t: Throwable) {
            if (t is CancellationException) throw t
            Log.w(TAG, "GL probe failed outright; the device keeps the ES 3.0 baseline", t)
            unprobed(identity)
        } finally {
            // Order matters: delete our objects first, then restore the caller's bindings.
            // Deleting a bound texture silently rebinds 0 to its unit, so restoring afterwards
            // is what puts the caller's own binding back rather than leaving it cleared.
            arena.releaseAll()
            guard?.restore()
            drainErrors()
        }
    }

    private fun gather(
        identity: GlIdentity,
        arena: GlArena,
    ): GlProbeReport {
        val extensions = probeCatching("read extensions") { extensions() } ?: emptySet()
        val version = GlVersion.parse(identity.versionString)
        val es31 = version != null && version >= GlVersion(3, 1)

        val vao = probeCatching("create probe VAO") { arena.vertexArray() } ?: 0
        val scratch = probeCatching("create RGBA8 scratch target") { arena.target(SPECS.getValue(ProbedFormat.RGBA8)) }
        val floatProgram =
            probeCatching("build float-fill program") {
                arena.program("float fill", FULLSCREEN_VERT, FLOAT_FRAG)
            } ?: 0
        val uintProgram =
            probeCatching("build packed-uint program") {
                arena.program("packed uint fill", FULLSCREEN_VERT, UINT_FRAG)
            } ?: 0
        val sampleProgram =
            probeCatching("build texture-sample program") {
                arena.program("texture sample", FULLSCREEN_VERT, SAMPLE_FRAG)
            } ?: 0

        val formats =
            ProbedFormat.entries.associateWith { format ->
                probeCatching("probe ${format.name}") {
                    probeFormat(arena, vao, format, floatProgram, uintProgram, sampleProgram, scratch)
                } ?: FAILED
            }

        // Hoisted out of the constructor call below: these run draws and readbacks, and the
        // order they run in should be visible rather than implied by argument position.
        val vertexFetchProven =
            probeCatching("probe vertex texture fetch") { probeVertexTextureFetch(arena, vao, scratch) } ?: false
        val timerPresent = TIMER_QUERY_EXTENSION in extensions
        val timerProven =
            timerPresent &&
                (probeCatching("probe timer query") { probeTimerQuery(arena, vao, floatProgram, scratch) } ?: false)

        return GlProbeReport(
            vendor = identity.vendor,
            renderer = identity.renderer,
            versionString = identity.versionString,
            extensions = extensions,
            maxTextureSize = limit(GLES30.GL_MAX_TEXTURE_SIZE),
            maxColorAttachments = limit(GLES30.GL_MAX_COLOR_ATTACHMENTS),
            maxVertexTextureImageUnits = limit(GLES30.GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS),
            vertexTextureFetchProven = vertexFetchProven,
            // The ES 3.1 compute limits are only asked for when the version string claims 3.1.
            // On a 3.0 driver these pnames raise GL_INVALID_ENUM, and while `limit` handles
            // that, spraying errors at a driver we are about to hand a real frame to is a bad
            // way to start. Zero is the honest answer for a 3.0 context anyway, and
            // GlCapabilities treats it as "enables nothing".
            maxComputeWorkGroupInvocations = if (es31) limit(GLES31.GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS) else 0,
            maxComputeStorageBlocks = if (es31) limit(GLES31.GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS) else 0,
            maxFragmentStorageBlocks = if (es31) limit(GLES31.GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS) else 0,
            maxComputeImageUniforms = if (es31) limit(GLES31.GL_MAX_COMPUTE_IMAGE_UNIFORMS) else 0,
            programBinaryFormats = limit(GLES30.GL_NUM_PROGRAM_BINARY_FORMATS),
            timerQueryPresent = timerPresent,
            timerQueryProven = timerProven,
            formats = formats,
        )
    }

    /**
     * The report of a device nothing has been measured on. **The one function here that needs
     * no GL context** — it exists so a caller that could not get a context still has a report
     * to hand [FormatPolicy], which resolves every role to the core-mandated RGBA8 floor. Per
     * the ABI, a format absent from the map counts as failed, so this claims nothing at all,
     * which is the honest state of a device before its first probe pass.
     */
    fun unprobed(identity: GlIdentity = UNKNOWN_IDENTITY): GlProbeReport =
        GlProbeReport(
            vendor = identity.vendor,
            renderer = identity.renderer,
            versionString = identity.versionString,
            extensions = emptySet(),
            maxTextureSize = 0,
            maxColorAttachments = 0,
            maxVertexTextureImageUnits = 0,
            vertexTextureFetchProven = false,
            maxComputeWorkGroupInvocations = 0,
            maxComputeStorageBlocks = 0,
            maxFragmentStorageBlocks = 0,
            maxComputeImageUniforms = 0,
            programBinaryFormats = 0,
            timerQueryPresent = false,
            timerQueryProven = false,
            formats = emptyMap(),
        )

    // ---------------------------------------------------------------- per-format behaviour

    private fun probeFormat(
        arena: GlArena,
        vao: Int,
        format: ProbedFormat,
        floatProgram: Int,
        uintProgram: Int,
        sampleProgram: Int,
        scratch: ProbeTarget?,
    ): FormatProbe {
        val spec = SPECS.getValue(format)
        val target = arena.target(spec)

        val rendersExactly =
            target.complete &&
                (
                    probeCatching("render ${format.name}") {
                        if (spec.integer) {
                            renderPackedExactly(vao, uintProgram, target)
                        } else {
                            renderExactly(vao, floatProgram, target, spec)
                        }
                    } ?: false
                )

        // Integer colour attachments cannot blend and integer textures cannot filter — those
        // are spec facts, not driver quirks, so probing them would only produce GL errors.
        // That is precisely why RGBA32UI state costs `uintBitsToFloat` in every reader and why
        // FormatPolicy hands interpolation of packed state back to the shader.
        val blendsAdditively =
            target.complete &&
                !spec.integer &&
                (probeCatching("blend ${format.name}") { blendsAdditively(vao, floatProgram, target, spec) } ?: false)

        // Filtering is probed independently of everything above, and deliberately NOT gated on
        // FBO completeness. It has to be: half-float *filtering* is core ES 3.0 while
        // half-float *rendering* is an extension, so on a baseline device R16F is a perfectly
        // filterable texture that cannot be a colour attachment. That combination is exactly
        // the audio-texture role — uploaded, sampled, never drawn into — and gating this probe
        // on attachability would silently push every audio texture down to pre-scaled RGBA8 on
        // every device without a colour-buffer-float extension, which is most of the baseline.
        // The probe renders into the RGBA8 scratch, never into the format under test, so it
        // has no need of that target at all.
        val filtersLinearly =
            !spec.integer &&
                (
                    probeCatching("filter ${format.name}") {
                        filtersLinearly(arena, vao, sampleProgram, spec, scratch)
                    } ?: false
                )

        return FormatProbe(
            attachable = target.complete,
            rendersExactly = rendersExactly,
            blendsAdditively = blendsAdditively,
            filtersLinearly = filtersLinearly,
        )
    }

    /**
     * Draws a known constant and reads it back. The tolerance is per-format because "exact"
     * means different things in 8-bit UNORM and in fp16, and because the 8-bit path has to
     * survive whichever rounding rule the driver picked for 0.5 * 255.
     */
    private fun renderExactly(
        vao: Int,
        program: Int,
        target: ProbeTarget,
        spec: FormatSpec,
    ): Boolean {
        if (program == 0) return false
        bindDraw(target.framebuffer)
        clearFloat()
        GLES30.glUseProgram(program)
        uniform4f(program, "uValue", RENDER_INPUT)
        drawFullscreen(vao)
        val first = readFloats(target.framebuffer, spec, 0) ?: return false
        val last = readFloats(target.framebuffer, spec, PROBE_SIZE * PROBE_SIZE - 1) ?: return false
        return matches(first, RENDER_INPUT, spec) && matches(last, RENDER_INPUT, spec)
    }

    /**
     * The RGBA32UI probe, and the most load-bearing one in the file.
     *
     * It does not check that "some uints came back". It packs known floats with
     * `floatBitsToUint` on the GPU and compares against `Float.toRawBits` on the CPU, so a pass
     * proves the exact invariant the ES 3.0 baseline is built on: state written as float bits
     * in an integer texture survives a round trip bit-for-bit. `highp` on both float and int is
     * what makes that true — a driver honouring `mediump` here would return plausible-looking
     * numbers that are wrong in the low mantissa bits, which is the failure mode a value-range
     * check would sail straight past.
     *
     * The clear goes through `glClearBufferuiv`: `glClearColor` does not apply to an integer
     * attachment, and getting that wrong leaves the target holding whatever the driver felt
     * like, which then reads back as a probe failure for the wrong reason.
     */
    private fun renderPackedExactly(
        vao: Int,
        program: Int,
        target: ProbeTarget,
    ): Boolean {
        if (program == 0) return false
        bindDraw(target.framebuffer)
        GLES30.glClearBufferuiv(GLES30.GL_COLOR, 0, intArrayOf(0, 0, 0, 0), 0)
        GLES30.glUseProgram(program)
        uniform4f(program, "uValue", PACK_INPUT)
        drawFullscreen(vao)
        val expected = IntArray(PACK_INPUT.size) { PACK_INPUT[it].toRawBits() }
        val first = readUints(target.framebuffer, 0) ?: return false
        val last = readUints(target.framebuffer, PROBE_SIZE * PROBE_SIZE - 1) ?: return false
        return first.contentEquals(expected) && last.contentEquals(expected)
    }

    /**
     * Accumulates 0.25 twice with GL_ONE/GL_ONE and requires 0.5 back.
     *
     * This is the probe [FormatPolicy]'s linear-accumulation rung insists on, and the reason it
     * insists: a format that attaches but does not additively blend is the exact driver lie
     * that would otherwise silently break every deposit field. Note what is *not* being probed
     * — a log-packed accumulator — because log(a) + log(b) is log(a*b), so additive blending
     * into a log target multiplies densities instead of summing them. The log curve belongs at
     * display; the accumulator stays linear, and `TexelEncoding` has no logarithmic member so
     * that this cannot be re-litigated at a call site.
     */
    private fun blendsAdditively(
        vao: Int,
        program: Int,
        target: ProbeTarget,
        spec: FormatSpec,
    ): Boolean {
        if (program == 0) return false
        bindDraw(target.framebuffer)
        clearFloat()
        GLES30.glUseProgram(program)
        uniform4f(program, "uValue", BLEND_INPUT)
        GLES30.glEnable(GLES30.GL_BLEND)
        GLES30.glBlendEquation(GLES30.GL_FUNC_ADD)
        GLES30.glBlendFunc(GLES30.GL_ONE, GLES30.GL_ONE)
        drawFullscreen(vao)
        drawFullscreen(vao)
        GLES30.glDisable(GLES30.GL_BLEND)
        val read = readFloats(target.framebuffer, spec, 0) ?: return false
        return matches(read, BLEND_EXPECTED, spec)
    }

    /**
     * Magnifies a two-texel source and samples exactly between the texel centres.
     *
     * The destination is RGBA8, whose renderability is core-mandated, so the filter verdict
     * never depends on the renderability of the format under test — a format can be filterable
     * and not renderable (that is the whole point of the audio-texture role, which is uploaded
     * and never drawn into) and the two probes must not contaminate each other.
     *
     * The accept window is wide on purpose. Filter weights are computed in fixed point with an
     * implementation-defined number of subtexel bits, so the midpoint is 0.5 give or take a
     * couple of LSBs. NEAREST returns 0.0 or 1.0, so anything near the middle is proof of
     * interpolation and the exact weight is not what is being claimed. A format the driver
     * cannot filter makes the texture incomplete instead of raising an error, and an
     * incomplete texture samples as (0,0,0,1) — which lands outside the window, so that case
     * reports "not filterable" without any special handling.
     */
    private fun filtersLinearly(
        arena: GlArena,
        vao: Int,
        program: Int,
        spec: FormatSpec,
        scratch: ProbeTarget?,
    ): Boolean {
        if (program == 0 || scratch == null || !scratch.complete) return false
        val source = arena.filterSource(spec)
        bindDraw(scratch.framebuffer)
        clearFloat()
        GLES30.glUseProgram(program)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, source)
        GLES30.glUniform1i(GLES30.glGetUniformLocation(program, "uSrc"), 0)
        drawFullscreen(vao)
        val read = readFloats(scratch.framebuffer, SPECS.getValue(ProbedFormat.RGBA8), 0) ?: return false
        return read[0] > FILTER_WINDOW_LOW && read[0] < FILTER_WINDOW_HIGH
    }

    /**
     * Does a vertex texture fetch and reads the result back.
     *
     * `GlCapabilities` requires this proof rather than trusting
     * `GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS`, and rightly: the unit count is a promise, and the
     * SwissGL-shaped baseline reads particle state in the *vertex* stage, so a driver that
     * reports units and then samples black would empty every particle field on screen with no
     * error anywhere. `texelFetch` is used rather than `texture` so the result cannot depend on
     * filter state, and the four channels carry four different values so a shader returning a
     * constant cannot pass by accident.
     */
    private fun probeVertexTextureFetch(
        arena: GlArena,
        vao: Int,
        scratch: ProbeTarget?,
    ): Boolean {
        if (scratch == null || !scratch.complete) return false
        // A driver with no vertex texture units is allowed to fail the link rather than the
        // fetch, so a zero program here is itself a negative result, not an error.
        val program = arena.program("vertex texture fetch", VTF_VERT, VTF_FRAG)
        if (program == 0) return false
        val source = arena.vertexFetchSource()
        bindDraw(scratch.framebuffer)
        clearFloat()
        GLES30.glUseProgram(program)
        GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, source)
        GLES30.glUniform1i(GLES30.glGetUniformLocation(program, "uSrc"), 0)
        drawFullscreen(vao)
        val read = readFloats(scratch.framebuffer, SPECS.getValue(ProbedFormat.RGBA8), 0) ?: return false
        return matches(read, VERTEX_FETCH_TEXEL, SPECS.getValue(ProbedFormat.RGBA8))
    }

    /**
     * The behavioural half of the timer-query trust ladder: present -> untrusted -> trusted.
     *
     * A timing is trusted only if the query completes, reports a non-zero duration inside a
     * plausible range, and `GL_GPU_DISJOINT_EXT` is clear — a disjoint event means the GPU was
     * preempted or clocked down mid-query and every timing that straddles it is garbage. Which
     * is the point of the ladder: an adaptive quality controller fed garbage timings drops
     * tiers on a device that was never slow.
     *
     * The 32-bit getter is used because that is what `android.opengl` exposes; the extension's
     * 64-bit entry points have no binding. A 4x4 draw is microseconds, and the ceiling below is
     * two orders of magnitude under the 4.29 s that a nanosecond count needs to wrap, so the
     * truncation cannot turn a real timing into a fake one.
     */
    private fun probeTimerQuery(
        arena: GlArena,
        vao: Int,
        program: Int,
        scratch: ProbeTarget?,
    ): Boolean {
        if (program == 0 || scratch == null || !scratch.complete) return false
        val query = arena.query()
        drainErrors()
        GLES30.glBeginQuery(GL_TIME_ELAPSED_EXT, query)
        if (GLES30.glGetError() != GLES30.GL_NO_ERROR) return false
        bindDraw(scratch.framebuffer)
        clearFloat()
        GLES30.glUseProgram(program)
        uniform4f(program, "uValue", RENDER_INPUT)
        drawFullscreen(vao)
        GLES30.glEndQuery(GL_TIME_ELAPSED_EXT)
        if (GLES30.glGetError() != GLES30.GL_NO_ERROR) return false

        val out = IntArray(1)
        var ready = false
        for (spin in 0 until QUERY_POLL_LIMIT) {
            GLES30.glGetQueryObjectuiv(query, GLES30.GL_QUERY_RESULT_AVAILABLE, out, 0)
            if (out[0] != 0) {
                ready = true
                break
            }
            // One glFinish rather than a sleep: this runs on the GL thread before the first
            // frame, so blocking on the GPU is honest and bounded, whereas a spin with no
            // flush can wait on work that was never submitted.
            if (spin == 0) GLES30.glFinish()
        }
        if (!ready) return false
        GLES30.glGetQueryObjectuiv(query, GLES30.GL_QUERY_RESULT, out, 0)
        val elapsedNs = out[0].toLong() and 0xFFFF_FFFFL
        // Reading the disjoint flag also clears it, which is the extension's documented way of
        // asking "was anything disjoint since I last asked".
        val disjoint = limit(GL_GPU_DISJOINT_EXT) != 0
        return !disjoint && elapsedNs > 0L && elapsedNs < TIMER_CEILING_NS
    }

    // ------------------------------------------------------------------------- GL plumbing

    private fun bindDraw(framebuffer: Int) {
        GLES30.glBindFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER, framebuffer)
        GLES30.glViewport(0, 0, PROBE_SIZE, PROBE_SIZE)
    }

    /** `glClearBufferfv` rather than `glClear`, so the context's clear colour is never touched. */
    private fun clearFloat() {
        GLES30.glClearBufferfv(GLES30.GL_COLOR, 0, floatArrayOf(0f, 0f, 0f, 0f), 0)
    }

    private fun drawFullscreen(vao: Int) {
        GLES30.glBindVertexArray(vao)
        GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)
        GLES30.glBindVertexArray(0)
    }

    private fun uniform4f(
        program: Int,
        name: String,
        values: FloatArray,
    ) {
        val location = GLES30.glGetUniformLocation(program, name)
        if (location >= 0) GLES30.glUniform4fv(location, 1, values, 0)
    }

    private fun string(name: Int): String = GLES30.glGetString(name).orEmpty()

    /**
     * Reads an integer limit, answering 0 for anything the driver rejects.
     *
     * `glGetIntegerv` leaves the output untouched when it raises GL_INVALID_ENUM, so without
     * the error check an unknown pname would return whatever the array held — which for a
     * reused buffer is the *previous* limit, and that is how a 3.0 device ends up claiming
     * 3.1-sized compute limits.
     */
    private fun limit(pname: Int): Int {
        drainErrors()
        val out = IntArray(1)
        GLES30.glGetIntegerv(pname, out, 0)
        return if (GLES30.glGetError() != GLES30.GL_NO_ERROR) 0 else out[0]
    }

    /**
     * ES 3.0 replaced the space-separated `GL_EXTENSIONS` string with indexed queries but kept
     * the string, and drivers have been seen listing an extension in one and not the other.
     * The union is the honest set; a name appearing twice costs nothing.
     */
    private fun extensions(): Set<String> {
        val out = mutableSetOf<String>()
        val count = limit(GLES30.GL_NUM_EXTENSIONS)
        for (index in 0 until count) {
            GLES30.glGetStringi(GLES30.GL_EXTENSIONS, index)?.let { if (it.isNotEmpty()) out += it }
        }
        string(GLES30.GL_EXTENSIONS).split(' ').forEach { if (it.isNotEmpty()) out += it }
        drainErrors()
        return out
    }

    private fun drainErrors() {
        var drained = 0
        while (GLES30.glGetError() != GLES30.GL_NO_ERROR && drained < ERROR_DRAIN_LIMIT) drained++
    }

    /**
     * Reads one texel back as floats.
     *
     * ES 3.0 guarantees exactly two `glReadPixels` pairs: RGBA/UNSIGNED_BYTE for a normalized
     * target and RGBA_INTEGER/UNSIGNED_INT for an integer one. For a float target the only
     * guaranteed pair is whatever the driver names in GL_IMPLEMENTATION_COLOR_READ_FORMAT and
     * _TYPE, which has to be queried with the target bound. A pair this function cannot decode
     * makes the probe fail: an unreadable target is not a proven target, and the policy's whole
     * contract is that a rung is taken only on proof.
     */
    private fun readFloats(
        framebuffer: Int,
        spec: FormatSpec,
        texel: Int,
    ): FloatArray? {
        GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER, framebuffer)
        drainErrors()
        val format: Int
        val type: Int
        if (spec.internalFormat == GLES30.GL_RGBA8) {
            format = GLES30.GL_RGBA
            type = GLES30.GL_UNSIGNED_BYTE
        } else {
            format = limit(GLES30.GL_IMPLEMENTATION_COLOR_READ_FORMAT)
            type = limit(GLES30.GL_IMPLEMENTATION_COLOR_READ_TYPE)
        }
        val components = componentsOf(format)
        val bytes = bytesOf(type)
        if (components == 0 || bytes == 0) return null
        val stride = components * bytes
        val buffer = directBuffer(PROBE_SIZE * PROBE_SIZE * stride)
        GLES30.glReadPixels(0, 0, PROBE_SIZE, PROBE_SIZE, format, type, buffer)
        if (GLES30.glGetError() != GLES30.GL_NO_ERROR) return null
        // Components the read format does not carry default to (0, 0, 0, 1), matching what GL
        // itself substitutes for missing channels.
        val out = floatArrayOf(0f, 0f, 0f, 1f)
        val base = texel * stride
        for (channel in 0 until minOf(components, 4)) {
            val at = base + channel * bytes
            out[channel] =
                when (type) {
                    GLES30.GL_UNSIGNED_BYTE -> (buffer.get(at).toInt() and 0xFF) / 255f
                    GLES30.GL_HALF_FLOAT -> Half.toFloat(buffer.getShort(at))
                    GLES30.GL_FLOAT -> buffer.getFloat(at)
                    else -> return null
                }
        }
        return out
    }

    /** RGBA_INTEGER/UNSIGNED_INT is spec-guaranteed for an unsigned-integer colour buffer. */
    private fun readUints(
        framebuffer: Int,
        texel: Int,
    ): IntArray? {
        GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER, framebuffer)
        drainErrors()
        val buffer = directBuffer(PROBE_SIZE * PROBE_SIZE * 4 * Int.SIZE_BYTES)
        GLES30.glReadPixels(
            0,
            0,
            PROBE_SIZE,
            PROBE_SIZE,
            GLES30.GL_RGBA_INTEGER,
            GLES30.GL_UNSIGNED_INT,
            buffer,
        )
        if (GLES30.glGetError() != GLES30.GL_NO_ERROR) return null
        val base = texel * 4 * Int.SIZE_BYTES
        return IntArray(4) { buffer.getInt(base + it * Int.SIZE_BYTES) }
    }

    private fun directBuffer(bytes: Int): ByteBuffer = ByteBuffer.allocateDirect(bytes).order(ByteOrder.nativeOrder())

    private fun matches(
        read: FloatArray,
        expected: FloatArray,
        spec: FormatSpec,
    ): Boolean = (0 until spec.components).all { abs(read[it] - expected[it]) <= spec.tolerance }

    private fun componentsOf(format: Int): Int =
        when (format) {
            GLES30.GL_RED, GLES30.GL_RED_INTEGER -> 1
            GLES30.GL_RG, GLES30.GL_RG_INTEGER -> 2
            GLES30.GL_RGB, GLES30.GL_RGB_INTEGER -> 3
            GLES30.GL_RGBA, GLES30.GL_RGBA_INTEGER -> 4
            else -> 0
        }

    private fun bytesOf(type: Int): Int =
        when (type) {
            GLES30.GL_UNSIGNED_BYTE, GLES30.GL_BYTE -> 1
            GLES30.GL_HALF_FLOAT, GLES30.GL_UNSIGNED_SHORT, GLES30.GL_SHORT -> 2
            GLES30.GL_FLOAT, GLES30.GL_UNSIGNED_INT, GLES30.GL_INT -> 4
            else -> 0
        }

    /**
     * Runs one probe step, degrading a failure to null rather than to a crashed process.
     *
     * `Throwable` and not `Exception`: a missing GL entry point surfaces as an `Error`, and a
     * startup probe is the last place that should be fatal. Cancellation is rethrown because it
     * is control flow, not a failed measurement.
     */
    @Suppress("TooGenericExceptionCaught", "InstanceOfCheckForException")
    private inline fun <T> probeCatching(
        what: String,
        block: () -> T,
    ): T? =
        try {
            block()
        } catch (t: Throwable) {
            if (t is CancellationException) throw t
            Log.w(TAG, "GL probe step '$what' failed; recording it as unsupported", t)
            null
        }

    // ------------------------------------------------------------------- objects and state

    private class ProbeTarget(
        val texture: Int,
        val framebuffer: Int,
        val complete: Boolean,
    )

    /**
     * Owns every GL object the probe creates so that [releaseAll] is the single place teardown
     * can go wrong, instead of a `finally` block per probe. This runs on the GL thread at
     * startup: an object leaked here is leaked for the whole session.
     */
    private class GlArena {
        private val textures = mutableListOf<Int>()
        private val framebuffers = mutableListOf<Int>()
        private val vertexArrays = mutableListOf<Int>()
        private val programs = mutableListOf<Int>()
        private val queries = mutableListOf<Int>()

        fun vertexArray(): Int {
            val ids = IntArray(1)
            GLES30.glGenVertexArrays(1, ids, 0)
            // Bound once so it exists as an object with no enabled attribute arrays. The probe
            // shaders build their geometry from gl_VertexID, so this VAO's only job is to keep
            // the draw away from whatever arrays the caller had enabled.
            GLES30.glBindVertexArray(ids[0])
            GLES30.glBindVertexArray(0)
            vertexArrays += ids[0]
            return ids[0]
        }

        fun query(): Int {
            val ids = IntArray(1)
            GLES30.glGenQueries(1, ids, 0)
            queries += ids[0]
            return ids[0]
        }

        fun target(spec: FormatSpec): ProbeTarget {
            val texture = storage(spec, PROBE_SIZE, PROBE_SIZE, GLES30.GL_NEAREST)
            val ids = IntArray(1)
            GLES30.glGenFramebuffers(1, ids, 0)
            framebuffers += ids[0]
            GLES30.glBindFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER, ids[0])
            GLES30.glFramebufferTexture2D(
                GLES30.GL_DRAW_FRAMEBUFFER,
                GLES30.GL_COLOR_ATTACHMENT0,
                GLES30.GL_TEXTURE_2D,
                texture,
                0,
            )
            val status = GLES30.glCheckFramebufferStatus(GLES30.GL_DRAW_FRAMEBUFFER)
            drainErrors()
            return ProbeTarget(texture, ids[0], status == GLES30.GL_FRAMEBUFFER_COMPLETE)
        }

        /** Two texels, 0.0 then 1.0 in the first channel, filtered LINEAR. */
        fun filterSource(spec: FormatSpec): Int {
            val texture = storage(spec, FILTER_WIDTH, FILTER_HEIGHT, GLES30.GL_LINEAR)
            val texels = FloatArray(FILTER_WIDTH * spec.components)
            texels[0] = 0f
            texels[spec.components] = 1f
            upload(spec, FILTER_WIDTH, FILTER_HEIGHT, texels)
            return texture
        }

        /** One RGBA8 texel with four distinct channels, read by the vertex stage. */
        fun vertexFetchSource(): Int {
            val spec = SPECS.getValue(ProbedFormat.RGBA8)
            val texture = storage(spec, 1, 1, GLES30.GL_NEAREST)
            upload(spec, 1, 1, VERTEX_FETCH_TEXEL)
            return texture
        }

        fun program(
            what: String,
            vertexSource: String,
            fragmentSource: String,
        ): Int {
            val vertex = compile(GLES30.GL_VERTEX_SHADER, vertexSource)
            val fragment = compile(GLES30.GL_FRAGMENT_SHADER, fragmentSource)
            if (vertex == 0 || fragment == 0) {
                if (vertex != 0) GLES30.glDeleteShader(vertex)
                if (fragment != 0) GLES30.glDeleteShader(fragment)
                Log.w(TAG, "probe shader for '$what' did not compile; that probe reports unsupported")
                return 0
            }
            val program = GLES30.glCreateProgram()
            GLES30.glAttachShader(program, vertex)
            GLES30.glAttachShader(program, fragment)
            GLES30.glLinkProgram(program)
            // The shaders are deleted immediately: they are flagged for deletion and go away
            // with the program, so the arena only has to track one object per program.
            GLES30.glDeleteShader(vertex)
            GLES30.glDeleteShader(fragment)
            val status = IntArray(1)
            GLES30.glGetProgramiv(program, GLES30.GL_LINK_STATUS, status, 0)
            if (status[0] == 0) {
                Log.w(TAG, "probe program '$what' did not link: ${GLES30.glGetProgramInfoLog(program)}")
                GLES30.glDeleteProgram(program)
                return 0
            }
            programs += program
            return program
        }

        fun releaseAll() {
            bestEffort(TAG, "delete probe textures") {
                deleteAll(textures) { n, ids, offset -> GLES30.glDeleteTextures(n, ids, offset) }
            }
            bestEffort(TAG, "delete probe framebuffers") {
                deleteAll(framebuffers) { n, ids, offset -> GLES30.glDeleteFramebuffers(n, ids, offset) }
            }
            bestEffort(TAG, "delete probe vertex arrays") {
                deleteAll(vertexArrays) { n, ids, offset -> GLES30.glDeleteVertexArrays(n, ids, offset) }
            }
            bestEffort(TAG, "delete probe queries") {
                deleteAll(queries) { n, ids, offset -> GLES30.glDeleteQueries(n, ids, offset) }
            }
            bestEffort(TAG, "delete probe programs") {
                programs.forEach { GLES30.glDeleteProgram(it) }
                programs.clear()
            }
            GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, 0)
        }

        private fun deleteAll(
            ids: MutableList<Int>,
            delete: (Int, IntArray, Int) -> Unit,
        ) {
            if (ids.isNotEmpty()) delete(ids.size, ids.toIntArray(), 0)
            ids.clear()
        }

        /**
         * `glTexStorage2D` rather than `glTexImage2D`: immutable storage takes the sized
         * internal format on its own, so the probe cannot accidentally test an
         * internalformat/format/type triple the driver reinterprets into a different format
         * than the one being probed.
         */
        private fun storage(
            spec: FormatSpec,
            width: Int,
            height: Int,
            filter: Int,
        ): Int {
            val ids = IntArray(1)
            GLES30.glGenTextures(1, ids, 0)
            textures += ids[0]
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
            GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, ids[0])
            GLES30.glTexStorage2D(GLES30.GL_TEXTURE_2D, 1, spec.internalFormat, width, height)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, filter)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, filter)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
            GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
            return ids[0]
        }

        private fun upload(
            spec: FormatSpec,
            width: Int,
            height: Int,
            texels: FloatArray,
        ) {
            val bytes = bytesOf(spec.uploadType)
            val buffer = directBuffer(texels.size * bytes)
            texels.forEach { value ->
                when (spec.uploadType) {
                    GLES30.GL_UNSIGNED_BYTE -> buffer.put((value * 255f).toInt().coerceIn(0, 255).toByte())
                    GLES30.GL_HALF_FLOAT -> buffer.putShort(Half.toHalf(value))
                    GLES30.GL_FLOAT -> buffer.putFloat(value)
                    else -> Unit
                }
            }
            buffer.position(0)
            GLES30.glTexSubImage2D(
                GLES30.GL_TEXTURE_2D,
                0,
                0,
                0,
                width,
                height,
                spec.uploadFormat,
                spec.uploadType,
                buffer,
            )
            drainErrors()
        }

        private fun compile(
            type: Int,
            source: String,
        ): Int {
            val shader = GLES30.glCreateShader(type)
            GLES30.glShaderSource(shader, source)
            GLES30.glCompileShader(shader)
            val status = IntArray(1)
            GLES30.glGetShaderiv(shader, GLES30.GL_COMPILE_STATUS, status, 0)
            if (status[0] == 0) {
                Log.w(TAG, "probe shader did not compile: ${GLES30.glGetShaderInfoLog(shader)}")
                GLES30.glDeleteShader(shader)
                return 0
            }
            return shader
        }
    }

    /**
     * Captures the slice of context state the probe writes, neutralises it, and puts it back.
     *
     * The probe can run on a throwaway context where all of this is already at its default —
     * but it can also run on the app's own context in `onSurfaceCreated`, and there it runs
     * before the first frame. Anything left changed here becomes a rendering bug in a file that
     * never mentions probing.
     *
     * Three of these are easy to miss and each one silently corrupts a *verdict* rather than
     * causing an error:
     * - a **sampler object** bound to unit 0 overrides the texture's own filter parameters, so
     *   the filter probe would measure the caller's sampler instead of the format;
     * - **dithering** is enabled by default and perturbs 8-bit writes, which is exactly the
     *   exactness the RGBA8 probe claims to measure;
     * - a **pixel pack buffer** left bound redirects `glReadPixels` into that buffer, so every
     *   readback would return whatever the probe's own scratch memory happened to hold.
     */
    private class GlStateGuard private constructor() {
        private val drawFramebuffer = getInt(GLES30.GL_DRAW_FRAMEBUFFER_BINDING)
        private val readFramebuffer = getInt(GLES30.GL_READ_FRAMEBUFFER_BINDING)
        private val program = getInt(GLES30.GL_CURRENT_PROGRAM)
        private val vertexArray = getInt(GLES30.GL_VERTEX_ARRAY_BINDING)
        private val packBuffer = getInt(GLES30.GL_PIXEL_PACK_BUFFER_BINDING)
        private val unpackBuffer = getInt(GLES30.GL_PIXEL_UNPACK_BUFFER_BINDING)
        private val activeTexture = getInt(GLES30.GL_ACTIVE_TEXTURE)
        private var unitTexture = 0
        private var unitSampler = 0
        private val viewport = IntArray(4)
        private val colorMask = BooleanArray(4)
        private val blend = GLES30.glIsEnabled(GLES30.GL_BLEND)
        private val scissor = GLES30.glIsEnabled(GLES30.GL_SCISSOR_TEST)
        private val depth = GLES30.glIsEnabled(GLES30.GL_DEPTH_TEST)
        private val stencil = GLES30.glIsEnabled(GLES30.GL_STENCIL_TEST)
        private val cull = GLES30.glIsEnabled(GLES30.GL_CULL_FACE)
        private val dither = GLES30.glIsEnabled(GLES30.GL_DITHER)
        private val blendSrcRgb = getInt(GLES30.GL_BLEND_SRC_RGB)
        private val blendDstRgb = getInt(GLES30.GL_BLEND_DST_RGB)
        private val blendSrcAlpha = getInt(GLES30.GL_BLEND_SRC_ALPHA)
        private val blendDstAlpha = getInt(GLES30.GL_BLEND_DST_ALPHA)
        private val blendEquationRgb = getInt(GLES30.GL_BLEND_EQUATION_RGB)
        private val blendEquationAlpha = getInt(GLES30.GL_BLEND_EQUATION_ALPHA)
        private val packAlignment = getInt(GLES30.GL_PACK_ALIGNMENT)
        private val unpackAlignment = getInt(GLES30.GL_UNPACK_ALIGNMENT)
        private val packRowLength = getInt(GLES30.GL_PACK_ROW_LENGTH)
        private val packSkipPixels = getInt(GLES30.GL_PACK_SKIP_PIXELS)
        private val packSkipRows = getInt(GLES30.GL_PACK_SKIP_ROWS)
        private val unpackRowLength = getInt(GLES30.GL_UNPACK_ROW_LENGTH)
        private val unpackSkipPixels = getInt(GLES30.GL_UNPACK_SKIP_PIXELS)
        private val unpackSkipRows = getInt(GLES30.GL_UNPACK_SKIP_ROWS)

        init {
            GLES30.glGetIntegerv(GLES30.GL_VIEWPORT, viewport, 0)
            GLES30.glGetBooleanv(GLES30.GL_COLOR_WRITEMASK, colorMask, 0)
            GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
            unitTexture = getInt(GLES30.GL_TEXTURE_BINDING_2D)
            unitSampler = getInt(GLES30.GL_SAMPLER_BINDING)
        }

        private fun neutralise() {
            GLES30.glBindSampler(0, 0)
            GLES30.glDisable(GLES30.GL_DITHER)
            GLES30.glDisable(GLES30.GL_BLEND)
            GLES30.glDisable(GLES30.GL_SCISSOR_TEST)
            GLES30.glDisable(GLES30.GL_DEPTH_TEST)
            GLES30.glDisable(GLES30.GL_STENCIL_TEST)
            GLES30.glDisable(GLES30.GL_CULL_FACE)
            GLES30.glColorMask(true, true, true, true)
            GLES30.glBlendEquation(GLES30.GL_FUNC_ADD)
            GLES30.glBindBuffer(GLES30.GL_PIXEL_PACK_BUFFER, 0)
            GLES30.glBindBuffer(GLES30.GL_PIXEL_UNPACK_BUFFER, 0)
            // Alignment 1 on both sides: the probe's rows are 4 to 64 bytes wide depending on
            // the format under test, and a stale alignment of 4 would silently shift every row
            // of an R16F readback.
            GLES30.glPixelStorei(GLES30.GL_PACK_ALIGNMENT, 1)
            GLES30.glPixelStorei(GLES30.GL_UNPACK_ALIGNMENT, 1)
            GLES30.glPixelStorei(GLES30.GL_PACK_ROW_LENGTH, 0)
            GLES30.glPixelStorei(GLES30.GL_PACK_SKIP_PIXELS, 0)
            GLES30.glPixelStorei(GLES30.GL_PACK_SKIP_ROWS, 0)
            GLES30.glPixelStorei(GLES30.GL_UNPACK_ROW_LENGTH, 0)
            GLES30.glPixelStorei(GLES30.GL_UNPACK_SKIP_PIXELS, 0)
            GLES30.glPixelStorei(GLES30.GL_UNPACK_SKIP_ROWS, 0)
            drainErrors()
        }

        fun restore() {
            bestEffort(TAG, "restore GL state after probing") {
                // glBindTexture applies to whichever unit is active, so the unit is set
                // explicitly rather than assumed from wherever the probe happened to leave it.
                GLES30.glActiveTexture(GLES30.GL_TEXTURE0)
                GLES30.glBindSampler(0, unitSampler)
                GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, unitTexture)
                GLES30.glActiveTexture(activeTexture)
                GLES30.glUseProgram(program)
                GLES30.glBindVertexArray(vertexArray)
                GLES30.glBindBuffer(GLES30.GL_PIXEL_PACK_BUFFER, packBuffer)
                GLES30.glBindBuffer(GLES30.GL_PIXEL_UNPACK_BUFFER, unpackBuffer)
                GLES30.glBindFramebuffer(GLES30.GL_DRAW_FRAMEBUFFER, drawFramebuffer)
                GLES30.glBindFramebuffer(GLES30.GL_READ_FRAMEBUFFER, readFramebuffer)
                GLES30.glViewport(viewport[0], viewport[1], viewport[2], viewport[3])
                GLES30.glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3])
                GLES30.glBlendFuncSeparate(blendSrcRgb, blendDstRgb, blendSrcAlpha, blendDstAlpha)
                GLES30.glBlendEquationSeparate(blendEquationRgb, blendEquationAlpha)
                setEnabled(GLES30.GL_BLEND, blend)
                setEnabled(GLES30.GL_SCISSOR_TEST, scissor)
                setEnabled(GLES30.GL_DEPTH_TEST, depth)
                setEnabled(GLES30.GL_STENCIL_TEST, stencil)
                setEnabled(GLES30.GL_CULL_FACE, cull)
                setEnabled(GLES30.GL_DITHER, dither)
                GLES30.glPixelStorei(GLES30.GL_PACK_ALIGNMENT, packAlignment)
                GLES30.glPixelStorei(GLES30.GL_UNPACK_ALIGNMENT, unpackAlignment)
                GLES30.glPixelStorei(GLES30.GL_PACK_ROW_LENGTH, packRowLength)
                GLES30.glPixelStorei(GLES30.GL_PACK_SKIP_PIXELS, packSkipPixels)
                GLES30.glPixelStorei(GLES30.GL_PACK_SKIP_ROWS, packSkipRows)
                GLES30.glPixelStorei(GLES30.GL_UNPACK_ROW_LENGTH, unpackRowLength)
                GLES30.glPixelStorei(GLES30.GL_UNPACK_SKIP_PIXELS, unpackSkipPixels)
                GLES30.glPixelStorei(GLES30.GL_UNPACK_SKIP_ROWS, unpackSkipRows)
            }
        }

        private fun setEnabled(
            capability: Int,
            enabled: Boolean,
        ) {
            if (enabled) GLES30.glEnable(capability) else GLES30.glDisable(capability)
        }

        companion object {
            fun captureAndNeutralise(): GlStateGuard = GlStateGuard().also { it.neutralise() }

            private fun getInt(pname: Int): Int {
                val out = IntArray(1)
                GLES30.glGetIntegerv(pname, out, 0)
                return out[0]
            }
        }
    }

    // ------------------------------------------------------------------------ probe inputs

    private class FormatSpec(
        val internalFormat: Int,
        val uploadFormat: Int,
        val uploadType: Int,
        val components: Int,
        val tolerance: Float,
        val integer: Boolean,
    )

    private val SPECS: Map<ProbedFormat, FormatSpec> =
        mapOf(
            // 2/255 covers whichever rounding rule the driver picked for 0.5 * 255 (127 or 128)
            // plus one LSB of slack; an 8-bit target that is off by more than that is not
            // rendering the value it was asked for.
            ProbedFormat.RGBA8 to
                FormatSpec(GLES30.GL_RGBA8, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, 4, 2f / 255f, false),
            // The probe constants (0.25, 0.5, 0.75, 1.0) are all exact in fp16, so the only
            // slack needed is for a driver that rounds through a different intermediate.
            ProbedFormat.R16F to
                FormatSpec(GLES30.GL_R16F, GLES30.GL_RED, GLES30.GL_HALF_FLOAT, 1, 1e-3f, false),
            ProbedFormat.RG16F to
                FormatSpec(GLES30.GL_RG16F, GLES30.GL_RG, GLES30.GL_HALF_FLOAT, 2, 1e-3f, false),
            ProbedFormat.RGBA16F to
                FormatSpec(GLES30.GL_RGBA16F, GLES30.GL_RGBA, GLES30.GL_HALF_FLOAT, 4, 1e-3f, false),
            ProbedFormat.R32F to
                FormatSpec(GLES30.GL_R32F, GLES30.GL_RED, GLES30.GL_FLOAT, 1, 1e-6f, false),
            // Integer: no tolerance is meaningful, the comparison is bit-exact and `components`
            // is unused because `matches` is never called for it.
            ProbedFormat.RGBA32UI to
                FormatSpec(GLES30.GL_RGBA32UI, GLES30.GL_RGBA_INTEGER, GLES30.GL_UNSIGNED_INT, 4, 0f, true),
        )

    private val FAILED =
        FormatProbe(
            attachable = false,
            rendersExactly = false,
            blendsAdditively = false,
            filtersLinearly = false,
        )

    private val RENDER_INPUT = floatArrayOf(0.25f, 0.5f, 0.75f, 1.0f)

    /** 0.25 twice with GL_ONE/GL_ONE must land on 0.5 — including in alpha, which is why every
     * channel is 0.25 rather than the render probe's ramp: an alpha of 1.0 added to itself
     * clamps at 1.0 in a normalized target and would read as a blend failure. */
    private val BLEND_INPUT = floatArrayOf(0.25f, 0.25f, 0.25f, 0.25f)
    private val BLEND_EXPECTED = floatArrayOf(0.5f, 0.5f, 0.5f, 0.5f)

    /**
     * Four exactly-representable, normal floats with distinct bit patterns — one negative, one
     * below 1.0, two above — so a driver that drops the sign bit or flushes anything shows up.
     * No NaN and no denormal: those are the two cases a conformant driver is allowed to mangle,
     * and probing them would fail devices that are actually fine.
     */
    private val PACK_INPUT = floatArrayOf(1.5f, -2.25f, 3.75f, 0.5f)

    private val VERTEX_FETCH_TEXEL = floatArrayOf(64f / 255f, 128f / 255f, 192f / 255f, 1f)

    private const val FILTER_WINDOW_LOW = 0.30f
    private const val FILTER_WINDOW_HIGH = 0.70f

    /**
     * `precision highp float; precision highp int;` is the §6.3 header rule, not decoration:
     * the packed-state path needs `floatBitsToUint` to be bit-exact, and a mediump int would
     * quietly truncate the mantissa. Geometry comes from `gl_VertexID` so no probe needs a
     * vertex buffer, an attribute, or the caller's array state.
     */
    private const val FULLSCREEN_VERT =
        """#version 300 es
precision highp float;
precision highp int;
void main() {
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
"""

    private const val FLOAT_FRAG =
        """#version 300 es
precision highp float;
precision highp int;
uniform vec4 uValue;
out vec4 fragColor;
void main() {
    fragColor = uValue;
}
"""

    /** `out uvec4` and `floatBitsToUint` — the two consequences §6.3 names for RGBA32UI state. */
    private const val UINT_FRAG =
        """#version 300 es
precision highp float;
precision highp int;
uniform vec4 uValue;
out uvec4 fragColor;
void main() {
    fragColor = floatBitsToUint(uValue);
}
"""

    private const val SAMPLE_FRAG =
        """#version 300 es
precision highp float;
precision highp int;
uniform sampler2D uSrc;
out vec4 fragColor;
void main() {
    fragColor = vec4(texture(uSrc, vec2(0.5, 0.5)).r, 0.0, 0.0, 1.0);
}
"""

    private const val VTF_VERT =
        """#version 300 es
precision highp float;
precision highp int;
uniform sampler2D uSrc;
out vec4 vFetched;
void main() {
    vFetched = texelFetch(uSrc, ivec2(0, 0), 0);
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
"""

    private const val VTF_FRAG =
        """#version 300 es
precision highp float;
precision highp int;
in vec4 vFetched;
out vec4 fragColor;
void main() {
    fragColor = vFetched;
}
"""
}
