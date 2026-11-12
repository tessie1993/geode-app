package dev.geode.ui.cast

import androidx.annotation.StringRes
import dev.geode.R
import dev.geode.cast.LinkState
import dev.geode.engine.bridge.GeodeNative

/** True while the stream runs for the link (starting or live): the sheet has something to show. */
internal val LinkState.running: Boolean get() = this is LinkState.Starting || this is LinkState.Live

/** The value Visuals › Settings shows beside "Show on a computer". */
@StringRes
internal fun linkRowValue(
    state: LinkState,
    locked: Boolean,
): Int =
    when {
        state is LinkState.Live -> R.string.link_row_live
        state is LinkState.Starting -> R.string.link_row_starting
        locked -> R.string.link_row_locked
        else -> R.string.link_row_idle
    }

/** What TalkBack hears for the stage's Share to PC icon. */
@StringRes
internal fun linkButtonLabel(
    state: LinkState,
    locked: Boolean,
): Int =
    when {
        state.running -> R.string.link_button_live
        locked -> R.string.link_row_locked
        else -> R.string.link_button
    }

/** What the sheet says the share is doing. */
@StringRes
internal fun linkSheetStatus(state: LinkState): Int? =
    when (state) {
        is LinkState.Live -> R.string.link_sheet_live
        LinkState.Starting -> R.string.link_sheet_starting
        else -> null
    }

/** Why a share ended, once (a GEODE_LINK_REASON_*). */
@StringRes
internal fun linkEndedMessage(reason: Int): Int =
    when (reason) {
        GeodeNative.LINK_REASON_NO_LAN -> R.string.link_ended_no_lan
        GeodeNative.LINK_REASON_BACKGROUND -> R.string.link_ended_background
        GeodeNative.LINK_REASON_NETWORK -> R.string.link_ended_network
        GeodeNative.LINK_REASON_TRIAL -> R.string.link_ended_trial
        else -> R.string.link_ended_stream
    }
