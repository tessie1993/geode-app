package dev.geode.data.backup

import java.io.File

/**
 * One step that turns a backup at version [from] into version `from + 1`.
 *
 * [transform] gets the manifest and the directory the archive was unpacked into, already checked
 * against the manifest's sizes and hashes. It may move, rewrite or drop files there, and returns
 * a manifest whose sections and entries describe what it left behind; the version is set for it.
 */
class BackupMigration(
    val from: Int,
    val transform: (manifest: BackupManifest, root: File) -> BackupManifest,
)

object BackupMigrations {
    /**
     * Each step turns its version into the next. When the format changes, bump
     * [BackupFormat.CURRENT_VERSION] and add the step from the old version here; an older backup
     * then walks through every step on its way in.
     */
    val steps: List<BackupMigration> = listOf(BackupMigration(1, ::splitBorrowedKeys))

    /**
     * Version 1 kept the macros, LFOs, envelopes and reactions inside Settings' `prefs/geode-prefs.json`.
     * Version 2 files them under Creative ([BackupSection.borrowedKeys]), so they move into Creative's
     * slice here, and an old backup's macros come back with Creative, as a new backup's would.
     *
     * Only when the backup holds Creative already. Adding Creative to a Settings-only backup would offer
     * a Creative restore whose staging has no presets, palettes or projects, and restoring it would swap
     * the phone's own for nothing. Such a backup keeps its file as it was; its macros are then nobody's
     * to restore, and the phone keeps its own.
     */
    internal fun splitBorrowedKeys(
        manifest: BackupManifest,
        root: File,
    ): BackupManifest {
        var entries = manifest.entries
        for (section in BackupSection.entries.filter { it.id in manifest.sections }) {
            for ((name, keys) in section.borrowedKeys) {
                val ownerPath = BackupFormat.prefsPath(name)
                val owner = File(root, ownerPath)
                if (entries.none { it.path == ownerPath } || !owner.isFile) continue
                val (moved, kept) = PrefsJson.decode(owner.readText()).entries.partition { it.key in keys }
                if (moved.isEmpty()) continue
                val slicePath = BackupFormat.slicePath(section, name)
                val written =
                    listOf(ownerPath to kept, slicePath to moved).map { (path, values) ->
                        val bytes = PrefsJson.encode(values.associate { it.toPair() }).toByteArray(Charsets.UTF_8)
                        val file = File(root, path).apply { parentFile?.mkdirs() }
                        val (size, sha) = file.outputStream().use { BackupArchive.copyHashed(bytes.inputStream(), it, Long.MAX_VALUE) }
                        BackupEntry(path, size, sha)
                    }
                entries = entries.filter { it.path != ownerPath && it.path != slicePath } + written
            }
        }
        return manifest.copy(entries = entries)
    }

    /** Runs every step from the manifest's version up to [target]. A gap in the chain is DAMAGED. */
    fun migrate(
        manifest: BackupManifest,
        root: File,
        steps: List<BackupMigration> = this.steps,
        target: Int = BackupFormat.CURRENT_VERSION,
    ): BackupManifest {
        var current = manifest
        while (current.version < target) {
            val step =
                steps.firstOrNull { it.from == current.version }
                    ?: fail(BackupProblem.DAMAGED, "no migration from version ${current.version}")
            current = step.transform(current, root).copy(version = current.version + 1)
        }
        return current
    }
}
