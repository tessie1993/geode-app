package dev.geode.ui

import androidx.compose.runtime.Composable
import dev.geode.render.scene.ParamKeys
import dev.geode.render.scene.SceneParams

/**
 * The Reactivity tab's continuous motion controls.
 *
 * Wave three replaced transient-driven reaction (the old pulse/shake/form-drive/beat-response/
 * flash/strobe dials) with `core/viz/MotionField`: every family's parameters are reshaped from
 * running-average band ratios, phase-locked oscillators and slow re-targets, never from a beat,
 * a drum, an instrument or a transient. These five sliders are the only user-facing controls
 * over that system, each 0..1 and each already clamped natively so a loud passage cannot push
 * the motion past its bound.
 */
@Composable
internal fun MotionSection(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    SectionHeader("Motion")
    ControlHint(
        "How far the continuous motion system moves the scene. Nothing here flashes or jumps " +
            "on a beat - every change is a smoothed, always-on drift the music leans on.",
    )
    LabeledSlider(ParamKeys.MOTION_AMOUNT, p.motionAmount, 0f..1f) { onChange(p.copy(motionAmount = it)) }
    LabeledSlider(ParamKeys.MOTION_BREATH, p.motionBreath, 0f..1f) { onChange(p.copy(motionBreath = it)) }
    LabeledSlider(ParamKeys.MOTION_ORBIT, p.motionOrbit, 0f..1f) { onChange(p.copy(motionOrbit = it)) }
    LabeledSlider(ParamKeys.MOTION_DRIFT, p.motionDrift, 0f..1f) { onChange(p.copy(motionDrift = it)) }
    LabeledSlider(ParamKeys.MOTION_HUE, p.motionHue, 0f..1f) { onChange(p.copy(motionHue = it)) }
}
