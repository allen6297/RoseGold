module rglexer

# ---------------------------------------------------------------------
#  A FAITHFUL RoseGold lexer, written in RoseGold.
#
#  Unlike bootstrap2.rg (which skips whitespace and only knows single-char
#  operators), this one is a real port of the C++ lexer: it strips line and
#  block comments, implements the OFFSIDE RULE (INDENT / DEDENT / NEWLINE via
#  an indentation stack, with `(` / `[` suppressing newlines), handles two-char
#  operators, string escapes, and floats, and recognizes the full keyword set.
#
#  Its output matches `rosegoldc --tokens` byte-for-byte (the test harness
#  diffs the two) — so this is the lexer stage of RoseGold, self-hosted.
# ---------------------------------------------------------------------

const KEYWORDS = "module import as pub internal private static fn var const return pass if elif else while for in break continue try catch raise yield class struct trait enum init match extends extend uses true false"
const OPCHARS = "()[]<>=!+-*/%,:."

class Lexer:
    var src: String
    var pos: Int
    var n: Int
    var indents: List<Int>
    var paren: Int
    var lineStart: Bool
    var last: String
    var kw: List<String>

    init(source: String):
        self.src = ""
        self.pos = 0
        self.n = 0
        self.indents = [0]
        self.paren = 0
        self.lineStart = true
        self.last = ""
        self.kw = split(KEYWORDS, " ")
        self.src = self.strip(source)
        self.n = len(self.src)

    fn cur(self) -> String:
        if self.pos < self.n:
            return self.src[self.pos]
        return ""

    fn at(self, i: Int) -> String:
        if i < self.n:
            return self.src[i]
        return ""

    fn isDigit(self, c: String) -> Bool:
        return c >= "0" && c <= "9"

    fn isAlpha(self, c: String) -> Bool:
        return (c >= "a" && c <= "z") || (c >= "A" && c <= "Z") || c == "_"

    fn isKeyword(self, w: String) -> Bool:
        var i = 0
        while i < len(self.kw):
            if self.kw[i] == w:
                return true
            i = i + 1
        return false

    fn isOpChar(self, c: String) -> Bool:
        var i = 0
        while i < len(OPCHARS):
            if OPCHARS[i] == c:
                return true
            i = i + 1
        return false

    fn isTwoOp(self, s: String) -> Bool:
        return s == "->" || s == "=>" || s == "==" || s == "!=" || s == "<=" || s == ">=" || s == "&&" || s == "||"

    fn top(self) -> Int:
        return self.indents[len(self.indents) - 1]

    fn emit(self, kind: String):
        print(kind)
        self.last = kind

    fn emitVal(self, kind: String, val: String):
        print(kind + " " + val)
        self.last = kind

    # Remove line (`# ...`) and block (`#/ ... /#`) comments, preserving newlines
    # and string contents, so indentation widths still line up. Mirrors the C++ stripComments.
    fn strip(self, raw: String) -> String:
        var out = ""
        var i = 0
        var m = len(raw)
        var inStr = false
        while i < m:
            var c = raw[i]
            if inStr:
                out = out + c
                if c == "\\" && i + 1 < m:
                    out = out + raw[i + 1]
                    i = i + 2
                    continue
                if c == "\"":
                    inStr = false
                i = i + 1
                continue
            if c == "\"":
                inStr = true
                out = out + c
                i = i + 1
                continue
            if c == "#" && i + 1 < m && raw[i + 1] == "/":
                out = out + "  "
                i = i + 2
                while i + 1 < m && !(raw[i] == "/" && raw[i + 1] == "#"):
                    if raw[i] == "\n":
                        out = out + "\n"
                    else:
                        out = out + " "
                    i = i + 1
                if i + 1 < m:
                    out = out + "  "
                    i = i + 2
                continue
            if c == "#":
                while i < m && raw[i] != "\n":
                    i = i + 1
                continue
            out = out + c
            i = i + 1
        return out

    fn run(self):
        while self.pos < self.n:
            # --- offside rule: indentation at the start of a logical line ---
            if self.lineStart && self.paren == 0:
                var width = 0
                while self.pos < self.n && (self.cur() == " " || self.cur() == "\t"):
                    if self.cur() == "\t":
                        width = width + 8
                    else:
                        width = width + 1
                    self.pos = self.pos + 1
                if self.pos >= self.n:
                    break
                if self.cur() == "\n":
                    self.pos = self.pos + 1
                    continue
                if width > self.top():
                    push(self.indents, width)
                    self.emit("INDENT")
                elif width < self.top():
                    while width < self.top():
                        pop(self.indents)
                        self.emit("DEDENT")
                self.lineStart = false
                continue

            var c = self.cur()
            if c == "\n":
                if self.paren == 0:
                    if self.last != "NEWLINE" && self.last != "INDENT" && self.last != "DEDENT":
                        self.emit("NEWLINE")
                    self.lineStart = true
                self.pos = self.pos + 1
            elif c == " " || c == "\t" || c == "\r":
                self.pos = self.pos + 1
            elif c == "\"":
                self.pos = self.pos + 1
                while self.pos < self.n && self.cur() != "\"":
                    if self.cur() == "\\" && self.pos + 1 < self.n:
                        self.pos = self.pos + 2
                    else:
                        self.pos = self.pos + 1
                self.pos = self.pos + 1
                self.emit("STR")
            elif self.isDigit(c):
                var num = ""
                while self.pos < self.n && self.isDigit(self.cur()):
                    num = num + self.cur()
                    self.pos = self.pos + 1
                if self.cur() == "." && self.isDigit(self.at(self.pos + 1)):
                    num = num + "."
                    self.pos = self.pos + 1
                    while self.pos < self.n && self.isDigit(self.cur()):
                        num = num + self.cur()
                        self.pos = self.pos + 1
                    self.emitVal("FLT", num)
                else:
                    self.emitVal("INT", num)
            elif self.isAlpha(c):
                var w = ""
                while self.pos < self.n && (self.isAlpha(self.cur()) || self.isDigit(self.cur())):
                    w = w + self.cur()
                    self.pos = self.pos + 1
                if self.isKeyword(w):
                    self.emitVal("KW", w)
                else:
                    self.emitVal("IDENT", w)
            else:
                var two = c + self.at(self.pos + 1)
                if self.isTwoOp(two):
                    self.emitVal("OP", two)
                    self.pos = self.pos + 2
                else:
                    self.emitVal("OP", c)
                    if c == "(" || c == "[":
                        self.paren = self.paren + 1
                    elif c == ")" || c == "]":
                        if self.paren > 0:
                            self.paren = self.paren - 1
                    self.pos = self.pos + 1

        # --- end of file: trailing NEWLINE, close open indents, END ---
        if self.last != "" && self.last != "NEWLINE" && self.last != "INDENT" && self.last != "DEDENT":
            self.emit("NEWLINE")
        while len(self.indents) > 1:
            pop(self.indents)
            self.emit("DEDENT")
        self.emit("END")

fn main():
    var src = readFile("examples/prog.rg")
    var lx = Lexer(src)
    lx.run()
