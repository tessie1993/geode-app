package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import dev.geode.render.BlendMode
import dev.geode.render.TransitionCatalog
import dev.geode.render.VisualSafety
import dev.geode.render.VisualizerView
import kotlinx.coroutines.flow.MutableStateFlow

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

@Composable
fun VisualizerEngineBindings(
    viewModel: PlayerViewModel,
    visualizerView: VisualizerView,
) {
    val settingsViewModel: SettingsViewModel = geodeViewModel()
    val visualsViewModel: VisualsViewModel = geodeViewModel()
    val viz by viewModel.vizState.collectAsStateWithLifecycle()
    val lfos by visualsViewModel.lfos.collectAsStateWithLifecycle()
    val adsrs by visualsViewModel.adsrs.collectAsStateWithLifecycle()
    val playerPrefs by settingsViewModel.playerPrefs.collectAsStateWithLifecycle()
    val gui by settingsViewModel.guiPrefs.collectAsStateWithLifecycle()
    val layers by LayersBus.state.collectAsStateWithLifecycle()

    LaunchedEffect(Unit) {
        visualizerView.visualizerRenderer.onShaderError = viewModel::reportShaderError
        visualizerView.visualizerRenderer.pcmProvider = { viewModel.latestPcm() }
        LayersBus.availableScenes.value = visualizerView.visualizerRenderer.availableSceneIds()
        viewModel.features.collect {
            val enriched = viewModel.enrichFeatures(it)
            visualizerView.visualizerRenderer.features = enriched
        }
    }
    LaunchedEffect(viz.sceneId) {
        visualizerView.visualizerRenderer.requestedSceneId = viz.sceneId
        LayersBus.activeSceneId.value = viz.sceneId
    }
    LaunchedEffect(layers) {
        val renderer = visualizerView.visualizerRenderer
        renderer.layerSceneId = if (layers.enabled) layers.sceneId else null
        renderer.layerMix = layers.mix
        renderer.layerBlend = layers.blend
    }
    LaunchedEffect(viz.params) {
        visualizerView.visualizerRenderer.sceneParams = viz.params
    }
    LaunchedEffect(playerPrefs.keepScreenOn) {
        visualizerView.keepScreenOn = playerPrefs.keepScreenOn
    }
    LaunchedEffect(lfos, adsrs) {
        visualizerView.visualizerRenderer.lfoEngine.configs = lfos
        visualizerView.visualizerRenderer.adsrEngine.configs = adsrs
    }
    LaunchedEffect(viz.transitionId, viz.transitionDurationSec) {
        val id = VisualSafety.transitionId(viz.transitionId)
        val renderer = visualizerView.visualizerRenderer
        renderer.transitionId = id
        TransitionCatalog.builtIn(id)?.let { renderer.transitionStyle = it }
        renderer.transitionDurationMs = (viz.transitionDurationSec * 1000).toLong()
        visualizerView.queueEvent { renderer.warmTransition(id) }
    }
    LaunchedEffect(gui.reducedMotion) {
        visualizerView.visualizerRenderer.reducedMotion = gui.reducedMotion
    }
    LaunchedEffect(Unit) {
        viewModel.morphFade.collect { visualizerView.visualizerRenderer.beginParamMorph(it) }
    }
    LaunchedEffect(Unit) {
        visualizerView.visualizerRenderer.onMilkPresetLoaded = { visualsViewModel.noteMilkPreset(it) }
        viewModel.activeMilkPath.value?.let { visualizerView.visualizerRenderer.loadMilkPreset(it) }
        viewModel.vizApply.collect { apply ->
            apply.milkPath?.let {
                visualizerView.visualizerRenderer.loadMilkPreset(it)
            }
            apply.customShader?.let {
                visualizerView.visualizerRenderer.submitShader(apply.sceneId ?: viz.sceneId, it)
            }
        }
    }
}
