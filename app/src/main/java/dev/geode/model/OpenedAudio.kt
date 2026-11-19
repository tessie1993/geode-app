package dev.geode.model

import dev.geode.engine.bridge.ShellNative
import java.security.SecureRandom

/**
 * The rules for an audio file another app opens in this one ("Open with", `ACTION_VIEW`). Playing is
 * not importing: the file is streamed from the sender's content uri, never copied, never added to the
 * library, history or the saved resumption queue, because the read grant dies with the sender's task
 * and a uri kept past it could only fail next launch. Everything here is a value the intent or the
 * provider handed over, so each is treated as hostile. The rules are native's (core/shell/OpenedAudio
 * through [ShellNative]); the platform calls live in `data/OpenedAudioSource`.
 */
object OpenedAudio {
    private val random = SecureRandom()

    /** What the activity does with a delivered intent. */
    enum class Route {
        /** Not an opened audio file: left for the other handlers. */
        NotOurs,

        /** An opened audio file delivered again from Recents, after its grant may have ended: dropped. */
        Stale,

        /** An opened audio file to play. */
        Open,
    }

    /**
     * Routes a delivered intent from its action, its data uri's scheme, and the
     * type the sender declared; nothing else in the intent is read. Only `content` is taken: a `file`
     * uri is how the app's own files would be reached (`FileUriExposedException` keeps honest senders
     * off it since Android 7). The type only routes, the player sniffs the real format.
     */
    fun route(
        action: String?,
        scheme: String?,
        type: String?,
        fromHistory: Boolean,
    ): Route =
        when (ShellNative.openedAudioRoute(action, scheme, type, fromHistory)) {
            ShellNative.OPENED_OPEN -> Route.Open
            ShellNative.OPENED_STALE -> Route.Stale
            else -> Route.NotOurs
        }

    /**
     * Whether the app must refuse to play the uri. [providerPackage] is the package that owns the uri's
     * authority (null when no provider answers to it) and [readGranted] whether this app holds an
     * explicit read grant on it. The app's own providers are refused (a confused deputy: another app
     * steering it at its own data), and so is a uri nobody granted, so a sender cannot borrow the app's
     * own storage or media permission to reach a file it could not read itself.
     */
    fun refuses(
        scheme: String?,
        authority: String?,
        ownPackage: String,
        providerPackage: String?,
        readGranted: Boolean,
    ): Boolean = ShellNative.openedAudioRefuses(scheme, authority, ownPackage, providerPackage, readGranted)

    /**
     * The provider's display name made fit to show: control, format (bidi overrides, zero-width),
     * private-use, unassigned and lone surrogate code points dropped, whitespace runs folded to one
     * space, a file extension cut, and a length cap. Null when nothing is left.
     */
    fun displayName(raw: String?): String? = ShellNative.openedAudioDisplayName(raw)

    /** A fresh queue id for an opened file: distinct per open, and carrying no part of the uri. */
    fun mediaId(): String = ShellNative.openedMediaId(ByteArray(16).also(random::nextBytes))

    /** Whether [mediaId] marks an opened file, so `PlayRecorder` leaves it out of history and resumption. */
    fun isOpened(mediaId: String?): Boolean = ShellNative.openedAudioIsOpened(mediaId)
}
