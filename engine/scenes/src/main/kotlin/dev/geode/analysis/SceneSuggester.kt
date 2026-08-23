package dev.geode.analysis

import dev.geode.render.scene.SceneIds

enum class IntelligenceMode { MANUAL, SUGGEST, AUTO }

object SceneSuggester {
    const val SCENE_JULIA = SceneIds.JULIA
    const val SCENE_TUNNEL = SceneIds.TUNNEL

    val SCENE_MYCELIUM: String =
        requireNotNull(dev.geode.render.scene.VisualStyleCatalog.myco("myco_polycephalum")).id

    data class Affinity(
        val sceneId: String,
        val tempoBpm: ClosedFloatingPointRange<Float> = 60f..180f,
        val energy: ClosedFloatingPointRange<Float> = 0f..1f,
        val brightness: ClosedFloatingPointRange<Float> = 0f..1f,
        val percussiveBias: Float = 0f,
        val widthBias: Float = 0f,
    )

    val AFFINITIES: List<Affinity> =
        listOf(
            Affinity(
                SceneIds.AURORA,
                tempoBpm = 50f..110f,
                energy = 0f..0.18f,
                brightness = 0.1f..0.55f,
                percussiveBias = -0.4f,
                widthBias = 0.6f,
            ),
            Affinity(SceneIds.WATER, tempoBpm = 60f..115f, energy = 0.05f..0.22f, brightness = 0.1f..0.5f, percussiveBias = -0.5f),
            Affinity(SceneIds.CURLFLOW, tempoBpm = 70f..130f, energy = 0.1f..0.3f, brightness = 0.15f..0.6f, percussiveBias = -0.2f),
            Affinity(SceneIds.JULIA, tempoBpm = 70f..140f, energy = 0.1f..0.3f, brightness = 0.15f..0.5f),
            Affinity(SceneIds.CYMATICS, tempoBpm = 60f..130f, energy = 0.08f..0.3f, brightness = 0.1f..0.5f, percussiveBias = -0.8f),
            Affinity(
                SceneIds.FLUID,
                tempoBpm = 90f..150f,
                energy = 0.18f..0.45f,
                brightness = 0.2f..0.7f,
                percussiveBias = 0.3f,
                widthBias = 0.4f,
            ),
            Affinity(SceneIds.TUNNEL, tempoBpm = 90f..160f, energy = 0.15f..0.4f, brightness = 0.45f..1f),
            Affinity(
                SCENE_MYCELIUM,
                tempoBpm = 95f..200f,
                energy = 0.16f..0.7f,
                brightness = 0.1f..0.75f,
                percussiveBias = 0.6f,
                widthBias = 0.2f,
            ),
            Affinity(
                SceneIds.DEFAULT,
                tempoBpm = 60f..128f,
                energy = 0.08f..0.5f,
                brightness = 0.15f..0.8f,
                widthBias = 0.5f,
            ),
        )

    internal fun fit(
        value: Float,
        range: ClosedFloatingPointRange<Float>,
    ): Float {
        if (value in range) return 1f
        val span = (range.endInclusive - range.start).coerceAtLeast(1e-3f)
        val d = if (value < range.start) range.start - value else value - range.endInclusive
        return (1f - d / span).coerceAtLeast(0f)
    }

    internal fun score(
        a: Affinity,
        bpm: Float,
        energy: Float,
        centroid: Float,
        pulseConfidence: Float,
        chromaConfidence: Float,
        stereoWidth: Float,
    ): Float =
        1.5f * fit(energy, a.energy) +
            1.0f * fit(bpm, a.tempoBpm) +
            1.0f * fit(centroid, a.brightness) +
            a.percussiveBias * (pulseConfidence - chromaConfidence) +
            a.widthBias * (stereoWidth - 0.25f)

    fun suggestForTrack(timeline: FeatureTimeline): String {
        val frames = timeline.frames

        fun avg(pick: (AudioFeatures) -> Float): Float =
            if (frames.isEmpty()) 0f else frames.map { pick(it.features).toDouble() }.average().toFloat()
        return suggest(
            bpm = timeline.bpm,
            energy = timeline.averageEnergy,
            centroid = timeline.averageCentroid,
            pulseConfidence = avg { it.pulseConfidence },
            chromaConfidence = avg { it.chromaConfidence },
            stereoWidth = avg { it.stereoWidth },
        )
    }

    fun suggest(
        bpm: Float,
        energy: Float,
        centroid: Float,
        pulseConfidence: Float = 0f,
        chromaConfidence: Float = 0f,
        stereoWidth: Float = 0f,
    ): String = AFFINITIES.maxBy { score(it, bpm, energy, centroid, pulseConfidence, chromaConfidence, stereoWidth) }.sceneId
}
