package dev.geode.data

import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Process
import dev.geode.model.OpenedAudio
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.async
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeoutOrNull

/**
 * The platform checks for an audio file another app opened ([OpenedAudio]), main-safe: each call hops
 * to [io] itself. It reads no byte of the file; the player streams it from the uri on its own threads.
 *
 * The display-name query goes to the sender's provider, which may never answer, so it runs on
 * [background], outside the caller's scope, and is given up after [NAME_TIMEOUT_MS]: a provider that
 * hangs costs one parked IO thread, never the main thread or the play.
 */
class OpenedAudioSource(
    private val context: Context,
    private val io: CoroutineDispatcher,
) {
    sealed interface Inspection {
        data object Refused : Inspection

        /** [name] is already sanitised; null when the provider gave none in time. */
        data class Playable(
            val name: String?,
        ) : Inspection
    }

    private val background = CoroutineScope(SupervisorJob() + io)

    suspend fun inspect(uri: Uri): Inspection {
        if (withContext(io) { refused(uri) }) return Inspection.Refused
        val query = background.async { context.contentResolver.displayNameOf(uri) }
        val raw = withTimeoutOrNull(NAME_TIMEOUT_MS) { query.await() }
        if (raw == null) query.cancel()
        return Inspection.Playable(OpenedAudio.displayName(raw))
    }

    /** The checks of "Abuse based on untrusted content:// URI" (developer.android.com, ContentResolver risks). */
    private fun refused(uri: Uri): Boolean {
        val authority = uri.authority
        val provider =
            authority?.let {
                runCatching { context.packageManager.resolveContentProvider(it, PackageManager.ComponentInfoFlags.of(0L)) }
                    .getOrNull()
            }
        val granted =
            runCatching {
                context.checkUriPermission(uri, Process.myPid(), Process.myUid(), Intent.FLAG_GRANT_READ_URI_PERMISSION) ==
                    PackageManager.PERMISSION_GRANTED
            }.getOrDefault(false)
        return OpenedAudio.refuses(uri.scheme, authority, context.packageName, provider?.packageName, granted)
    }

    private companion object {
        const val NAME_TIMEOUT_MS = 2_000L
    }
}
