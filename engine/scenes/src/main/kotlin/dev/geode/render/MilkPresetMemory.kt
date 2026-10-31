package dev.geode.render

/**
 * The MilkDrop preset a renderer is showing, kept on the JVM side of the native renderer.
 *
 * The native renderer does not live as long as the view that owns it: [VisualizerView] destroys
 * it whenever the view leaves the window (a tab change, the live view collapsing, PiP), and the
 * one built on return knows no preset. projectM then starts on its idle preset, which is the
 * "reset" this exists to prevent: [VisualizerRenderer] hands [restorePath] to every new native
 * renderer before its first frame.
 *
 * Only a load the native side has reported counts as shown. A request that never reports was
 * either still in flight or failed to compile, and a failed one would only bring back black, so
 * it is used just while nothing has loaded yet. Session-only by design: nothing here is saved.
 */
class MilkPresetMemory {
    @Volatile
    private var requested: String? = null

    @Volatile
    private var shown: String? = null

    /** What a rebuilt native renderer must start on, or null when no preset has been chosen. */
    val restorePath: String? get() = shown ?: requested

    /** The UI asked for [path]; any thread. */
    fun request(path: String) {
        requested = path
    }

    /** The native side compiled [path], whether asked for here or by auto-advance; GL thread. */
    fun loaded(path: String) {
        shown = path
    }
}
