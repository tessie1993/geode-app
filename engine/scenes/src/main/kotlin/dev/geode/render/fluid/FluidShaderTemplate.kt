package dev.geode.render.fluid

import android.content.Context

object FluidShaderTemplate {
    private const val ASSET = "shaders/fluid_splat_frag.glsl"

    fun splat(context: Context): String =
        runCatching {
            context.assets
                .open(ASSET)
                .bufferedReader()
                .use { it.readText() }
        }.getOrDefault("")
}
