package dev.geode.playback

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import androidx.annotation.OptIn
import androidx.core.content.ContextCompat
import androidx.media3.common.MediaItem
import androidx.media3.common.MediaMetadata
import androidx.media3.common.Player
import androidx.media3.common.util.UnstableApi
import androidx.media3.session.CacheBitmapLoader
import androidx.media3.session.DefaultMediaNotificationProvider
import androidx.media3.session.LibraryResult
import androidx.media3.session.MediaLibraryService
import androidx.media3.session.MediaSession
import androidx.media3.session.SessionCommand
import androidx.media3.session.SessionError
import androidx.media3.session.SessionResult
import com.google.common.collect.ImmutableList
import com.google.common.util.concurrent.Futures
import com.google.common.util.concurrent.ListenableFuture
import dev.geode.RingLog
import dev.geode.data.HistoryStore
import dev.geode.data.SessionStore
import dev.geode.widget.WidgetPublisher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel

@OptIn(UnstableApi::class)
class PlaybackService : MediaLibraryService() {
    private var session: MediaLibrarySession? = null
    private var artworkLoader: SessionBitmapLoader? = null
    private var widget: WidgetPublisher? = null
    private var buttons: NotificationButtons? = null
    private var queueEnd: QueueEnd? = null
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)

    override fun onCreate() {
        super.onCreate()
        val loader = SessionBitmapLoader(this)
        artworkLoader = loader
        val player = PlaybackEngine.acquireForService(this).player
        val tree = LibraryTree(this)
        val notificationButtons = NotificationButtons(this, player).also { buttons = it }
        queueEnd = QueueEnd(this, player, tree, scope).also { player.addListener(it) }
        // Media3's default status-bar icon is a generic note; ours is the mark's silhouette.
        setMediaNotificationProvider(
            DefaultMediaNotificationProvider.Builder(this).build().apply {
                setSmallIcon(dev.geode.R.drawable.ic_stat_muzviz)
            },
        )
        session =
            MediaLibrarySession
                .Builder(this, player, LibraryCallback(this, tree, notificationButtons))
                .setSessionActivity(openAppIntent())
                .setMediaButtonPreferences(notificationButtons.buttons())
                .setBitmapLoader(CacheBitmapLoader(loader))
                .build()
                // The app plays through PlaybackEngine directly, so no MediaController ever
                // connects to fetch the session through onGetSession. Until a session is added,
                // MediaSessionService neither posts the playback notification nor calls
                // startForeground, and the startForegroundService() from ensureRunning() ends in
                // ForegroundServiceDidNotStartInTimeException a few seconds into every track.
                .also { addSession(it) }
        session?.let(notificationButtons::attach)
        player.addListener(notificationButtons)
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
        private val buttons: NotificationButtons,
    ) : MediaLibrarySession.Callback {
        /**
         * Who may browse the library, and who may only press play: [LibraryAccess] decides,
         * and an untrusted caller gets the transport (play, pause, skip, seek) and what is
         * playing now. Media3 then refuses the library commands to it before any callback runs;
         * the checks in the browse callbacks below only back that up.
         */
        override fun onConnect(
            session: MediaSession,
            controller: MediaSession.ControllerInfo,
        ): MediaSession.ConnectionResult =
            if (mayBrowse(controller)) {
                MediaSession.ConnectionResult
                    .AcceptedResultBuilder()
                    .setAvailableSessionCommands(buttons.commands(MediaSession.ConnectionResult.DEFAULT_SESSION_AND_LIBRARY_COMMANDS))
                    .setAvailablePlayerCommands(MediaSession.ConnectionResult.DEFAULT_PLAYER_COMMANDS)
                    .build()
            } else {
                MediaSession.ConnectionResult
                    .AcceptedResultBuilder()
                    .setAvailableSessionCommands(buttons.commands(MediaSession.ConnectionResult.DEFAULT_UNTRUSTED_SESSION_COMMANDS))
                    .setAvailablePlayerCommands(TRANSPORT_ONLY)
                    .build()
            }

        /**
         * Asked again by every browse and queue callback rather than remembered from
         * [onConnect]: the answer costs two package-manager reads, and a cache keyed by
         * [MediaSession.ControllerInfo] would hang Android Auto's browsing on how Media3 matches
         * a legacy browser's later calls to its connection.
         */
        private fun mayBrowse(controller: MediaSession.ControllerInfo): Boolean {
            if (controller.isTrusted) return true
            val pm = context.packageManager
            val pkg = controller.packageName
            val systemApp =
                runCatching {
                    pm.getApplicationInfo(pkg, 0).flags and
                        (ApplicationInfo.FLAG_SYSTEM or ApplicationInfo.FLAG_UPDATED_SYSTEM_APP) != 0
                }.getOrDefault(false)
            return LibraryAccess.mayBrowse(
                CallerFacts(
                    packageName = pkg,
                    trusted = false,
                    packageVerified = controller.isPackageNameVerified,
                    systemApp = systemApp,
                    installer = runCatching { installerOf(pm, pkg) }.getOrNull(),
                ),
            )
        }

        @Suppress("DEPRECATION")
        private fun installerOf(
            pm: PackageManager,
            pkg: String,
        ): String? =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                pm.getInstallSourceInfo(pkg).installingPackageName
            } else {
                pm.getInstallerPackageName(pkg)
            }

        /** The media notification's extra buttons ([NotificationButtons]); anything else is not ours. */
        override fun onCustomCommand(
            session: MediaSession,
            controller: MediaSession.ControllerInfo,
            customCommand: SessionCommand,
            args: Bundle,
        ): ListenableFuture<SessionResult> =
            buttons.onCustomCommand(customCommand)?.let { Futures.immediateFuture(it) }
                ?: super.onCustomCommand(session, controller, customCommand, args)

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
        ): ListenableFuture<LibraryResult<MediaItem>> {
            // A caller that may not browse connects to an empty root: the legacy browser
            // protocol has no "accepted but no library", and an error here reads as a crash.
            if (!mayBrowse(browser)) return Futures.immediateFuture(LibraryResult.ofItem(EMPTY_ROOT, params))
            // Coarse cache invalidation: a browser re-entering at the root (a fresh Android Auto
            // connection, or a manual refresh) is the signal to drop the memoised track list so
            // library edits made elsewhere in the app are picked up.
            tree.invalidate()
            return Futures.immediateFuture(LibraryResult.ofItem(tree.root(), params))
        }

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
                    val all = if (mayBrowse(browser)) tree.children(parentId) else emptyList()
                    // page * pageSize overflows Int when Media3 passes pageSize = Integer.MAX_VALUE
                    // (its "everything" request), which drove `from` negative and crashed subList.
                    // Do the multiply in Long, then clamp both ends into 0..all.size.
                    val from = (page.toLong() * pageSize.toLong()).coerceIn(0L, all.size.toLong()).toInt()
                    val until = (from.toLong() + pageSize.toLong()).coerceIn(from.toLong(), all.size.toLong()).toInt()
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
                    when {
                        !mayBrowse(browser) -> LibraryResult.ofError(SessionError.ERROR_PERMISSION_DENIED)
                        else ->
                            tree.item(mediaId)?.let { LibraryResult.ofItem(it, null) }
                                ?: LibraryResult.ofError(SessionError.ERROR_BAD_VALUE)
                    }
                },
                resumptionExecutor,
            )

        override fun onAddMediaItems(
            mediaSession: MediaSession,
            controller: MediaSession.ControllerInfo,
            mediaItems: List<MediaItem>,
        ): ListenableFuture<List<MediaItem>> =
            Futures.submit(
                java.util.concurrent.Callable { fromCaller(controller, mediaItems).map { resolve(it) } },
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
                    val items = fromCaller(controller, mediaItems)
                    val single = items.singleOrNull()?.takeIf { it.localConfiguration == null }
                    val queued = single?.let { tree.queueFor(it.mediaId) }
                    if (queued != null) {
                        MediaSession.MediaItemsWithStartPosition(queued.first, queued.second, startPositionMs)
                    } else {
                        MediaSession.MediaItemsWithStartPosition(items.map { resolve(it) }, startIndex, startPositionMs)
                    }
                },
                resumptionExecutor,
            )

        /**
         * A caller that may not browse queues nothing. ExoPlayer opens a URI with this app's own
         * grants, so a content:// or file path from such a caller would be played, and its tags
         * shown as "now playing", with access the caller never had; and a bare media id, run
         * through our tree, would let it probe the library one guessed id at a time through the
         * current item it may read. Media3 already refuses it the queue commands, so this only
         * backs that up; the failed future goes back to the caller as an error.
         */
        private fun fromCaller(
            controller: MediaSession.ControllerInfo,
            items: List<MediaItem>,
        ): List<MediaItem> {
            if (!mayBrowse(controller)) throw SecurityException("${controller.packageName} may not queue media")
            return items
        }

        private fun resolve(item: MediaItem): MediaItem = if (item.localConfiguration != null) item else tree.playable(item.mediaId) ?: item
    }

    override fun onGetSession(controllerInfo: MediaSession.ControllerInfo): MediaLibrarySession? = session

    override fun onDestroy() {
        session?.player?.let { player ->
            buttons?.let(player::removeListener)
            queueEnd?.let(player::removeListener)
        }
        buttons?.detach()
        buttons = null
        queueEnd = null
        scope.cancel()
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
        /** What a caller that may not browse connects to: a folder with nothing in it. */
        private val EMPTY_ROOT: MediaItem =
            MediaItem
                .Builder()
                .setMediaId("geode:no-library")
                .setMediaMetadata(
                    MediaMetadata
                        .Builder()
                        .setIsBrowsable(true)
                        .setIsPlayable(false)
                        .build(),
                ).build()

        /**
         * An untrusted controller's player commands: Media3's read-only set (state, position,
         * the current item and its metadata) minus the timeline, which is the whole queue,
         * plus the transport a headset or a third-party remote needs.
         */
        private val TRANSPORT_ONLY: Player.Commands =
            MediaSession.ConnectionResult.DEFAULT_UNTRUSTED_PLAYER_COMMANDS
                .buildUpon()
                .remove(Player.COMMAND_GET_TIMELINE)
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
                    Player.COMMAND_SEEK_BACK,
                    Player.COMMAND_SEEK_FORWARD,
                ).build()

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
            // startService() throws IllegalStateException on API 26+ when called while the app is
            // backgrounded (this is invoked from onIsPlayingChanged, which can fire off-screen), and
            // runCatching used to swallow that silently, leaving the service never started.
            // startForegroundService() is allowed from the background, and MediaSessionService (which
            // PlaybackService extends) promotes itself to the foreground and posts the playback
            // notification as soon as its session's player reports isPlaying, satisfying the 5s window.
            runCatching {
                ContextCompat.startForegroundService(context, Intent(context, PlaybackService::class.java))
            }.onFailure { RingLog.note("PlaybackService.ensureRunning", "startForegroundService failed", it) }
        }

        fun stop(context: Context) {
            runCatching {
                context.stopService(Intent(context, PlaybackService::class.java))
            }
        }
    }
}

