package dev.geode.nav.platform

import androidx.activity.BackEventCompat
import androidx.activity.OnBackPressedCallback
import androidx.activity.OnBackPressedDispatcher
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.lifecycleScope
import dev.geode.nav.BackGesture
import dev.geode.nav.Navigator
import kotlinx.coroutines.launch

/**
 * Feeds the platform's back — the button, the edge swipe and its predictive preview — into the
 * [Navigator]. The callback is enabled exactly while the navigator has something to do, so when
 * it has not, the system finishes the activity as it normally would.
 */
fun Navigator.bindBack(
    dispatcher: OnBackPressedDispatcher,
    owner: LifecycleOwner,
) {
    val callback =
        object : OnBackPressedCallback(state.value.canGoBack) {
            override fun handleOnBackStarted(backEvent: BackEventCompat) {
                val edge = if (backEvent.swipeEdge == BackEventCompat.EDGE_LEFT) BackGesture.Edge.LEFT else BackGesture.Edge.RIGHT
                backStarted(edge)
            }

            override fun handleOnBackProgressed(backEvent: BackEventCompat) {
                backProgressed(backEvent.progress)
            }

            override fun handleOnBackCancelled() {
                backCancelled()
            }

            override fun handleOnBackPressed() {
                back()
            }
        }
    dispatcher.addCallback(owner, callback)
    owner.lifecycleScope.launch {
        state.collect { callback.isEnabled = it.canGoBack }
    }
}
