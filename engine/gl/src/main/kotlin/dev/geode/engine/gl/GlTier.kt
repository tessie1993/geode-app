package dev.geode.engine.gl

/**
 * Why a device sits on the baseline, as a closed set rather than free text.
 *
 * A tier without a reason is a mystery in a bug report: "this phone looks wrong" with no way
 * to tell a genuine ES 3.0 device from a 3.1 device whose limits undershoot its own spec
 * floor. Modelling the reason as a sealed type rather than a `String` means a new reason has
 * to be *named*, and every renderer of reasons stops compiling until it handles it.
 */
sealed interface BaselineCause {
    /** One sentence, safe to log or show verbatim on a debug capability screen. */
    val sentence: String

    /**
     * Nothing was measured — EGL never handed us a context. Distinct from every other cause,
     * which is a measurement that came back negative.
     */
    data class NoProbeContext(
        val detail: String,
    ) : BaselineCause {
        override val sentence: String
            get() = "no GL context could be probed ($detail); the ES 3.0 baseline is the one claim that needs no evidence"
    }

    /**
     * `GL_VERSION` did not parse. Emulators, ANGLE builds and the occasional vendor fork all
     * produce strings [GlVersion] handles; anything left over is a driver we know nothing
     * about, and §6.3's rule is that an unreadable version enables nothing.
     */
    data class VersionUnparseable(
        val versionString: String,
    ) : BaselineCause {
        override val sentence: String
            get() = "GL_VERSION did not parse (\"$versionString\"); an unreadable version enables nothing enhanced"
    }

    /** An honest ES 3.0 device. This is not a degraded state — it is the shipping path. */
    data class BelowEs31(
        val version: GlVersion,
    ) : BaselineCause {
        override val sentence: String
            get() = "ES $version has no compute shaders; fragment ping-pong is the correct path here, not a fallback"
    }

    /**
     * The interesting failure. A context that claims 3.1 but reports fewer than the 128
     * invocations per work group the ES 3.1 spec *mandates* is not a slow 3.1 device: it is a
     * context whose version string cannot be trusted for this capability at all.
     */
    data class ComputeLimitsBelowSpecFloor(
        val workGroupInvocations: Int,
    ) : BaselineCause {
        override val sentence: String
            get() =
                "the context claims ES 3.1 but reports $workGroupInvocations compute invocations per work group, " +
                    "below the spec floor of 128; a version string that undershoots its own minimum is not evidence"
    }

    /**
     * Compute exists but cannot scatter. The whole argument for the compute tier is writing to
     * arbitrary destinations without emitting a primitive; without image load/store there is
     * nowhere to scatter *to*, and a compute pass buys nothing the fragment path lacks.
     */
    data class NoImageLoadStore(
        val computeImageUniforms: Int,
    ) : BaselineCause {
        override val sentence: String
            get() =
                "compute is present but only $computeImageUniforms compute image uniforms are, so a step cannot " +
                    "scatter into an image; compute without scatter buys nothing the fragment path lacks"
    }
}

/**
 * The evidence behind a [GlTier.Compute] verdict, carried so the tier can explain itself the
 * same way a baseline verdict does. Constructing the compute tier requires one of these, which
 * is how "an unreasoned tier" is made unrepresentable rather than merely discouraged.
 */
data class ComputeProof(
    val version: GlVersion,
    val workGroupInvocations: Int,
    val computeImageUniforms: Int,
    val storageBuffersInCompute: Boolean,
) {
    val sentence: String
        get() =
            "ES $version with $workGroupInvocations compute invocations per work group and " +
                "$computeImageUniforms image uniforms" +
                (if (storageBuffersInCompute) ", plus compute SSBOs" else ", but no compute SSBOs") +
                "; a simulation step can scatter without emitting a primitive"
}

/**
 * The one question the renderer actually asks a capability report: what path does this device
 * run? Everything finer-grained — which format a role gets, whether timer queries can be
 * trusted — is [FormatPlan] and [GlCapabilities]; this type is the fork in the render graph.
 *
 * Deliberately **not** on this type: any notion of how *much* work to do. Resolution
 * multipliers belong to the quality profile table, and that table needs a simulation
 * multiplier separate from the display one (§6.3: a field sim at native resolution with a
 * nine-tap diffusion kernel is texture-fetch-bound on a mid-tier Mali long before it is ALU
 * bound). Reading a tier as a quality level would silently re-merge those two numbers.
 */
sealed interface GlTier {
    /** Stable short token for logs and HUD text. Not localised, not user-facing prose. */
    val label: String

    /** The reason this tier was chosen, in one sentence. */
    val because: String

    /**
     * Proven ES 3.1 compute. A simulation step runs with no rasterization pass at all: no
     * fullscreen triangle, no FBO bind and tile resolve per step, shared memory between
     * invocations in a work group, and scatter writes a fragment shader cannot express.
     *
     * The strongest argument for this tier is not ALU throughput, it is **the tiler**. Every
     * scattered deposit drawn as an instanced quad lands at an essentially random tile, and
     * both Mali's polygon-list build and Adreno's visibility stream write per-primitive,
     * per-tile records to main memory for it — the exact opposite of the locality a tiler is
     * built to exploit. Shrinking the quad from 4x4 to 2x2 cuts fragments and does *nothing*
     * to binning cost, so the obvious lever does not pull the actual constraint. A compute
     * dispatch emits no primitives, so it never enters the binning pipeline: that is what this
     * tier buys, and it is a bandwidth win, not an ALU win.
     */
    data class Compute(
        val proof: ComputeProof,
    ) : GlTier {
        override val label: String get() = "compute-es31"
        override val because: String get() = proof.sentence
    }

    /**
     * The ES 3.0 fragment ping-pong path: state in textures, a fullscreen triangle steps it,
     * the result ping-pongs between two targets. This is the correct ES 3.0 baseline and it
     * stays the fallback forever — the compute tier is an optimisation on top of a path that
     * must keep working, never a replacement for it.
     */
    data class Baseline(
        val cause: BaselineCause,
    ) : GlTier {
        override val label: String get() = "baseline-es30"
        override val because: String get() = cause.sentence
    }

    companion object {
        private val ES_3_1 = GlVersion(3, 1)

        /**
         * Decides the tier from facts and the capabilities derived from them. Pure: no GL
         * context needed, safe to call from any thread, safe to re-run on cached facts.
         *
         * The checks run in the order a reader would ask them, so the *first* thing that
         * failed is the thing reported. A device that fails three preconditions is described
         * by the earliest one, which is the one a driver engineer would want named.
         */
        fun of(
            report: GlProbeReport,
            capabilities: GlCapabilities,
        ): GlTier {
            val version =
                capabilities.version
                    ?: return Baseline(BaselineCause.VersionUnparseable(report.versionString))
            if (version < ES_3_1) return Baseline(BaselineCause.BelowEs31(version))
            if (!capabilities.computeShaders) {
                return Baseline(BaselineCause.ComputeLimitsBelowSpecFloor(report.maxComputeWorkGroupInvocations))
            }
            if (!capabilities.imageLoadStore) {
                return Baseline(BaselineCause.NoImageLoadStore(report.maxComputeImageUniforms))
            }
            return Compute(
                ComputeProof(
                    version = version,
                    workGroupInvocations = report.maxComputeWorkGroupInvocations,
                    computeImageUniforms = report.maxComputeImageUniforms,
                    storageBuffersInCompute = capabilities.storageBuffersInCompute,
                ),
            )
        }
    }
}
