package dev.geode.engine.gl

enum class TexelEncoding {
    LINEAR,

    FLOAT_BITS_IN_UINT,

    PRE_SCALED,
}

data class ResolvedFormat(
    val format: ProbedFormat,
    val encoding: TexelEncoding,
    val filterable: Boolean,
    val because: String,
)

data class FormatPlan(
    val simulationState: ResolvedFormat,
    val advectedField: ResolvedFormat,
    val filterableField: ResolvedFormat,
    val linearAccumulation: ResolvedFormat,
    val audioTexture: ResolvedFormat,
    val linearColorTarget: ResolvedFormat,
)

object FormatPolicy {
    fun resolve(report: GlProbeReport): FormatPlan =
        FormatPlan(
            simulationState = simulationState(report),
            advectedField = advectedField(report),
            filterableField = filterableField(report),
            linearAccumulation = linearAccumulation(report),
            audioTexture = audioTexture(report),
            linearColorTarget = linearColorTarget(report),
        )

    private fun simulationState(report: GlProbeReport): ResolvedFormat {
        val state = report.probeOf(ProbedFormat.RGBA32UI)
        if (state.renderable) {
            return ResolvedFormat(
                format = ProbedFormat.RGBA32UI,
                encoding = TexelEncoding.FLOAT_BITS_IN_UINT,
                filterable = false,
                because = "RGBA32UI proved renderable; float bits packed into uint channels",
            )
        }
        val half = report.probeOf(ProbedFormat.RGBA16F)
        if (half.renderable) {
            return ResolvedFormat(
                format = ProbedFormat.RGBA16F,
                encoding = TexelEncoding.LINEAR,
                filterable = half.filtersLinearly,
                because = "RGBA32UI failed its probe on this driver; RGBA16F proved renderable",
            )
        }
        return lastResort("neither RGBA32UI nor RGBA16F proved renderable")
    }

    /**
     * A four-channel field that is **resampled between texels** every frame and carries values
     * above 1 — a dye advected along a flow, not a lattice stepped in place.
     *
     * Distinct from [simulationState], which prefers the packed `RGBA32UI` encoding precisely
     * because it needs no float extension. That encoding cannot be filtered at all — an integer
     * texture with `GL_LINEAR` is incomplete — so every back-traced fetch becomes four loads and
     * two mixes in the shader. For a lattice that reads whole texels that costs nothing, because
     * it never resamples; for a field whose *every* texel resamples every frame it is four
     * texture-cache lookups where the hardware would have done one, and 128 bits per texel of
     * state to fetch them from instead of 64.
     *
     * Distinct from [linearColorTarget], whose `RGBA8` fallback is a display-linear buffer with
     * the range clamped at 1.0. A feedback dye clamped at 1.0 loses exactly the thing the tone
     * map exists to show: two filaments crossing have to read brighter than one, and above 1.0
     * they stop doing so. So this role's fallback pre-scales instead — the same trade the
     * hand-written field shaders already made with their `uStateScale`, spending resolution to
     * keep the headroom.
     */
    private fun advectedField(report: GlProbeReport): ResolvedFormat {
        val half = report.probeOf(ProbedFormat.RGBA16F)
        if (half.renderable) {
            return ResolvedFormat(
                format = ProbedFormat.RGBA16F,
                encoding = TexelEncoding.LINEAR,
                // Read off the probe rather than assumed from renderability: filtering half
                // floats is OES_texture_half_float_linear, a separate extension from rendering
                // to them. Asking for GL_LINEAR where it is absent leaves the texture incomplete
                // and every fetch reads zero — a field that simply never lights up, with no
                // error raised anywhere to find it by. A false here is not a failure: the shader
                // interpolates by hand instead.
                filterable = half.filtersLinearly,
                because = "RGBA16F proved renderable; the dye keeps its headroom above 1 in half floats",
            )
        }
        return ResolvedFormat(
            format = ProbedFormat.RGBA8,
            encoding = TexelEncoding.PRE_SCALED,
            filterable = true,
            because = "RGBA16F is not renderable here; pre-scaled RGBA8 keeps the headroom at 8 bits of resolution",
        )
    }

    private fun filterableField(report: GlProbeReport): ResolvedFormat {
        val rg = report.probeOf(ProbedFormat.RG16F)
        if (rg.renderable && rg.filtersLinearly) {
            return ResolvedFormat(
                format = ProbedFormat.RG16F,
                encoding = TexelEncoding.LINEAR,
                filterable = true,
                because = "RG16F proved renderable and filterable",
            )
        }
        val state = report.probeOf(ProbedFormat.RGBA32UI)
        if (state.renderable) {
            return ResolvedFormat(
                format = ProbedFormat.RGBA32UI,
                encoding = TexelEncoding.FLOAT_BITS_IN_UINT,
                filterable = false,
                because = "RG16F unproven; packed state with manual interpolation in the shader",
            )
        }
        return lastResort("neither RG16F nor RGBA32UI proved renderable")
    }

    private fun linearAccumulation(report: GlProbeReport): ResolvedFormat {
        val r16f = report.probeOf(ProbedFormat.R16F)
        if (r16f.renderable && r16f.blendsAdditively) {
            return ResolvedFormat(
                format = ProbedFormat.R16F,
                encoding = TexelEncoding.LINEAR,
                filterable = r16f.filtersLinearly,
                because = "R16F proved renderable and additively blendable",
            )
        }
        return ResolvedFormat(
            format = ProbedFormat.RGBA8,
            encoding = TexelEncoding.PRE_SCALED,
            filterable = true,
            because = "R16F failed the render-and-blend probe; pre-scaled RGBA8 keeps deposits linear",
        )
    }

    private fun audioTexture(report: GlProbeReport): ResolvedFormat {
        val r16f = report.probeOf(ProbedFormat.R16F)
        if (r16f.filtersLinearly) {
            return ResolvedFormat(
                format = ProbedFormat.R16F,
                encoding = TexelEncoding.LINEAR,
                filterable = true,
                because = "R16F proved filterable; audio textures are uploaded, so renderability is not required",
            )
        }
        return ResolvedFormat(
            format = ProbedFormat.RGBA8,
            encoding = TexelEncoding.PRE_SCALED,
            filterable = true,
            because = "R16F failed the filter probe despite being core; pre-scaled RGBA8 fallback",
        )
    }

    private fun linearColorTarget(report: GlProbeReport): ResolvedFormat {
        val half = report.probeOf(ProbedFormat.RGBA16F)
        if (half.renderable) {
            return ResolvedFormat(
                format = ProbedFormat.RGBA16F,
                encoding = TexelEncoding.LINEAR,
                filterable = half.filtersLinearly,
                because = "RGBA16F proved renderable; linear-light headroom available",
            )
        }
        return ResolvedFormat(
            format = ProbedFormat.RGBA8,
            encoding = TexelEncoding.LINEAR,
            filterable = true,
            because = "RGBA16F unproven; RGBA8 target with range clamped at 1.0",
        )
    }

    private fun lastResort(why: String): ResolvedFormat =
        ResolvedFormat(
            format = ProbedFormat.RGBA8,
            encoding = TexelEncoding.PRE_SCALED,
            filterable = true,
            because = "$why; RGBA8 is the core-mandated floor",
        )
}
