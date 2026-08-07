package dev.rosegold

import com.intellij.lang.annotation.AnnotationHolder
import com.intellij.lang.annotation.ExternalAnnotator
import com.intellij.lang.annotation.HighlightSeverity
import com.intellij.openapi.util.TextRange
import com.intellij.psi.PsiFile
import java.io.File

data class RGInput(val path: String)
data class RGError(val line: Int, val message: String)

// Live diagnostics: run `rosegoldc --check <file>` off the EDT and turn its
// `module:line: message` output into inline error annotations.
class RoseGoldAnnotator : ExternalAnnotator<RGInput, List<RGError>>() {

    override fun collectInformation(file: PsiFile): RGInput? {
        val path = file.virtualFile?.path ?: return null
        return RGInput(path)
    }

    override fun doAnnotate(info: RGInput): List<RGError> {
        val compiler = findCompiler(info.path) ?: return emptyList()
        return try {
            val proc = ProcessBuilder(compiler, "--check", info.path)
                .redirectErrorStream(true)
                .start()
            val out = proc.inputStream.bufferedReader().readText()
            proc.waitFor()
            val re = Regex("""^\s+[A-Za-z0-9_.]+(?::(\d+))?:\s+(.*)$""")
            out.lines().mapNotNull { line ->
                val m = re.find(line) ?: return@mapNotNull null
                RGError(m.groupValues[1].toIntOrNull() ?: 1, m.groupValues[2])
            }
        } catch (e: Exception) {
            emptyList()
        }
    }

    override fun apply(file: PsiFile, results: List<RGError>, holder: AnnotationHolder) {
        val doc = file.viewProvider.document ?: return
        if (doc.lineCount == 0) return
        for (e in results) {
            val idx = (e.line - 1).coerceIn(0, doc.lineCount - 1)
            val range = TextRange(doc.getLineStartOffset(idx), doc.getLineEndOffset(idx))
            holder.newAnnotation(HighlightSeverity.ERROR, e.message).range(range).create()
        }
    }

    private fun findCompiler(filePath: String): String? {
        var dir: File? = File(filePath).parentFile
        repeat(8) {
            val d = dir ?: return null
            for (rel in listOf("cpp/rosegoldc", "rosegoldc")) {
                val cand = File(d, rel)
                if (cand.exists()) return cand.absolutePath
            }
            dir = d.parentFile
        }
        return null
    }
}
