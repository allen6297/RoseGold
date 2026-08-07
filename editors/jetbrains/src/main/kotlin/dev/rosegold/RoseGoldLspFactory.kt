package dev.rosegold

import com.intellij.openapi.project.Project
import com.redhat.devtools.lsp4ij.LanguageServerFactory
import com.redhat.devtools.lsp4ij.server.ProcessStreamConnectionProvider
import com.redhat.devtools.lsp4ij.server.StreamConnectionProvider
import java.io.File

// Connects JetBrains IDEs to the native RoseGold language server (`rosegoldc --lsp`)
// through LSP4IJ. All features -- diagnostics, hover, go-to-definition, completion,
// references, rename, document symbols, signature help, semantic tokens, folding,
// inlay hints -- come straight from the same server the VS Code extension uses.
class RoseGoldLspFactory : LanguageServerFactory {
    override fun createConnectionProvider(project: Project): StreamConnectionProvider =
        RoseGoldLspServer(project)
}

class RoseGoldLspServer(project: Project) : ProcessStreamConnectionProvider() {
    init {
        setCommands(listOf(findRosegoldc(project), "--lsp"))
        project.basePath?.let { workingDirectory = it }
    }
}

// Locate the compiler binary: an explicit ROSEGOLDC env var, then cpp/rosegoldc or
// rosegoldc under the project root, else assume it is on PATH.
fun findRosegoldc(project: Project): String {
    System.getenv("ROSEGOLDC")?.let { if (File(it).canExecute()) return it }
    project.basePath?.let { base ->
        for (rel in listOf("cpp/rosegoldc", "rosegoldc")) {
            val f = File(base, rel)
            if (f.canExecute()) return f.absolutePath
        }
    }
    return "rosegoldc"
}
