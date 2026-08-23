plugins {
    id("geode.android-library")
}

android {
    namespace = "dev.geode.engine.scenes"
}

dependencies {
    api(project(":engine:gl"))
    api(project(":engine:audio-core"))
    implementation(libs.kotlinx.coroutines.android)
}
