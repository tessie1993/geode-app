plugins {
    id("org.jetbrains.kotlin.jvm")
    id("geode.kotlin-common")
}

java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}

val catalog = extensions.getByType<VersionCatalogsExtension>().named("libs")

dependencies {
    "testImplementation"(catalog.findLibrary("junit").get())
    "testImplementation"(catalog.findLibrary("kotlinx-coroutines-test").get())
}
