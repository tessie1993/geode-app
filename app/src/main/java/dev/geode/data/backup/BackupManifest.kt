package dev.geode.data.backup

import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

/** One file in the archive, as the manifest promises it: the restore checks both numbers. */
data class BackupEntry(
    val path: String,
    val size: Long,
    val sha256: String,
)

/**
 * `manifest.json` at the root of a backup zip.
 *
 * [sections] stays as raw ids rather than [BackupSection]s: a migration may rename one, so the
 * ids are only checked against the current enum after [BackupMigrations] has run.
 */
data class BackupManifest(
    val format: String,
    val version: Int,
    val appVersionCode: Int,
    val createdAtMs: Long,
    val sections: List<String>,
    val entries: List<BackupEntry>,
) {
    fun knownSections(): Set<BackupSection> = sections.mapNotNull(BackupSection::fromId).toSet()

    fun encode(): String =
        JSONObject()
            .put(KEY_FORMAT, format)
            .put(KEY_VERSION, version)
            .put(KEY_APP_VERSION, appVersionCode)
            .put(KEY_CREATED_AT, createdAtMs)
            .put(KEY_SECTIONS, JSONArray(sections))
            .put(
                KEY_ENTRIES,
                JSONArray().also { array ->
                    entries.forEach { e ->
                        array.put(JSONObject().put(KEY_PATH, e.path).put(KEY_SIZE, e.size).put(KEY_SHA, e.sha256))
                    }
                },
            ).toString(2)

    companion object {
        private const val KEY_FORMAT = "format"
        private const val KEY_VERSION = "version"
        private const val KEY_APP_VERSION = "appVersionCode"
        private const val KEY_CREATED_AT = "createdAtMs"
        private const val KEY_SECTIONS = "sections"
        private const val KEY_ENTRIES = "entries"
        private const val KEY_PATH = "path"
        private const val KEY_SIZE = "size"
        private const val KEY_SHA = "sha256"

        /**
         * The format id and the version are checked first and on their own, so a backup from a
         * newer app is reported as exactly that, however different the rest of it looks.
         */
        fun decode(text: String): BackupManifest {
            val root =
                try {
                    JSONObject(text)
                } catch (e: JSONException) {
                    throw BackupException(BackupProblem.NOT_A_BACKUP, "manifest is not JSON", e)
                }
            if (root.optString(KEY_FORMAT) != BackupFormat.FORMAT_ID) {
                fail(BackupProblem.NOT_A_BACKUP, "format is '${root.optString(KEY_FORMAT)}'")
            }
            val version = root.optInt(KEY_VERSION, 0)
            if (version > BackupFormat.CURRENT_VERSION) {
                fail(BackupProblem.NEWER_VERSION, "version $version, this app reads up to ${BackupFormat.CURRENT_VERSION}")
            }
            if (version < 1) fail(BackupProblem.DAMAGED, "version $version")
            return try {
                val sections = root.getJSONArray(KEY_SECTIONS)
                val entries = root.getJSONArray(KEY_ENTRIES)
                BackupManifest(
                    format = BackupFormat.FORMAT_ID,
                    version = version,
                    appVersionCode = root.getInt(KEY_APP_VERSION),
                    createdAtMs = root.getLong(KEY_CREATED_AT),
                    sections = (0 until sections.length()).map(sections::getString),
                    entries =
                        (0 until entries.length()).map { i ->
                            val e = entries.getJSONObject(i)
                            BackupEntry(e.getString(KEY_PATH), e.getLong(KEY_SIZE), e.getString(KEY_SHA))
                        },
                )
            } catch (e: JSONException) {
                throw BackupException(BackupProblem.DAMAGED, "manifest is incomplete", e)
            }
        }
    }
}
