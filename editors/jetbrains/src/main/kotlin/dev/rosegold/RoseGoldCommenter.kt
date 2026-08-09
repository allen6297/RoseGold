package dev.rosegold

import com.intellij.lang.Commenter

// Ctrl+/ toggles `#` line comments; Ctrl+Shift+/ toggles `#/ ... /#` block comments.
class RoseGoldCommenter : Commenter {
    override fun getLineCommentPrefix() = "#"
    override fun getBlockCommentPrefix() = "#/"
    override fun getBlockCommentSuffix() = "/#"
    override fun getCommentedBlockCommentPrefix() = null
    override fun getCommentedBlockCommentSuffix() = null
}
