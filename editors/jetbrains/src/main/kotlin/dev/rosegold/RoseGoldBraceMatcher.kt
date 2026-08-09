package dev.rosegold

import com.intellij.lang.BracePair
import com.intellij.lang.PairedBraceMatcher
import com.intellij.psi.PsiFile
import com.intellij.psi.tree.IElementType

// Highlights matching ( )/[ ] and auto-inserts the closing brace when typing.
class RoseGoldBraceMatcher : PairedBraceMatcher {
    private val pairs = arrayOf(
        BracePair(RoseGoldTokens.LPAREN, RoseGoldTokens.RPAREN, false),
        BracePair(RoseGoldTokens.LBRACKET, RoseGoldTokens.RBRACKET, false),
    )

    override fun getPairs(): Array<BracePair> = pairs
    override fun isPairedBracesAllowedBeforeType(lbrace: IElementType, next: IElementType?): Boolean = true
    override fun getCodeConstructStart(file: PsiFile?, openingBraceOffset: Int): Int = openingBraceOffset
}
