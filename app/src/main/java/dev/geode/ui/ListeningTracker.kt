package dev.geode.ui

import android.net.Uri
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import dev.geode.data.FavouritesRepository
import dev.geode.data.HistoryStore
import dev.geode.data.SessionRepository
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/**
 * The UI's side of listening: favourites, listen time, and restoring the last queue when the app opens.
 * The plays themselves and the resumption queue are written by [dev.geode.playback.PlayRecorder] on the
 * playback session, which outlives this one (#371); here they are only read.
 */
internal class ListeningTracker(
    private val historyStore: HistoryStore,
    private val favouritesRepository: FavouritesRepository,
    private val sessionRepository: SessionRepository,
    private val storeScope: CoroutineScope,
    private val host: Host,
) {
    interface Host {
        val player: Player
        val currentUri: Uri?

        fun mediaItemFor(uri: Uri): MediaItem

        fun mediaItemFor(track: QueueTrack): MediaItem
    }

    private val _historyTick = MutableStateFlow(0)

    /** Bumps on every recorded play and favourite toggle, so history screens re-read. */
    val historyTick: StateFlow<Int> = _historyTick

    /** The playback session recorded a play ([dev.geode.playback.PlaybackSession.playsRecorded]). */
    fun onPlayRecorded() = _historyTick.update { it + 1 }

    val favourites: StateFlow<Set<String>> = favouritesRepository.favourites

    private var listenTickAtMs = 0L
    private var listenTickUri: String? = null

    fun recentlyPlayed() = historyStore.recentlyPlayed()

    fun recentlyPlayed(limit: Int) = historyStore.recentlyPlayed(limit)

    fun playCountOf(uri: String): Int = historyStore.entryFor(uri)?.playCount ?: 0

    fun toggleFavourite(uri: String) {
        storeScope.launch {
            favouritesRepository.toggle(uri)
            _historyTick.update { it + 1 }
        }
    }

    fun accrueListenTime() {
        val uri = host.currentUri?.toString()
        val now = System.currentTimeMillis()
        val playing = host.player.isPlaying && uri != null
        if (!playing || uri != listenTickUri) {
            if (listenTickAtMs != 0L) historyStore.flush()
            listenTickAtMs = if (playing) now else 0L
            listenTickUri = if (playing) uri else null
            return
        }
        val delta = now - listenTickAtMs
        listenTickAtMs = now
        if (delta in 1..MAX_LISTEN_TICK_MS) historyStore.addListenTime(uri, delta)
    }

    fun flushListenTime() {
        accrueListenTime()
        historyStore.flush()
    }

    fun awaitHistoryWrites(timeoutMs: Long) = historyStore.awaitWrites(timeoutMs)

    @Suppress("ReturnCount")
    suspend fun prepareLastPlayed(): Uri? {
        val player = host.player
        val saved = sessionRepository.load()
        if (saved != null) {
            val restored =
                runCatching {
                    player.setMediaItems(
                        saved.tracks.map { host.mediaItemFor(QueueTrack(it.uri, it.title, it.artist)) },
                        saved.index,
                        saved.positionMs,
                    )
                    player.prepare()
                    Uri.parse(saved.tracks[saved.index].uri)
                }.getOrNull()
            if (restored != null) return restored
        }
        val last = historyStore.recentlyPlayed(1).firstOrNull() ?: return null
        return runCatching {
            val uri = Uri.parse(last.uri)
            player.setMediaItems(listOf(host.mediaItemFor(uri)))
            player.prepare()
            uri
        }.getOrNull()
    }

    private companion object {
        const val MAX_LISTEN_TICK_MS = 5_000L
    }
}
