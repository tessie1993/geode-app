package dev.geode.export

import android.content.Context
import android.net.Uri

/**
 * A finished render's file in MediaStore, as My videos deletes it. The rest of the old clip library
 * (listing, describing and renaming the Movies folder's videos for the dropped video editor, ADR 0007)
 * had no caller left and is gone; My videos lists a project's renders instead.
 */
object StudioClips {
    fun delete(
        context: Context,
        uri: String,
    ): Boolean =
        runCatching {
            context.contentResolver.delete(Uri.parse(uri), null, null) > 0
        }.getOrDefault(false)
}
