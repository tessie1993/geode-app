package dev.geode.data

/** One track as the MediaStore reports it; app-side edits to it live in [LibraryTrack]. */
data class DeviceTrack(
    val uri: String,
    val title: String,
    val artist: String,
    val album: String,
    val folder: String,
    val durationMs: Long,
    val addedSec: Long = 0L,
)
