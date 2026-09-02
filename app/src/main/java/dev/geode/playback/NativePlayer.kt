package dev.geode.playback

import android.content.Context
import android.net.Uri
import android.os.Handler
import android.os.Looper
import androidx.annotation.OptIn
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.PlaybackException
import androidx.media3.common.Player
import androidx.media3.common.SimpleBasePlayer
import androidx.media3.common.util.UnstableApi
import com.google.common.util.concurrent.Futures
import com.google.common.util.concurrent.ListenableFuture
import dev.geode.RingLog
import dev.geode.engine.bridge.GeodeNative
import java.io.FileNotFoundException
import java.util.concurrent.Callable
import java.util.concurrent.Executors

/**
 * The native engine behind the Media3 [Player] API, so the session, service and UI stay unchanged.
 * Playlist edits and commands run on the main thread; file descriptors are opened on one worker and
 * handed to the engine, which owns them from then on. A poll on the main thread mirrors the engine's
 * state, position and gapless joins back into [SimpleBasePlayer].
 */
@OptIn(UnstableApi::class)
class NativePlayer(
    context: Context,
    private val tap: NativeTapPump,
    private val dsp: NativePlayerDsp,
) : SimpleBasePlayer(Looper.getMainLooper()) {
    private class Entry(
        val item: MediaItem,
        val uid: Long,
    ) {
        var durationUs: Long = C.TIME_UNSET
    }

    private val resolver = context.contentResolver
    private val handle = GeodeNative.playerCreate()
    private val main = Handler(Looper.getMainLooper())
    private val worker =
        Executors.newSingleThreadExecutor { runnable ->
            Thread(runnable, "geode-native-player").apply { isDaemon = true }
        }
    private val entries = ArrayList<Entry>()
    private val loads = HashMap<Long, Int>()
    private var nextUid = 1L
    private var nextLoad = 1L
    private var currentIndex = 0
    private var prepared = false
    private var playWhenReady = false
    private var repeatMode = Player.REPEAT_MODE_OFF
    private var shuffle = false
    private var shuffleOrder: List<Int> = emptyList()
    private var volume = 1f
    private var gapless = true
    private var crossfadeMs = 0
    private var loadedId = -1L
    private var queuedId = -1L
    private var queuedIndex = -1
    private var error: PlaybackException? = null
    private var released = false

    private val poll =
        object : Runnable {
            override fun run() {
                if (released) return
                syncFromEngine()
                invalidateState()
                main.postDelayed(this, POLL_MS)
            }
        }

    init {
        tap.start(handle)
        dsp.attach(handle)
        main.post(poll)
    }

    /** Crossfade and gapless choices from the playback prefs. */
    fun applyPrefs(
        crossfadeMs: Int,
        curve: Int,
        gapless: Boolean,
    ) {
        this.crossfadeMs = crossfadeMs
        this.gapless = gapless
        GeodeNative.playerSetCrossfade(handle, crossfadeMs, curve)
        queueNext()
    }

    override fun getState(): State {
        val engine = if (released) ENGINE_IDLE else GeodeNative.playerState(handle)
        val playbackState =
            when {
                error != null || !prepared || entries.isEmpty() -> Player.STATE_IDLE
                engine == ENGINE_READY -> Player.STATE_READY
                engine == ENGINE_ENDED -> Player.STATE_ENDED
                else -> Player.STATE_BUFFERING
            }
        val positionMs = if (released) 0L else GeodeNative.playerPositionUs(handle) / 1000L
        return State
            .Builder()
            .setAvailableCommands(COMMANDS)
            .setPlayWhenReady(playWhenReady, Player.PLAY_WHEN_READY_CHANGE_REASON_USER_REQUEST)
            .setPlaybackState(playbackState)
            .setPlayerError(error)
            .setRepeatMode(repeatMode)
            .setShuffleModeEnabled(shuffle)
            .setVolume(volume)
            .setIsLoading(playbackState == Player.STATE_BUFFERING)
            .setPlaylist(
                entries.map { entry ->
                    MediaItemData
                        .Builder(entry.uid)
                        .setMediaItem(entry.item)
                        .setDurationUs(entry.durationUs)
                        .setIsSeekable(true)
                        .build()
                },
            ).setCurrentMediaItemIndex(if (entries.isEmpty()) C.INDEX_UNSET else currentIndex)
            .setContentPositionMs(positionMs)
            .build()
    }

    override fun handleSetPlayWhenReady(playWhenReady: Boolean): ListenableFuture<*> {
        this.playWhenReady = playWhenReady
        if (playWhenReady) GeodeNative.playerPlay(handle) else GeodeNative.playerPause(handle)
        return done()
    }

    override fun handlePrepare(): ListenableFuture<*> {
        prepared = true
        error = null
        return if (loadedId < 0) openCurrent(0L) else done()
    }

    override fun handleStop(): ListenableFuture<*> {
        prepared = false
        loadedId = -1
        queuedId = -1
        GeodeNative.playerStop(handle)
        return done()
    }

    override fun handleRelease(): ListenableFuture<*> {
        released = true
        main.removeCallbacksAndMessages(null)
        worker.shutdown()
        tap.stop()
        dsp.release()
        GeodeNative.playerDestroy(handle)
        return done()
    }

    override fun handleSetRepeatMode(repeatMode: Int): ListenableFuture<*> {
        this.repeatMode = repeatMode
        queueNext()
        return done()
    }

    override fun handleSetShuffleModeEnabled(shuffleModeEnabled: Boolean): ListenableFuture<*> {
        shuffle = shuffleModeEnabled
        reshuffle()
        queueNext()
        return done()
    }

    override fun handleSetVolume(volume: Float): ListenableFuture<*> {
        this.volume = volume.coerceIn(0f, 1f)
        GeodeNative.playerSetVolume(handle, this.volume)
        return done()
    }

    override fun handleSeek(
        mediaItemIndex: Int,
        positionMs: Long,
        seekCommand: Int,
    ): ListenableFuture<*> {
        if (entries.isEmpty()) return done()
        val index = mediaItemIndex.coerceIn(0, entries.lastIndex)
        val position = if (positionMs == C.TIME_UNSET) 0L else positionMs.coerceAtLeast(0L)
        if (index != currentIndex || loadedId < 0) {
            currentIndex = index
            error = null
            return openCurrent(position)
        }
        GeodeNative.playerSeek(handle, position * 1000L)
        return done()
    }

    override fun handleSetMediaItems(
        mediaItems: List<MediaItem>,
        startIndex: Int,
        startPositionMs: Long,
    ): ListenableFuture<*> {
        entries.clear()
        mediaItems.forEach { entries.add(Entry(it, nextUid++)) }
        currentIndex = if (startIndex == C.INDEX_UNSET) 0 else startIndex.coerceIn(0, maxOf(0, entries.lastIndex))
        loadedId = -1
        queuedId = -1
        error = null
        reshuffle()
        if (entries.isEmpty()) {
            GeodeNative.playerStop(handle)
            return done()
        }
        return if (prepared) openCurrent(if (startPositionMs == C.TIME_UNSET) 0L else startPositionMs) else done()
    }

    override fun handleAddMediaItems(
        index: Int,
        mediaItems: List<MediaItem>,
    ): ListenableFuture<*> {
        val at = index.coerceIn(0, entries.size)
        entries.addAll(at, mediaItems.map { Entry(it, nextUid++) })
        if (at <= currentIndex && loadedId >= 0) currentIndex += mediaItems.size
        reshuffle()
        if (prepared && loadedId < 0) return openCurrent(0L)
        queueNext()
        return done()
    }

    override fun handleRemoveMediaItems(
        fromIndex: Int,
        toIndex: Int,
    ): ListenableFuture<*> {
        val to = toIndex.coerceAtMost(entries.size)
        if (fromIndex >= to) return done()
        val removingCurrent = currentIndex in fromIndex until to
        repeat(to - fromIndex) { entries.removeAt(fromIndex) }
        reshuffle()
        if (!removingCurrent) {
            if (currentIndex >= to) currentIndex -= to - fromIndex
            queueNext()
            return done()
        }
        loadedId = -1
        queuedId = -1
        if (entries.isEmpty()) {
            currentIndex = 0
            GeodeNative.playerStop(handle)
            return done()
        }
        currentIndex = fromIndex.coerceAtMost(entries.lastIndex)
        return if (prepared) openCurrent(0L) else done()
    }

    override fun handleMoveMediaItems(
        fromIndex: Int,
        toIndex: Int,
        newIndex: Int,
    ): ListenableFuture<*> {
        val current = entries.getOrNull(currentIndex)
        val moving = entries.subList(fromIndex, toIndex).toList()
        repeat(toIndex - fromIndex) { entries.removeAt(fromIndex) }
        entries.addAll(newIndex.coerceIn(0, entries.size), moving)
        currentIndex = entries.indexOf(current).coerceAtLeast(0)
        reshuffle()
        queueNext()
        return done()
    }

    private fun syncFromEngine() {
        val playing = GeodeNative.playerCurrentToken(handle)
        if (playing >= 0 && playing != loadedId) {
            // The engine joined into the pre-rolled track on its own.
            val index = loads[playing] ?: return
            loadedId = playing
            queuedId = -1
            currentIndex = index.coerceAtMost(entries.lastIndex)
            queueNext()
        }
        loads.keys.retainAll { it == loadedId || it == queuedId }
        if (GeodeNative.playerState(handle) == ENGINE_ERROR && error == null) {
            error = PlaybackException(GeodeNative.playerLastError(handle), null, PlaybackException.ERROR_CODE_DECODING_FAILED)
        }
        val durationUs = GeodeNative.playerDurationUs(handle)
        if (durationUs > 0) entries.getOrNull(currentIndex)?.durationUs = durationUs
        dsp.sync()
    }

    private fun openCurrent(positionMs: Long): ListenableFuture<*> {
        val entry = entries.getOrNull(currentIndex) ?: return done()
        val id = nextLoad++
        loads[id] = currentIndex
        loadedId = id
        queuedId = -1
        val play = playWhenReady
        return Futures.submit(
            Callable {
                val fd = openFd(entry.item)
                if (fd == null) {
                    main.post {
                        error =
                            PlaybackException("cannot open ${entry.item.mediaId}", null, PlaybackException.ERROR_CODE_IO_FILE_NOT_FOUND)
                    }
                    return@Callable
                }
                GeodeNative.playerOpen(handle, fd.first, 0L, fd.second, id)
                if (positionMs > 0L) GeodeNative.playerSeek(handle, positionMs * 1000L)
                if (play) GeodeNative.playerPlay(handle)
                main.post { queueNext() }
            },
            worker,
        )
    }

    /** Pre-rolls the track that follows so the engine can join into it; a change of mind clears it. */
    private fun queueNext() {
        if (released || loadedId < 0) return
        val index = if (gapless || crossfadeMs > 0) nextIndex() else -1
        if (index < 0) {
            if (queuedId >= 0) {
                queuedId = -1
                GeodeNative.playerSetNext(handle, -1, 0L, 0L, 0L)
            }
            return
        }
        if (queuedId >= 0 && queuedIndex == index) return
        val entry = entries[index]
        val id = nextLoad++
        loads[id] = index
        queuedId = id
        queuedIndex = index
        worker.execute {
            val fd = openFd(entry.item) ?: return@execute
            GeodeNative.playerSetNext(handle, fd.first, 0L, fd.second, id)
        }
    }

    private fun nextIndex(): Int {
        if (entries.isEmpty()) return -1
        if (repeatMode == Player.REPEAT_MODE_ONE) return currentIndex
        val order = if (shuffle) shuffleOrder else entries.indices.toList()
        val at = order.indexOf(currentIndex)
        val following = at + 1
        return when {
            following < order.size -> order[following]
            repeatMode == Player.REPEAT_MODE_ALL -> order[0]
            else -> -1
        }
    }

    private fun reshuffle() {
        shuffleOrder = entries.indices.shuffled()
    }

    private fun openFd(item: MediaItem): Pair<Int, Long>? {
        val uri: Uri = item.localConfiguration?.uri ?: return null
        return try {
            resolver.openFileDescriptor(uri, "r")?.let { pfd ->
                val length = pfd.statSize
                pfd.detachFd() to (if (length > 0) length else 0L)
            }
        } catch (e: FileNotFoundException) {
            RingLog.note(TAG, "openFileDescriptor failed", e)
            null
        } catch (e: SecurityException) {
            RingLog.note(TAG, "openFileDescriptor refused", e)
            null
        }
    }

    private fun done(): ListenableFuture<*> = Futures.immediateVoidFuture()

    private companion object {
        const val TAG = "NativePlayer"
        const val POLL_MS = 200L

        // GeodePlayerState in core/api/geode_api.h.
        const val ENGINE_IDLE = 0
        const val ENGINE_READY = 2
        const val ENGINE_ENDED = 3
        const val ENGINE_ERROR = 4

        val COMMANDS: Player.Commands =
            Player.Commands
                .Builder()
                .addAll(
                    Player.COMMAND_PLAY_PAUSE,
                    Player.COMMAND_PREPARE,
                    Player.COMMAND_STOP,
                    Player.COMMAND_SEEK_TO_DEFAULT_POSITION,
                    Player.COMMAND_SEEK_IN_CURRENT_MEDIA_ITEM,
                    Player.COMMAND_SEEK_TO_PREVIOUS_MEDIA_ITEM,
                    Player.COMMAND_SEEK_TO_PREVIOUS,
                    Player.COMMAND_SEEK_TO_NEXT_MEDIA_ITEM,
                    Player.COMMAND_SEEK_TO_NEXT,
                    Player.COMMAND_SEEK_TO_MEDIA_ITEM,
                    Player.COMMAND_SEEK_BACK,
                    Player.COMMAND_SEEK_FORWARD,
                    Player.COMMAND_SET_REPEAT_MODE,
                    Player.COMMAND_SET_SHUFFLE_MODE,
                    Player.COMMAND_GET_CURRENT_MEDIA_ITEM,
                    Player.COMMAND_GET_TIMELINE,
                    Player.COMMAND_GET_METADATA,
                    Player.COMMAND_SET_MEDIA_ITEM,
                    Player.COMMAND_CHANGE_MEDIA_ITEMS,
                    Player.COMMAND_GET_VOLUME,
                    Player.COMMAND_SET_VOLUME,
                    Player.COMMAND_RELEASE,
                ).build()
    }
}
