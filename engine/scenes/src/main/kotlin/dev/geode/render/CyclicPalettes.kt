package dev.geode.render

import android.content.Context
import android.opengl.GLES30
import dev.geode.engine.scenes.R
import java.nio.ByteBuffer

internal object CyclicPalettes {
    const val SIZE: Int = 256

    val NAMES: List<String> = listOf("bamO", "brocO", "corkO", "romaO", "vikO")

    fun rowCoordinate(index: Int): Float = (index.coerceIn(0, NAMES.size - 1) + 0.5f) / NAMES.size

    fun createTexture(context: Context): Int {
        val expected = SIZE * NAMES.size * 3
        val bytes =
            runCatching {
                context.resources.openRawResource(R.raw.cyclic_palettes).use { it.readBytes() }
            }.getOrNull() ?: return 0
        if (bytes.size < expected) return 0
        val ids = IntArray(1)
        GLES30.glGenTextures(1, ids, 0)
        val tex = ids[0]
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, tex)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_REPEAT)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
        val buf = ByteBuffer.allocateDirect(expected).put(bytes, 0, expected).apply { position(0) }
        GLES30.glPixelStorei(GLES30.GL_UNPACK_ALIGNMENT, 1)
        GLES30.glTexImage2D(
            GLES30.GL_TEXTURE_2D,
            0,
            GLES30.GL_RGB8,
            SIZE,
            NAMES.size,
            0,
            GLES30.GL_RGB,
            GLES30.GL_UNSIGNED_BYTE,
            buf,
        )
        GLES30.glPixelStorei(GLES30.GL_UNPACK_ALIGNMENT, 4)
        return tex
    }
}
