module bootstrap

# ---------------------------------------------------------------------
#  A RoseGold LEXER -- written in RoseGold, run by rosegoldc.
#
#  A self-hosting fragment: a real piece of RoseGold's own compiler
#  front-end, implemented in the language itself. It scans an embedded
#  source string (RoseGold has no file I/O yet) and prints the token
#  stream -- the same job the C++/Python lexers do.
#
#  Uses: classes with mutable fields (self.pos advances as we scan),
#  string indexing + comparison, recursion-free while loops, match-free
#  classification. Full self-hosting additionally needs file I/O and
#  growable collections, which the runtime doesn't expose yet.
# ---------------------------------------------------------------------

class Lexer:
    var src: String
    var pos: Int
    var n: Int

    init(source: String):
        self.src = source
        self.pos = 0
        self.n = len(source)

    func atEnd(self) -> Bool:
        return self.pos >= self.n

    func cur(self) -> String:
        if self.pos < self.n:
            return self.src[self.pos]
        return ""

    func isDigit(self, c: String) -> Bool:
        return c >= "0" && c <= "9"

    func isAlpha(self, c: String) -> Bool:
        return (c >= "a" && c <= "z") || (c >= "A" && c <= "Z") || c == "_"

    func isKeyword(self, w: String) -> Bool:
        return w == "func" || w == "var" || w == "const" || w == "class" || w == "enum" || w == "if" || w == "elif" || w == "else" || w == "while" || w == "for" || w == "match" || w == "return" || w == "true" || w == "false"

    # scan a run of characters: digits (kind 0) or identifier chars (kind 1)
    func scan(self, digits: Bool) -> String:
        var s = ""
        while !self.atEnd():
            var c = self.cur()
            var ok = false
            if digits:
                ok = self.isDigit(c)
            else:
                ok = self.isAlpha(c) || self.isDigit(c)
            if !ok:
                return s
            s = s + c
            self.pos = self.pos + 1
        return s

    func scanString(self) -> String:
        self.pos = self.pos + 1
        var s = ""
        while !self.atEnd() && self.cur() != "\"":
            s = s + self.cur()
            self.pos = self.pos + 1
        self.pos = self.pos + 1
        return s

    func run(self):
        while !self.atEnd():
            var c = self.cur()
            if c == " " || c == "\n" || c == "\t":
                self.pos = self.pos + 1
            elif self.isDigit(c):
                print("INT     ", self.scan(true))
            elif self.isAlpha(c):
                var w = self.scan(false)
                if self.isKeyword(w):
                    print("KEYWORD ", w)
                else:
                    print("IDENT   ", w)
            elif c == "\"":
                print("STRING  ", self.scanString())
            else:
                print("OP      ", c)
                self.pos = self.pos + 1

func main():
    var source = "func f(x): var s = \"hi\" return x + 42"
    print("source:", source)
    print("-----")
    var lx = Lexer(source)
    lx.run()
