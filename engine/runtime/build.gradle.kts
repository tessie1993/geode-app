plugins {
    id("geode.android-library")
}

android {
    namespace = "dev.geode.engine.runtime"
}

dependencies {
    api(project(":engine:scenes"))
    api(project(":engine:audio-android"))
}
