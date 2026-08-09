package dev.rosegold

import com.intellij.lexer.LexerBase
import com.intellij.psi.TokenType
import com.intellij.psi.tree.IElementType

// A compact hand-written lexer for highlighting (mirrors the compiler's lexer,
// minus the offside rule -- IntelliJ handles layout itself for coloring).
class RoseGoldLexer : LexerBase() {
    private var buf: CharSequence = ""
    private var bufEnd = 0
    private var tokStart = 0
    private var tokEnd = 0
    private var tok: IElementType? = null

    private val keywords = setOf(
        // TOP LEVEL
        "module", "import",

        //DECLARATION DECORATORS

        // DECLARATION LEVEL
        "var", "const", "signal", "enum",

        //METHOD DECORATORS

        // METHOD PREFIX
        "pub", "internal", "private", "static",

        // METHOD LEVEL
        "fn", "class", "struct", "trait", "extern",

        // METHOD BODY
        "init",

        // METHOD LOGIC
        "while", "for", "in", "match",
        "if", "elif", "else",
        "break", "continue", "try", "catch", "raise", "yield",
        "return", "pass",

        // METHOD SUFFIX
        "uses", "extend", "extends",

        // HELPERS
        "as", "true", "false"
    )

    override fun start(buffer: CharSequence, startOffset: Int, endOffset: Int, initialState: Int) {
        buf = buffer; bufEnd = endOffset; tokStart = startOffset; scan()
    }

    override fun getState() = 0
    override fun getTokenType(): IElementType? = tok
    override fun getTokenStart() = tokStart
    override fun getTokenEnd() = tokEnd
    override fun getBufferSequence() = buf
    override fun getBufferEnd() = bufEnd
    override fun advance() {
        tokStart = tokEnd; scan()
    }

    // True if the just-lexed identifier is immediately followed (past inline spaces)
    // by '(' -- a function call or definition. Newlines aren't skipped, so a name at
    // end of line isn't miscolored.
    private fun followedByCall(from: Int): Boolean {
        var j = from
        while (j < bufEnd && (buf[j] == ' ' || buf[j] == '\t')) j++
        return j < bufEnd && buf[j] == '('
    }

    private fun scan() {
        if (tokStart >= bufEnd) {
            tok = null; tokEnd = tokStart; return
        }
        val c = buf[tokStart]
        var i = tokStart
        when {
            c == ' ' || c == '\t' || c == '\r' || c == '\n' -> {
                while (i < bufEnd && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' || buf[i] == '\n')) i++
                tok = TokenType.WHITE_SPACE
            }

            c == '#' && i + 1 < bufEnd && buf[i + 1] == '/' -> {
                i += 2
                while (i + 1 < bufEnd && !(buf[i] == '/' && buf[i + 1] == '#')) i++
                i = if (i + 1 < bufEnd) i + 2 else bufEnd
                tok = RoseGoldTokens.COMMENT
            }

            c == '#' -> {
                while (i < bufEnd && buf[i] != '\n') i++; tok = RoseGoldTokens.COMMENT
            }

            c == '"' -> {
                i++
                while (i < bufEnd && buf[i] != '"') {
                    if (buf[i] == '\\' && i + 1 < bufEnd) i += 2 else i++
                }
                if (i < bufEnd) i++
                tok = RoseGoldTokens.STRING
            }

            c.isDigit() -> {
                while (i < bufEnd && (buf[i].isDigit() || buf[i] == '.')) i++; tok = RoseGoldTokens.NUMBER
            }

            c.isLetter() || c == '_' -> {
                while (i < bufEnd && (buf[i].isLetterOrDigit() || buf[i] == '_')) i++
                val w = buf.subSequence(tokStart, i).toString()
                tok = when {
                    keywords.contains(w) -> RoseGoldTokens.KEYWORD
                    w[0].isUpperCase() -> RoseGoldTokens.TYPE   // types / enum variants / constructors
                    followedByCall(i) -> RoseGoldTokens.FUNCTION  // `name(` -> a call or fn definition
                    else -> RoseGoldTokens.IDENT
                }
            }

            c == '(' -> { i++; tok = RoseGoldTokens.LPAREN }
            c == ')' -> { i++; tok = RoseGoldTokens.RPAREN }
            c == '[' -> { i++; tok = RoseGoldTokens.LBRACKET }
            c == ']' -> { i++; tok = RoseGoldTokens.RBRACKET }

            else -> {
                i++; tok = RoseGoldTokens.OPERATOR
            }
        }
        tokEnd = i
    }
}
