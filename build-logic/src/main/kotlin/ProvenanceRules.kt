data class ProvenanceSourceRecord(
    val id: String,
    val url: String?,
    val tier: String,
    val licence: String,
    val commit: String?,
    val importedFiles: List<String>,
)

data class ScannedFile(
    val path: String,
    val text: String,
)

sealed interface ProvenanceViolation {
    val where: String

    data class OriginWithoutSpdx(override val where: String) : ProvenanceViolation

    data class UnknownOrigin(override val where: String, val url: String) : ProvenanceViolation

    data class OriginCommitMismatch(
        override val where: String,
        val cited: String,
        val pinned: String,
    ) : ProvenanceViolation

    data class ForbiddenTier(override val where: String, val id: String, val tier: String) : ProvenanceViolation

    data class LicenceMismatch(
        override val where: String,
        val declared: String,
        val registry: String,
    ) : ProvenanceViolation

    data class ForbiddenSourceMentioned(override val where: String, val id: String) : ProvenanceViolation

    data class MissingNotice(override val where: String) : ProvenanceViolation
}

object ProvenanceRules {
    private val ADOPTABLE = setOf("ADAPT", "RETAIN")

    private val SPDX_IDS = setOf("MIT", "Apache-2.0", "BSD-2-Clause", "BSD-3-Clause", "Unlicense", "ISC", "LGPL-2.1", "GPL-3.0", "AGPL-3.0")

    private val ORIGIN = Regex("""Origin:\s*(\S+?)(?:@([0-9a-f]{7,40}))?\s*$""", RegexOption.MULTILINE)
    private val SPDX = Regex("""SPDX-License-Identifier:\s*(\S+)""")

    fun check(
        files: List<ScannedFile>,
        sources: List<ProvenanceSourceRecord>,
        notices: String,
    ): List<ProvenanceViolation> {
        val byUrl = sources.filter { it.url != null }.associateBy { normalise(it.url!!) }
        return files.flatMap { file -> checkFile(file, byUrl) } + noticeViolations(sources, notices)
    }

    private fun checkFile(
        file: ScannedFile,
        byUrl: Map<String, ProvenanceSourceRecord>,
    ): List<ProvenanceViolation> {
        val cited = ORIGIN.findAll(file.text).toList()
        val citedUrls = cited.map { normalise(it.groupValues[1]) }.toSet()
        val mentioned =
            byUrl.values.filter { it.tier !in ADOPTABLE && normalise(it.url.orEmpty()) !in citedUrls && mentions(file.text, it) }
        return mentioned.map { ProvenanceViolation.ForbiddenSourceMentioned(file.path, it.id) } +
            cited.flatMap { match -> citationViolations(file, match.groupValues[1], match.groupValues[2], byUrl) }
    }

    private fun citationViolations(
        file: ScannedFile,
        url: String,
        commit: String,
        byUrl: Map<String, ProvenanceSourceRecord>,
    ): List<ProvenanceViolation> {
        val source =
            byUrl[normalise(url)]
                ?: return listOf(ProvenanceViolation.UnknownOrigin(file.path, url))
        val out = mutableListOf<ProvenanceViolation>()
        if (source.tier !in ADOPTABLE) out += ProvenanceViolation.ForbiddenTier(file.path, source.id, source.tier)
        if (commit.isNotEmpty() && source.commit != null && !source.commit.startsWith(commit)) {
            out += ProvenanceViolation.OriginCommitMismatch(file.path, commit, source.commit)
        }
        val declared = SPDX.find(file.text)?.groupValues?.get(1)
        if (declared == null) {
            out += ProvenanceViolation.OriginWithoutSpdx(file.path)
        } else if (source.licence in SPDX_IDS && declared != source.licence) {
            out += ProvenanceViolation.LicenceMismatch(file.path, declared, source.licence)
        }
        return out
    }

    private fun noticeViolations(
        sources: List<ProvenanceSourceRecord>,
        notices: String,
    ): List<ProvenanceViolation> =
        sources
            .filter { it.importedFiles.isNotEmpty() }
            .filterNot { source -> source.url?.let { notices.contains(it) } == true }
            .map { ProvenanceViolation.MissingNotice(it.id) }

    private fun mentions(
        text: String,
        source: ProvenanceSourceRecord,
    ): Boolean = source.url?.let { text.contains(normalise(it)) } == true

    private fun normalise(url: String): String = url.substringAfter("://").trimEnd('/').removeSuffix(".git")
}
