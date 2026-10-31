package dev.geode.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.runtime.Composable
import dev.geode.render.LfoTarget
import dev.geode.render.scene.ParamKeys
import dev.geode.render.scene.SceneParams

/**
 * Customize › Camera: the one camera every style, the wallpaper, the dream and exports share
 * (core/viz/CameraRig), moved through the scene by the music.
 *
 * Every move is its own row: how far it goes, and which sound moves it (the style's DNA picks a feel
 * per style: a music video, a floaty journey or a club). A move the user gives a sound replaces the
 * rig's own music move for that channel (CameraRig::stepAside), so a row is the whole story of it.
 */
@Composable
internal fun CameraTab(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val lanes = LocalMusicLanes.current
    Column {
        SectionHeader("Camera")
        ControlHint(
            "The music moves the camera through the scene. Each move listens to its own sound; tap " +
                "the sound to choose another, drag to set how far it goes. 3D styles are places the " +
                "camera travels through. Flat styles are moved as a picture (pan, zoom, tilt, roll), " +
                "and Fall through in Layers gives them depth.",
        )
        ControlLabelRow(ParamKeys.CAM_PATH, ParamKeys.CAM_PATH)
        ChipRow(SceneParams.CAMERA_PATHS, p.camPath.coerceIn(0, SceneParams.CAMERA_PATHS.lastIndex)) {
            onChange(p.copy(camPath = it))
        }
        SectionHeader("Moves")
        LabeledSlider(
            ParamKeys.CAM_FLY,
            p.camFly,
            0f..2f,
            display = "Fly through",
            target = LfoTarget.CAM_FLY,
            default = SceneParams.DEFAULT.camFly,
        ) { onChange(p.copy(camFly = it)) }
        LabeledSlider(
            ParamKeys.CAM_PUSH,
            p.camPush,
            -1f..1f,
            display = "Dolly in and out",
            target = LfoTarget.CAM_PUSH,
            default = SceneParams.DEFAULT.camPush,
        ) { onChange(p.copy(camPush = it)) }
        LabeledSlider(
            ParamKeys.CAM_YAW,
            p.camYaw,
            -1f..1f,
            display = "Orbit and turn",
            target = LfoTarget.CAM_YAW,
            default = SceneParams.DEFAULT.camYaw,
        ) { onChange(p.copy(camYaw = it)) }
        LabeledSlider(
            ParamKeys.CAM_ORBIT,
            p.camOrbit,
            0f..1f,
            display = "Swing wide",
            target = LfoTarget.CAM_ORBIT,
            default = SceneParams.DEFAULT.camOrbit,
        ) { onChange(p.copy(camOrbit = it)) }
        LabeledSlider(
            ParamKeys.CAM_CRANE,
            p.camCrane,
            -1f..1f,
            display = "Crane up and down",
            target = LfoTarget.CAM_CRANE,
            default = SceneParams.DEFAULT.camCrane,
        ) { onChange(p.copy(camCrane = it)) }
        LabeledSlider(
            ParamKeys.CAM_PITCH,
            p.camPitch,
            -1f..1f,
            display = "Tilt",
            target = LfoTarget.CAM_PITCH,
            default = SceneParams.DEFAULT.camPitch,
        ) { onChange(p.copy(camPitch = it)) }
        LabeledSlider(
            ParamKeys.CAM_ROLL,
            p.camRoll,
            -1f..1f,
            display = "Roll",
            target = LfoTarget.CAM_ROLL,
            default = SceneParams.DEFAULT.camRoll,
        ) { onChange(p.copy(camRoll = it)) }
        LabeledSlider(
            ParamKeys.CAM_SHAKE,
            p.camShake,
            0f..1f,
            display = "Shake",
            target = LfoTarget.CAM_SHAKE,
            default = SceneParams.DEFAULT.camShake,
        ) { onChange(p.copy(camShake = it)) }
        LabeledSlider(
            ParamKeys.CAM_FOV,
            p.camFov,
            0.5f..2f,
            display = "Lens",
            target = LfoTarget.CAM_FOV,
            default = SceneParams.DEFAULT.camFov,
        ) { onChange(p.copy(camFov = it)) }
        if (lanes != null) {
            MusicTriggerRow(lanes, LfoTarget.CAM_CUT, "Cut", "Jump to a new place in the scene, at most about once a bar.")
        }
        SectionHeader("Feel")
        LabeledSlider(ParamKeys.CAM_SMOOTH, p.camSmooth, 0f..1f, default = SceneParams.DEFAULT.camSmooth) {
            onChange(p.copy(camSmooth = it))
        }
        CheckRow(ParamKeys.CAM_TOUCH, p.camTouch) { onChange(p.copy(camTouch = it)) }
        CheckRow(ParamKeys.CAM_GYRO, p.camGyro) { onChange(p.copy(camGyro = it)) }
        ControlHint("Exports always fly the recorded path; the phone's tilt is never baked in.")
    }
}

