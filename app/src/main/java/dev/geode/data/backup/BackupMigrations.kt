package dev.geode.data.backup

import java.io.File

/**
 * One step that turns a backup at version [from] into version `from + 1`.
 *
 * [apply] gets the manifest and the directory the archive was unpacked into, already checked
 * against the manifest's sizes and hashes. It may move, rewrite or drop files there, and returns
 * a manifest whose sections and entries describe what it left behind; the version is set for it.
 */
class BackupMigration(
    val from: Int,
    val apply: (manifest: BackupManifest, root: File) -> BackupManifest,
)

object BackupMigrations {
    /**
     * Version 1 is the only format so far. When the format changes, bump
     * [BackupFormat.CURRENT_VERSION] and add `BackupMigration(1) { manifest, root -> ... }` here;
     * a version 1 backup then walks through it on its way in.
     */
    val steps: List<BackupMigration> = emptyList()

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
            current = step.apply(current, root).copy(version = current.version + 1)
        }
        return current
    }
}
