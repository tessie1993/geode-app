package dev.geode.ui

import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.content.pm.PackageManager
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.platform.LocalContext
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.LifecycleResumeEffect

/** Where one runtime permission stands, as far as the screen asking for it needs to know. */
enum class PermissionStatus {
    GRANTED,

    /** Not granted and not refused in this session: ask, with the reason beside the button. */
    NOT_ASKED,

    /** Refused once; Android will show the dialog again, so explain and offer to retry. */
    DENIED,

    /**
     * Refused with "don't ask again", or twice: Android now answers no without a dialog, so the
     * only way back is the app's page in system Settings.
     */
    PERMANENTLY_DENIED,
}

/**
 * What a refusal means, from whether Android would show a rationale before ([rationaleBefore])
 * and after ([rationaleAfter]) the request, and the status the request was made from.
 *
 * "No rationale" after a refusal is ambiguous on its own: it is what Android reports after "don't
 * ask again", but also after a first dialog the person simply dismissed, and on the first tap after
 * a restart for a permission refused for good in an earlier session (the dialog never shows). So a
 * refusal only counts as final when the evidence says Android stopped asking: a rationale that was
 * on and went off, or a second rationale-less refusal in a row. Otherwise it is an ordinary
 * refusal, which the screen explains, and the next tap settles it.
 */
internal fun statusAfterRefusal(
    rationaleBefore: Boolean,
    rationaleAfter: Boolean,
    previous: PermissionStatus,
): PermissionStatus =
    when {
        rationaleAfter -> PermissionStatus.DENIED
        rationaleBefore || previous == PermissionStatus.DENIED -> PermissionStatus.PERMANENTLY_DENIED
        else -> PermissionStatus.DENIED
    }

@Stable
class PermissionRequestState internal constructor(
    private val state: MutableState<PermissionStatus>,
    private val launch: () -> Unit,
    private val settings: () -> Unit,
) {
    val status: PermissionStatus get() = state.value

    val granted: Boolean get() = status == PermissionStatus.GRANTED

    val refused: Boolean get() = status == PermissionStatus.DENIED || status == PermissionStatus.PERMANENTLY_DENIED

    /** Shows the system dialog, or, once that is no longer possible, the app's Settings page. */
    fun request() = if (status == PermissionStatus.PERMANENTLY_DENIED) settings() else launch()

    fun openSettings() = settings()
}

/**
 * Asks for [permission] in context: the screen explains why before the first request, explains
 * again after a refusal, and sends the person to Settings once Android stops asking. The status is
 * re-read on every resume, so coming back from Settings with the permission on just works.
 */
@Composable
fun rememberPermissionRequest(
    permission: String,
    onGranted: () -> Unit = {},
): PermissionRequestState {
    val context = LocalContext.current
    val currentOnGranted by rememberUpdatedState(onGranted)
    val status =
        remember(permission) {
            mutableStateOf(if (context.isGranted(permission)) PermissionStatus.GRANTED else PermissionStatus.NOT_ASKED)
        }
    fun rationale(): Boolean = context.findActivity()?.shouldShowRequestPermissionRationale(permission) ?: false
    val rationaleBefore = remember(permission) { booleanArrayOf(false) }
    val launcher =
        rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { allowed ->
            if (allowed) {
                status.value = PermissionStatus.GRANTED
                currentOnGranted()
            } else {
                status.value = statusAfterRefusal(rationaleBefore[0], rationale(), status.value)
            }
        }
    LifecycleResumeEffect(permission) {
        if (status.value != PermissionStatus.GRANTED && context.isGranted(permission)) {
            // Only a person the app sent to Settings came back to finish turning the feature on;
            // a grant made for some other reason updates the status without acting on it.
            val cameBackFromSettings = status.value == PermissionStatus.PERMANENTLY_DENIED
            status.value = PermissionStatus.GRANTED
            if (cameBackFromSettings) currentOnGranted()
        }
        onPauseOrDispose { }
    }
    return remember(status, launcher) {
        PermissionRequestState(
            state = status,
            launch = {
                rationaleBefore[0] = rationale()
                launcher.launch(permission)
            },
            settings = { context.openAppSettings() },
        )
    }
}

private fun Context.isGranted(permission: String): Boolean =
    ContextCompat.checkSelfPermission(this, permission) == PackageManager.PERMISSION_GRANTED

internal tailrec fun Context.findActivity(): Activity? =
    when (this) {
        is Activity -> this
        is ContextWrapper -> baseContext.findActivity()
        else -> null
    }
