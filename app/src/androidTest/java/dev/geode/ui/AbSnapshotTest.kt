package dev.geode.ui

import android.content.Context
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.SemanticsActions
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performSemanticsAction
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import dev.geode.data.LfoStore
import dev.geode.render.AdsrConfig
import dev.geode.render.BlendMode
import dev.geode.render.LfoConfig
import dev.geode.render.LfoTarget
import dev.geode.render.scene.BuildStyleReactionsUseCase
import dev.geode.render.scene.SceneIds
import dev.geode.render.scene.SceneParams
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * A/B on every Customize subtab. Each subtab keeps its edits somewhere different: the params
 * (Shape, Colour, Motion, FX, Scene, Fluid, Cymatics), the reaction routes (Events), the LFOs and
 * envelopes (Reactivity), the knob positions (Macros) and the layer (Scene). A snapshot that held
 * only the params made Set and Recall look broken on the others, so each is checked here.
 */
@RunWith(AndroidJUnit4::class)
class AbSnapshotTest {
    @get:Rule
    val compose = createComposeRule()

    private val context: Context = InstrumentationRegistry.getInstrumentation().targetContext
    private val prefs = context.getSharedPreferences("ab_snapshot_test", Context.MODE_PRIVATE)
    private val sceneId = SceneIds.KIFS
    private val dna = BuildStyleReactionsUseCase()

    private var params = SceneParams.DEFAULT
    private var knobs = listOf(0.5f, 0f)

    private val modulation =
        ModulationController(
            LfoStore(prefs),
            prefs,
            object : ModulationController.Host {
                override val params get() = this@AbSnapshotTest.params

                override val sceneId get() = this@AbSnapshotTest.sceneId

                override val knobs get() = this@AbSnapshotTest.knobs

                override fun setSceneParams(params: SceneParams) {
                    this@AbSnapshotTest.params = params
                }

                override fun setKnobs(values: List<Float>) {
                    knobs = values
                }
            },
        )

    @Before
    fun clean() {
        prefs.edit().clear().commit()
        LayersBus.state.value = LayersUiState()
    }

    @After
    fun restoreLayers() {
        LayersBus.state.value = LayersUiState()
    }

    /** Edits every kind of state a subtab can touch, the way that subtab would. */
    private fun dress(
        zoom: Float,
        hue: Float,
        seed: Int,
        lfoTarget: LfoTarget,
        knob: Float,
        layer: LayersUiState,
    ) {
        modulation.editSceneParams(params.copy(zoom = zoom, hueRange = hue))
        modulation.setReactions(dna(sceneId, seed))
        modulation.setLfo(0, LfoConfig(enabled = true, target = lfoTarget))
        modulation.setAdsr(0, AdsrConfig(enabled = true, targets = listOf(lfoTarget)))
        knobs = listOf(knob, knob / 2f)
        LayersBus.state.value = layer
    }

    private val layerA = LayersUiState(enabled = true, sceneId = SceneIds.DEFAULT, mix = 0.2f, blend = BlendMode.NORMAL)
    private val layerB = LayersUiState(enabled = false, sceneId = null, mix = 0.8f, blend = BlendMode.SCREEN)

    private fun assertLook(
        zoom: Float,
        hue: Float,
        seed: Int,
        lfoTarget: LfoTarget,
        knob: Float,
        layer: LayersUiState,
    ) {
        assertEquals("params (Shape, Colour, Motion, FX…)", zoom, params.zoom, EPS)
        assertEquals("params (Colour)", hue, params.hueRange, EPS)
        assertEquals("reactions (Events)", dna(sceneId, seed), modulation.reactions.value)
        assertEquals("LFO (Reactivity)", lfoTarget, modulation.lfos.value[0].target)
        assertEquals("envelope (Reactivity)", listOf(lfoTarget), modulation.adsrs.value[0].targets)
        assertEquals("knobs (Macros)", listOf(knob, knob / 2f), knobs)
        assertEquals("layer (Scene)", layer, LayersBus.state.value)
    }

    @Test
    fun recallBringsBackEverySubtabsState() {
        dress(0.6f, 0.1f, 1, LfoTarget.ZOOM, 0.9f, layerA)
        modulation.captureA()
        dress(1.6f, 0.7f, 7, LfoTarget.SATURATION, 0.1f, layerB)
        modulation.captureB()

        modulation.recallA()
        assertLook(0.6f, 0.1f, 1, LfoTarget.ZOOM, 0.9f, layerA)
        modulation.recallB()
        assertLook(1.6f, 0.7f, 7, LfoTarget.SATURATION, 0.1f, layerB)
    }

    @Test
    fun theMorphGlidesNumbersAndFlipsTheRestAtTheMiddle() {
        dress(0.6f, 0.1f, 1, LfoTarget.ZOOM, 0.8f, layerA)
        modulation.captureA()
        dress(1.6f, 0.7f, 7, LfoTarget.SATURATION, 0.0f, layerB)
        modulation.captureB()

        modulation.blendAb(0.25f)
        assertEquals(0.85f, params.zoom, EPS)
        assertEquals(0.6f, knobs[0], EPS)
        assertEquals(0.35f, LayersBus.state.value.mix, EPS)
        assertEquals(dna(sceneId, 1), modulation.reactions.value)
        assertEquals(LfoTarget.ZOOM, modulation.lfos.value[0].target)
        assertTrue(LayersBus.state.value.enabled)

        modulation.blendAb(0.75f)
        assertEquals(1.35f, params.zoom, EPS)
        assertEquals(dna(sceneId, 7), modulation.reactions.value)
        assertEquals(LfoTarget.SATURATION, modulation.lfos.value[0].target)
        assertEquals(false, LayersBus.state.value.enabled)
    }

    @Test
    fun layeringSitsOnTheSceneTabAndASearchFindsIt() {
        var query by mutableStateOf("")
        compose.setContent {
            MaterialTheme {
                CompositionLocalProvider(LocalSceneId provides sceneId, LocalParamQuery provides query) {
                    Column(Modifier.verticalScroll(rememberScrollState())) {
                        SceneTab(SceneParams.DEFAULT, {}, null, 1f, {}, {})
                    }
                }
            }
        }
        compose.onNodeWithText("Layers enabled").performScrollTo().assertIsDisplayed()
        compose.onNodeWithText("Layers enabled").performSemanticsAction(SemanticsActions.OnClick)
        compose.waitForIdle()
        assertTrue(LayersBus.state.value.enabled)

        query = "layer"
        compose.onNodeWithText("Layers enabled").performScrollTo().assertIsDisplayed()
    }

    private companion object {
        const val EPS = 1e-4f
    }
}
