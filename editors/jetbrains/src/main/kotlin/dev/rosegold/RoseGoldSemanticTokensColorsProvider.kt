package dev.rosegold

import com.intellij.openapi.editor.colors.TextAttributesKey
import com.intellij.psi.PsiFile
import com.redhat.devtools.lsp4ij.features.semanticTokens.SemanticTokensColorsProvider
import dev.rosegold.RoseGoldSyntaxHighlighter.Companion as C

// Aligns the language server's semantic tokens with the lexer's coloring so names
// don't revert to a default color when the server responds. Type-like tokens map to
// the lexer's TYPE key. `function`/`method` are intentionally left to the lexer,
// which distinguishes declarations (`fn name`) from calls (`name(`) — a distinction
// the server can't express (its legend advertises no token modifiers).
class RoseGoldSemanticTokensColorsProvider : SemanticTokensColorsProvider {
    override fun getTextAttributesKey(
        tokenType: String,
        tokenModifiers: List<String>,
        file: PsiFile,
    ): TextAttributesKey? = when (tokenType) {
        "type", "class", "enum", "interface" -> C.TYPE
        else -> null   // function/method (lexer owns decl-vs-call), variable, parameter, …
    }
}
