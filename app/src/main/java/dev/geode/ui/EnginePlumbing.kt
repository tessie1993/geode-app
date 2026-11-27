package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.data.LookSnapshot
import dev.geode.data.LookUnderlay
import dev.geode.data.VizUiState
import dev.geode.geodeContainer
import dev.geode.stage.StageView
import dev.geode.ui.theme.rememberSystemReducedMotion
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch

/**
 * The stage's look from the session's state (#345): the one builder live Visuals and every export
 * (which pins it, ExportController.Host.look) share, so the two cannot drift apart.
 */
internal fun liveLook(
    viz: VizUiState,
    lfos: String,
    adsrs: String,
    reactions: String?,
    soundTuning: List<Float>?,
    // Android's "Remove animations" (data/SystemMotion, #336), the only reduced-motion flag.
    reducedMotion: Boolean,
    underlay: LookUnderlay? = null,
): LookSnapshot =
    LookSnapshot(
        sceneId = viz.sceneId,
        params = viz.params,
        lfos = lfos,
        adsrs = adsrs,
        reactions = reactions,
        soundTuning = soundTuning,
        reducedMotion = reducedMotion,
        transitionId = viz.transitionId,
        transitionMs = (viz.transitionDurationSec * MS_PER_SECOND).toLong(),
        underlay = underlay,
    )

private const val MS_PER_SECOND = 1000

@Composable
fun VisualizerEngineBindings(
    viewModel: PlayerViewModel,
    visualizerView: StageView,
    liveFps: Int,
) {
    val settingsViewModel: SettingsViewModel = geodeViewModel()
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    // Only the two ids, derived: a Customize slider tick changes vizState but must not re-run this.
    val vizState = viewModel.vizState.collectAsStateWithLifecycle()
    val activeSceneId by remember { derivedStateOf { vizState.value.sceneId } }
    val activeTransitionId by remember { derivedStateOf { vizState.value.transitionId } }
    val playerPrefs by settingsViewModel.playerPrefs.collectAsStateWithLifecycle()
    val performanceStore = LocalContext.current.geodeContainer.performance
    val performance by performanceStore.settings.collectAsStateWithLifecycle()
    val mainScope = rememberCoroutineScope()
    // Watched live: turning it on while the visual plays calms the visual at once.
    val reducedMotion = rememberSystemReducedMotion()

    LaunchedEffect(Unit) {
        visualizerView.stage.onShaderError = viewModel::reportShaderError
    }
    LaunchedEffect(visualizerView) {
        dev.geode.audio.AudioBus.heard.collect { visualizerView.stage.hear(it) }
    }
    // W02: the background image is decoded to the surface's current pixel size, so a rotation
    // or a fold needs a re-decode just as much as a fresh pick does. onSurfaceSizeChanged fires
    // inside the view's surface callback, so the re-decode is launched rather than run there.
    // The stage outlives this composition, so the handler goes with it.
    DisposableEffect(visualizerView) {
        val stage = visualizerView.stage
        stage.onSurfaceSizeChanged = { w, h -> mainScope.launch { visualsViewModel.setBackgroundRenderSize(w, h) } }
        onDispose { stage.onSurfaceSizeChanged = { _, _ -> } }
    }
    // The look is one value fed from one collector (#345): a slider drag or a pinch edits params at
    // pointer rate, and an effect keyed on them relaunched a coroutine per edit, each a frame late. The
    // stage passes on only the parts that changed (`geode_stage_set_look`), so an unrelated vizState change costs nothing.
    // The underlay is mapped once per background push: a new instance would re-send the whole image.
    LaunchedEffect(reducedMotion) {
        val underlay =
            visualsViewModel.backgroundPush.map { b ->
                b.pixels?.let { LookUnderlay(it.pixels, it.width, it.height, b.blend, b.amount) }
            }
        val vm = visualsViewModel
        val modulation = combine(vm.lfos, vm.adsrs, vm.reactions) { l, a, r -> Triple(l, a, r) }
        val tuning = settingsViewModel.soundInput.map { it.soundTuning }
        combine(viewModel.vizState, modulation, tuning, underlay) { v, m, t, u ->
            liveLook(v, m.first, m.second, m.third, t, reducedMotion, u)
        }.collect { visualizerView.stage.look = it }
    }
    // The Song events page previews on this stage and reads its event lights.
    DisposableEffect(visualizerView) {
        visualsViewModel.attachResponderStage(visualizerView.stage)
        onDispose { visualsViewModel.attachResponderStage(null) }
    }
    LaunchedEffect(Unit) {
        viewModel.vizState
            .map { it.params.flag("camGyro") }
            .distinctUntilChanged()
            .collect { visualizerView.gyroWanted = it }
    }
    LaunchedEffect(Unit) {
        // Settings › Gestures & controls › Touch › Smear: handed over as set; native bends the picture (core/viz/TouchSmear).
        settingsViewModel.visuals
            .map { it.touchSmear to it.touchSmearStrength }
            .distinctUntilChanged()
            .collect { (on, strength) -> visualizerView.stage.setTouchSmear(on, strength) }
    }
    LaunchedEffect(playerPrefs.keepScreenOn) {
        visualizerView.keepScreenOn = playerPrefs.keepScreenOn
    }
    LaunchedEffect(activeTransitionId) {
        // The look carries the transition; this only compiles it ahead of the next style change.
        visualizerView.stage.warmTransition(activeTransitionId)
    }
    // The app's one live rate (rememberLiveFps): the stored pick as this window's display runs it.
    LaunchedEffect(performance, liveFps) {
        visualizerView.applyPerformance(performance.copy(fpsCap = liveFps))
    }
    LaunchedEffect(Unit) {
        visualizerView.stage.liquidMode.collect { viewModel.reportLiquidMode(it) }
    }
    LaunchedEffect(Unit) {
        viewModel.morphFade.collect { visualizerView.stage.beginParamMorph(it) }
    }
    LaunchedEffect(Unit) {
        visualizerView.stage.onDirectorStyleMoment = viewModel::onDirectorStyleMoment
        viewModel.vizApply.collect { apply ->
            apply.customShader?.let {
                visualizerView.stage.submitShader(apply.sceneId ?: activeSceneId, it)
            }
        }
    }
}
