package dev.geode.render.scene

/** What a failed shader build throws; the export passes (native) report through it. */
object GlUtil {
    class ShaderCompileException(
        message: String,
    ) : RuntimeException(message)
}
