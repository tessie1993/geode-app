plugins {
    `kotlin-dsl`
}

// build-logic is the one project that cannot apply geode.kotlin-common - it defines it - so it
// never inherited that file's toolchain pin, and its Java compilation silently followed whichever
// JDK launched Gradle. Against Android Studio's bundled JBR 25 that produced
// "Inconsistent JVM-target compatibility detected for tasks 'compileJava' (25) and
// 'compileKotlin' (17)". Pinning source/target here rather than relying on the toolchain alone is
// deliberate: it makes compileJava emit 17 regardless of the daemon JVM, which is the same 17 the
// kotlin block below targets, so the two agree on any developer's machine.
java {
    toolchain {
        languageVersion.set(JavaLanguageVersion.of(21))
    }
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}

dependencies {
    implementation(libs.ktlint.gradle)
    implementation(libs.detekt.gradle)
    implementation(libs.kotlin.gradle.plugin)
    implementation(libs.android.gradle.plugin)
}
