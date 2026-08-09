package dev.rosegold

import com.intellij.openapi.editor.colors.TextAttributesKey
import com.intellij.psi.PsiFile
import com.redhat.devtools.lsp4ij.features.semanticTokens.SemanticTokensColorsProvider
import dev.rosegold.RoseGoldSyntaxHighlighter.Companion as C

// Maps the language server's semantic tokens to the SAME color keys as the
// highlighting lexer. Without this, LSP4IJ's default repaints a function/type name
// with its own (often no-op) color once the server responds, so the lexer's color
// briefly shows and then reverts. Here function/method/type tokens keep the lexer's
// colors, and everything else (variables, parameters, …) falls through to default.
class RoseGoldSemanticTokensColorsProvider : SemanticTokensColorsProvider {
    override fun getTextAttributesKey(
        tokenType: String,
        tokenModifiers: List<String>,
        file: PsiFile,
    ): TextAttributesKey? = when (tokenType) {
        "function", "method" -> C.FUNCTION
        "type", "class", "enum", "interface" -> C.TYPE
        else -> null   // variable / parameter / property: keep the default color
    }
}
