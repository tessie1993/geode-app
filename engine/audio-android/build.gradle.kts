plugins {
    id("geode.android-library")
}

android {
    namespace = "dev.geode.engine.audioandroid"
}

dependencies {
    api(project(":engine:audio-core"))
    implementation(libs.media3.exoplayer)
    implementation(libs.media3.common)
}
