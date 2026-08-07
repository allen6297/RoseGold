# RoseGold — JetBrains plugin

Language support for [RoseGold](../../README.md) `.rg` files in IntelliJ IDEA and
other JetBrains IDEs. The counterpart of the [VS Code extension](../vscode).

## What it provides
- **`.rg` file type** — the IDE recognizes RoseGold source files.
- **Syntax highlighting** — keywords, types (Capitalized identifiers), strings,
  `#` / `#/ … /#` comments, numbers, operators (a compact `LexerBase` in
  `RoseGoldLexer.kt` mapped to the IDE's standard color keys).
- **Live diagnostics** — an `ExternalAnnotator` runs `rosegoldc --check` off the
  UI thread and turns its `module:line: message` output into inline error
  annotations — the same native front-end gate the VS Code extension uses.

## Build & run
This is a standard IntelliJ Platform (Gradle) plugin. Building downloads the
IntelliJ SDK via Gradle. Toolchain (pinned & verified):

- **Gradle 8.13** (wrapper) · **Kotlin 2.0.21** · **IntelliJ Platform Gradle Plugin 2.1.0**
- **JDK 21** — targets IDEA **2024.2** (which runs on JBR 21). `build.gradle.kts`
  uses `jvmToolchain(21)`, and `gradle.properties` pins `org.gradle.java.home` to
  a real JDK 21 so the Kotlin compiler never runs on a newer JDK it can't parse.

```bash
cd editors/jetbrains
./gradlew runIde        # launch a sandbox IDE with the plugin loaded
./gradlew buildPlugin   # produce build/distributions/rosegold-jetbrains-0.1.0.zip
```
Install the built zip via **Settings → Plugins → ⚙ → Install Plugin from Disk…**.

> **`JAVA_HOME` gotcha:** the Kotlin compiler ICEs if it runs on a JDK it's too
> old to parse (e.g. JDK 25). Make sure `JAVA_HOME` points at a JDK 17–21 in the
> terminal you build from (`echo $JAVA_HOME`). The `org.gradle.java.home` line in
> `gradle.properties` pins the build JVM to Temurin 21 regardless; edit it if your
> JDK 21 lives elsewhere (`/usr/libexec/java_home -v 21`).

## Diagnostics setup
The annotator locates the compiler by walking up from the file's directory,
looking for `cpp/rosegoldc` or `rosegoldc`. Build it first:

```bash
clang++ -std=c++17 -O2 -o cpp/rosegoldc cpp/src/main.cpp
```

## Quick alternative (no build): TextMate
JetBrains IDEs natively read TextMate bundles and VS Code extension folders. For
highlighting without building this plugin, point **Settings → Editor → TextMate
Bundles → +** at the `editors/vscode/` folder (it reuses the same grammar and
language configuration). That gives colors; the Gradle plugin above adds the
native `rosegoldc --check` diagnostics on top.

## Layout
```
build.gradle.kts · settings.gradle.kts · gradle.properties
src/main/resources/META-INF/plugin.xml
src/main/kotlin/dev/rosegold/
  RoseGoldLanguage.kt        # Language, FileType, token types
  RoseGoldLexer.kt           # LexerBase for highlighting
  RoseGoldSyntaxHighlighter.kt
  RoseGoldAnnotator.kt       # rosegoldc --check -> inline diagnostics
```
