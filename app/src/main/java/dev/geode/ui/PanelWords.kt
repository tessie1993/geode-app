package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import dev.geode.R

/**
 * The Customize panel's words that are not one control's label. Core names a word by its id
 * (core/viz/panel/PanelText) and hands over what fills it; the text is a string resource here, so the
 * shell owns every word it draws. `PanelWordsTest` holds this map to core's ids.
 */
internal object PanelWords {
    val byId: Map<String, Int> =
        linkedMapOf(
            "macro_editor_title" to R.string.macro_editor_title,
            "customize_your_colours" to R.string.customize_your_colours,
            "customize_palette_from_artwork" to R.string.customize_palette_from_artwork,
            "customize_personality" to R.string.customize_personality,
            "customize_new_personality" to R.string.customize_new_personality,
            "customize_song_events" to R.string.customize_song_events,
            "customize_moments" to R.string.customize_moments,
            "customize_reactions" to R.string.customize_reactions,
            "customize_envelopes" to R.string.customize_envelopes,
            "customize_waves" to R.string.customize_waves,
            "customize_randomize_waves" to R.string.customize_randomize_waves,
            "customize_layers" to R.string.customize_layers,
            "customize_shader_code" to R.string.customize_shader_code,
            "customize_rise" to R.string.customize_rise,
            "customize_fall" to R.string.customize_fall,
            "customize_transition_length" to R.string.customize_transition_length,
            "customize_liquid_mode" to R.string.customize_liquid_mode,
            "customize_shape_held" to R.string.customize_shape_held,
            "customize_reaction_summary" to R.string.customize_reaction_summary,
            "customize_target_inert" to R.string.customize_target_inert,
            "customize_target_inert_pick" to R.string.customize_target_inert_pick,
            "customize_label_value" to R.string.customize_label_value,
            "look_summary_custom" to R.string.look_summary_custom,
            "palette_stop" to R.string.palette_stop,
            "palette_stop_add" to R.string.palette_stop_add,
            "palette_stop_remove" to R.string.palette_stop_remove,
            "palette_from_current" to R.string.palette_from_current,
            "palette_name_label" to R.string.palette_name_label,
            "palette_save" to R.string.palette_save,
            "palette_saved_heading" to R.string.palette_saved_heading,
            "palette_none_yet" to R.string.palette_none_yet,
            "palette_edit" to R.string.palette_edit,
            "palette_delete" to R.string.palette_delete,
            "palette_stack_heading" to R.string.palette_stack_heading,
            "palette_stack_explainer" to R.string.palette_stack_explainer,
            "palette_stack_add" to R.string.palette_stack_add,
            "palette_stack_add_yours" to R.string.palette_stack_add_yours,
            "palette_stack_add_mark" to R.string.palette_stack_add_mark,
            "palette_stack_remove" to R.string.palette_stack_remove,
            "palette_stack_up" to R.string.palette_stack_up,
            "palette_stack_amount" to R.string.palette_stack_amount,
            "palette_stack_share" to R.string.palette_stack_share,
            "palette_stack_mix" to R.string.palette_stack_mix,
            "palette_stack_blend" to R.string.palette_stack_blend,
        )

    /** How a native cell opens when it names a word, and what parts the id from what fills it. */
    const val WORD: String = "id:"
    const val UNIT: Char = '\u001F'
}

/** A word the Customize panel draws, by its id; "" for an id the panel does not have. */
@Composable
internal fun panelText(id: String): String = PanelWords.byId[id]?.let { stringResource(it) }.orEmpty()

/** A panel sentence with its placeholders filled by [args]; one that does not fill reads unfilled rather than throwing mid-frame. */
@Composable
internal fun panelFormat(
    id: String,
    vararg args: String,
): String {
    val pattern = panelText(id)
    return runCatching { String.format(pattern, *args) }.getOrDefault(pattern)
}

/** A native row's cell as text: a word core names by id (core/viz/panel/PanelText `ref`) resolved here, anything else as it is. */
@Composable
@Suppress("SpreadOperator")
internal fun cellText(cell: String): String {
    if (!cell.startsWith(PanelWords.WORD)) return cell
    val parts = cell.substring(PanelWords.WORD.length).split(PanelWords.UNIT)
    return if (parts.size == 1) panelText(parts[0]) else panelFormat(parts[0], *parts.drop(1).toTypedArray())
}
