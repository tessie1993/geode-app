package dev.geode.engine.bridge

// The JNI declarations of geode_backup_jni.cpp. Every call is one plan: lists cross as String[] in and out,
// laid out as core/api/geode_api.h describes for `geode_backup_*`; the shell's BackupWire reads and writes them.
// An answer is null when native could not build it.
object BackupNative {
    init {
        GeodeNative.ensureLoaded()
    }

    /** The sections in write order, with their prefs files and the filesDir roots an export walks (`geode_backup_layout`). */
    external fun layout(): Array<String>?

    /** What an export holds, in zip order, from the prefs [prefs] and the files [files] the shell found (`geode_backup_plan_export`). */
    external fun planExport(
        prefs: Array<String>,
        files: Array<String>,
    ): Array<String>?

    /** The manifest text, from each entry the shell wrote: path, size, SHA-256 hex (`geode_backup_finish_export`). */
    external fun finishExport(
        entries: Array<String>,
        appVersionCode: Int,
        createdAtMs: Long,
    ): Array<String>?

    /** Which zip entries to extract where, each with a byte budget, from the zip's listing and manifest (`geode_backup_plan_unpack`). */
    external fun planUnpack(request: Array<String>): Array<String>?

    /** Whether each extracted item's canonical target lies inside the canonical staging directory (`geode_backup_check_staging`). */
    external fun checkStaging(request: Array<String>): Array<String>?

    /** One extracted item's size and digest against what the manifest promised (`geode_backup_check_extracted`). */
    external fun checkExtracted(item: Array<String>): Array<String>?

    /** Migration, the allowlist and the preview, from the manifest and the staged prefs texts (`geode_backup_finish_unpack`). */
    external fun finishUnpack(request: Array<String>): Array<String>?

    /** Per section the user chose: the files to swap and the prefs to remove and write (`geode_backup_plan_restore`). */
    external fun planRestore(request: Array<String>): Array<String>?

    /** `beatrealms-backup-YYYYMMDD.zip` (`geode_backup_file_name`). */
    external fun fileName(
        year: Int,
        month: Int,
        day: Int,
    ): String
}
