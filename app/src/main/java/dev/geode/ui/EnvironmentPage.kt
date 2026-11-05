package dev.geode.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.res.stringResource
import dev.geode.R
import dev.geode.engine.bridge.GeodeNative
import dev.geode.model.GeneratorAssignment
import dev.geode.render.VisualizerView
import dev.geode.render.bridge.NativeViz
import dev.geode.render.scene.ParamScope
import kotlinx.coroutines.delay

/**
 * Advanced › Environment (#472): which style each of a Scene's four surfaces samples as a generator
 * (sky, wall, material, camera plane), whether a slot is pinned, and whether the auto-director may
 * swap the unpinned ones at sections and drops. It stands in for the Environment group of #302's
 * Edit screen until that screen exists, and like the layer it lasts for the session only.
 *
 * WHY a thin caller: the assignment, the pool and the swap rule are native
 * (`core/scene/generator`). This page reads `geode_viz_generator_text` and calls the setters, both
 * on the GL thread through [VisualizerView.queueEvent], so a call never meets a handle the GL thread
 * is releasing; the answer hops back with [VisualizerView.post]. It reads again every few seconds
 * while open, because a director swap changes the assignment without the user.
 */
@Composable
internal fun EnvironmentPage(visualizerView: VisualizerView) {
    val pool = remember { GeneratorAssignment.parsePool(GeodeNative.generatorPoolText()) }
    val assignment = remember { mutableStateOf(GeneratorAssignment()) }
    val call = remember(visualizerView) { nativeCall(visualizerView, assignment) }
    LaunchedEffect(call) {
        while (true) {
            call {}
            delay(REFRESH_MS)
        }
    }
    ControlHint(stringResource(R.string.environment_intro))
    if (!sectionVisible()) return
    val none = stringResource(R.string.environment_none)
    val options = remember(pool, none) { listOf(none) + pool.map(::sceneDisplayLabel) }
    val current = assignment.value
    SLOT_NAMES.forEachIndexed { slot, nameRes ->
        val name = stringResource(nameRes)
        val chosen = current.slots[slot]
        val at = pool.indexOf(chosen.styleId)
        val selected =
            when {
                chosen.styleId.isEmpty() -> 0
                at >= 0 -> at + 1
                else -> -1
            }
        DropdownRow(name, options, selected, placeholder = sceneDisplayLabel(chosen.styleId)) { i ->
            val id = if (i == 0) "" else pool[i - 1]
            call { it.setGenerator(slot, id) }
        }
        if (chosen.styleId.isNotEmpty()) {
            CheckRow(stringResource(R.string.environment_pin, name), chosen.pinned, scope = ParamScope.UNIVERSAL) { on ->
                call { it.setGeneratorPin(slot, on) }
            }
        }
    }
    val policyName = stringResource(R.string.environment_policy)
    SectionHeader(policyName)
    ControlHint(stringResource(R.string.environment_policy_hint))
    val policies =
        listOf(
            stringResource(R.string.environment_policy_only_me),
            stringResource(R.string.environment_policy_director),
        )
    DropdownRow(policyName, policies, current.policy) { i ->
        call { it.setGeneratorPolicy(i) }
    }
}

/**
 * Runs [change] on the GL thread against the live renderer, then reads the assignment back there and
 * hands it to [into] on the main thread. `call {}` is a plain read.
 */
private fun nativeCall(
    view: VisualizerView,
    into: MutableState<GeneratorAssignment>,
): ((NativeViz) -> Unit) -> Unit =
    { change ->
        view.queueEvent {
            val native = view.visualizerRenderer.nativeViz
            change(native)
            val text = native.generatorText()
            view.post { into.value = GeneratorAssignment.parse(text) }
        }
    }

/** String resources in native's `generator::Slot` order: sky, wall, material, camera plane. */
private val SLOT_NAMES =
    listOf(
        R.string.environment_sky,
        R.string.environment_wall,
        R.string.environment_material,
        R.string.environment_plane,
    )

// Often enough to show a director swap soon after it lands, rare enough to cost nothing.
private const val REFRESH_MS = 2_000L
