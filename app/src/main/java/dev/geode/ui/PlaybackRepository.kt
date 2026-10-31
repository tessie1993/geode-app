package dev.geode.ui

import kotlinx.coroutines.flow.StateFlow

interface PlaybackRepository {
    val state: StateFlow<PlayerUiState>

    /** The playback position in ms, refreshed on the session's poll tick. */
    val positionMs: StateFlow<Long>

    val queue: StateFlow<QueueUiState>

    val abLoop: StateFlow<AbLoop?>

    val notice: StateFlow<String?>
}

internal class SessionPlaybackRepository(
    private val session: PlayerSession,
) : PlaybackRepository {
    override val state: StateFlow<PlayerUiState> get() = session.uiState

    override val positionMs: StateFlow<Long> get() = session.positionMs

    override val queue: StateFlow<QueueUiState> get() = session.queue

    override val abLoop: StateFlow<AbLoop?> get() = session.abLoop

    override val notice: StateFlow<String?> get() = session.playbackNotice
}
