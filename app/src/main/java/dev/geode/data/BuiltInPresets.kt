package dev.geode.data

import dev.geode.engine.bridge.GeodeNative
import dev.geode.render.bridge.SceneParamsCodec
import dev.geode.render.scene.ParamSchema
import dev.geode.render.scene.SceneParams

/**
 * The bundled looks, read from the engine. Which looks ship, on which styles, and every value they set
 * are native (core/viz/CataloguePresets, the catalogue's presets section, #648): the shell asks once
 * through [GeodeNative.cataloguePresetsText] and reads the rows. Each look is a set of values by param
 * name (#346's schema), laid over [SceneParams]'s defaults through the wire codec, so a look reads back
 * exactly the params the old Kotlin literal built; core/tests/presets_test.cpp pins those values.
 */
object BuiltInPresets {
    /** The table version this shell reads; core/viz/CataloguePresets.hpp's `kPresetsTextVersion`. */
    private const val VERSION: Int = 1

    val ALL: List<Preset> by lazy { parse(GeodeNative.cataloguePresetsText()) }

    fun isBuiltIn(name: String): Boolean = name.contains(" · ")

    /**
     * Reads the `geode_catalogue_presets_text` table: a `presets|<version>|<looks>|<presets>` header,
     * `look|<key>|<attack>|<decay>|<name>=<value>,...` rows and `preset|<name>|<sceneId>|<look key>` rows.
     * Presets that share a look share one [SceneParams]. Throws on a version, a param or a look key this
     * shell does not know, since a misread look would apply other values than the engine ships.
     */
    internal fun parse(text: String): List<Preset> {
        val rows = text.lines().filter { it.isNotBlank() }.map { it.split('|') }
        val header = rows.firstOrNull().orEmpty()
        check(header.size == 4 && header[0] == "presets" && header[1] == VERSION.toString()) {
            "native presets table is $header, this shell reads version $VERSION"
        }
        val looks = mutableMapOf<String, Preset>()
        val presets = mutableListOf<Preset>()
        for (f in rows.drop(1)) {
            when (f[0]) {
                "look" -> looks[f[1]] = Preset(f[1], "", f[2].toFloat(), f[3].toFloat(), params = params(f[4]))
                "preset" -> {
                    val look = checkNotNull(looks[f[3]]) { "preset ${f[1]} names no look ${f[3]}" }
                    presets += look.copy(name = f[1], sceneId = f[2])
                }
            }
        }
        check(looks.size == header[2].toInt() && presets.size == header[3].toInt()) {
            "presets table counts disagree with $header"
        }
        return presets
    }

    private fun params(values: String): SceneParams {
        val frame = SceneParamsCodec.pack(SceneParams())
        for (pair in values.split(',').filter { it.isNotEmpty() }) {
            val (name, value) = pair.split('=')
            frame[checkNotNull(ParamSchema[name]) { "bundled look sets unknown param $name" }.slot] = value.toFloat()
        }
        return SceneParamsCodec.unpack(frame, SceneParams())
    }
}
