package dev.geode.export

import androidx.annotation.StringRes
import dev.geode.R

enum class ClipLook(
    @param:StringRes val labelRes: Int,
) {
    NONE(R.string.studio_as_shot),
    PUNCH(R.string.studio_look_punch),
    BLEACH(R.string.studio_look_bleach),
    NEON(R.string.studio_look_neon),
    NIGHT(R.string.studio_look_night),
    MONO(R.string.studio_look_mono),
    INVERT(R.string.studio_look_invert),
    ;

    fun applyTo(edit: ClipEdit): ClipEdit =
        edit.copy(
            brightness = brightness,
            contrast = contrast,
            saturation = saturation,
            hueDegrees = hue,
            monochrome = this == MONO,
            invert = this == INVERT,
        )

    private val brightness: Float
        get() =
            when (this) {
                PUNCH, MONO -> 0.02f
                BLEACH -> 0.06f
                NIGHT -> -0.14f
                else -> 0f
            }

    private val contrast: Float
        get() =
            when (this) {
                PUNCH -> 0.28f
                BLEACH -> 0.18f
                NEON -> 0.22f
                NIGHT -> 0.12f
                MONO -> 0.2f
                else -> 0f
            }

    private val saturation: Float
        get() =
            when (this) {
                PUNCH -> 30f
                BLEACH -> -55f
                NEON -> 65f
                NIGHT -> -15f
                else -> 0f
            }

    private val hue: Float
        get() =
            when (this) {
                NEON -> 14f
                NIGHT -> -18f
                else -> 0f
            }
}

data class ClipEdit(
    val startMs: Long = 0L,
    val endMs: Long = 0L,
    val look: ClipLook = ClipLook.NONE,
    val brightness: Float = 0f,
    val contrast: Float = 0f,
    val saturation: Float = 0f,
    val hueDegrees: Float = 0f,
    val monochrome: Boolean = false,
    val invert: Boolean = false,
    val speed: Float = 1f,
    val rotationDegrees: Float = 0f,
    val ratio: ExportRatio? = null,
    val quality: ExportQuality = ExportQuality.FHD1080,
    val mute: Boolean = false,
    val caption: String = "",
    val lutUri: String? = null,
    val gammaRed: Float = 1f,
    val gammaGreen: Float = 1f,
    val gammaBlue: Float = 1f,
) {
    val hasGamma: Boolean get() = gammaRed != 1f || gammaGreen != 1f || gammaBlue != 1f

    fun trimmedMs(sourceDurationMs: Long): Long {
        val end = if (endMs > 0) endMs.coerceAtMost(sourceDurationMs) else sourceDurationMs
        return (end - startMs).coerceAtLeast(0L)
    }

    fun outputMs(sourceDurationMs: Long): Long = (trimmedMs(sourceDurationMs) / speed.coerceAtLeast(0.01f)).toLong()

    fun isIdentity(sourceDurationMs: Long): Boolean =
        startMs == 0L &&
            (endMs == 0L || endMs >= sourceDurationMs) &&
            brightness == 0f &&
            contrast == 0f &&
            saturation == 0f &&
            hueDegrees == 0f &&
            !monochrome &&
            !invert &&
            speed == 1f &&
            rotationDegrees == 0f &&
            ratio == null &&
            !mute &&
            caption.isBlank() &&
            lutUri == null &&
            !hasGamma
}
