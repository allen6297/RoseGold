module rgparser

#/
  A RoseGold PARSER written in RoseGold — self-hosting, rung 2 (rung 1 is
  rglexer.rg). It lexes a file with the offside rule (ported from rglexer) into a
  token list, then recursive-descent parses a core subset — module + globals +
  functions; statements var/assign/return/if-elif-else/while/for/expr/pass/
  break/continue; expressions across the full precedence ladder with calls,
  indexing, member access, and list literals — emitting flat S-expressions
  IDENTICAL to `rosegoldc --ast`. The harness diffs the two, so this is the
  parser stage of RoseGold, self-hosted.
/#

const KEYWORDS = "module import as pub internal private static func var const return pass if elif else while for in break continue try catch raise yield class trait enum init match extends extend extern uses signal true false"
const OPCHARS = "()[]<>=!+-*/%,:."

class Tok:
    var kind: String
    var val: String
    init(k: String, v: String):
        self.kind = k
        self.val = v

# ------------------------------- Lexer (offside rule) -------------------------------
class Lexer:
    var src: String
    var pos: Int
    var n: Int
    var indents: List<Int>
    var paren: Int
    var lineStart: Bool
    var last: String
    var kw: List<String>
    var toks: List<Tok>

    init(source: String):
        self.src = ""
        self.pos = 0
        self.n = 0
        self.indents = [0]
        self.paren = 0
        self.lineStart = true
        self.last = ""
        self.kw = split(KEYWORDS, " ")
        self.toks = []
        self.src = self.strip(source)
        self.n = len(self.src)

    func cur(self) -> String:
        if self.pos < self.n:
            return self.src[self.pos]
        return ""
    func at(self, i: Int) -> String:
        if i < self.n:
            return self.src[i]
        return ""
    func isDigit(self, c: String) -> Bool:
        return c >= "0" && c <= "9"
    func isAlpha(self, c: String) -> Bool:
        return (c >= "a" && c <= "z") || (c >= "A" && c <= "Z") || c == "_"
    func isKeyword(self, w: String) -> Bool:
        var i = 0
        while i < len(self.kw):
            if self.kw[i] == w:
                return true
            i = i + 1
        return false
    func isTwoOp(self, s: String) -> Bool:
        return s == "->" || s == "=>" || s == "==" || s == "!=" || s == "<=" || s == ">=" || s == "&&" || s == "||"
    func top(self) -> Int:
        return self.indents[len(self.indents) - 1]

    func emit(self, kind: String):
        push(self.toks, Tok(kind, ""))
        self.last = kind
    func emitVal(self, kind: String, val: String):
        push(self.toks, Tok(kind, val))
        self.last = kind

    func strip(self, raw: String) -> String:
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

    func run(self):
        while self.pos < self.n:
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
        if self.last != "" && self.last != "NEWLINE" && self.last != "INDENT" && self.last != "DEDENT":
            self.emit("NEWLINE")
        while len(self.indents) > 1:
            pop(self.indents)
            self.emit("DEDENT")
        self.emit("END")

