package dev.geode.model

data class QueueTrack(
    val uri: String,
    val title: String = "",
    val artist: String = "",
)

data class QueueUiState(
    val tracks: List<QueueTrack> = emptyList(),
    val index: Int = 0,
)
