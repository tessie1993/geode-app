package dev.geode.render

import android.content.Context
import android.opengl.GLES30
import dev.geode.engine.scenes.R
import dev.geode.render.scene.GlUtil

internal class TransitionPrograms(
    private val context: Context,
) {
    private companion object {
        const val MAX_PROGRAMS = 4
    }

    private var base = GlUtil.UniformCache(0)
    private var source = ""
    private val cache = LinkedHashMap<String, GlUtil.UniformCache>()
    private var uploadedFor: GlUtil.UniformCache? = null
    private var uploadedDef: TransitionCatalog.Def? = null

    fun create(fadeVert: String) {
        source = GlUtil.loadShader(context, R.raw.composite_frag)
        base = GlUtil.UniformCache(GlUtil.buildProgram(fadeVert, source))
        cache.clear()
        uploadedFor = null
        uploadedDef = null
    }

    fun definition(id: String): TransitionCatalog.Def? = TransitionCatalog.definition(context, id)

    @Suppress("ReturnCount")
    fun programFor(id: String): GlUtil.UniformCache {
        if (TransitionCatalog.builtIn(id) != null) return base
        cache[id]?.let {
            cache.remove(id)
            cache[id] = it
            return it
        }
        val def = definition(id) ?: return base
        val program =
            runCatching {
                GlUtil.UniformCache(
                    GlUtil.buildProgram(
                        GlUtil.loadShader(context, R.raw.fade_vert),
                        TransitionCatalog.spliceInto(source, def),
                    ),
                )
            }.getOrElse {
                android.util.Log.w("Transitions", "\"$id\" failed to link: ${it.message}")
                return base
            }
        while (cache.size >= MAX_PROGRAMS) {
            val oldest = cache.keys.first()
            cache.remove(oldest)?.let { p -> GLES30.glDeleteProgram(p.program) }
        }
        cache[id] = program
        return program
    }

    fun warm(id: String) {
        if (source.isNotEmpty()) programFor(id)
    }

    fun uploadParamsIfNeeded(
        program: GlUtil.UniformCache,
        def: TransitionCatalog.Def?,
    ) {
        if (def != null && (program !== uploadedFor || def !== uploadedDef)) {
            TransitionCatalog.uploadParams(program.program, def)
            uploadedFor = program
            uploadedDef = def
        }
    }
}
