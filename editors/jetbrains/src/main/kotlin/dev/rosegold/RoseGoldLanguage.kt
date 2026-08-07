package dev.rosegold

import com.intellij.lang.Language
import com.intellij.openapi.fileTypes.LanguageFileType
import com.intellij.psi.tree.IElementType
import javax.swing.Icon

object RoseGoldLanguage : Language("RoseGold") {
    private fun readResolve(): Any = RoseGoldLanguage
}

object RoseGoldFileType : LanguageFileType(RoseGoldLanguage) {
    override fun getName() = "RoseGold file"
    override fun getDescription() = "RoseGold source file"
    override fun getDefaultExtension() = "rg"
    override fun getIcon(): Icon? = null
}

class RoseGoldTokenType(debug: String) : IElementType(debug, RoseGoldLanguage)

object RoseGoldTokens {
    val KEYWORD = RoseGoldTokenType("ROSEGOLD_KEYWORD")
    val IDENT = RoseGoldTokenType("ROSEGOLD_IDENT")
    val TYPE = RoseGoldTokenType("ROSEGOLD_TYPE")
    val NUMBER = RoseGoldTokenType("ROSEGOLD_NUMBER")
    val STRING = RoseGoldTokenType("ROSEGOLD_STRING")
    val COMMENT = RoseGoldTokenType("ROSEGOLD_COMMENT")
    val OP = RoseGoldTokenType("ROSEGOLD_OP")
}
