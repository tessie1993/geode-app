package dev.geode.pro

import android.content.Context
import android.content.Intent

/**
 * Opens the app on the paywall for one feature, for surfaces outside the activity (the screensaver).
 * The launch intent names the app's own entry, so nothing outside the UI layer names it; the
 * activity reads the action and the feature back with [featureOf].
 */
object PaywallIntent {
    private const val ACTION = "dev.geode.action.OPEN_PAYWALL"
    private const val EXTRA_FEATURE = "dev.geode.extra.PRO_FEATURE"

    fun forFeature(
        context: Context,
        feature: ProFeature,
    ): Intent? =
        context.packageManager.getLaunchIntentForPackage(context.packageName)?.apply {
            action = ACTION
            putExtra(EXTRA_FEATURE, feature.name)
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        }

    /** The feature an [forFeature] intent asks the paywall for, or null for any other intent. */
    fun featureOf(intent: Intent?): ProFeature? {
        if (intent?.action != ACTION) return null
        val name = intent.getStringExtra(EXTRA_FEATURE) ?: return null
        return ProFeature.entries.firstOrNull { it.name == name }
    }
}
