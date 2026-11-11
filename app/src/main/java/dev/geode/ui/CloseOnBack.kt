package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.navigationevent.NavigationEventInfo
import androidx.navigationevent.compose.NavigationBackHandler
import androidx.navigationevent.compose.rememberNavigationEventState

/**
 * Back closes something inside a screen (a drawer, an open settings group, a hidden sheet) with no
 * preview of its own. NavigationEvent's handler, the API SDK 36+ asks for in place of activity's
 * `BackHandler`: it shares the activity's one dispatcher with `NavDisplay` and the shell's handler,
 * so the last one composed and enabled still wins. A close that should follow the finger takes its
 * own state instead (see `rememberCardBack` in `CustomizeBrowser`).
 */
@Composable
internal fun CloseOnBack(
    enabled: Boolean,
    onBack: () -> Unit,
) {
    val state = rememberNavigationEventState(currentInfo = NavigationEventInfo.None)
    NavigationBackHandler(state = state, isBackEnabled = enabled, onBackCompleted = onBack)
}
