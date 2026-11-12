package dev.geode.ui.tour

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.snapshotFlow
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.platform.LocalAccessibilityManager
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.currentStateAsState
import dev.geode.R
import dev.geode.model.GestureTip
import dev.geode.model.Tour
import dev.geode.model.TourChapter
import dev.geode.model.TourCover
import dev.geode.model.TourFacts
import dev.geode.model.TourPlace
import dev.geode.model.TourStepKind
import dev.geode.ui.PermissionRationales
import kotlinx.coroutines.delay

/**
 * Says a control was used the long way round, so its gesture can be mentioned once
 * ([TourViewModel.nudge]). A no-op outside the app's root, so a preview or a test needs nothing.
 */
internal val LocalTourNudge = staticCompositionLocalOf<(GestureTip) -> Unit> { {} }

/**
 * Draws the tour over the app and tells [viewModel] what the app looks like: the marked controls
 * on screen, the screen ([place]) and whether music plays. Every decision is the view model's.
 *
 * [onNavigate] opens the screen a step or a replay asks for; [onOfferPro] is the one-time offer
 * after the first look at Visuals. Both are handed back to the view model once acted on.
 */
@Composable
internal fun TourHost(
    viewModel: TourViewModel,
    state: TourUiState,
    place: TourPlace,
    playing: Boolean,
    cover: TourCover,
    onNavigate: (TourNav) -> Unit,
    onOfferPro: () -> Unit,
) {
    val placeNow by rememberUpdatedState(place)
    val playingNow by rememberUpdatedState(playing)
    LaunchedEffect(viewModel) {
        snapshotFlow { TourFacts(TourTargets.shown, placeNow, playingNow) }.collect(viewModel::onFacts)
    }
    // A system permission dialog pauses the activity; a screen's own rationale says so itself.
    val lifecycle by LocalLifecycleOwner.current.lifecycle.currentStateAsState()
    val held =
        cover.copy(
            permissionRationale = cover.permissionRationale || PermissionRationales.shown,
            away = cover.away || !lifecycle.isAtLeast(Lifecycle.State.RESUMED),
        )
    val blocked = Tour.held(held)
    LaunchedEffect(viewModel, held) { viewModel.onCover(held) }
    state.navigate?.let { nav ->
        LaunchedEffect(nav) {
            onNavigate(nav)
            viewModel.onNavigated()
        }
    }
    if (state.offerPro && !blocked) {
        LaunchedEffect(Unit) {
            onOfferPro()
            viewModel.onProOffered()
        }
    }
    val step = state.step
    when {
        // Checked first: the view model drops the step a beat later, and nothing may flash over an ask.
        blocked -> Unit
        step != null -> {
            val words = step.spot.words
            val skipLabel =
                stringResource(
                    if (step.spot.chapter == TourChapter.START && !state.replay) R.string.tour_skip_tour else R.string.tour_skip,
                )
            if (step.spot.kind == TourStepKind.WAIT) {
                TourHint(stringResource(words.body), skipLabel, viewModel::onSkip, emphasized = false)
            } else {
                val title = stringResource(words.title)
                TourPointer(
                    title = title,
                    body = stringResource(words.body),
                    paneTitle = stringResource(R.string.tour_pane_title, title, step.index, step.count),
                    index = step.index,
                    count = step.count,
                    target = TourTargets.boundsOf(step.spot),
                    primaryLabel = stringResource(words.action ?: if (step.last) R.string.tour_done else R.string.tour_next),
                    onPrimary = viewModel::onPrimary,
                    skipLabel = skipLabel,
                    onSkip = viewModel::onSkip,
                )
            }
        }
        state.replayNote ->
            TimedHint(stringResource(R.string.tour_replay_note), stringResource(R.string.tour_ok), viewModel::onReplayNoteShown)
        state.tip != null ->
            TimedHint(
                stringResource(state.tip.textRes),
                stringResource(R.string.tour_tip_got_it),
                viewModel::onTipRead,
                secondaryLabel = stringResource(R.string.tour_tips_off),
                onSecondary = viewModel::onTipsOff,
            )
    }
}

/** A hint that leaves by itself, after the time the user's accessibility settings ask for. */
@Composable
private fun TimedHint(
    text: String,
    actionLabel: String,
    onGone: () -> Unit,
    secondaryLabel: String? = null,
    onSecondary: () -> Unit = {},
) {
    val a11y = LocalAccessibilityManager.current
    val gone by rememberUpdatedState(onGone)
    LaunchedEffect(text) {
        delay(a11y?.calculateRecommendedTimeoutMillis(HINT_MS, containsText = true, containsControls = true) ?: HINT_MS)
        gone()
    }
    TourHint(text, actionLabel, onGone, secondaryLabel, onSecondary)
}

private const val HINT_MS = 7_000L
