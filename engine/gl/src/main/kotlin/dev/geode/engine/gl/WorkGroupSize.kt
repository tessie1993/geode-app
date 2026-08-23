package dev.geode.engine.gl

/**
 * How many work groups a dispatch needs to cover a grid. A separate type from [WorkGroupSize]
 * on purpose: a *count* of groups and a *size* of a group are both (x, y, z) triples of
 * positive integers and are checked against completely different limits
 * (`GL_MAX_COMPUTE_WORK_GROUP_COUNT` vs `GL_MAX_COMPUTE_WORK_GROUP_SIZE`). Sharing one type
 * would make passing the wrong one a silent `GL_INVALID_VALUE` at dispatch instead of a
 * compile error here.
 */
data class WorkGroupCount(
    val x: Int,
    val y: Int,
    val z: Int,
) {
    companion object {
        /**
         * ES 3.1's mandated minimum for `GL_MAX_COMPUTE_WORK_GROUP_COUNT`. A context below it
         * is not a small 3.1 device, it is a context whose version string is not evidence.
         */
        val SPEC_FLOOR = WorkGroupCount(x = 65535, y = 65535, z = 65535)
    }
}

/**
 * The `layout(local_size_x = ..., local_size_y = ...)` a compute step is compiled with.
 *
 * ### Why this is a value and not a constant
 *
 * The local size is a **compile-time constant inside the shader** — GLSL ES has no way to set
 * it from the host, so it has to be substituted into the source text before compilation, and a
 * program is therefore specialised to one size for its whole life. That is the reason this
 * type exists rather than a pair of ints threaded through: the size chosen from the device's
 * limits and the size written into the source must be the same value, and the only way to
 * guarantee that is for one object to be the source of both. [ComputeProgram] re-reads
 * `GL_COMPUTE_WORK_GROUP_SIZE` off the linked program and refuses a mismatch, which is the
 * check that catches a substitution that silently did not happen.
 *
 * ### Why 64 invocations
 *
 * 64 is the target because it is the smallest number that is a whole scheduling unit on every
 * GPU family this app ships to, and being an exact multiple of the hardware unit is what stops
 * a group from paying for lanes it never uses:
 *
 * - **Adreno** issues a wave of 64 fp32 lanes. A 64-invocation group is exactly one wave; a
 *   65-invocation group is two, the second one 1/64 useful.
 * - **Mali Valhall** runs 16-lane warps, Bifrost 4-lane quads. 64 is 4 warps or 16 quads —
 *   whole either way.
 * - **PowerVR** runs 32-wide. 64 is two.
 *
 * Going larger is not free. Occupancy is bounded by registers per invocation, not by group
 * size, so a 256-invocation group of a register-hungry step does not run 4x wider — it runs
 * the same width with a 4x coarser tail, and the tail is pure waste on any grid that is not a
 * multiple of the group. The step kernels here (read one texel, read a few neighbours, write
 * one texel) have no shared-memory reuse to amortise a bigger group, so the default stays 64.
 * A kernel that *does* cache a halo in `shared` memory has a real reason to ask for more, and
 * that is what the preferred-invocation parameter of [forGrid] is for.
 *
 * ### Why square, and why powers of two
 *
 * A step that reads its neighbours pays for a halo around each group's tile: an 8x8 tile with a
 * one-texel halo touches 100 texels for 64 results (1.56x), a 64x1 tile touches 198 (3.09x).
 * Square minimises the perimeter, and on a tiled/swizzled texture layout it also keeps each
 * group's fetches inside a small number of cache lines instead of striping across the whole
 * width. Powers of two keep rows aligned to those lines; a 5x13 group is legal and pointless.
 */
data class WorkGroupSize(
    val x: Int,
    val y: Int,
    val z: Int,
) {
    val invocations: Int get() = x * y * z

    /** The GLSL layout line this size compiles to. The single place the substitution happens. */
    val layoutQualifier: String
        get() = "layout(local_size_x = $x, local_size_y = $y, local_size_z = $z) in;"

    /**
     * Groups needed to cover [width] x [height]. Rounds **up**, so the last group in each axis
     * over-runs the grid — which is why every generated compute main() starts with a bounds
     * guard. Call this when the grid changes, not per frame: it allocates.
     */
    fun groupsFor(
        width: Int,
        height: Int,
    ): WorkGroupCount =
        WorkGroupCount(
            x = ceilDiv(width, x),
            y = ceilDiv(height, y),
            z = 1,
        )

    override fun toString(): String = "${x}x${y}x$z"

    companion object {
        /** See the class doc: one Adreno wave, four Valhall warps, two PowerVR pipelines. */
        const val TARGET_INVOCATIONS = 64

        /**
         * ES 3.1's mandated minimum for `GL_MAX_COMPUTE_WORK_GROUP_SIZE`. A context reporting
         * less than this is not a small 3.1 device, it is a context whose version string is not
         * evidence — [ComputeSupport] treats it as no compute at all.
         */
        val SPEC_FLOOR = WorkGroupSize(x = 128, y = 128, z = 64)

        /**
         * Picks a 2D local size for a grid-parallel step: as close to [preferredInvocations] as
         * the device allows, as square as the per-axis limits allow, powers of two throughout.
         *
         * **Deliberately not a function of the grid.** Clamping the group to a small grid would
         * make the local size change on resize, and since the size is baked into the shader
         * source that means recompiling the program every time the simulation resolution
         * changes — during a quality-profile step, on rotation, on every wallpaper surface
         * recreate. The tail waste it would save is bounded by one group per axis (at 8x8 on a
         * 320x180 grid: 2% of invocations, all of which exit at the bounds guard before doing
         * any work). Recompiling a program mid-animation costs more than that, every time.
         *
         * The loop grows the smaller axis first, so it walks 1x1 -> 2x1 -> 2x2 -> 4x2 -> 4x4 ->
         * 8x4 -> 8x8 and stops at the budget. If one axis is capped below the other it keeps
         * doubling the axis that still has room, degrading to a 1D group rather than failing.
         */
        fun forGrid(
            limits: ComputeLimits,
            preferredInvocations: Int = TARGET_INVOCATIONS,
        ): WorkGroupSize {
            val budget =
                largestPowerOfTwoAtMost(
                    preferredInvocations.coerceIn(1, limits.maxInvocationsPerGroup.coerceAtLeast(1)),
                )
            var x = 1
            var y = 1
            while (x * y * 2 <= budget) {
                val growX = x <= y && x * 2 <= limits.maxGroupSize.x
                if (growX) {
                    x *= 2
                } else if (y * 2 <= limits.maxGroupSize.y) {
                    y *= 2
                } else if (x * 2 <= limits.maxGroupSize.x) {
                    x *= 2
                } else {
                    break
                }
            }
            return WorkGroupSize(x = x, y = y, z = 1)
        }

        private fun largestPowerOfTwoAtMost(value: Int): Int {
            var result = 1
            while (result * 2 <= value) result *= 2
            return result
        }

        private fun ceilDiv(
            value: Int,
            divisor: Int,
        ): Int = (value + divisor - 1) / divisor
    }
}
