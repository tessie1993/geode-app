package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.rememberUpdatedState

/**
 * First run, after the photosensitivity notice: nothing more. It returns at once.
 *
 * Setup used to ask for music access and offer a folder here, before the person had seen the
 * app. Music access is now asked the first time Library opens, in context and with its reason
 * (`LibraryScreen`, through `rememberPermissionRequest`), and a folder is added from Library
 * too. The guided tour that follows leads there first, so nothing is lost by not asking up front.
 *
 * Kept as a gate, not deleted, so the shell's order (notice, then setup done, then the
 * end-of-onboarding paywall and the tour) stays one call and an older install whose setup never
 * finished is let through the same way.
 */
@Composable
internal fun FirstRunGate(onDone: () -> Unit) {
    val done by rememberUpdatedState(onDone)
    LaunchedEffect(Unit) { done() }
}
