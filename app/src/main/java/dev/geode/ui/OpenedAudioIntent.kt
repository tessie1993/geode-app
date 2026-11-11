package dev.geode.ui

import android.content.Context
import android.content.Intent
import android.widget.Toast
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import dev.geode.R
import dev.geode.model.OpenedAudio

/** Set once an audio file another app opened has started; `AppRoot` shows Player once. Process-wide like [MilkFileInbox]. */
internal object OpenedAudioInbox {
    var opened by mutableStateOf(false)
}

/** Shows Player after [OpenedAudioInbox] took a file in. */
@Composable
internal fun AudioFileOpened(appState: GeodeAppState) {
    if (!OpenedAudioInbox.opened) return
    LaunchedEffect(Unit) {
        OpenedAudioInbox.opened = false
        appState.navigateTo(GeodeDestination.PLAYER)
    }
}

/**
 * An audio file another app opened ("Open with", `ACTION_VIEW` on a content uri). Called from both
 * `onCreate` and `onNewIntent`, before the `.milk` handler, which would take any view. Reads the
 * action, the data uri and the declared type only, never an extra: the user chose to open the file
 * here, so playing it needs no confirm, and nothing else may follow from the intent. A view replayed
 * from Recents is dropped, as its grant may be gone. The uri is consumed here; the checks and the play
 * run off the main thread ([PlayerViewModel.openAudio]).
 */
internal fun receiveAudioFile(
    context: Context,
    intent: Intent?,
    playerViewModel: PlayerViewModel,
) {
    val uri = intent?.data ?: return
    val fromHistory = (intent.flags and Intent.FLAG_ACTIVITY_LAUNCHED_FROM_HISTORY) != 0
    val route = OpenedAudio.route(intent.action, uri.scheme, intent.type, uri.lastPathSegment, fromHistory)
    if (route == OpenedAudio.Route.NotOurs) return
    intent.data = null
    if (route == OpenedAudio.Route.Stale) return
    val app = context.applicationContext
    playerViewModel.openAudio(uri) { ok ->
        if (ok) {
            OpenedAudioInbox.opened = true
        } else {
            Toast.makeText(app, R.string.opened_audio_refused, Toast.LENGTH_LONG).show()
        }
    }
}
