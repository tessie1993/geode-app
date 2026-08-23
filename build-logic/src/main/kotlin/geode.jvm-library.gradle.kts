plugins {
    id("org.jetbrains.kotlin.jvm")
    id("geode.kotlin-common")
}

java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}

dependencies {
    "testImplementation"("junit:junit:4.13.2")
    "testImplementation"("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.10.2")
}
