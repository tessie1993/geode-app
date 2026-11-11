package dev.geode.data.prefs

// The schema of everything the app keeps in SharedPreferences (#1009, PR 1). Each store lists its
// keys as PREF_KEYS; `PrefsSchemaTest` holds the stores to those lists, the lists to each other
// and to the manual backup. Nothing reads the lists at run time yet.

/** The `putX` call a key is written with. */
enum class PrefType {
    BOOLEAN,
    INT,
    LONG,
    FLOAT,
    STRING,
    STRING_SET,
}

/** What Settings › Backup does with a key today. Only [PORTABLE] is exported and restored. */
enum class BackupClass {
    /** A choice or work of the user's, worth having on the next phone. */
    PORTABLE,

    /** True of this phone or this install only: a picked folder's grant, tuning for its hardware. */
    DEVICE,

    /** Something the app remembers that the user never set: seen-it flags, progress. */
    STATE,

    /** A copy of what Play or the player already knows. */
    CACHE,
}

/**
 * One key of the prefs file [file].
 *
 * [retired] keys are written by no build any more: a store still reads one to carry an old
 * install over, or only removes it on a save. [type] is null for a retired key nothing reads.
 * A [prefix] key stands for every key that starts with [name] (`bookmark:<uri>`).
 */
data class PrefKey(
    val file: String,
    val name: String,
    val type: PrefType?,
    val backup: BackupClass,
    val retired: Boolean = false,
    val prefix: Boolean = false,
) {
    fun matches(key: String): Boolean = if (prefix) key.startsWith(name) else key == name
}

class PrefKeysBuilder internal constructor(
    private val file: String,
) {
    internal val keys = mutableListOf<PrefKey>()

    fun add(
        type: PrefType,
        backup: BackupClass,
        names: Iterable<String>,
    ) {
        names.mapTo(keys) { PrefKey(file, it, type, backup) }
    }

    fun portable(
        type: PrefType,
        vararg names: String,
    ) = add(type, BackupClass.PORTABLE, names.asList())

    fun device(
        type: PrefType,
        vararg names: String,
    ) = add(type, BackupClass.DEVICE, names.asList())

    fun state(
        type: PrefType,
        vararg names: String,
    ) = add(type, BackupClass.STATE, names.asList())

    fun cache(
        type: PrefType,
        vararg names: String,
    ) = add(type, BackupClass.CACHE, names.asList())

    fun portablePrefix(
        type: PrefType,
        name: String,
    ) {
        keys += PrefKey(file, name, type, BackupClass.PORTABLE, prefix = true)
    }

    /** Retired keys ride in a backup like the file they sit in, so they are [BackupClass.PORTABLE] until removed. */
    fun retired(
        type: PrefType?,
        vararg names: String,
    ) {
        names.mapTo(keys) { PrefKey(file, it, type, BackupClass.PORTABLE, retired = true) }
    }
}

fun prefKeys(
    file: String,
    build: PrefKeysBuilder.() -> Unit,
): List<PrefKey> = PrefKeysBuilder(file).apply(build).keys.toList()
