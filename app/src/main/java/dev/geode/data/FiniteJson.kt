package dev.geode.data

import org.json.JSONObject

/** JSON is an untrusted boundary: a non-finite number decodes to [fallback], never to NaN/Inf. */
internal fun JSONObject.finiteDouble(
    name: String,
    fallback: Double,
): Double {
    val v = optDouble(name, fallback)
    return if (v.isFinite()) v else fallback
}

/** For required doubles that throw if missing: if non-finite, fall back to safe default. */
internal fun JSONObject.finiteRequiredDouble(
    name: String,
    fallback: Double = 0.0,
): Double {
    val v = getDouble(name)
    return if (v.isFinite()) v else fallback
}
