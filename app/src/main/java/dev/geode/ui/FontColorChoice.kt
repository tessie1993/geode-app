package dev.geode.ui

import androidx.annotation.StringRes
import dev.geode.R

data class FontColorChoice(
    @StringRes val labelRes: Int,
    val argb: Int?,
) {
    companion object {
        const val WHITE_ARGB = 0xFFFFFFFF.toInt()

        val CHOICES: List<FontColorChoice> =
            listOf(
                FontColorChoice(R.string.font_color_auto, null),
                FontColorChoice(R.string.font_color_white, WHITE_ARGB),
                FontColorChoice(R.string.font_color_moonstone, 0xFFF2EAD9.toInt()),
                FontColorChoice(R.string.font_color_pyrite, 0xFFD6B15A.toInt()),
                FontColorChoice(R.string.font_color_rose, 0xFFF6BFD0.toInt()),
                FontColorChoice(R.string.font_color_lavender, 0xFFCDB8F2.toInt()),
                FontColorChoice(R.string.font_color_ice, 0xFFBFE2F2.toInt()),
                FontColorChoice(R.string.font_color_mint, 0xFFBDEFD8.toInt()),
            )
    }
}