# ------------------------------- Parser (recursive descent) -------------------------------
class Parser:
    var toks: List<Tok>
    var pos: Int
    init(ts: List<Tok>):
        self.toks = ts
        self.pos = 0

    func kind(self) -> String:
        return self.toks[self.pos].kind
    func val(self) -> String:
        return self.toks[self.pos].val
    func isKind(self, k: String) -> Bool:
        return self.toks[self.pos].kind == k
    func isKw(self, w: String) -> Bool:
        return self.toks[self.pos].kind == "KW" && self.toks[self.pos].val == w
    func isOp(self, o: String) -> Bool:
        return self.toks[self.pos].kind == "OP" && self.toks[self.pos].val == o
    func next(self) -> Tok:
        var t = self.toks[self.pos]
        self.pos = self.pos + 1
        return t
    func eatOp(self, o: String):
        self.pos = self.pos + 1
    func skipNL(self):
        while self.isKind("NEWLINE"):
            self.pos = self.pos + 1
    func parseVis(self):
        while self.isKw("pub") || self.isKw("internal") || self.isKw("private") || self.isKw("static"):
            self.pos = self.pos + 1

    func dotted(self) -> String:
        var s = self.next().val
        while self.isOp(".") && self.toks[self.pos + 1].kind == "IDENT":
            self.pos = self.pos + 1
            s = s + "." + self.next().val
        return s

    func parseType(self) -> String:
        if self.isKw("func"):
            self.pos = self.pos + 1
            self.eatOp("(")
            var ps = ""
            if !self.isOp(")"):
                ps = self.parseType()
                while self.isOp(","):
                    self.pos = self.pos + 1
                    ps = ps + ", " + self.parseType()
            self.eatOp(")")
            self.eatOp("->")
            var r = self.parseType()
            return "func(" + ps + ") -> " + r
        var name = self.next().val
        if self.isOp("<"):
            self.pos = self.pos + 1
            var args = self.parseType()
            while self.isOp(","):
                self.pos = self.pos + 1
                args = args + ", " + self.parseType()
            self.eatOp(">")
            return name + "<" + args + ">"
        return name

    func oneParam(self) -> String:
        var name = self.next().val
        var ty = "Any"
        if self.isOp(":"):
            self.pos = self.pos + 1
            ty = self.parseType()
        return " (" + name + " " + ty + ")"

    func paramList(self) -> String:
        self.eatOp("(")
        var out = ""
        if !self.isOp(")"):
            out = out + self.oneParam()
            while self.isOp(","):
                self.pos = self.pos + 1
                out = out + self.oneParam()
        self.eatOp(")")
        return out

    # blocks: eat ':' NEWLINE INDENT stmts DEDENT ; returns " s1 s2 ..."
    func suite(self) -> String:
        self.eatOp(":")
        self.skipNL()
        if self.isKind("INDENT"):
            self.pos = self.pos + 1
        self.skipNL()
        var body = ""
        while !self.isKind("DEDENT") && !self.isKind("END"):
            body = body + " " + self.stmt()
            self.skipNL()
        if self.isKind("DEDENT"):
            self.pos = self.pos + 1
        return body

    # ---- expressions: precedence ladder mirroring the C++ parser ----
    func expr(self) -> String:
        return self.orE()
    func orE(self) -> String:
        var left = self.andE()
        while self.isOp("||"):
            var op = self.next().val
            left = "(binop " + op + " " + left + " " + self.andE() + ")"
        return left
    func andE(self) -> String:
        var left = self.eqE()
        while self.isOp("&&"):
            var op = self.next().val
            left = "(binop " + op + " " + left + " " + self.eqE() + ")"
        return left
    func eqE(self) -> String:
        var left = self.cmpE()
        while self.isOp("==") || self.isOp("!="):
            var op = self.next().val
            left = "(binop " + op + " " + left + " " + self.cmpE() + ")"
        return left
    func cmpE(self) -> String:
        var left = self.addE()
        while self.isOp("<") || self.isOp("<=") || self.isOp(">") || self.isOp(">="):
            var op = self.next().val
            left = "(binop " + op + " " + left + " " + self.addE() + ")"
        return left
    func addE(self) -> String:
        var left = self.mulE()
        while self.isOp("+") || self.isOp("-"):
            var op = self.next().val
            left = "(binop " + op + " " + left + " " + self.mulE() + ")"
        return left
    func mulE(self) -> String:
        var left = self.unaryE()
        while self.isOp("*") || self.isOp("/") || self.isOp("%"):
            var op = self.next().val
            left = "(binop " + op + " " + left + " " + self.unaryE() + ")"
        return left
    func unaryE(self) -> String:
        if self.isOp("!") || self.isOp("-"):
            var op = self.next().val
            return "(unary " + op + " " + self.unaryE() + ")"
        return self.postfixE()
    func postfixE(self) -> String:
        var e = self.primary()
        var go = true
        while go:
            if self.isOp("("):
                self.pos = self.pos + 1
                var args = ""
                if !self.isOp(")"):
                    args = args + " " + self.expr()
                    while self.isOp(","):
                        self.pos = self.pos + 1
                        args = args + " " + self.expr()
                self.eatOp(")")
                e = "(call " + e + args + ")"
            elif self.isOp("["):
                self.pos = self.pos + 1
                var idx = self.expr()
                self.eatOp("]")
                e = "(index " + e + " " + idx + ")"
            elif self.isOp("."):
                self.pos = self.pos + 1
                var field = self.next().val
                e = "(member " + e + " " + field + ")"
            else:
                go = false
        return e
    func primary(self) -> String:
        var k = self.kind()
        if k == "INT":
            return "(int " + self.next().val + ")"
        if k == "FLT":
            self.pos = self.pos + 1
            return "(flt)"
        if k == "STR":
            self.pos = self.pos + 1
            return "(str)"
        if self.isKw("true"):
            self.pos = self.pos + 1
            return "(bool true)"
        if self.isKw("false"):
            self.pos = self.pos + 1
            return "(bool false)"
        if k == "IDENT":
            return "(name " + self.next().val + ")"
        if self.isOp("("):
            self.pos = self.pos + 1
            var inner = self.expr()
            self.eatOp(")")
            return inner
        if self.isOp("["):
            self.pos = self.pos + 1
            var elems = ""
            if !self.isOp("]"):
                elems = elems + " " + self.expr()
                while self.isOp(","):
                    self.pos = self.pos + 1
                    if self.isOp("]"):
                        break
                    elems = elems + " " + self.expr()
            self.eatOp("]")
            return "(list" + elems + ")"
        return "(?)"

    # ---- statements ----
    func stmt(self) -> String:
        if self.isKw("if"):
            return self.ifStmt()
        if self.isKw("while"):
            return self.whileStmt()
        if self.isKw("for"):
            return self.forStmt()
        if self.isKw("var") || self.isKw("const"):
            return self.varStmt()
        if self.isKw("return"):
            return self.retStmt()
        if self.isKw("break"):
            self.pos = self.pos + 1
            return "(break)"
        if self.isKw("continue"):
            self.pos = self.pos + 1
            return "(continue)"
        if self.isKw("pass"):
            self.pos = self.pos + 1
            return "(pass)"
        var e = self.expr()
        if self.isOp("="):
            self.pos = self.pos + 1
            return "(assign " + e + " " + self.expr() + ")"
        return "(expr " + e + ")"
    func varStmt(self) -> String:
        self.pos = self.pos + 1
        var name = self.next().val
        if self.isOp(":"):
            self.pos = self.pos + 1
            self.parseType()
        if self.isOp("="):
            self.pos = self.pos + 1
            return "(var " + name + " " + self.expr() + ")"
        return "(var " + name + ")"
    func retStmt(self) -> String:
        self.pos = self.pos + 1
        if self.isKind("NEWLINE") || self.isKind("DEDENT") || self.isKind("END"):
            return "(return)"
        return "(return " + self.expr() + ")"
    func ifStmt(self) -> String:
        self.pos = self.pos + 1
        var cond = self.expr()
        var s = "(if " + cond + " (then" + self.suite() + ")"
        while self.isKw("elif"):
            self.pos = self.pos + 1
            var ec = self.expr()
            s = s + " (elif " + ec + self.suite() + ")"
        if self.isKw("else"):
            self.pos = self.pos + 1
            s = s + " (else" + self.suite() + ")"
        return s + ")"
    func whileStmt(self) -> String:
        self.pos = self.pos + 1
        var cond = self.expr()
        return "(while " + cond + self.suite() + ")"
    func forStmt(self) -> String:
        self.pos = self.pos + 1
        var name = self.next().val
        self.pos = self.pos + 1
        var it = self.expr()
        return "(for " + name + " " + it + self.suite() + ")"
    func funcDecl(self) -> String:
        self.pos = self.pos + 1
        var name = self.next().val
        var params = self.paramList()
        var ret = "Void"
        if self.isOp("->"):
            self.pos = self.pos + 1
            ret = self.parseType()
        return "(func " + name + " (params" + params + ") " + ret + self.suite() + ")"

    func program(self) -> String:
        self.skipNL()
        var modName = "$entry"
        if self.isKw("module"):
            self.pos = self.pos + 1
            modName = self.dotted()
        var result = "(module " + modName + ")"
        var globals = ""
        var funcs = ""
        self.skipNL()
        while !self.isKind("END"):
            self.skipNL()
            if self.isKind("END"):
                return result + globals + funcs
            self.parseVis()
            if self.isKw("func"):
                funcs = funcs + "\n" + self.funcDecl()
            elif self.isKw("var") || self.isKw("const"):
                globals = globals + "\n" + self.varStmt()
            else:
                self.pos = self.pos + 1
            self.skipNL()
        return result + globals + funcs

func main():
    var src = readFile("examples/selfhost/parse_sample.rg")
    var lx = Lexer(src)
    lx.run()
    var ps = Parser(lx.toks)
    print(ps.program())
