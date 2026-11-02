package dev.geode.data

import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import androidx.core.content.edit
import androidx.core.net.toUri
import dev.geode.util.bestEffort

/**
 * Where a recorded take's MP4 goes, asked once: the first stopped recording asks the user (Videos
 * library or a folder they pick), and every later take renders there without asking. Settings ›
 * Export & save reads and changes the same answer.
 *
 * Its own small prefs file rather than [ExportPrefsStore]'s Custom defaults, so "not asked yet"
 * stays distinct from "asked, chose the Videos library", which a [SaveTarget] alone cannot say.
 * The file is never in a backup: a folder's grant belongs to this install, and a new phone should
 * ask again.
 *
 * A folder only counts while its persistable write grant is still held. A user who revokes it (or
 * clears the app's data in Files) is asked again rather than having renders fall back silently.
 */
class RecordSaveStore(
    context: Context,
) {
    private val appContext = context.applicationContext
    private val prefs: SharedPreferences by lazy { appContext.getSharedPreferences(FILE, Context.MODE_PRIVATE) }

    /** The chosen target, or null when the user has not been asked yet (or the folder's grant is gone). */
    fun target(): SaveTarget? {
        val raw = prefs.getString(KEY_TARGET, null) ?: return null
        val target = SaveTarget.decode(raw)
        return if (target is SaveTarget.Folder && !holdsGrant(target.treeUri)) null else target
    }

    /**
     * Keeps [target] as the default. For a folder, takes the persistable grant the picker offered
     * first, so the answer outlives a restart; if that fails, nothing is stored and the next take
     * asks again.
     */
    fun choose(target: SaveTarget) {
        if (target is SaveTarget.Folder) {
            bestEffort(TAG, "take persistable grant") {
                appContext.contentResolver.takePersistableUriPermission(target.treeUri.toUri(), GRANT_FLAGS)
            }
            if (!holdsGrant(target.treeUri)) return
        }
        prefs.edit { putString(KEY_TARGET, target.encode()) }
    }

    /** Forgets the answer, so the next take asks again. */
    fun clear() = prefs.edit { remove(KEY_TARGET) }

    private fun holdsGrant(treeUri: String): Boolean =
        appContext.contentResolver.persistedUriPermissions.any { it.uri.toString() == treeUri && it.isWritePermission }

    private companion object {
        const val TAG = "RecordSave"
        const val FILE = "geode-record"
        const val KEY_TARGET = "save_target"
        const val GRANT_FLAGS = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
    }
}
