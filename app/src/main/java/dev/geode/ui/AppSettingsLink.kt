package dev.geode.ui

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.provider.Settings
import android.widget.Toast
import androidx.activity.result.ActivityResultLauncher
import dev.geode.R

fun Context.openAppSettings() {
    val appPage =
        Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS)
            .setData(Uri.fromParts("package", packageName, null))
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    if (start(appPage)) return
    start(Intent(Settings.ACTION_SETTINGS).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
}

private fun Context.start(intent: Intent): Boolean =
    try {
        startActivity(intent)
        true
    } catch (_: ActivityNotFoundException) {
        false
    } catch (_: SecurityException) {
        false
    }

/**
 * Opens a system document picker. A device without one (DocumentsUI disabled by the owner or an MDM,
 * some TVs and car units) throws ActivityNotFoundException from launch(); that gets a toast, not a crash.
 */
fun <I> ActivityResultLauncher<I>.launchPicker(
    context: Context,
    input: I,
) {
    try {
        launch(input)
    } catch (_: ActivityNotFoundException) {
        Toast.makeText(context, R.string.picker_unavailable, Toast.LENGTH_SHORT).show()
    }
}
