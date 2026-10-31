package dev.geode.model

data class LibraryTrack(
    val uri: String,
    val title: String,
    val artist: String = "",
    val durationMs: Long = 0L,
    val album: String = "",
    val genre: String = "",
    val year: Int = 0,
    val trackNo: Int = 0,
    val comment: String = "",
    val fileName: String = "",
    val sizeBytes: Long = 0L,
)
