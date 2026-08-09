package dev.rosegold

import com.intellij.openapi.editor.colors.TextAttributesKey
import com.intellij.openapi.fileTypes.SyntaxHighlighter
import com.intellij.openapi.options.colors.AttributesDescriptor
import com.intellij.openapi.options.colors.ColorDescriptor
import com.intellij.openapi.options.colors.ColorSettingsPage
import javax.swing.Icon
import dev.rosegold.RoseGoldSyntaxHighlighter.Companion as C

// Settings > Editor > Color Scheme > RoseGold: lets users recolor each token
// class and previews it on a sample program.
class RoseGoldColorSettingsPage : ColorSettingsPage {
    override fun getIcon(): Icon = RoseGoldIcons.FILE
    override fun getHighlighter(): SyntaxHighlighter = RoseGoldSyntaxHighlighter()
    override fun getDisplayName(): String = "RoseGold"
    override fun getAdditionalHighlightingTagToDescriptorMap(): Map<String, TextAttributesKey>? = null
    override fun getAttributeDescriptors(): Array<AttributesDescriptor> = DESCRIPTORS
    override fun getColorDescriptors(): Array<ColorDescriptor> = ColorDescriptor.EMPTY_ARRAY

    override fun getDemoText(): String = """
        module demo

        ## Doubles a number.
        extern "math":
            fn scale(x: Int, factor: Int) -> Int

        enum Shape:
            Circle(r: Float)
            Rect(w: Float, h: Float)

        class Counter:
            var count = 0
            fn bump(self, by: Int) -> Int:
                self.count = self.count + by
                return self.count

        fn main():
            var xs = [1, 2, 3]
            for x in xs:
                if x >= 2:
                    print("big", x)
    """.trimIndent()

    companion object {
        private val DESCRIPTORS = arrayOf(
            AttributesDescriptor("Keyword", C.KEYWORD),
            AttributesDescriptor("Control flow", C.CONTROL),
            AttributesDescriptor("Function declaration", C.FUNCTION_DECL),
            AttributesDescriptor("Function call", C.FUNCTION_CALL),
            AttributesDescriptor("Type", C.TYPE),
            AttributesDescriptor("Number", C.NUMBER),
            AttributesDescriptor("String", C.STRING),
            AttributesDescriptor("Comment", C.COMMENT),
            AttributesDescriptor("Operator", C.OP),
            AttributesDescriptor("Parentheses", C.PARENS),
            AttributesDescriptor("Brackets", C.BRACKETS),
        )
    }
}
