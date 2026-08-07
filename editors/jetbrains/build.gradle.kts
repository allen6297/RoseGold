plugins {
    id("java")
    id("org.jetbrains.kotlin.jvm") version "2.0.21"
    id("org.jetbrains.intellij.platform") version "2.1.0"
}

group = "dev.rosegold"
version = "0.3.0"

repositories {
    mavenCentral()
    intellijPlatform { defaultRepositories() }   // includes the JetBrains Marketplace (for LSP4IJ)
}

dependencies {
    intellijPlatform {
        // IDEA 2024.2 runs on JBR 21, matching the jvmToolchain below.
        intellijIdeaCommunity("2024.2")
        // LSP4IJ: the open-source library that runs a language server inside JetBrains IDEs.
        plugin("com.redhat.devtools.lsp4ij:0.14.0")
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
