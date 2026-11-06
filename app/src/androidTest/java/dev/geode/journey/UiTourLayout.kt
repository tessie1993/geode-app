package dev.geode.journey

import android.graphics.Rect
import android.os.Build
import android.view.accessibility.AccessibilityNodeInfo
import org.json.JSONArray
import org.json.JSONObject

/**
 * The accessibility tree beside each screenshot, in the shape `android layout` prints (text,
 * content-desc, interactions, state, bounds, center), so a reader can tell what TalkBack and
 * Switch Access see on the same frame the PNG shows.
 *
 * It is read through the instrumentation's UiAutomation, which is an accessibility client: the
 * tree is the one the platform hands TalkBack, after Compose has merged its semantics, not the
 * Compose test tree (which also holds nodes TalkBack never visits). That is also why it can
 * answer the audit's TalkBack question directly: [Dump.unlabeled] lists every node a user can
 * act on that has no text and no content description of its own or below it, which TalkBack
 * reads as "button" or "unlabelled".
 *
 * Only the active window is read (a dialog or popup while one is up, else the activity): the
 * window list needs an accessibility flag the default UiAutomation does not set.
 */
object UiTourLayout {
    class Dump(
        val nodes: JSONArray,
        val unlabeled: List<String>,
    )

    fun dump(): Dump {
        val nodes = JSONArray()
        val unlabeled = mutableListOf<String>()
        val root = runCatching { TourDevice.automation.rootInActiveWindow }.getOrNull() ?: return Dump(nodes, unlabeled)
        walk(root, nodes, unlabeled, depth = 0)
        return Dump(nodes, unlabeled)
    }

    /** Depth-first, keeping only the nodes `android layout` keeps: with content, or actionable. */
    private fun walk(
        node: AccessibilityNodeInfo,
        out: JSONArray,
        unlabeled: MutableList<String>,
        depth: Int,
    ) {
        val interactions =
            listOfNotNull(
                "CLICKABLE".takeIf { node.isClickable },
                "LONG-CLICKABLE".takeIf { node.isLongClickable },
                "SCROLLABLE".takeIf { node.isScrollable },
                "CHECKABLE".takeIf { node.isCheckable },
                "FOCUSABLE".takeIf { node.isFocusable },
            )
        val state =
            listOfNotNull(
                "CHECKED".takeIf { node.isChecked },
                "SELECTED".takeIf { node.isSelected },
                "FOCUSED".takeIf { node.isFocused },
                "DISABLED".takeIf { !node.isEnabled },
            )
        val text = node.text?.toString().orEmpty()
        val desc = node.contentDescription?.toString().orEmpty()
        val bounds = Rect().also(node::getBoundsInScreen)
        if (text.isNotEmpty() || desc.isNotEmpty() || interactions.isNotEmpty() || state.isNotEmpty()) {
            val entry =
                JSONObject()
                    .put("class", node.className?.toString().orEmpty())
                    .put("text", text)
                    .put("content-desc", desc)
                    .put("interactions", JSONArray(interactions))
                    .put("state", JSONArray(state))
                    .put("bounds", "[${bounds.left},${bounds.top}][${bounds.right},${bounds.bottom}]")
                    .put("center", "[${bounds.centerX()},${bounds.centerY()}]")
                    .put("depth", depth)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                entry.put("state-desc", node.stateDescription?.toString().orEmpty())
            }
            node.viewIdResourceName?.let { entry.put("resource-id", it) }
            out.put(entry)
        }
        val actionable = node.isClickable || node.isLongClickable || node.isCheckable
        if (actionable && node.isVisibleToUser && !hasLabel(node)) {
            unlabeled += "${node.className?.toString()?.substringAfterLast('.')} at [${bounds.centerX()},${bounds.centerY()}]"
        }
        for (i in 0 until node.childCount) {
            val child = runCatching { node.getChild(i) }.getOrNull() ?: continue
            walk(child, out, unlabeled, depth + 1)
        }
    }

    /** Text or a description on the node or anything under it: what TalkBack would read for it. */
    private fun hasLabel(node: AccessibilityNodeInfo): Boolean {
        if (!node.text.isNullOrBlank() || !node.contentDescription.isNullOrBlank()) return true
        for (i in 0 until node.childCount) {
            val child = runCatching { node.getChild(i) }.getOrNull() ?: continue
            if (hasLabel(child)) return true
        }
        return false
    }
}
