package dev.geode.ui

import kotlinx.coroutines.flow.StateFlow

interface PlaybackRepository {
    val state: StateFlow<PlayerUiState>

    val queue: StateFlow<QueueUiState>

    val abLoop: StateFlow<AbLoop?>

    val notice: StateFlow<String?>
}

internal class SessionPlaybackRepository(
    private val session: PlayerSession,
) : PlaybackRepository {
    override val state: StateFlow<PlayerUiState> get() = session.uiState

    override val queue: StateFlow<QueueUiState> get() = session.queue

    override val abLoop: StateFlow<AbLoop?> get() = session.abLoop

    override val notice: StateFlow<String?> get() = session.playbackNotice
}
