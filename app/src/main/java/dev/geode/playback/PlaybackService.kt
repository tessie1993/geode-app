package dev.geode.playback

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import androidx.annotation.OptIn
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.common.util.UnstableApi
import androidx.media3.session.CacheBitmapLoader
import androidx.media3.session.LibraryResult
import androidx.media3.session.MediaLibraryService
import androidx.media3.session.MediaSession
import androidx.media3.session.SessionError
import com.google.common.collect.ImmutableList
import com.google.common.util.concurrent.Futures
import com.google.common.util.concurrent.ListenableFuture
import dev.geode.data.HistoryStore
import dev.geode.data.SessionStore
import dev.geode.widget.WidgetPublisher

@OptIn(UnstableApi::class)
class PlaybackService : MediaLibraryService() {
    private var session: MediaLibrarySession? = null
    private var artworkLoader: SessionBitmapLoader? = null
    private var widget: WidgetPublisher? = null

    override fun onCreate() {
        super.onCreate()
        val loader = SessionBitmapLoader(this)
        artworkLoader = loader
        val player = PlaybackEngine.acquireForService(this).player
        session =
            MediaLibrarySession
                .Builder(this, player, LibraryCallback(this, LibraryTree(this)))
                .setSessionActivity(openAppIntent())
                .setBitmapLoader(CacheBitmapLoader(loader))
                .build()
        widget =
            WidgetPublisher(this, player).also {
                player.addListener(it)
                it.publish()
            }
    }

    /** Browsing answers on the resumption thread: the tree reads files and the MediaStore. */
    private class LibraryCallback(
        private val context: Context,
        private val tree: LibraryTree,
    ) : MediaLibrarySession.Callback {
        @OptIn(UnstableApi::class)
        override fun onPlaybackResumption(
            mediaSession: MediaSession,
            controller: MediaSession.ControllerInfo,
            isForPlayback: Boolean,
        ): ListenableFuture<MediaSession.MediaItemsWithStartPosition> =
            Futures.submit(
                java.util.concurrent.Callable {
                    lastPlayedResumption(context)
                        ?: throw UnsupportedOperationException("nothing was ever played")
                },
                resumptionExecutor,
            )

        override fun onGetLibraryRoot(
            session: MediaLibrarySession,
            browser: MediaSession.ControllerInfo,
            params: LibraryParams?,
        ): ListenableFuture<LibraryResult<MediaItem>> = Futures.immediateFuture(LibraryResult.ofItem(tree.root(), params))

        override fun onGetChildren(
            session: MediaLibrarySession,
            browser: MediaSession.ControllerInfo,
            parentId: String,
            page: Int,
            pageSize: Int,
            params: LibraryParams?,
        ): ListenableFuture<LibraryResult<ImmutableList<MediaItem>>> =
            Futures.submit(
                java.util.concurrent.Callable {
                    val all = tree.children(parentId)
                    val from = (page * pageSize).coerceAtMost(all.size)
                    val until = (from + pageSize).coerceAtMost(all.size)
                    LibraryResult.ofItemList(ImmutableList.copyOf(all.subList(from, until)), params)
                },
                resumptionExecutor,
            )

        override fun onGetItem(
            session: MediaLibrarySession,
            browser: MediaSession.ControllerInfo,
            mediaId: String,
        ): ListenableFuture<LibraryResult<MediaItem>> =
            Futures.submit(
                java.util.concurrent.Callable {
                    tree.item(mediaId)?.let { LibraryResult.ofItem(it, null) }
                        ?: LibraryResult.ofError(SessionError.ERROR_BAD_VALUE)
                },
                resumptionExecutor,
            )

        override fun onAddMediaItems(
            mediaSession: MediaSession,
            controller: MediaSession.ControllerInfo,
            mediaItems: List<MediaItem>,
        ): ListenableFuture<List<MediaItem>> =
            Futures.submit(
                java.util.concurrent.Callable { mediaItems.map { resolve(it) } },
                resumptionExecutor,
            )

        /** A row tapped in a browsed folder plays that whole folder from the row, like the library screen does. */
        override fun onSetMediaItems(
            mediaSession: MediaSession,
            controller: MediaSession.ControllerInfo,
            mediaItems: List<MediaItem>,
            startIndex: Int,
            startPositionMs: Long,
        ): ListenableFuture<MediaSession.MediaItemsWithStartPosition> =
            Futures.submit(
                java.util.concurrent.Callable {
                    val single = mediaItems.singleOrNull()?.takeIf { it.localConfiguration == null }
                    val queued = single?.let { tree.queueFor(it.mediaId) }
                    if (queued != null) {
                        MediaSession.MediaItemsWithStartPosition(queued.first, queued.second, startPositionMs)
                    } else {
                        MediaSession.MediaItemsWithStartPosition(mediaItems.map { resolve(it) }, startIndex, startPositionMs)
                    }
                },
                resumptionExecutor,
            )

        private fun resolve(item: MediaItem): MediaItem = if (item.localConfiguration != null) item else tree.playable(item.mediaId) ?: item
    }

