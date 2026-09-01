package dev.geode.engine.bridge

object GeodeNative {
    init {
        System.loadLibrary("geode")
    }

    external fun version(): String
}
