package dev.geode.engine.gl

enum class TimerQuerySupport { ABSENT, UNTRUSTED, TRUSTED }

data class GlCapabilities(
    val version: GlVersion?,
    val computeShaders: Boolean,
    val storageBuffersInCompute: Boolean,
    val storageBuffersInFragment: Boolean,
    val imageLoadStore: Boolean,
    val vertexTextureFetch: Boolean,
    val timerQueries: TimerQuerySupport,
    val programBinaries: Boolean,
) {
    companion object {
        private val ES_3_1 = GlVersion(3, 1)

        private const val MIN_COMPUTE_INVOCATIONS = 128
        private const val MIN_COMPUTE_STORAGE_BLOCKS = 4
        private const val MIN_COMPUTE_IMAGE_UNIFORMS = 4

        fun derive(report: GlProbeReport): GlCapabilities {
            val version = GlVersion.parse(report.versionString)
            val es31 = version != null && version >= ES_3_1
            return GlCapabilities(
                version = version,
                computeShaders = es31 && report.maxComputeWorkGroupInvocations >= MIN_COMPUTE_INVOCATIONS,
                storageBuffersInCompute = es31 && report.maxComputeStorageBlocks >= MIN_COMPUTE_STORAGE_BLOCKS,
                storageBuffersInFragment = es31 && report.maxFragmentStorageBlocks > 0,
                imageLoadStore = es31 && report.maxComputeImageUniforms >= MIN_COMPUTE_IMAGE_UNIFORMS,
                vertexTextureFetch = report.maxVertexTextureImageUnits > 0 && report.vertexTextureFetchProven,
                timerQueries =
                    when {
                        !report.timerQueryPresent -> TimerQuerySupport.ABSENT
                        !report.timerQueryProven -> TimerQuerySupport.UNTRUSTED
                        else -> TimerQuerySupport.TRUSTED
                    },
                programBinaries = report.programBinaryFormats > 0,
            )
        }
    }
}
