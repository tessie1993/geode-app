package dev.geode.playback

import androidx.media3.common.PlaybackException

object PlaybackErrors {
    sealed interface Action {
        data object SkipToNext : Action

        data object StopEndOfQueue : Action

        data object StopSourceUnavailable : Action
    }

    const val MAX_CONSECUTIVE_FAILURES: Int = 3

    fun decide(
        consecutiveFailures: Int,
        hasNext: Boolean,
    ): Action =
        when {
            consecutiveFailures >= MAX_CONSECUTIVE_FAILURES -> Action.StopSourceUnavailable
            !hasNext -> Action.StopEndOfQueue
            else -> Action.SkipToNext
        }

    fun describe(
        errorCode: Int,
        trackTitle: String?,
        action: Action,
    ): String {
        val subject = trackTitle?.takeIf { it.isNotBlank() }?.let { "“$it”" } ?: "This track"
        val cause =
            when (errorCode) {
                PlaybackException.ERROR_CODE_IO_FILE_NOT_FOUND ->
                    "$subject is missing. It may have been moved, renamed or deleted."

                PlaybackException.ERROR_CODE_IO_NO_PERMISSION ->
                    "Geode can no longer read $subject. Re-add the folder to restore access."

                PlaybackException.ERROR_CODE_DECODING_FAILED,
                PlaybackException.ERROR_CODE_DECODER_INIT_FAILED,
                PlaybackException.ERROR_CODE_DECODER_QUERY_FAILED,
                ->
                    "$subject could not be decoded. This device may not support its format."

                PlaybackException.ERROR_CODE_IO_UNSPECIFIED,
                PlaybackException.ERROR_CODE_IO_READ_POSITION_OUT_OF_RANGE,
                ->
                    "$subject could not be read. The file may be damaged or the storage unavailable."

                PlaybackException.ERROR_CODE_PARSING_CONTAINER_MALFORMED,
                PlaybackException.ERROR_CODE_PARSING_MANIFEST_MALFORMED,
                ->
                    "$subject is not a file Geode can open — its contents do not match its type."

                else -> "$subject could not be played."
            }
        val consequence =
            when (action) {
                Action.SkipToNext -> " Skipping to the next track."
                Action.StopEndOfQueue -> " Nothing else is queued, so playback stopped."
                Action.StopSourceUnavailable ->
                    " Several tracks in a row are unavailable, so playback stopped — " +
                        "the storage or the folder permission is probably gone."
            }
        return cause + consequence
    }
}
