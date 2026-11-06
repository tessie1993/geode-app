package dev.geode.render

enum class EnvBand(
    val label: String,
) {
    BASS("Bass"),
    MID("Mid"),
    TREBLE("Treble"),
    RMS("Level"),
    BRIGHTNESS("Brightness"),
    WIDTH("Stereo width"),
}

data class AdsrConfig(
    val enabled: Boolean = false,
    val targets: List<LfoTarget> = emptyList(),
    val attack: Float = 0.05f,
    val decay: Float = 0.25f,
    val sustain: Float = 0.5f,
    val release: Float = 0.35f,
    val amount: Float = 0.5f,
    val band: EnvBand = EnvBand.BASS,
    val gateThreshold: Float = 0.25f,
    val sustainTrack: Boolean = false,
    val retrigger: Boolean = true,
)

// The envelopes' configs only; native runs the envelope and apply logic (see [PushedConfigs]).
class AdsrEngine : PushedConfigs<AdsrConfig>(List(COUNT) { AdsrConfig() }) {
    companion object {
        const val COUNT = 2
    }
}
