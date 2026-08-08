# RoseGold — JetBrains plugin

Language support for [RoseGold](../../README.md) `.rg` files in IntelliJ IDEA and
other JetBrains IDEs. The counterpart of the [VS Code extension](../vscode) — and,
like it, powered by the native `rosegoldc --lsp` language server.

## What it provides
- **`.rg` file type** — the IDE recognizes RoseGold source files.
- **Syntax highlighting** — a compact `LexerBase` (`RoseGoldLexer.kt`) mapped to
  the IDE's standard color keys (keywords, types, strings, comments, numbers, ops).
- **Full language features via LSP** — the plugin launches `rosegoldc --lsp` and
  connects to it through **LSP4IJ**, so IDEA gets everything the VS Code extension
  does: diagnostics, hover, go-to-definition, completion, find-references, rename,
  document symbols, signature help, semantic highlighting, folding, and inlay hints.

## Requirements
- **IntelliJ IDEA (or any JetBrains IDE) 2025.2+**, Community or Ultimate.
- The **LSP4IJ** plugin. It is declared as a dependency, so on install the IDE
  will offer to install it from the Marketplace (or add it via **Settings →
  Plugins → Marketplace → "LSP4IJ"**). No Ultimate edition required.
- The **`rosegoldc`** binary, built from this repo:
  ```bash
  clang++ -std=c++17 -O2 -o cpp/rosegoldc cpp/src/main.cpp
  ```
  The plugin locates it via the `ROSEGOLDC` env var, else `cpp/rosegoldc` /
  `rosegoldc` under the project root, else your `PATH`.

## Build & run
Standard IntelliJ Platform (Gradle) plugin; building downloads the IntelliJ SDK
and LSP4IJ. Toolchain (pinned & verified building):

- **Gradle 8.13** (wrapper) · **Kotlin 2.2.0** · **IntelliJ Platform Gradle Plugin 2.10.0** · **LSP4IJ 0.20.1**
- **JDK 21** — builds against IDEA **2025.2** (which runs on JBR 21). `gradle.properties`
  pins `org.gradle.java.home` to a real JDK 21 so the Kotlin compiler never runs
  on a newer JDK it can't parse. Kotlin is **2.2.0** to match 2025.2's jars (which
  carry 2.2.0 metadata — an older Kotlin can't read them).

```bash
cd editors/jetbrains
./gradlew runIde        # launch a sandbox IDE with the plugin loaded
./gradlew buildPlugin   # produce build/distributions/rosegold-jetbrains-0.4.0.zip
```
Install the built zip via **Settings → Plugins → ⚙ → Install Plugin from Disk…**
(install LSP4IJ first, or accept the prompt).

> **`JAVA_HOME` gotcha:** the Kotlin compiler ICEs if it runs on a JDK it's too old
> to parse (e.g. JDK 25). The `org.gradle.java.home` line in `gradle.properties`
> pins the build JVM to Temurin 21 regardless; edit it if your JDK 21 lives
> elsewhere (`/usr/libexec/java_home -v 21`).

## How it works
`rosegoldc --lsp` is a JSON-RPC-over-stdio language server built into the compiler
(see [`cpp/src/lsp.hpp`](../../cpp/src/lsp.hpp)). LSP4IJ starts it as a child
process and speaks LSP to it — the exact same server, and protocol, the VS Code
extension uses. To watch the traffic, open **LSP Consoles** (LSP4IJ adds a tool
window) and enable tracing for the "RoseGold Language Server".

## Layout
```
build.gradle.kts · settings.gradle.kts · gradle.properties
src/main/resources/META-INF/plugin.xml   # FileType + highlighter + LSP4IJ <server>/<languageMapping>
src/main/kotlin/dev/rosegold/
  RoseGoldLanguage.kt          # Language, FileType, token types
  RoseGoldLexer.kt             # LexerBase for highlighting
  RoseGoldSyntaxHighlighter.kt
  RoseGoldLspFactory.kt        # LSP4IJ LanguageServerFactory -> `rosegoldc --lsp`
```
