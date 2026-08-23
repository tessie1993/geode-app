package dev.geode.render.fluid

import android.content.Context
import dev.geode.engine.scenes.R

/**
 * The built-in fluid injection shader, as editable source.
 *
 * The customise screen seeds its force/dye shader editors with this and compares against it to
 * tell an edited shader from an untouched one. Exposing the text keeps the shader assets
 * themselves — and the resource ids that name them — inside the engine.
 */
object FluidShaderTemplate {
    /** Source of the default splat shader, or an empty string if it cannot be read. */
    fun splat(context: Context): String =
        runCatching {
            context.resources
                .openRawResource(R.raw.fluid_splat_frag)
                .bufferedReader()
                .use { it.readText() }
        }.getOrDefault("")
}
