package dev.geode.data

import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.util.Log
import androidx.core.content.edit
import androidx.core.net.toUri

/**
 * Where a recorded take's MP4 goes, asked once: the first stopped recording asks the user (Videos
 * library or a folder they pick) and may keep that answer, so every later take renders there
 * without asking. [clear] forgets it.
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
     * Answers the ask-once question with [target], and keeps it as the default only when
     * [remember] is set; otherwise just this take renders there and the next stop asks again.
     *
     * For a folder, the persistable grant the picker offered is taken either way: the render runs
     * in the background and can outlive the picker's temporary grant, so even a one-off folder
     * needs it. If the grant cannot be held, nothing is stored and false comes back, so the caller
     * keeps asking instead of rendering to a folder it cannot write.
     */
    fun choose(
        target: SaveTarget,
        remember: Boolean = true,
    ): Boolean {
        if (target is SaveTarget.Folder) {
            if (!appContext.contentResolver.persistGrant(target.treeUri.toUri(), GRANT_FLAGS)) {
                Log.w(TAG, "take persistable grant failed")
            }
            if (!holdsGrant(target.treeUri)) return false
        }
        if (remember) prefs.edit { putString(KEY_TARGET, target.encode()) }
        return true
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
