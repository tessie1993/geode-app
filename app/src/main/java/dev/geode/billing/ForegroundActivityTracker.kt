package dev.geode.billing

import android.app.Activity
import android.app.Application
import android.os.Bundle
import java.lang.ref.WeakReference
import javax.inject.Inject
import javax.inject.Singleton

/**
 * The activity Play's purchase sheet launches from.
 *
 * `launchBillingFlow` needs an Activity, and neither the paywall's ViewModel nor the repository
 * may hold one. Registered once from `GeodeApp`, this keeps a weak reference to whichever activity
 * is resumed, so the repository can borrow it for the one call and nothing outlives it.
 */
@Singleton
class ForegroundActivityTracker
    @Inject
    constructor() : Application.ActivityLifecycleCallbacks {
        private var resumed: WeakReference<Activity>? = null

        val current: Activity? get() = resumed?.get()?.takeUnless { it.isFinishing || it.isDestroyed }

        override fun onActivityResumed(activity: Activity) {
            resumed = WeakReference(activity)
        }

        override fun onActivityPaused(activity: Activity) {
            if (resumed?.get() === activity) resumed = null
        }

        override fun onActivityCreated(
            activity: Activity,
            savedInstanceState: Bundle?,
        ) = Unit

        override fun onActivityStarted(activity: Activity) = Unit

        override fun onActivityStopped(activity: Activity) = Unit

        override fun onActivitySaveInstanceState(
            activity: Activity,
            outState: Bundle,
        ) = Unit

        override fun onActivityDestroyed(activity: Activity) = Unit
    }
