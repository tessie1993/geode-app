package dev.geode.engine.gl

enum class ProbedFormat { RGBA8, R16F, RG16F, RGBA16F, R32F, RGBA32UI }

data class FormatProbe(
    val attachable: Boolean,
    val rendersExactly: Boolean,
    val blendsAdditively: Boolean,
    val filtersLinearly: Boolean,
) {
    val renderable: Boolean get() = attachable && rendersExactly
}

data class GlProbeReport(
    val vendor: String,
    val renderer: String,
    val versionString: String,
    val extensions: Set<String>,
    val maxTextureSize: Int,
    val maxColorAttachments: Int,
    val maxVertexTextureImageUnits: Int,
    val vertexTextureFetchProven: Boolean,
    val maxComputeWorkGroupInvocations: Int,
    val maxComputeStorageBlocks: Int,
    val maxFragmentStorageBlocks: Int,
    val maxComputeImageUniforms: Int,
    val programBinaryFormats: Int,
    val timerQueryPresent: Boolean,
    val timerQueryProven: Boolean,
    val formats: Map<ProbedFormat, FormatProbe>,
) {
    fun probeOf(format: ProbedFormat): FormatProbe = formats[format] ?: UNPROVEN

    private companion object {
        val UNPROVEN =
            FormatProbe(
                attachable = false,
                rendersExactly = false,
                blendsAdditively = false,
                filtersLinearly = false,
            )
    }
}
