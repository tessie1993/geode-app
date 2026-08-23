package dev.geode.engine.gl

import android.opengl.GLES30
import android.opengl.GLES31

/**
 * Everything a compute step needs to know about this device's compute hardware before it can
 * choose anything.
 *
 * Not one number and not a boolean. A step has to pick a local size, and the only inputs to
 * that choice are these limits; a caller handed `hasCompute = true` would have no option but
 * to hardcode 8x8 and hope.
 *
 * These are also the limits `GlProbeReport` does not carry. It has the *total* invocations per
 * group, which `GlCapabilities` already checks against the 128 floor — but not the per-axis
 * maxima, and a context can clear the total and still cap `local_size_y` at 1. So the second
 * half of "never infer support from the version string" happens here, against numbers nobody
 * has read before.
 */
data class ComputeLimits(
    val maxInvocationsPerGroup: Int,
    val maxGroupSize: WorkGroupSize,
    val maxGroupCount: WorkGroupCount,
    val sharedMemoryBytes: Int,
    val imageUniforms: Int,
    val textureImageUnits: Int,
    val storageBuffers: Boolean,
) {
    /**
     * Whether `shared` storage clears the ES 3.1 floor. Not a gate — a step that declares no
     * shared variables does not care, and dropping the tier over a limit the kernel never
     * touches would refuse compute on a device that runs it perfectly. A step that *does* cache
     * a halo asks this before sizing its cache, and a false here in a log is a real anomaly.
     */
    val sharedMemoryMeetsSpecFloor: Boolean
        get() = sharedMemoryBytes >= ComputeSupport.SPEC_FLOOR_SHARED_MEMORY_BYTES

    /** One line for logcat or a debug capability screen. */
    val summary: String
        get() =
            "invocations=$maxInvocationsPerGroup groupSize=$maxGroupSize " +
                "groupCount=${maxGroupCount.x}x${maxGroupCount.y}x${maxGroupCount.z} " +
                "shared=${sharedMemoryBytes}B${if (sharedMemoryMeetsSpecFloor) "" else " (BELOW SPEC FLOOR)"} " +
                "images=$imageUniforms textures=$textureImageUnits ssbo=$storageBuffers"
}

/**
 * Why a device is not running compute, as a closed set. Mirrors [BaselineCause]'s reasoning:
 * a reason that is free text cannot be handled exhaustively, and "this phone looks wrong" with
 * no way to tell a genuine ES 3.0 device from a lying 3.1 one is how device bugs go unfixed.
 */
sealed interface NoCompute {
    /** One sentence, safe to log or show verbatim on a debug capability screen. */
    val sentence: String

    /**
     * The tier already decided. Carries [BaselineCause] rather than restating it, so there is
     * exactly one place in the tree that words "this is an ES 3.0 device".
     */
    data class DeviceIsBaseline(
        val cause: BaselineCause,
    ) : NoCompute {
        override val sentence: String get() = cause.sentence
    }

    /**
     * The interesting failure, and the one the tier cannot catch. `GlProbeReport` carries
     * `maxComputeWorkGroupInvocations` — the *total* per group — but not the per-axis maxima,
     * so a context can clear the 128-invocation floor and still report, say, a maximum
     * `local_size_y` of 1. Every 2D step would then be forced into a 1D group, and the driver
     * would be below its own spec minimum of (128, 128, 64) while looking fine from the tier's
     * point of view.
     */
    data class GroupSizeBelowSpecFloor(
        val reported: WorkGroupSize,
        val floor: WorkGroupSize,
    ) : NoCompute {
        override val sentence: String
            get() =
                "the context claims ES 3.1 but its maximum work group size is $reported, below the " +
                    "spec floor of $floor; a version string that undershoots its own minimum is not evidence"
    }

    /**
     * Same reasoning one axis up. A grid this app dispatches never approaches 65535 groups on
     * an axis, so this fires only on a driver that is misreporting — which is exactly what it
     * is here to notice.
     */
    data class GroupCountBelowSpecFloor(
        val reported: WorkGroupCount,
        val floor: WorkGroupCount,
    ) : NoCompute {
        override val sentence: String
            get() =
                "the context claims ES 3.1 but allows only ${reported.x}x${reported.y}x${reported.z} work " +
                    "groups per dispatch, below the spec floor of ${floor.x}x${floor.y}x${floor.z}"
    }

    /**
     * The limit queries themselves failed — every read came back through the error path. A
     * context that cannot answer `glGetIntegeri_v` for a core 3.1 pname is not one to hand a
     * dispatch to.
     */
    data class LimitsUnreadable(
        val detail: String,
    ) : NoCompute {
        override val sentence: String
            get() = "the ES 3.1 compute limits could not be read ($detail); nothing enhanced is enabled on unread limits"
    }
}

/**
 * The narrow question a simulation asks: *can this device run my step as a dispatch, and if so
 * within what limits?*
 *
 * Sealed rather than a data class with a flag, so a caller has to handle both arms and cannot
 * read the limits of a device that has none. There is deliberately no `isAvailable` property —
 * adding one would immediately grow the `if (hasCompute)` ladder this whole layer exists to
 * delete. Callers that only want to log read [because].
 */
sealed interface ComputeSupport {
    /** The reason for this answer, in one sentence. */
    val because: String

    data class Available(
        val limits: ComputeLimits,
        val proof: ComputeProof,
    ) : ComputeSupport {
        override val because: String get() = "${proof.sentence}; ${limits.summary}"
    }

