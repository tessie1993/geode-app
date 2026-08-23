package dev.geode.engine.gl

object CapabilityCache {
    const val SCHEMA_VERSION = 1

    private const val HEADER = "geode-gl-probe-cache"

    fun encode(report: GlProbeReport): String =
        buildString {
            appendLine("$HEADER v$SCHEMA_VERSION")
            appendLine("vendor=${report.vendor}")
            appendLine("renderer=${report.renderer}")
            appendLine("version=${report.versionString}")
            appendLine("extensions=${report.extensions.sorted().joinToString(" ")}")
            appendLine("maxTextureSize=${report.maxTextureSize}")
            appendLine("maxColorAttachments=${report.maxColorAttachments}")
            appendLine("maxVertexTextureImageUnits=${report.maxVertexTextureImageUnits}")
            appendLine("vertexTextureFetchProven=${report.vertexTextureFetchProven}")
            appendLine("maxComputeWorkGroupInvocations=${report.maxComputeWorkGroupInvocations}")
            appendLine("maxComputeStorageBlocks=${report.maxComputeStorageBlocks}")
            appendLine("maxFragmentStorageBlocks=${report.maxFragmentStorageBlocks}")
            appendLine("maxComputeImageUniforms=${report.maxComputeImageUniforms}")
            appendLine("programBinaryFormats=${report.programBinaryFormats}")
            appendLine("timerQueryPresent=${report.timerQueryPresent}")
            appendLine("timerQueryProven=${report.timerQueryProven}")
            ProbedFormat.entries.forEach { format ->
                report.formats[format]?.let { probe ->
                    appendLine("format.${format.name}=${flags(probe)}")
                }
            }
        }

    fun decode(
        text: String,
        vendor: String,
        renderer: String,
        versionString: String,
    ): GlProbeReport? {
        val lines = text.lines().filter { it.isNotEmpty() }
        val reader = LineReader(lines)
        if (reader.next() != "$HEADER v$SCHEMA_VERSION") return null
        val cachedVendor = reader.value("vendor") ?: return null
        val cachedRenderer = reader.value("renderer") ?: return null
        val cachedVersion = reader.value("version") ?: return null
        if (cachedVendor != vendor || cachedRenderer != renderer || cachedVersion != versionString) return null

        val extensions =
            reader.value("extensions")?.split(' ')?.filter { it.isNotEmpty() }?.toSet() ?: return null
        val maxTextureSize = reader.int("maxTextureSize") ?: return null
        val maxColorAttachments = reader.int("maxColorAttachments") ?: return null
        val maxVertexTextureImageUnits = reader.int("maxVertexTextureImageUnits") ?: return null
        val vertexTextureFetchProven = reader.bool("vertexTextureFetchProven") ?: return null
        val maxComputeWorkGroupInvocations = reader.int("maxComputeWorkGroupInvocations") ?: return null
        val maxComputeStorageBlocks = reader.int("maxComputeStorageBlocks") ?: return null
        val maxFragmentStorageBlocks = reader.int("maxFragmentStorageBlocks") ?: return null
        val maxComputeImageUniforms = reader.int("maxComputeImageUniforms") ?: return null
        val programBinaryFormats = reader.int("programBinaryFormats") ?: return null
        val timerQueryPresent = reader.bool("timerQueryPresent") ?: return null
        val timerQueryProven = reader.bool("timerQueryProven") ?: return null

        val formats = mutableMapOf<ProbedFormat, FormatProbe>()
        while (!reader.exhausted) {
            val line = reader.next() ?: return null
            val name = line.substringBefore('=').removePrefix("format.")
            if (!line.startsWith("format.") || name == line) return null
            val format = ProbedFormat.entries.firstOrNull { it.name == name } ?: return null
            val probe = unflags(line.substringAfter('=')) ?: return null
            if (formats.put(format, probe) != null) return null
        }

        return GlProbeReport(
            vendor = cachedVendor,
            renderer = cachedRenderer,
            versionString = cachedVersion,
            extensions = extensions,
            maxTextureSize = maxTextureSize,
            maxColorAttachments = maxColorAttachments,
            maxVertexTextureImageUnits = maxVertexTextureImageUnits,
            vertexTextureFetchProven = vertexTextureFetchProven,
            maxComputeWorkGroupInvocations = maxComputeWorkGroupInvocations,
            maxComputeStorageBlocks = maxComputeStorageBlocks,
            maxFragmentStorageBlocks = maxFragmentStorageBlocks,
            maxComputeImageUniforms = maxComputeImageUniforms,
            programBinaryFormats = programBinaryFormats,
            timerQueryPresent = timerQueryPresent,
            timerQueryProven = timerQueryProven,
            formats = formats,
        )
    }

    private fun flags(probe: FormatProbe): String =
        listOf(probe.attachable, probe.rendersExactly, probe.blendsAdditively, probe.filtersLinearly)
            .joinToString("") { if (it) "1" else "0" }

    private fun unflags(text: String): FormatProbe? {
        if (text.length != 4 || text.any { it != '0' && it != '1' }) return null
        return FormatProbe(
            attachable = text[0] == '1',
            rendersExactly = text[1] == '1',
            blendsAdditively = text[2] == '1',
            filtersLinearly = text[3] == '1',
        )
    }

    private class LineReader(private val lines: List<String>) {
        private var index = 0

        val exhausted: Boolean get() = index >= lines.size

        fun next(): String? = if (exhausted) null else lines[index++]

        fun value(key: String): String? {
            val line = next() ?: return null
            if (!line.startsWith("$key=")) return null
            return line.substring(key.length + 1)
        }

        fun int(key: String): Int? = value(key)?.toIntOrNull()

        fun bool(key: String): Boolean? =
            when (value(key)) {
                "true" -> true
                "false" -> false
                else -> null
            }
    }
}
