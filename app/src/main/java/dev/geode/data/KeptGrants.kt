package dev.geode.data

import android.content.Context
import androidx.annotation.WorkerThread

/**
 * The folder trees the app keeps a persisted grant on: the preset folder (Settings › Folders),
 * Record's folder and the music folders. Android keeps one grant per URI however many places took
 * it, so a folder's grant goes back ([MediaStoreSource.releaseAccess]) only once none of them names
 * it any more. A caller changing the preset or Record folder passes the new one as [preset] or
 * [record], since its store may not have caught up yet. Read off the main thread.
 */
@WorkerThread
fun keptFolders(
    context: Context,
    preset: String? = savedPresetFolder(context),
    record: String? = RecordSaveStore(context).savedFolder(),
): Set<String> = setOfNotNull(preset, record) + savedMusicFolders(context)
