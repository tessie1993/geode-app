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
import dev.geode.data.VizUiState
import dev.geode.engine.bridge.GeodeNative
import dev.geode.geodeContainer
import dev.geode.render.BlendMode
import dev.geode.render.VisualizerView
import dev.geode.render.look.LookSnapshot
import dev.geode.render.look.LookUnderlay
import dev.geode.ui.theme.rememberSystemReducedMotion
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch

data class LayersUiState(
    val enabled: Boolean = false,
    val sceneId: String? = null,
    val mix: Float = 0.5f,
    val blend: BlendMode = BlendMode.SCREEN,
)

object LayersBus {
    val state = MutableStateFlow(LayersUiState())

    val availableScenes = MutableStateFlow<List<String>>(emptyList())

    val activeSceneId = MutableStateFlow<String?>(null)
}

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
    visualizerView: VisualizerView,
) {
    val settingsViewModel: SettingsViewModel = geodeViewModel()
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val studioViewModel: StudioViewModel = geodeViewModel()
    // Only the two ids, derived: a Customize slider tick changes vizState but must not re-run this.
    val vizState = viewModel.vizState.collectAsStateWithLifecycle()
    val activeSceneId by remember { derivedStateOf { vizState.value.sceneId } }
    val activeTransitionId by remember { derivedStateOf { vizState.value.transitionId } }
    val playerPrefs by settingsViewModel.playerPrefs.collectAsStateWithLifecycle()
    val layers by LayersBus.state.collectAsStateWithLifecycle()
    val performanceStore = LocalContext.current.geodeContainer.performance
    val performance by performanceStore.settings.collectAsStateWithLifecycle()
    val mainScope = rememberCoroutineScope()
    // Watched live: turning it on while the visual plays calms the visual at once.
    val reducedMotion = rememberSystemReducedMotion()

    LaunchedEffect(Unit) {
        visualizerView.visualizerRenderer.onShaderError = viewModel::reportShaderError
        visualizerView.visualizerRenderer.soundInput = { dev.geode.audio.AudioBus.soundInput }
        // W02: the background image is decoded to the surface's current pixel size, so a rotation
        // or a fold needs a re-decode just as much as a fresh pick does. onSurfaceSizeChanged fires
        // on the GL thread; BackgroundController's state is only ever touched from Main, so this
        // hops back rather than calling straight through.
        visualizerView.visualizerRenderer.onSurfaceSizeChanged = { w, h ->
            mainScope.launch { visualsViewModel.setBackgroundRenderSize(w, h) }
        }
        LayersBus.availableScenes.value = visualizerView.visualizerRenderer.availableSceneIds()
    }
    // The look is one value fed from one collector (#345): a slider drag or a pinch edits params at
    // pointer rate, and an effect keyed on them relaunched a coroutine per edit, each a frame late. The
    // renderer pushes only the parts that changed (NativeViz.setLook), so an unrelated vizState change costs nothing.
    // The underlay is mapped once per background push: a new instance would re-send the whole image.
    LaunchedEffect(reducedMotion) {
        val underlay =
            visualsViewModel.backgroundPush.map { b ->
                b.pixels?.let { LookUnderlay(it.pixels, it.width, it.height, b.blend.ordinal, b.amount) }
            }
        val vm = visualsViewModel
        val modulation = combine(vm.lfos, vm.adsrs, vm.reactions) { l, a, r -> Triple(l, a, r) }
        val tuning = settingsViewModel.soundInput.map { it.soundTuning }
        combine(viewModel.vizState, modulation, tuning, underlay) { v, m, t, u ->
            liveLook(v, m.first, m.second, m.third, t, reducedMotion, u)
        }.collect { visualizerView.visualizerRenderer.look = it }
    }
    // Record copies this stage's frames; one leaving the screen ends a recording it was making.
    DisposableEffect(visualizerView) {
        studioViewModel.attachRecordStage(visualizerView)
        onDispose { studioViewModel.attachRecordStage(null) }
    }
    LaunchedEffect(Unit) {
        viewModel.vizState
            .map { it.params.camGyro }
            .distinctUntilChanged()
            .collect { visualizerView.gyroWanted = it }
    }
    LaunchedEffect(activeSceneId) {
        LayersBus.activeSceneId.value = activeSceneId
    }
    LaunchedEffect(layers) {
        // Process-wide (core/viz/LayerState): exports, the wallpaper and the dream draw it too (#364).
        val sceneId = if (layers.enabled) layers.sceneId else null
        visualizerView.visualizerRenderer.submitLayer(sceneId, layers.mix, layers.blend)
    }
    LaunchedEffect(Unit) {
        // Settings › Gestures & controls › Touch › Smear: handed over as set; native bends the picture (core/viz/TouchSmear).
        settingsViewModel.visuals
            .map { it.touchSmear to it.touchSmearStrength }
            .distinctUntilChanged()
            .collect { (on, strength) -> visualizerView.visualizerRenderer.nativeViz.setTouchSmear(on, strength) }
    }
    LaunchedEffect(playerPrefs.keepScreenOn) {
        visualizerView.keepScreenOn = playerPrefs.keepScreenOn
    }
    LaunchedEffect(activeTransitionId) {
        // The look carries the transition; this only compiles it ahead of the next style change.
        val id = activeTransitionId
        val renderer = visualizerView.visualizerRenderer
        visualizerView.queueEvent { renderer.warmTransition(id) }
    }
    // The stored live cap as this window's display runs it (native, core/viz/frame/LiveFps): a 120 picked on a
    // fast panel is 60 on a 60 Hz one, so a hot phone's first thermal step still costs frames there.
    val refreshRates = rememberSeamlessRefreshRates()
    LaunchedEffect(performance, refreshRates) {
        visualizerView.applyPerformance(performance.copy(fpsCap = GeodeNative.liveFpsCap(performance.fpsCap, refreshRates)))
    }
    LaunchedEffect(Unit) {
        visualizerView.visualizerRenderer.liquidMode.collect { viewModel.reportLiquidMode(it) }
    }
    LaunchedEffect(Unit) {
        viewModel.morphFade.collect { visualizerView.visualizerRenderer.beginParamMorph(it) }
    }
    LaunchedEffect(Unit) {
        visualizerView.visualizerRenderer.onMilkPresetLoaded = { visualsViewModel.noteMilkPreset(it) }
        visualizerView.visualizerRenderer.onDirectorStyleMoment = { visualizerView.post { viewModel.onDirectorStyleMoment() } }
        viewModel.vizApply.collect { apply ->
            apply.milkPath?.let {
                visualizerView.visualizerRenderer.loadMilkPreset(it)
            }
            apply.customShader?.let {
                visualizerView.visualizerRenderer.submitShader(apply.sceneId ?: activeSceneId, it)
            }
        }
    }
}