    override fun onGetSession(controllerInfo: MediaSession.ControllerInfo): MediaLibrarySession? = session

    override fun onDestroy() {
        widget?.let { publisher ->
            session?.player?.removeListener(publisher)
            publisher.clear()
        }
        widget = null
        session?.release()
        session = null
        artworkLoader?.release()
        artworkLoader = null
        PlaybackEngine.releaseService()
        super.onDestroy()
    }

    private fun openAppIntent(): PendingIntent {
        val launch =
            packageManager.getLaunchIntentForPackage(packageName)
                ?: Intent(Intent.ACTION_MAIN).setPackage(packageName)
        launch.addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP)
        return PendingIntent.getActivity(
            this,
            0,
            launch,
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
    }

    companion object {
        private val resumptionExecutor =
            java.util.concurrent.Executors.newSingleThreadExecutor { r ->
                Thread(r, "geode-resumption").apply { isDaemon = true }
            }

        @OptIn(UnstableApi::class)
        @Suppress("ReturnCount")
        internal fun lastPlayedResumption(context: Context): MediaSession.MediaItemsWithStartPosition? {
            SessionStore(context).load()?.let { saved ->
                val items =
                    saved.tracks.map { t ->
                        MediaItem
                            .Builder()
                            .setUri(t.uri)
                            // Distinct per track: MediaMetadata.equals ignores extras, so without
                            // this two untitled tracks compare equal and the platform session skips
                            // the metadata update. artworkUri used to supply this discriminator.
                            .setMediaId(t.uri)
                            .setMediaMetadata(
                                MediaMetadata
                                    .Builder()
                                    .setTitle(t.title)
                                    .setArtist(t.artist.ifBlank { null })
                                    .setExtras(MediaArtwork.embeddedArtExtras(t.uri))
                                    .build(),
                            ).build()
                    }
                return MediaSession.MediaItemsWithStartPosition(items, saved.index, saved.positionMs)
            }
            val last = HistoryStore(context).recentlyPlayed(1).firstOrNull() ?: return null
            val item =
                MediaItem
                    .Builder()
                    .setUri(last.uri)
                    .setMediaId(last.uri)
                    .setMediaMetadata(
                        MediaMetadata
                            .Builder()
                            .setTitle(last.title)
                            .setArtist(last.artist)
                            .setExtras(MediaArtwork.embeddedArtExtras(last.uri))
                            .build(),
                    ).build()
            return MediaSession.MediaItemsWithStartPosition(listOf(item), 0, 0L)
        }

        fun ensureRunning(context: Context) {
            runCatching {
                context.startService(Intent(context, PlaybackService::class.java))
            }
        }

        fun stop(context: Context) {
            runCatching {
                context.stopService(Intent(context, PlaybackService::class.java))
            }
        }
    }
}
