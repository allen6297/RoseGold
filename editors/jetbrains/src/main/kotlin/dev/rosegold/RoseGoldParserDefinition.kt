package dev.rosegold

import com.intellij.extapi.psi.ASTWrapperPsiElement
import com.intellij.extapi.psi.PsiFileBase
import com.intellij.lang.ASTNode
import com.intellij.lang.ParserDefinition
import com.intellij.lang.PsiParser
import com.intellij.lexer.Lexer
import com.intellij.openapi.project.Project
import com.intellij.psi.FileViewProvider
import com.intellij.psi.PsiElement
import com.intellij.psi.PsiFile
import com.intellij.psi.tree.IFileElementType
import com.intellij.psi.tree.TokenSet

// A minimal PSI so each lexer token is its own leaf element. Without this the
// whole file is a single PSI element, which makes Cmd-hover / go-to-declaration
// underline the entire file instead of the identifier under the cursor. All
// actual language intelligence still comes from the LSP server.
val ROSEGOLD_FILE = IFileElementType(RoseGoldLanguage)

class RoseGoldParserDefinition : ParserDefinition {
    override fun createLexer(project: Project?): Lexer = RoseGoldLexer()
    override fun getFileNodeType(): IFileElementType = ROSEGOLD_FILE
    override fun getCommentTokens(): TokenSet = TokenSet.create(RoseGoldTokens.COMMENT)
    override fun getStringLiteralElements(): TokenSet = TokenSet.create(RoseGoldTokens.STRING)
    override fun createElement(node: ASTNode): PsiElement = ASTWrapperPsiElement(node)
    override fun createFile(viewProvider: FileViewProvider): PsiFile = RoseGoldPsiFile(viewProvider)

    // Flat parse: advance over every token under the file root; IntelliJ makes each token a leaf.
    override fun createParser(project: Project?): PsiParser = PsiParser { root, builder ->
        val mark = builder.mark()
        while (!builder.eof()) builder.advanceLexer()
        mark.done(root)
        builder.treeBuilt
    }
}

class RoseGoldPsiFile(viewProvider: FileViewProvider) : PsiFileBase(viewProvider, RoseGoldLanguage) {
    override fun getFileType() = RoseGoldFileType
}
