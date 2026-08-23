import groovy.json.JsonSlurper

val SCANNED_EXTENSIONS = setOf("kt", "kts", "java", "c", "h", "cpp", "glsl")

@Suppress("UNCHECKED_CAST")
fun readProvenanceRegistry(json: String): List<ProvenanceSourceRecord> {
    val root = JsonSlurper().parseText(json) as? Map<String, Any?> ?: return emptyList()
    val sources = root["sources"] as? List<Map<String, Any?>> ?: emptyList()
    return sources.map { entry ->
        ProvenanceSourceRecord(
            id = entry["id"] as? String ?: "",
            url = entry["url"] as? String,
            tier = entry["tier"] as? String ?: "",
            licence = entry["license"] as? String ?: "",
            commit = (entry["pin"] as? Map<String, Any?>)?.get("commit") as? String,
            importedFiles = (entry["importedFiles"] as? List<String>).orEmpty(),
        )
    }
}
