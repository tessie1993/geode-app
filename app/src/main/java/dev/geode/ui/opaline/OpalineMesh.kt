package dev.geode.ui.opaline

import org.json.JSONArray
import org.json.JSONObject
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.sqrt

/** Reader for the bundled, authored glTF 2.0 meshes. No network, scripts or external buffers. */
internal data class OpalineMesh(
    val pieces: List<Piece>,
    val minimum: FloatArray,
    val maximum: FloatArray,
) {
    data class Piece(
        val positions: FloatArray,
        val normals: FloatArray,
        val indices: IntArray,
        val morphs: List<FloatArray>,
        val transform: FloatArray,
        val color: FloatArray,
        val roughness: Float,
        val transmission: Float,
        val iridescence: Float,
        val motion: String,
        val travel: FloatArray,
        val deformable: Boolean,
    )

    companion object {
        val ELEMENTS = listOf("A01", "A03", "A05", "A22", "B01", "B03", "B07", "B09", "B13", "C01", "C02", "C03", "C04", "C20", "D03")

        fun read(bytes: ByteArray): OpalineMesh {
            val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
            require(buffer.int == 0x46546c67 && buffer.int == 2 && buffer.int == bytes.size) { "Invalid GLB header" }
            val jsonSize = buffer.int
            require(buffer.int == 0x4e4f534a && jsonSize in 1..buffer.remaining()) { "Missing GLB JSON" }
            val json = ByteArray(jsonSize).also { buffer.get(it) }
            val root = JSONObject(String(json, Charsets.UTF_8))
            val binarySize = buffer.int
            require(buffer.int == 0x004e4942 && binarySize == buffer.remaining()) { "Missing GLB binary" }
            val binary = buffer.slice().order(ByteOrder.LITTLE_ENDIAN)
            val accessors = root.getJSONArray("accessors")
            val views = root.getJSONArray("bufferViews")
            fun floats(index: Int): FloatArray {
                val a = accessors.getJSONObject(index)
                require(a.getInt("componentType") == 5126 && !a.has("sparse"))
                val components = when (a.getString("type")) { "VEC3" -> 3; else -> error("Expected VEC3") }
                val view = views.getJSONObject(a.getInt("bufferView"))
                require(view.optInt("buffer") == 0)
                val start = view.optInt("byteOffset") + a.optInt("byteOffset")
                val stride = view.optInt("byteStride", components * 4)
                return FloatArray(a.getInt("count") * components) { i ->
                    binary.getFloat(start + i / components * stride + i % components * 4).also { require(it.isFinite()) }
                }
            }
            fun indices(index: Int): IntArray {
                val a = accessors.getJSONObject(index)
                val view = views.getJSONObject(a.getInt("bufferView"))
                val start = view.optInt("byteOffset") + a.optInt("byteOffset")
                return IntArray(a.getInt("count")) { i ->
                    when (a.getInt("componentType")) {
                        5123 -> binary.getShort(start + i * 2).toInt() and 0xffff
                        5125 -> binary.getInt(start + i * 4)
                        else -> error("Unsupported index type")
                    }
                }
            }
            val pieces = mutableListOf<Piece>()
            val low = FloatArray(3) { Float.POSITIVE_INFINITY }
            val high = FloatArray(3) { Float.NEGATIVE_INFINITY }
            val nodes = root.getJSONArray("nodes")
            fun visit(index: Int, parent: FloatArray) {
                val node = nodes.getJSONObject(index)
                val transform = multiply(parent, transform(node))
                val extras = node.optJSONObject("extras")
                val motion = extras?.optJSONObject("motion")
                if (node.has("mesh")) {
                    val primitives = root.getJSONArray("meshes").getJSONObject(node.getInt("mesh")).getJSONArray("primitives")
                    for (p in 0 until primitives.length()) {
                        val primitive = primitives.getJSONObject(p)
                        require(primitive.optInt("mode", 4) == 4)
                        val attributes = primitive.getJSONObject("attributes")
                        val positions = floats(attributes.getInt("POSITION"))
                        val normals = floats(attributes.getInt("NORMAL"))
                        val indices = indices(primitive.getInt("indices"))
                        require(normals.size == positions.size && indices.all { it in 0 until positions.size / 3 })
                        for (v in positions.indices step 3) {
                            for (axis in 0..2) {
                                val value = transform[axis] * positions[v] + transform[4 + axis] * positions[v + 1] +
                                    transform[8 + axis] * positions[v + 2] + transform[12 + axis]
                                low[axis] = minOf(low[axis], value)
                                high[axis] = maxOf(high[axis], value)
                            }
                        }
                        val material = root.getJSONArray("materials").getJSONObject(primitive.getInt("material"))
                        val pbr = material.getJSONObject("pbrMetallicRoughness")
                        val extensions = material.optJSONObject("extensions")
                        val targets = primitive.optJSONArray("targets")
                        val morphs = (0 until (targets?.length() ?: 0)).map { floats(targets!!.getJSONObject(it).getInt("POSITION")) }
                        require(morphs.all { it.size == positions.size })
                        val axis = if (motion?.optString("axis", "x") == "y") 1 else 0
                        val translation = node.optJSONArray("translation")?.optDouble(axis, 0.0)?.toFloat() ?: 0f
                        val min = motion?.optDouble("min", -0.94)?.toFloat() ?: 0f
                        val max = motion?.optDouble("max", 0.94)?.toFloat() ?: 0f
                        // Slider offsets are expressed in its parent's coordinates, including parent rotation/scale.
                        val travel = floatArrayOf(parent[axis * 4], parent[axis * 4 + 1], parent[axis * 4 + 2], min - translation, max - min)
                        pieces += Piece(positions, normals, indices, morphs, transform,
                            pbr.getJSONArray("baseColorFactor").floats(4), pbr.optDouble("roughnessFactor", 0.2).toFloat(),
                            extensions?.optJSONObject("KHR_materials_transmission")?.optDouble("transmissionFactor")?.toFloat() ?: 0f,
                            extensions?.optJSONObject("KHR_materials_iridescence")?.optDouble("iridescenceFactor")?.toFloat() ?: 0f,
                            motion?.optString("kind").orEmpty(), travel, extras?.optBoolean("deformable") == true)
                    }
                }
                node.optJSONArray("children")?.let { children -> for (i in 0 until children.length()) visit(children.getInt(i), transform) }
            }
            val scene = root.getJSONArray("scenes").getJSONObject(root.optInt("scene")).getJSONArray("nodes")
            for (i in 0 until scene.length()) visit(scene.getInt(i), identity())
            require(pieces.isNotEmpty() && low.all { it.isFinite() } && high.all { it.isFinite() })
            return OpalineMesh(pieces, low, high)
        }

        private fun JSONArray.floats(size: Int) = FloatArray(size) { getDouble(it).toFloat() }
        private fun identity() = FloatArray(16) { if (it % 5 == 0) 1f else 0f }
        private fun multiply(a: FloatArray, b: FloatArray) = FloatArray(16) { i ->
            (0..3).sumOf { k -> (a[k * 4 + i % 4] * b[i / 4 * 4 + k]).toDouble() }.toFloat()
        }
        private fun transform(node: JSONObject): FloatArray {
            node.optJSONArray("matrix")?.let { return it.floats(16) }
            val q = node.optJSONArray("rotation")?.floats(4) ?: floatArrayOf(0f, 0f, 0f, 1f)
            val length = sqrt(q.sumOf { (it * it).toDouble() }).toFloat()
            require(length > 0f)
            val x = q[0] / length; val y = q[1] / length; val z = q[2] / length; val w = q[3] / length
            val matrix = floatArrayOf(
                1-2*y*y-2*z*z, 2*x*y+2*z*w, 2*x*z-2*y*w, 0f,
                2*x*y-2*z*w, 1-2*x*x-2*z*z, 2*y*z+2*x*w, 0f,
                2*x*z+2*y*w, 2*y*z-2*x*w, 1-2*x*x-2*y*y, 0f,
                0f, 0f, 0f, 1f,
            )
            val scale = node.optJSONArray("scale")?.floats(3) ?: floatArrayOf(1f, 1f, 1f)
            for (c in 0..2) for (r in 0..2) matrix[c * 4 + r] *= scale[c]
            node.optJSONArray("translation")?.let { for (i in 0..2) matrix[12 + i] = it.getDouble(i).toFloat() }
            return matrix
        }
    }
}
