package dev.geode.util

import android.os.Process
import java.util.concurrent.ThreadFactory

internal fun backgroundThreadFactory(
    name: String,
    daemon: Boolean = true,
) = ThreadFactory { task ->
    Thread(
        {
            Process.setThreadPriority(Process.THREAD_PRIORITY_BACKGROUND)
            task.run()
        },
        name,
    ).apply { isDaemon = daemon }
}
