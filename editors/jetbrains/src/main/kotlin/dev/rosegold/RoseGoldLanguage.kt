package dev.rosegold

import com.intellij.lang.Language
import com.intellij.openapi.fileTypes.LanguageFileType
import com.intellij.openapi.util.IconLoader
import com.intellij.psi.tree.IElementType
import javax.swing.Icon

object RoseGoldLanguage : Language("RoseGold") {
    private fun readResolve(): Any = RoseGoldLanguage
}

object RoseGoldIcons {
    @JvmField val FILE: Icon = IconLoader.getIcon("/icons/rosegold.svg", RoseGoldIcons::class.java)
}

object RoseGoldFileType : LanguageFileType(RoseGoldLanguage) {
    override fun getName() = "RoseGold file"
    override fun getDescription() = "RoseGold source file"
    override fun getDefaultExtension() = "rg"
    override fun getIcon(): Icon = RoseGoldIcons.FILE
}

class RoseGoldTokenType(debug: String) : IElementType(debug, RoseGoldLanguage)

object RoseGoldTokens {
    val KEYWORD = RoseGoldTokenType("ROSEGOLD_KEYWORD")
    val IDENT = RoseGoldTokenType("ROSEGOLD_IDENTIFIER")
    val FUNCTION = RoseGoldTokenType("ROSEGOLD_FUNCTION")
    val TYPE = RoseGoldTokenType("ROSEGOLD_TYPE")
    val NUMBER = RoseGoldTokenType("ROSEGOLD_NUMBER")
    val STRING = RoseGoldTokenType("ROSEGOLD_STRING")
    val COMMENT = RoseGoldTokenType("ROSEGOLD_COMMENT")
    val OPERATOR = RoseGoldTokenType("ROSEGOLD_OPERATOR")
    val LPAREN = RoseGoldTokenType("ROSEGOLD_LPAREN")
    val RPAREN = RoseGoldTokenType("ROSEGOLD_RPAREN")
    val LBRACKET = RoseGoldTokenType("ROSEGOLD_LBRACKET")
    val RBRACKET = RoseGoldTokenType("ROSEGOLD_RBRACKET")
}
