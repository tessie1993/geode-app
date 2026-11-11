plugins {
    id("com.android.library")
    id("geode.kotlin-common")
}

extensions.configure<com.android.build.api.dsl.LibraryExtension> {
    compileSdk = 37
    defaultConfig {
        minSdk = 33
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

val catalog = extensions.getByType<VersionCatalogsExtension>().named("libs")

dependencies {
    "testImplementation"(catalog.findLibrary("junit").get())
    "testImplementation"(catalog.findLibrary("kotlinx-coroutines-test").get())
}
