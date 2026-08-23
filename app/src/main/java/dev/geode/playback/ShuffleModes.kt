package dev.geode.playback

import kotlin.math.ln
import kotlin.random.Random

data class ShuffleTrack(
    val album: String = "",
    val artist: String = "",
    val weight: Double = 1.0,
)

sealed interface ShuffleMode {
    data object InOrder : ShuffleMode

    data object Tracks : ShuffleMode

    data object Albums : ShuffleMode

    data object Spread : ShuffleMode

    data object Weighted : ShuffleMode
}

object ShuffleModes {
    fun plan(
        mode: ShuffleMode,
        tracks: List<ShuffleTrack>,
        seed: Long,
    ): List<Int> =
        when (mode) {
            ShuffleMode.InOrder -> tracks.indices.toList()
            ShuffleMode.Tracks -> tracks.indices.shuffled(Random(seed))
            ShuffleMode.Albums -> byAlbum(tracks, Random(seed))
            ShuffleMode.Spread -> spreadArtists(tracks, Random(seed))
            ShuffleMode.Weighted -> byWeight(tracks, Random(seed))
        }

    private fun byAlbum(
        tracks: List<ShuffleTrack>,
        random: Random,
    ): List<Int> {
        val albums = tracks.indices.groupBy { tracks[it].album }
        return albums.keys.shuffled(random).flatMap { albums.getValue(it) }
    }

    private fun spreadArtists(
        tracks: List<ShuffleTrack>,
        random: Random,
    ): List<Int> {
        val remaining = LinkedHashMap<String, ArrayDeque<Int>>()
        for (i in tracks.indices.shuffled(random)) {
            remaining.getOrPut(tracks[i].artist) { ArrayDeque() }.addLast(i)
        }
        val order = ArrayList<Int>(tracks.size)
        var last: String? = null
        while (remaining.isNotEmpty()) {
            val artist = fullestOtherThan(remaining, last)
            val queue = remaining.getValue(artist)
            order += queue.removeFirst()
            if (queue.isEmpty()) remaining.remove(artist)
            last = artist
        }
        return order
    }

    private fun fullestOtherThan(
        remaining: Map<String, ArrayDeque<Int>>,
        last: String?,
    ): String =
        remaining.entries
            .filter { it.key != last }
            .maxByOrNull { it.value.size }
            ?.key
            ?: remaining.keys.first()

    private fun byWeight(
        tracks: List<ShuffleTrack>,
        random: Random,
    ): List<Int> =
        tracks.indices
            .filter { tracks[it].weight.isFinite() && tracks[it].weight > 0.0 }
            .map { it to ln(1.0 - random.nextDouble()) / tracks[it].weight }
            .sortedByDescending { (_, key) -> key }
            .map { (index, _) -> index }
}
