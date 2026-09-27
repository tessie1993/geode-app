package dev.geode.playback

/** Playback intent survives a temporary interruption, but never a user pause or permanent loss. */
internal class PlaybackFocusState {
    enum class RequestResult {
        GRANTED,
        DELAYED,
        DENIED,
    }

    enum class Change {
        GAIN,
        LOSS,
        TRANSIENT_LOSS,
        DUCK,
    }

    data class State(
        val playRequested: Boolean = false,
        val canPlay: Boolean = false,
        val volumeMultiplier: Float = 1f,
    )

    var state = State()
        private set

    fun request(result: RequestResult): State {
        state =
            when (result) {
                RequestResult.GRANTED -> State(playRequested = true, canPlay = true)
                RequestResult.DELAYED -> State(playRequested = true)
                RequestResult.DENIED -> State()
            }
        return state
    }

    fun pause(): State {
        state = State()
        return state
    }

    fun change(change: Change): State {
        if (!state.playRequested) return state
        state =
            when (change) {
                Change.GAIN -> State(playRequested = true, canPlay = true)
                Change.LOSS -> State()
                Change.TRANSIENT_LOSS -> State(playRequested = true)
                Change.DUCK -> State(playRequested = true, canPlay = true, volumeMultiplier = 0.2f)
            }
        return state
    }
}
