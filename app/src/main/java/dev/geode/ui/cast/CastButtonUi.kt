package dev.geode.ui.cast

import androidx.annotation.StringRes
import dev.geode.R
import dev.geode.cast.CastFailure
import dev.geode.cast.CastState

/** What the Cast button adds over Media3's route icon: a spinner while the stream starts, and what TalkBack hears. */
internal data class CastButtonUi(
    val busy: Boolean = false,
    @StringRes val status: Int? = null,
    val deviceName: String? = null,
)

internal fun castButtonUi(state: CastState): CastButtonUi =
    when (state) {
        is CastState.Connecting ->
            CastButtonUi(
                busy = true,
                status = if (state.deviceName == null) R.string.cast_connecting_unnamed else R.string.cast_connecting,
                deviceName = state.deviceName,
            )
        is CastState.Casting ->
            CastButtonUi(
                status = if (state.deviceName == null) R.string.cast_casting_unnamed else R.string.cast_casting,
                deviceName = state.deviceName,
            )
        else -> CastButtonUi()
    }

/** The message a one-shot cast state leaves once it is shown; null when it has none to show here. */
@StringRes
internal fun castMessage(state: CastState): Int? =
    when (state) {
        is CastState.Failed ->
            when (state.reason) {
                CastFailure.STREAM -> R.string.cast_failed
                CastFailure.NO_LAN -> R.string.cast_failed_no_lan
                CastFailure.BACKGROUND_START -> R.string.cast_failed_background
                CastFailure.NETWORK_CHANGED -> R.string.cast_failed_network
                CastFailure.TV_COULD_NOT_PLAY -> R.string.cast_failed_tv_play
                CastFailure.TV_UNREACHABLE -> R.string.cast_failed_tv_unreachable
            }
        else -> null
    }
