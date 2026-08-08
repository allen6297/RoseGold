plugins {
    id("java")
    id("org.jetbrains.kotlin.jvm") version "2.2.0"   // match IDEA 2025.2's bundled Kotlin (its jars carry 2.2.0 metadata)
    id("org.jetbrains.intellij.platform") version "2.10.0"   // 2.1.0 mis-parses 2025.2's launch layout (runIde: "Index: 1, Size: 1")
}

group = "dev.rosegold"
version = "0.4.0"

repositories {
    mavenCentral()
    intellijPlatform { defaultRepositories() }   // includes the JetBrains Marketplace (for LSP4IJ)
}

dependencies {
    intellijPlatform {
        // IDEA 2025.2 runs on JBR 21, matching the jvmToolchain below. (2025.x's
        // JavaVersion parser understands Java 25, unlike 2024.2 — see the runIde note.)
        intellijIdeaCommunity("2025.2.6")
        // LSP4IJ: the open-source library that runs a language server inside JetBrains IDEs.
        plugin("com.redhat.devtools.lsp4ij:0.20.1")
        // (instrumentationTools() removed — instrumentation is default-on and auto-resolved in newer plugin versions.)
    }
}

// Build with the JDK 21 you have installed (Temurin). Do NOT use 17 (absent) or
// the phantom JDK 25 that broke the Kotlin compiler.
kotlin { jvmToolchain(21) }

intellijPlatform {
    pluginConfiguration {
        ideaVersion {
            // Built against 2025.2 with Kotlin 2.2.0 (required to read 2025.2's jars),
            // whose output isn't reliably compatible with 2024.x's Kotlin 2.0 runtime.
            sinceBuild = "252"
            untilBuild = provider { null }
        }
    }
}

// Optional settings-search pre-index; it launches a headless IDE and is flaky. The
// IDE builds the index on demand, so skip it.
tasks.buildSearchableOptions { enabled = false }
