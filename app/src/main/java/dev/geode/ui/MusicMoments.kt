package dev.geode.ui

import androidx.annotation.StringRes
import dev.geode.R
import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.HandReactions
import dev.geode.render.bridge.ModConfigCodec
import dev.geode.render.scene.BuildStyleReactionsUseCase
import dev.geode.util.bestEffort

/**
 * A musical moment on Customize › Music: one row ("Kick", "Drop") that the user gives actions to.
 *
 * Only the row's label lives here. What a moment is (its sources, the one it follows, the one it fires
 * on, how long it is felt), which moments are the user's and how their routes merge with the live DNA
 * are native (ADR 0003): `Moment` and `hand::` in core/viz/Reactions.hpp, the rest in
 * core/viz/ReactionDna. The ordinal is the wire value, so the order is the native enum's.
 */
internal enum class MusicMoment(
    @StringRes val label: Int,
) {
    KICK(R.string.music_moment_kick),
    SNARE(R.string.music_moment_snare),
    HAT(R.string.music_moment_hat),
    BASS(R.string.music_moment_bass),
    DROP(R.string.music_moment_drop),
    BUILD_UP(R.string.music_moment_build_up),
    CHORD_CHANGE(R.string.music_moment_chord_change),
    NEW_SECTION(R.string.music_moment_new_section),
    NOTE(R.string.music_moment_note),
    LOUDNESS(R.string.music_moment_loudness),
}

/**
 * What a moment can do to the picture, one chip each. Only the label lives here: the route each one
 * deals (target, amount, smoothing, step or follow) is native beside the DNA (`MusicAction` in
 * core/viz/ReactionDna). The ordinal is the wire value.
 */
internal enum class MusicAction(
    @StringRes val label: Int,
) {
    FLASH(R.string.music_action_flash),
    ZOOM(R.string.music_action_zoom),
    SHAKE(R.string.music_action_shake),
    COLOUR(R.string.music_action_colour),
    CAMERA(R.string.music_action_camera),
    WARP(R.string.music_action_warp),
}

/** One row as native reads it from what plays now (geode_music_state). */
internal data class MomentState(
    /** The hand's: the "Mine" mark and the ⋮ › Reset to Auto. */
    val mine: Boolean = false,
    /** How many routes play for it; its Strength shows while there are any. */
    val routes: Int = 0,
    val strength: Float = 1f,
    val on: Set<MusicAction> = emptySet(),
    val fits: Set<MusicAction> = MusicAction.entries.toSet(),
)

/**
 * Customize › Music's calls into native. Each tap sends the moment, the action or the Strength, and the
 * user's side ([HandReactions]: their routes and the hand mask); native works the edit out and hands both
 * back, for the shell to save (LfoStore) and push (`HandMask`).
 */
internal object MusicMoments {
    /** The top of each moment's Strength slider (core/viz `kMaxMomentStrength`). */
    const val MAX_STRENGTH = 2f

    private const val OP_TOGGLE = 0
    private const val OP_STRENGTH = 1
    private const val OP_RESET = 2
    private const val TAG = "MusicMoments"

    /** The actions whose target reaches [sceneId] (native decides; geode_music_actions_for): the page hides the rest. */
    fun actionsFor(sceneId: String): List<MusicAction> {
        // Native unreachable: show every chip rather than none, and log it.
        var bits = -1
        bestEffort(TAG, "musicActionsFor($sceneId)") { bits = GeodeNative.musicActionsFor(sceneId) }
        return MusicAction.entries.filter { bits and (1 shl it.ordinal) != 0 }
    }

    /** Every row, in [MusicMoment] order. */
    fun rows(
        sceneId: String,
        seed: Int,
        hand: HandReactions,
    ): List<MomentState> {
        val f = runCatching { GeodeNative.musicState(sceneId, seed, pack(hand), hand.mask) }.getOrDefault(FloatArray(0))
        val n = GeodeNative.MUSIC_STATE_FLOATS
        return MusicMoment.entries.map { m ->
            val o = m.ordinal * n
            if (o + n > f.size) {
                MomentState()
            } else {
                MomentState(f[o] > 0.5f, f[o + 1].toInt(), f[o + 2], actions(f[o + 3]), actions(f[o + 4]))
            }
        }
    }

    fun toggle(
        sceneId: String,
        seed: Int,
        hand: HandReactions,
        moment: MusicMoment,
        action: MusicAction,
    ): HandReactions = edit(sceneId, seed, hand, OP_TOGGLE, moment, action, 0f)

    fun withStrength(
        sceneId: String,
        seed: Int,
        hand: HandReactions,
        moment: MusicMoment,
        value: Float,
    ): HandReactions = edit(sceneId, seed, hand, OP_STRENGTH, moment, null, value)

    fun resetToAuto(
        sceneId: String,
        seed: Int,
        hand: HandReactions,
        moment: MusicMoment,
    ): HandReactions = edit(sceneId, seed, hand, OP_RESET, moment, null, 0f)

    private fun edit(
        sceneId: String,
        seed: Int,
        hand: HandReactions,
        op: Int,
        moment: MusicMoment,
        action: MusicAction?,
        value: Float,
    ): HandReactions {
        val out = IntArray(2)
        val floats =
            runCatching {
                GeodeNative.musicEdit(sceneId, seed, pack(hand), hand.mask, op, moment.ordinal, action?.ordinal ?: 0, value, out)
            }.getOrNull() ?: return hand
        return HandReactions(if (out[1] == 1) ModConfigCodec.unpackReactions(floats) else null, out[0])
    }

    private fun pack(hand: HandReactions): FloatArray? = hand.routes?.let { BuildStyleReactionsUseCase.packExact(it) }

    private fun actions(bits: Float): Set<MusicAction> {
        val b = bits.toInt()
        return MusicAction.entries.filterTo(mutableSetOf()) { b and (1 shl it.ordinal) != 0 }
    }
}
