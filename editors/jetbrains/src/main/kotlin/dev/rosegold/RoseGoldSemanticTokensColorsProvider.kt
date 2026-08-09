package dev.rosegold

import com.intellij.openapi.editor.colors.TextAttributesKey
import com.intellij.psi.PsiFile
import com.redhat.devtools.lsp4ij.features.semanticTokens.SemanticTokensColorsProvider
import dev.rosegold.RoseGoldSyntaxHighlighter.Companion as C

// Aligns the language server's semantic tokens with the lexer's coloring so names
// keep a consistent color when the server responds. The server tags declaration
// sites with the "declaration" modifier, so function/method names split into
// declaration vs call/use colors — and even a function passed as a value (which the
// lexer can't spot) is colored here. Type-like tokens map to the lexer's TYPE key.
class RoseGoldSemanticTokensColorsProvider : SemanticTokensColorsProvider {
    override fun getTextAttributesKey(
        tokenType: String,
        tokenModifiers: List<String>,
        file: PsiFile,
    ): TextAttributesKey? {
        val isDecl = tokenModifiers.contains("declaration")
        return when (tokenType) {
            "function", "method" -> if (isDecl) C.FUNCTION_DECL else C.FUNCTION_CALL
            "type", "class", "enum", "interface" -> C.TYPE
            else -> null   // variable / parameter / property: keep the default color
        }
    }
}
