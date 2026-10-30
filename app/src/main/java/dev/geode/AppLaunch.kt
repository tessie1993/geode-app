package dev.geode

import android.content.Context
import android.content.Intent

/**
 * The intent that opens the app the way its launcher icon does.
 *
 * The widget and the foreground-service notifications only ever mean "bring the app up". Naming
 * `ui.MainActivity` for that made `widget`, `audio` and `export` depend on the UI layer, which
 * is the one direction the module split forbids. The launcher entry is `MainActivity` anyway, so
 * the behaviour is the same, and an already-running task is brought forward instead of stacked.
 */
fun Context.appLaunchIntent(): Intent =
    packageManager.getLaunchIntentForPackage(packageName)
        ?: Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER).setPackage(packageName)
