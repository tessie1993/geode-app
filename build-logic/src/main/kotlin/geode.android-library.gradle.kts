plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
    id("geode.kotlin-common")
}

extensions.configure<com.android.build.gradle.LibraryExtension> {
    compileSdk = 36
    defaultConfig {
        minSdk = 26
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    "testImplementation"("junit:junit:4.13.2")
    "testImplementation"("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.10.2")
}
