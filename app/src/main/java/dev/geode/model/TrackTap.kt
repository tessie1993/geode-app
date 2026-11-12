package dev.geode.model

/**
 * What a tap on a track in Library does (Settings › Preferences › Library). [PLAY_LIST] is the tap
 * as it was before the choice existed: the track plays, and the list it was tapped in becomes the
 * queue. The ⋮ menu still offers every other way, whatever is chosen here.
 *
 * [PLAY_NEXT] and [ADD_TO_QUEUE] only make sense beside something already loaded: with nothing in
 * the player a tap plays as [PLAY_LIST] does, so the first tap of a session is never a silent no-op.
 */
enum class TrackTap {
    PLAY_LIST,
    PLAY_ALONE,
    PLAY_NEXT,
    ADD_TO_QUEUE,
    ;

    companion object {
        fun fromName(name: String?): TrackTap = entries.firstOrNull { it.name == name } ?: PLAY_LIST
    }
}