    data class Unavailable(
        val cause: NoCompute,
    ) : ComputeSupport {
        override val because: String get() = cause.sentence
    }

    companion object {
        /**
         * ES 3.1 mandates at least 16 KiB of `shared` storage per work group. Read and
         * reported but **not** gated on: a step that declares no `shared` variables does not
         * care, and refusing compute over a limit the kernel never touches would drop the tier
         * on a device that runs it perfectly.
         */
        const val SPEC_FLOOR_SHARED_MEMORY_BYTES = 16384

        /**
         * Answers the question for this device.
         *
         * **Requires a current GL context on the calling thread when [profile] says compute** —
         * the per-axis limits are not in `GlProbeReport`, so they are read here. When the
         * profile is already on the baseline nothing touches GL at all, which is what makes
         * this safe to call unconditionally from a scene's `init()`.
         *
         * Cheap enough not to need caching: six `glGetInteger*` calls, once per scene build.
         * Deliberately *not* memoised — a memo would have to be invalidated on context loss,
         * and getting that wrong means answering for a context that no longer exists.
         */
        fun query(profile: GlProfile): ComputeSupport =
            when (val tier = profile.tier) {
                is GlTier.Baseline -> Unavailable(NoCompute.DeviceIsBaseline(tier.cause))
                is GlTier.Compute -> queryLimits(profile, tier.proof)
            }

        private fun queryLimits(
            profile: GlProfile,
            proof: ComputeProof,
        ): ComputeSupport {
            val maxInvocations = limit(GLES31.GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS)
            val groupSize =
                WorkGroupSize(
                    x = indexedLimit(GLES31.GL_MAX_COMPUTE_WORK_GROUP_SIZE, AXIS_X),
                    y = indexedLimit(GLES31.GL_MAX_COMPUTE_WORK_GROUP_SIZE, AXIS_Y),
                    z = indexedLimit(GLES31.GL_MAX_COMPUTE_WORK_GROUP_SIZE, AXIS_Z),
                )
            val groupCount =
                WorkGroupCount(
                    x = indexedLimit(GLES31.GL_MAX_COMPUTE_WORK_GROUP_COUNT, AXIS_X),
                    y = indexedLimit(GLES31.GL_MAX_COMPUTE_WORK_GROUP_COUNT, AXIS_Y),
                    z = indexedLimit(GLES31.GL_MAX_COMPUTE_WORK_GROUP_COUNT, AXIS_Z),
                )

            if (maxInvocations <= 0) {
                return Unavailable(NoCompute.LimitsUnreadable("GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS read as 0"))
            }
            if (belowFloor(groupSize)) {
                return Unavailable(
                    NoCompute.GroupSizeBelowSpecFloor(groupSize, WorkGroupSize.SPEC_FLOOR),
                )
            }
            if (belowFloor(groupCount)) {
                return Unavailable(
                    NoCompute.GroupCountBelowSpecFloor(groupCount, WorkGroupCount.SPEC_FLOOR),
                )
            }

            return Available(
                limits =
                    ComputeLimits(
                        maxInvocationsPerGroup = maxInvocations,
                        maxGroupSize = groupSize,
                        maxGroupCount = groupCount,
                        sharedMemoryBytes = limit(GLES31.GL_MAX_COMPUTE_SHARED_MEMORY_SIZE),
                        // Taken from the report rather than re-read: it is already a probed
                        // fact, and reading the same pname twice invites the two answers to
                        // disagree in a bug report.
                        imageUniforms = profile.report.maxComputeImageUniforms,
                        textureImageUnits = limit(GLES31.GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS),
                        storageBuffers = profile.capabilities.storageBuffersInCompute,
                    ),
                proof = proof,
            )
        }

        private const val AXIS_X = 0
        private const val AXIS_Y = 1
        private const val AXIS_Z = 2

        private const val ERROR_DRAIN_LIMIT = 32

        private fun belowFloor(size: WorkGroupSize): Boolean =
            size.x < WorkGroupSize.SPEC_FLOOR.x ||
                size.y < WorkGroupSize.SPEC_FLOOR.y ||
                size.z < WorkGroupSize.SPEC_FLOOR.z

        private fun belowFloor(count: WorkGroupCount): Boolean =
            count.x < WorkGroupCount.SPEC_FLOOR.x ||
                count.y < WorkGroupCount.SPEC_FLOOR.y ||
                count.z < WorkGroupCount.SPEC_FLOOR.z

        /**
         * Reads an integer limit, answering 0 for anything the driver rejects. Same reasoning
         * as `GlProber.limit`: `glGetIntegerv` leaves the output array untouched on
         * `GL_INVALID_ENUM`, so without draining first and checking after, an unknown pname
         * returns whatever the array happened to hold.
         */
        private fun limit(pname: Int): Int {
            drainErrors()
            val out = IntArray(1)
            GLES30.glGetIntegerv(pname, out, 0)
            return if (GLES30.glGetError() != GLES30.GL_NO_ERROR) 0 else out[0]
        }

        private fun indexedLimit(
            pname: Int,
            index: Int,
        ): Int {
            drainErrors()
            val out = IntArray(1)
            GLES30.glGetIntegeri_v(pname, index, out, 0)
            return if (GLES30.glGetError() != GLES30.GL_NO_ERROR) 0 else out[0]
        }

        private fun drainErrors() {
            var drained = 0
            while (GLES30.glGetError() != GLES30.GL_NO_ERROR && drained < ERROR_DRAIN_LIMIT) drained++
        }
    }
}
