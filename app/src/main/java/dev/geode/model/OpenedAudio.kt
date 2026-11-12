package dev.geode.model

import java.util.UUID

/**
 * The rules for an audio file another app opens in this one ("Open with", `ACTION_VIEW`). Playing is
 * not importing: the file is streamed from the sender's content uri, never copied, never added to the
 * library, history or the saved resumption queue, because the read grant dies with the sender's task
 * and a uri kept past it could only fail next launch. Everything here is a value the intent or the
 * provider handed over, so each is treated as hostile; the platform calls live in
 * `data/OpenedAudioSource`.
 */
object OpenedAudio {
    /** Marks a queue item as an opened file, so `PlayRecorder` leaves it out of history and resumption. */
    const val MEDIA_ID_PREFIX = "geode:opened:"

    /** The longest name shown for an opened file, in code points. */
    const val MAX_NAME_CODE_POINTS = 120

    private const val ACTION_VIEW = "android.intent.action.VIEW"
    private const val SCHEME_CONTENT = "content"
    private val EXTRA_AUDIO_TYPES = setOf("application/ogg", "application/x-flac")

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
     * Routes a delivered intent from its action, its data uri's scheme and last path segment, and the
     * type the sender declared; nothing else in the intent is read. Only `content` is taken: a `file`
     * uri is how the app's own files would be reached (`FileUriExposedException` keeps honest senders
     * off it since Android 7). The type only routes, the player sniffs the real format; an untyped view
     * is taken unless its name ends in `.milk`, the one other file the app opens.
     */
    fun route(
        action: String?,
        scheme: String?,
        type: String?,
        lastSegment: String?,
        fromHistory: Boolean,
    ): Route {
        if (action != ACTION_VIEW || scheme != SCHEME_CONTENT) return Route.NotOurs
        val audio = if (type == null) !isMilkName(lastSegment) else isAudioType(type)
        return when {
            !audio -> Route.NotOurs
            fromHistory -> Route.Stale
            else -> Route.Open
        }
    }

    fun isAudioType(type: String): Boolean {
        val base = type.substringBefore(';').trim().lowercase()
        return (base.startsWith("audio/") && base.length > "audio/".length) || base in EXTRA_AUDIO_TYPES
    }

    private fun isMilkName(segment: String?): Boolean = segment?.trim()?.endsWith(".milk", ignoreCase = true) == true

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
    ): Boolean =
        when {
            scheme != SCHEME_CONTENT || authority.isNullOrBlank() -> true
            authority == ownPackage || authority.startsWith("$ownPackage.") -> true
            providerPackage == null || providerPackage == ownPackage -> true
            else -> !readGranted
        }

    /**
     * The provider's display name made fit to show: control, format (bidi overrides, zero-width),
     * private-use, unassigned and lone surrogate code points dropped, whitespace runs folded to one
     * space, a file extension cut, and at most [MAX_NAME_CODE_POINTS] code points. Null when nothing is left.
     */
    fun displayName(raw: String?): String? {
        if (raw.isNullOrEmpty()) return null
        val clean = StringBuilder()
        var count = 0
        var spacePending = false
        raw.codePoints().forEach { cp ->
            when {
                count >= MAX_NAME_CODE_POINTS -> Unit
                Character.isWhitespace(cp) || Character.isSpaceChar(cp) -> spacePending = clean.isNotEmpty()
                isHidden(cp) -> Unit
                else -> {
                    if (spacePending) {
                        clean.append(' ')
                        count++
                        spacePending = false
                    }
                    if (count < MAX_NAME_CODE_POINTS) {
                        clean.appendCodePoint(cp)
                        count++
                    }
                }
            }
        }
        val name = clean.toString()
        val extension = name.substringAfterLast('.', "")
        val stem = if (EXTENSION.matches(extension)) name.substringBeforeLast('.').trimEnd() else name
        return stem.ifEmpty { name }.ifEmpty { null }
    }

    private val EXTENSION = Regex("[A-Za-z0-9]{1,5}")

    private fun isHidden(cp: Int): Boolean =
        when (Character.getType(cp).toByte()) {
            Character.CONTROL,
            Character.FORMAT,
            Character.PRIVATE_USE,
            Character.SURROGATE,
            Character.UNASSIGNED,
            -> true

            else -> false
        }

    /** A fresh queue id for an opened file: distinct per open, and carrying no part of the uri. */
    fun mediaId(): String = MEDIA_ID_PREFIX + UUID.randomUUID()

    fun isOpened(mediaId: String?): Boolean = mediaId?.startsWith(MEDIA_ID_PREFIX) == true
}
