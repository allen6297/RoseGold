module bootstrap2

# ---------------------------------------------------------------------
#  A RoseGold tokenizer that READS A REAL SOURCE FILE and builds a
#  token LIST -- now possible thanks to the stdlib (readFile + push).
#
#  This is the stronger self-hosting fragment: source-on-disk in, a
#  growable collection of tokens out -- the two capabilities that were
#  blocking real compiler work. Written in RoseGold, run by rosegoldc.
# ---------------------------------------------------------------------

class Lexer:
    var src: String
    var pos: Int
    var n: Int
    var toks: List<String>

    init(source: String):
        self.src = source
        self.pos = 0
        self.n = len(source)
        self.toks = []

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
        return w == "func" || w == "var" || w == "const" || w == "class" || w == "enum" || w == "if" || w == "elif" || w == "else" || w == "while" || w == "for" || w == "match" || w == "return" || w == "module" || w == "true" || w == "false"

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

    func run(self) -> List<String>:
        while !self.atEnd():
            var c = self.cur()
            if c == " " || c == "\n" || c == "\t":
                self.pos = self.pos + 1
            elif self.isDigit(c):
                push(self.toks, "INT:" + self.scan(true))
            elif self.isAlpha(c):
                var w = self.scan(false)
                if self.isKeyword(w):
                    push(self.toks, "KW:" + w)
                else:
                    push(self.toks, "ID:" + w)
            elif c == "\"":
                self.pos = self.pos + 1
                var s = ""
                while !self.atEnd() && self.cur() != "\"":
                    s = s + self.cur()
                    self.pos = self.pos + 1
                self.pos = self.pos + 1
                push(self.toks, "STR:" + s)
            else:
                push(self.toks, "OP:" + c)
                self.pos = self.pos + 1
        return self.toks

func main():
    var src = readFile("examples/fib.rg")
    var lx = Lexer(src)
    var toks = lx.run()
    print("tokenized examples/fib.rg ->", len(toks), "tokens")
    var i = 0
    while i < len(toks):
        print(toks[i])
        i = i + 1
