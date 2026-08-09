package dev.rosegold

import com.intellij.lexer.Lexer
import com.intellij.openapi.editor.DefaultLanguageHighlighterColors as D
import com.intellij.openapi.editor.colors.TextAttributesKey
import com.intellij.openapi.fileTypes.SyntaxHighlighter
import com.intellij.openapi.fileTypes.SyntaxHighlighterBase
import com.intellij.openapi.fileTypes.SyntaxHighlighterFactory
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.psi.tree.IElementType

class RoseGoldSyntaxHighlighter : SyntaxHighlighterBase() {
    override fun getHighlightingLexer(): Lexer = RoseGoldLexer()

    override fun getTokenHighlights(tokenType: IElementType): Array<TextAttributesKey> = when (tokenType) {
        RoseGoldTokens.KEYWORD -> pack(KEYWORD)
        RoseGoldTokens.FUNCTION -> pack(FUNCTION)
        RoseGoldTokens.TYPE -> pack(TYPE)
        RoseGoldTokens.NUMBER -> pack(NUMBER)
        RoseGoldTokens.STRING -> pack(STRING)
        RoseGoldTokens.COMMENT -> pack(COMMENT)
        RoseGoldTokens.OPERATOR -> pack(OP)
        RoseGoldTokens.LPAREN, RoseGoldTokens.RPAREN -> pack(PARENS)
        RoseGoldTokens.LBRACKET, RoseGoldTokens.RBRACKET -> pack(BRACKETS)
        else -> emptyArray()
    }

    companion object {
        private fun key(name: String, base: TextAttributesKey) =
            TextAttributesKey.createTextAttributesKey(name, base)

        val KEYWORD = key("ROSEGOLD_KEYWORD", D.KEYWORD)
        val FUNCTION = key("ROSEGOLD_FUNCTION", D.FUNCTION_DECLARATION)
        val TYPE = key("ROSEGOLD_TYPE", D.CLASS_NAME)
        val NUMBER = key("ROSEGOLD_NUMBER", D.NUMBER)
        val STRING = key("ROSEGOLD_STRING", D.STRING)
        val COMMENT = key("ROSEGOLD_COMMENT", D.LINE_COMMENT)
        val OP = key("ROSEGOLD_OPERATOR", D.OPERATION_SIGN)
        val PARENS = key("ROSEGOLD_PARENS", D.PARENTHESES)
        val BRACKETS = key("ROSEGOLD_BRACKETS", D.BRACKETS)
    }
}

class RoseGoldSyntaxHighlighterFactory : SyntaxHighlighterFactory() {
    override fun getSyntaxHighlighter(project: Project?, file: VirtualFile?): SyntaxHighlighter =
        RoseGoldSyntaxHighlighter()
}