/**
 * Customize › Layers: what happens to the picture itself, over every style (core/viz/Layers and
 * core/viz/ParticleLayer): it falls through itself, grows outward like a fractal, melts, spawns
 * copies, freezes, shatters, and throws particles. Each layer listens to a sound no other layer
 * uses, so several happen at once on different parts of the music.
 */
@Composable
internal fun LayersTab(
    p: SceneParams,
    onChange: (SceneParams) -> Unit,
) {
    val lanes = LocalMusicLanes.current
    Column {
        SectionHeader("Through itself")
        ControlHint(
            "The picture falls into itself or back out, grows mirrored copies outward and melts. " +
                "Styles often mix two sounds on Fall through: one pulls in, the other pushes back out.",
        )
        LabeledSlider(ParamKeys.FALL, p.fall, -1f..1f, target = LfoTarget.FALL, default = SceneParams.DEFAULT.fall) {
            onChange(p.copy(fall = it))
        }
        LabeledSlider(
            ParamKeys.FRACTAL_GROWTH,
            p.fractal,
            0f..1f,
            target = LfoTarget.FRACTAL,
            default = SceneParams.DEFAULT.fractal,
        ) {
            onChange(p.copy(fractal = it))
        }
        LabeledSlider(ParamKeys.MELTING, p.melt, 0f..1f, target = LfoTarget.MELT, default = SceneParams.DEFAULT.melt) {
            onChange(p.copy(melt = it))
        }
        if (lanes != null) {
            SectionHeader("Moments")
            ControlHint("Things that happen on a sound: a copy is born and dissolves, patches freeze and thaw, the picture shatters.")
            MusicTriggerRow(lanes, LfoTarget.SPAWN, ParamKeys.SPAWN, "A copy of the picture grows out and dissolves.")
            MusicTriggerRow(lanes, LfoTarget.FREEZE, ParamKeys.FREEZE, "Patches of the picture hold still, then thaw.")
            MusicTriggerRow(lanes, LfoTarget.SHATTER, ParamKeys.SHATTER, "The picture breaks into shards that fly apart.")
        }
        SectionHeader("Particles")
        ControlHint(
            "Pieces of the style thrown off as glowing particles, coloured by where they left. They " +
                "move on the music's time: when it stops they hang in the air.",
        )
        LabeledSlider(
            ParamKeys.PARTICLES,
            p.particles,
            0f..1f,
            target = LfoTarget.PARTICLES,
            default = SceneParams.DEFAULT.particles,
        ) { onChange(p.copy(particles = it)) }
        LabeledSlider(
            ParamKeys.PARTICLE_FALL,
            p.particleFall,
            -1f..1f,
            display = "Fly up … fall",
            target = LfoTarget.PARTICLE_FALL,
            default = SceneParams.DEFAULT.particleFall,
        ) { onChange(p.copy(particleFall = it)) }
    }
}