/**
 * What [LibraryAccess] needs to know about a controller, read once from the platform:
 * [MediaSession.ControllerInfo] for the first three, the package manager for the rest.
 */
internal data class CallerFacts(
    val packageName: String,
    /** [MediaSession.ControllerInfo.isTrusted]. */
    val trusted: Boolean,
    /** [MediaSession.ControllerInfo.isPackageNameVerified]: the name belongs to the caller's uid. */
    val packageVerified: Boolean,
    /** Preinstalled on the system image, updated or not. */
    val systemApp: Boolean,
    /** The package that installed it; null when sideloaded or unknown. */
    val installer: String?,
)

/**
 * Who may browse the library, and who may only press play.
 *
 * [PlaybackService] is exported because System UI, Bluetooth, watches and Android Auto all
 * reach it from other processes, and nothing in the manifest can tell them apart from any other
 * installed app. Left to itself, Media3 1.11 gives an untrusted caller the read-only library
 * commands: any app, with no READ_MEDIA_AUDIO grant of its own, could list every title, artist
 * and folder on the device through us. That is the wrong way round for a music library.
 * Browsing goes only to:
 * - trusted controllers: this app, the system uid, holders of MEDIA_CONTENT_CONTROL (System UI,
 *   Bluetooth) and enabled notification listeners;
 * - apps preinstalled on the system image (Android Auto on Android 10+, an Automotive head
 *   unit's media centre, the OEM's own launcher), when the name is verified;
 * - Android Auto, Wear OS and the Google app installed from the store, recognised by name *and*
 *   by Google Play being the installer. Play never lets anyone else publish under those package
 *   names, so an app that borrows one gets nothing unless it was sideloaded and also lies about
 *   its installer, which takes adb. A signature check would be stricter in theory, but it needs
 *   those apps' certificates pinned here, and a pin that is wrong or rotated silently cuts
 *   Android Auto off.
 */
internal object LibraryAccess {
    /** Store-installed media surfaces that browse: Android Auto, Wear OS, the Google app (Assistant). */
    private val GOOGLE_BROWSERS =
        setOf(
            "com.google.android.projection.gearhead",
            "com.google.android.wearable.app",
            "com.google.android.googlequicksearchbox",
        )

    private const val PLAY_STORE = "com.android.vending"

    fun mayBrowse(caller: CallerFacts): Boolean =
        caller.trusted ||
            (caller.packageVerified && caller.systemApp) ||
            (caller.packageVerified && caller.packageName in GOOGLE_BROWSERS && caller.installer == PLAY_STORE)
}
