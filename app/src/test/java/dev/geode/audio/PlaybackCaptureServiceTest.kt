package dev.geode.audio

import android.content.Context
import android.media.projection.MediaProjection
import android.os.Looper
import androidx.test.core.app.ApplicationProvider
import org.junit.After
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.Robolectric
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import java.lang.reflect.Proxy

/**
 * The system stops a projection when a new consent replaces it, and that stop reaches the service's callback
 * through the main looper, after the new projection is in place: it must not end the new capture.
 */
@RunWith(RobolectricTestRunner::class)
class PlaybackCaptureServiceTest {
    @After
    fun tearDown() {
        MediaProjectionHolder.publish(null)
    }

    @Test
    fun aReplacedProjectionsLateStopLeavesTheNewOne() {
        val service = Robolectric.buildService(PlaybackCaptureService::class.java).create().get()
        val first = FakeProjection()
        service.adopt(first.projection)
        first.stopBySystem()
        val second = FakeProjection()
        service.adopt(second.projection)
        shadowOf(Looper.getMainLooper()).idle()
        assertSame(second.projection, MediaProjectionHolder.projection.value)
    }

    @Test
    fun theCurrentProjectionsStopEndsTheCapture() {
        val service = Robolectric.buildService(PlaybackCaptureService::class.java).create().get()
        val only = FakeProjection()
        service.adopt(only.projection)
        only.stopBySystem()
        shadowOf(Looper.getMainLooper()).idle()
        assertNull(MediaProjectionHolder.projection.value)
    }

    /** A real [MediaProjection] over a stand-in for the system's side of it, which keeps the callback it is handed. */
    private class FakeProjection {
        private var systemCallback: Any? = null

        val projection: MediaProjection

        init {
            val iface = Class.forName("android.media.projection.IMediaProjection")
            val impl =
                Proxy.newProxyInstance(iface.classLoader, arrayOf(iface)) { _, method, args ->
                    if (method.name == "start") systemCallback = args[0]
                    when (method.returnType) {
                        java.lang.Boolean.TYPE -> false
                        Integer.TYPE -> 0
                        else -> null
                    }
                }
            projection =
                MediaProjection::class.java
                    .getDeclaredConstructor(Context::class.java, iface)
                    .newInstance(ApplicationProvider.getApplicationContext<Context>(), impl)
        }

        fun stopBySystem() {
            Class
                .forName("android.media.projection.IMediaProjectionCallback")
                .getMethod("onStop")
                .invoke(checkNotNull(systemCallback))
        }
    }
}
