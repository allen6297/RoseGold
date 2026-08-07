plugins {
    id("java")
    id("org.jetbrains.kotlin.jvm") version "2.0.21"
    id("org.jetbrains.intellij.platform") version "2.1.0"
}

group = "dev.rosegold"
version = "0.1.0"

repositories {
    mavenCentral()
    intellijPlatform { defaultRepositories() }
}

dependencies {
    intellijPlatform {
        // IDEA 2024.2 runs on JBR 21, matching the jvmToolchain below.
        intellijIdeaCommunity("2024.2")
        instrumentationTools()   // required by the :instrumentCode task
    }
}

// Build with the JDK 21 you have installed (Temurin). Do NOT use 17 (absent) or
// the phantom JDK 25 that broke the Kotlin compiler.
kotlin { jvmToolchain(21) }

intellijPlatform {
    pluginConfiguration {
        ideaVersion {
            sinceBuild = "242"
            untilBuild = provider { null }
        }
    }
}
