package dev.geode.ui

import androidx.annotation.StringRes
import dev.geode.R
import dev.geode.engine.bridge.GeodeNative
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
 * core/viz/ReactionDna). The ordinal is the wire value. The menu's order and groups are native's
 * ([MusicMoments.chips]); MusicMomentsTest holds both enums to native's `moment` and `music` rows.
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

/** One action in a moment's menu, with the group native puts it in (`LIGHT`, `MOTION`): a divider sits where the group changes. */
internal data class ActionChip(
    val action: MusicAction,
    val group: String,
)

/** One row as native reads it from what plays now (geode_mod_music_state). */
internal data class MomentState(
    /** The hand's: the "Mine" mark and the ⋮ › Reset to Auto. */
    val mine: Boolean = false,
    /** How many routes play for it; its Strength shows while there are any. */
    val routes: Int = 0,
    val strength: Float = 1f,
    val on: Set<MusicAction> = emptySet(),
    val fits: Set<MusicAction> = MusicAction.entries.toSet(),
)

/** One edit of the reactions, as `ModulationController.editReactions` takes it: a `GeodeNative.MOD_EDIT_*` and its values. */
internal typealias ReactionsEdit = (op: Int, a: Int, b: Int, value: Float) -> Unit

/**
 * Customize › Music's reads of native. Every tap goes back as a [ReactionsEdit]; native works the edit out
 * from the user's saved routes and the hand mask, and hands both back for the shell to save and push.
 */
internal object MusicMoments {
    private const val TAG = "MusicMoments"

    /** The actions whose target reaches [sceneId] (native decides; geode_music_actions_for): the page hides the rest. */
    fun actionsFor(sceneId: String): List<ActionChip> {
        // Native unreachable: show every chip rather than none, and log it.
        var bits = -1
        bestEffort(TAG, "musicActionsFor($sceneId)") { bits = GeodeNative.musicActionsFor(sceneId) }
        return chips(bits)
    }

    /** The actions in [bits] (one per [MusicAction] ordinal), in the menu's order and groups as native's catalog lists them. */
    fun chips(bits: Int): List<ActionChip> =
        ModCatalog.words("music").mapNotNull { w ->
            MusicAction.entries
                .getOrNull(w.ordinal)
                ?.takeIf { bits and (1 shl it.ordinal) != 0 }
                ?.let { ActionChip(it, w.more.firstOrNull().orEmpty()) }
        }

    /** Every row, in [MusicMoment] order, over the user's saved [reactions] and the hand [mask]. */
    fun rows(
        sceneId: String,
        seed: Int,
        reactions: String?,
        mask: Int,
    ): List<MomentState> {
        val f = runCatching { GeodeNative.modMusicState(sceneId, seed, reactions, mask) }.getOrDefault(FloatArray(0))
        val n = GeodeNative.MOD_MUSIC_VALUES
        return MusicMoment.entries.map { m ->
            val o = m.ordinal * n
            if (o + n > f.size) {
                MomentState()
            } else {
                MomentState(f[o] > 0.5f, f[o + 1].toInt(), f[o + 2], actions(f[o + 3]), actions(f[o + 4]))
            }
        }
    }

    private fun actions(bits: Float): Set<MusicAction> {
        val b = bits.toInt()
        return MusicAction.entries.filterTo(mutableSetOf()) { b and (1 shl it.ordinal) != 0 }
    }
}
