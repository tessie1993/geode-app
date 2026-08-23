package dev.geode.export

import androidx.media3.common.Effect
import androidx.media3.common.MediaItem
import androidx.media3.common.audio.SpeedProvider
import androidx.media3.common.util.UnstableApi
import androidx.media3.effect.Brightness
import androidx.media3.effect.Contrast
import androidx.media3.effect.HslAdjustment
import androidx.media3.effect.OverlayEffect
import androidx.media3.effect.Presentation
import androidx.media3.effect.RgbFilter
import androidx.media3.effect.ScaleAndRotateTransformation
import androidx.media3.effect.StaticOverlaySettings
import androidx.media3.effect.TextOverlay

enum class ClipLook(
    val label: String,
) {
    NONE("As shot"),
    PUNCH("Punch"),
    BLEACH("Bleach"),
    NEON("Neon"),
    NIGHT("Night"),
    MONO("Mono"),
    INVERT("Invert"),
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
) {
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
            caption.isBlank()

    fun clipping(): MediaItem.ClippingConfiguration =
        MediaItem.ClippingConfiguration
            .Builder()
            .setStartPositionMs(startMs.coerceAtLeast(0L))
            .apply { if (endMs > startMs) setEndPositionMs(endMs) }
            .build()

    @UnstableApi
    fun videoEffects(): List<Effect> =
        buildList {
            if (brightness != 0f) add(Brightness(brightness))
            if (contrast != 0f) add(Contrast(contrast))
            if (saturation != 0f || hueDegrees != 0f) {
                add(
                    HslAdjustment
                        .Builder()
                        .adjustSaturation(saturation)
                        .adjustHue(hueDegrees)
                        .build(),
                )
            }
            if (monochrome) add(RgbFilter.createGrayscaleFilter())
            if (invert) add(RgbFilter.createInvertedFilter())
            if (rotationDegrees != 0f) {
                add(
                    ScaleAndRotateTransformation
                        .Builder()
                        .setRotationDegrees(rotationDegrees)
                        .build(),
                )
            }
            ratio?.let { r ->
                val aspect = ExportAspect.of(quality, r)
                add(
                    Presentation.createForWidthAndHeight(
                        aspect.width,
                        aspect.height,
                        Presentation.LAYOUT_SCALE_TO_FIT_WITH_CROP,
                    ),
                )
            }
            caption.takeIf { it.isNotBlank() }?.let { text ->
                add(
                    OverlayEffect(
                        listOf(
                            TextOverlay.createStaticTextOverlay(
                                android.text.SpannableString(text),
                                StaticOverlaySettings
                                    .Builder()
                                    .setBackgroundFrameAnchor(0f, -0.82f)
                                    .setOverlayFrameAnchor(0f, -1f)
                                    .build(),
                            ),
                        ),
                    ),
                )
            }
        }

    @UnstableApi
    fun speedProvider(): SpeedProvider? =
        if (speed == 1f) {
            null
        } else {
            object : SpeedProvider {
                override fun getSpeed(timeUs: Long): Float = speed

                override fun getNextSpeedChangeTimeUs(timeUs: Long): Long = androidx.media3.common.C.TIME_UNSET
            }
        }
}
