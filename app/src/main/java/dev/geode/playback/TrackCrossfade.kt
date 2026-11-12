package dev.geode.playback

import androidx.media3.common.Player
import dev.geode.audio.dsp.NativeDspProcessor
import dev.geode.engine.bridge.GeodeNative

/**
 * Settings › Playback › Crossfade. One track fades out and the next fades in, back to back on the
 * one player. It is not an overlap: two tracks sounding at once would need a second player mixed
 * into the tap, and the tap stays first and single (docs/AUDIO_CHAIN.md). The owner chose the
 * setting, and this is the shape that keeps the audio chain as it is.
 *
 * The fade itself is native (`core/audio/dsp/GainStage`): half of [crossfadeMs] on each side of the
 * join, counted in the samples of each track, so it lands on the join whether the app is open or
 * closed. This only tells native the length and whether anything follows the current item on its own
 * (a next item, or repeat one); a track nothing follows plays out at full level. A skip, a seek or a
 * pick from the queue starts at full level, as gapless did; native tells those apart from a track that
 * followed on by the end of stream Media3 queues before a gapless hand-off. The tap sits before the
 * DSP, so the visuals keep hearing the music at full level through the fade.
 */
internal class TrackCrossfade(
    private val player: Player,
    private val dsp: NativeDspProcessor,
) : Player.Listener {
    var crossfadeMs: Int = 0
        set(value) {
            field = value
            GeodeNative.dspSetCrossfade(dsp.nativeHandle(), value)
            pushFollowsOn()
        }

    override fun onEvents(
        player: Player,
        events: Player.Events,
    ) {
        if (FOLLOWS_ON_EVENTS.any(events::contains)) pushFollowsOn()
    }

    private fun pushFollowsOn() =
        GeodeNative.dspSetFollowsOn(dsp.nativeHandle(), player.hasNextMediaItem() || player.repeatMode == Player.REPEAT_MODE_ONE)

    private companion object {
        /** What changes whether something follows the current item. */
        val FOLLOWS_ON_EVENTS =
            intArrayOf(
                Player.EVENT_TIMELINE_CHANGED,
                Player.EVENT_MEDIA_ITEM_TRANSITION,
                Player.EVENT_REPEAT_MODE_CHANGED,
                Player.EVENT_SHUFFLE_MODE_ENABLED_CHANGED,
            )
    }
}
