package dev.geode.render

import android.content.Context
import android.opengl.GLES30
import dev.geode.engine.scenes.R
import java.nio.ByteBuffer

internal object BlueNoise {
    const val SIZE: Int = 64

    const val DITHER_AMOUNT: Float = 1f / 255f

    fun createTexture(context: Context): Int {
        val bytes =
            runCatching {
                context.resources.openRawResource(R.raw.blue_noise_64).use { it.readBytes() }
            }.getOrNull() ?: return 0
        if (bytes.size < SIZE * SIZE) return 0
        val ids = IntArray(1)
        GLES30.glGenTextures(1, ids, 0)
        val tex = ids[0]
        GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, tex)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_NEAREST)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_NEAREST)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_REPEAT)
        GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_REPEAT)
        val buf = ByteBuffer.allocateDirect(SIZE * SIZE).put(bytes, 0, SIZE * SIZE).apply { position(0) }
        GLES30.glPixelStorei(GLES30.GL_UNPACK_ALIGNMENT, 1)
        GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D, 0, GLES30.GL_R8, SIZE, SIZE, 0, GLES30.GL_RED, GLES30.GL_UNSIGNED_BYTE, buf)
        GLES30.glPixelStorei(GLES30.GL_UNPACK_ALIGNMENT, 4)
        return tex
    }
}
