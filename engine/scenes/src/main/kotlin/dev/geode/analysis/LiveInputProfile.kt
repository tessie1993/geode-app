package dev.geode.analysis

import dev.geode.render.scene.SceneParams

enum class LiveInputProfile(
    val label: String,
    val summary: String,
    val beatSigma: Float,
    val beatIntervalMs: Float,
    val attack: Float,
    val decay: Float,
    private val audioDrive: Float,
    private val bassGain: Float,
    private val midGain: Float,
    private val trebGain: Float,
    private val beatResponse: Float,
) {
    SPEAKER(
        label = "Speaker",
        summary = "Music playing nearby, loud and clear. Closest to normal playback.",
        beatSigma = BeatTuning.SENSITIVITY_DEFAULT,
        beatIntervalMs = BeatTuning.INTERVAL_MS_DEFAULT,
        attack = 0.6f,
        decay = 0.12f,
        audioDrive = 1f,
        bassGain = 1f,
        midGain = 1f,
        trebGain = 1f,
        beatResponse = 1f,
    ),

    ROOM(
        label = "Room",
        summary = "A PA across a room: softer transients, boomy low end, reverb tail.",
        beatSigma = BeatTuning.SENSITIVITY_MIN,
        beatIntervalMs = 260f,
        attack = 0.45f,
        decay = 0.2f,
        audioDrive = 1.35f,
        bassGain = 0.75f,
        midGain = 1.05f,
        trebGain = 1.3f,
        beatResponse = 1.15f,
    ),

    INSTRUMENT(
        label = "Instrument",
        summary = "Played into the phone: sharp attacks, wide dynamics, no steady beat.",
        beatSigma = BeatTuning.SENSITIVITY_MIN,
        beatIntervalMs = BeatTuning.INTERVAL_MS_MIN,
        attack = 0.85f,
        decay = 0.28f,
        audioDrive = 1.6f,
        bassGain = 0.85f,
        midGain = 1.25f,
        trebGain = 1.15f,
        beatResponse = 1.4f,
    ),

    VOICE(
        label = "Voice",
        summary = "Speech or singing: mids carry it, and syllables are not beats.",
        beatSigma = 2.2f,
        beatIntervalMs = 500f,
        attack = 0.5f,
        decay = 0.35f,
        audioDrive = 1.5f,
        bassGain = 0.5f,
        midGain = 1.4f,
        trebGain = 0.9f,
        beatResponse = 0.7f,
    ),
    ;

    fun apply(params: SceneParams): SceneParams =
        params.copy(
            audioDrive = audioDrive,
            bassGain = bassGain,
            midGain = midGain,
            trebGain = trebGain,
            beatResponse = beatResponse,
        )
}
