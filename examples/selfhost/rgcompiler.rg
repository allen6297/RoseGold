module rgcompiler

#/
  A RoseGold BYTECODE COMPILER written in RoseGold — the last self-hosting rung
  (after lexer, parser, checker). It lexes + parses a functions-only subset into
  an AST, then compiles each function to stack-VM bytecode, replicating the C++
  compiler's codegen exactly: local slot assignment, a single monotonic constant
  pool, the operator/builtin/CALL emission, and jump patching for if/while. The
  per-function disassembly is byte-identical to `rosegoldc --bytecode`; the
  harness diffs the two on examples/compile_sample.rg. The compiler stage of
  RoseGold, self-hosted.
/#

const KEYWORDS = "module import as pub internal private static func var const return pass if elif else while for in break continue try catch raise yield class trait enum init match extends extend extern uses signal true false"
const BINAMES = "print len range push pop str ord chr substr split int readFile writeFile map set get has keys remove sqrt sin cos tan atan2 floor ceil round pow abs min max lerp clamp random randint srandom coroutine resume done vec2 vec3 dot vlen norm __emit"

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
                    self.pos = self.pos + 1
                    while self.pos < self.n && self.isDigit(self.cur()):
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

# ------------------------------- AST -------------------------------
class Node:
    var kind: String
    var sval: String
    var kids: List<Node>
    init(k: String):
        self.kind = k
        self.sval = ""
        self.kids = []

class FuncDef:
    var name: String
    var params: List<String>
    var body: Node
    init(nm: String):
        self.name = nm
        self.params = []
        self.body = Node("block")

# ------------------------------- Parser (builds AST) -------------------------------
class Parser:
    var toks: List<Tok>
    var pos: Int
    var funcs: List<FuncDef>
    init(ts: List<Tok>):
        self.toks = ts
        self.pos = 0
        self.funcs = []

    func kind(self) -> String:
        return self.toks[self.pos].kind
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

    func skipType(self):
        # consume a type (Name, Name<...>, or func(...) -> T) — the compiler ignores types
        if self.isKw("func"):
            self.pos = self.pos + 1
            self.eatOp("(")
            if !self.isOp(")"):
                self.skipType()
                while self.isOp(","):
                    self.pos = self.pos + 1
                    self.skipType()
            self.eatOp(")")
            self.eatOp("->")
            self.skipType()
            return
        self.pos = self.pos + 1        # Name
        if self.isOp("<"):
            self.pos = self.pos + 1
            self.skipType()
            while self.isOp(","):
                self.pos = self.pos + 1
                self.skipType()
            self.eatOp(">")

    # ---- expressions ----
    func expr(self) -> Node:
        return self.orE()
    func bin(self, left: Node, op: String, right: Node) -> Node:
        var nd = Node("binop")
        nd.sval = op
        push(nd.kids, left)
        push(nd.kids, right)
        return nd
    func orE(self) -> Node:
        var left = self.andE()
        while self.isOp("||"):
            var op = self.next().val
            left = self.bin(left, op, self.andE())
        return left
    func andE(self) -> Node:
        var left = self.eqE()
        while self.isOp("&&"):
            var op = self.next().val
            left = self.bin(left, op, self.eqE())
        return left
    func eqE(self) -> Node:
        var left = self.cmpE()
        while self.isOp("==") || self.isOp("!="):
            var op = self.next().val
            left = self.bin(left, op, self.cmpE())
        return left
    func cmpE(self) -> Node:
        var left = self.addE()
        while self.isOp("<") || self.isOp("<=") || self.isOp(">") || self.isOp(">="):
            var op = self.next().val
            left = self.bin(left, op, self.addE())
        return left
    func addE(self) -> Node:
        var left = self.mulE()
        while self.isOp("+") || self.isOp("-"):
            var op = self.next().val
            left = self.bin(left, op, self.mulE())
        return left
    func mulE(self) -> Node:
        var left = self.unaryE()
        while self.isOp("*") || self.isOp("/") || self.isOp("%"):
            var op = self.next().val
            left = self.bin(left, op, self.unaryE())
        return left
    func unaryE(self) -> Node:
        if self.isOp("!") || self.isOp("-"):
            var op = self.next().val
            var nd = Node("unary")
            nd.sval = op
            push(nd.kids, self.unaryE())
            return nd
        return self.postfixE()
    func postfixE(self) -> Node:
        var e = self.primary()
        var go = true
        while go:
            if self.isOp("("):
                self.pos = self.pos + 1
                var call = Node("call")
                push(call.kids, e)
                if !self.isOp(")"):
                    push(call.kids, self.expr())
                    while self.isOp(","):
                        self.pos = self.pos + 1
                        push(call.kids, self.expr())
                self.eatOp(")")
                e = call
            elif self.isOp("["):
                self.pos = self.pos + 1
                var idx = Node("index")
                push(idx.kids, e)
                push(idx.kids, self.expr())
                self.eatOp("]")
                e = idx
            elif self.isOp("."):
                self.pos = self.pos + 1
                var m = Node("member")
                m.sval = self.next().val
                push(m.kids, e)
                e = m
            else:
                go = false
        return e
    func primary(self) -> Node:
        var k = self.kind()
        if k == "INT":
            self.pos = self.pos + 1
            return Node("int")
        if k == "FLT":
            self.pos = self.pos + 1
            return Node("flt")
        if k == "STR":
            self.pos = self.pos + 1
            return Node("str")
        if self.isKw("true") || self.isKw("false"):
            self.pos = self.pos + 1
            return Node("bool")
        if k == "IDENT":
            var nd = Node("name")
            nd.sval = self.next().val
            return nd
        if self.isOp("("):
            self.pos = self.pos + 1
            var inner = self.expr()
            self.eatOp(")")
            return inner
        if self.isOp("["):
            self.pos = self.pos + 1
            var nd = Node("list")
            if !self.isOp("]"):
                push(nd.kids, self.expr())
                while self.isOp(","):
                    self.pos = self.pos + 1
                    if self.isOp("]"):
                        break
                    push(nd.kids, self.expr())
            self.eatOp("]")
            return nd
        return Node("bad")

    # ---- statements ----
    func suite(self) -> Node:
        self.eatOp(":")
        self.skipNL()
        if self.isKind("INDENT"):
            self.pos = self.pos + 1
        self.skipNL()
        var block = Node("block")
        while !self.isKind("DEDENT") && !self.isKind("END"):
            push(block.kids, self.stmt())
            self.skipNL()
        if self.isKind("DEDENT"):
            self.pos = self.pos + 1
        return block
    func stmt(self) -> Node:
        if self.isKw("if"):
            return self.ifStmt()
        if self.isKw("while"):
            self.pos = self.pos + 1
            var nd = Node("while")
            push(nd.kids, self.expr())
            push(nd.kids, self.suite())
            return nd
        if self.isKw("var") || self.isKw("const"):
            return self.varStmt()
        if self.isKw("return"):
            self.pos = self.pos + 1
            var nd = Node("return")
            if !self.isKind("NEWLINE") && !self.isKind("DEDENT") && !self.isKind("END"):
                push(nd.kids, self.expr())
            return nd
        if self.isKw("break"):
            self.pos = self.pos + 1
            return Node("break")
        if self.isKw("continue"):
            self.pos = self.pos + 1
            return Node("continue")
        if self.isKw("pass"):
            self.pos = self.pos + 1
            return Node("pass")
        var e = self.expr()
        if self.isOp("="):
            self.pos = self.pos + 1
            var nd = Node("assign")
            push(nd.kids, e)
            push(nd.kids, self.expr())
            return nd
        var ex = Node("expr")
        push(ex.kids, e)
        return ex
    func varStmt(self) -> Node:
        self.pos = self.pos + 1
        var nd = Node("var")
        nd.sval = self.next().val
        if self.isOp(":"):
            self.pos = self.pos + 1
            self.skipType()
        if self.isOp("="):
            self.pos = self.pos + 1
            push(nd.kids, self.expr())
        return nd
    func ifStmt(self) -> Node:
        self.pos = self.pos + 1
        var nd = Node("if")
        push(nd.kids, self.expr())
        push(nd.kids, self.suite())
        while self.isKw("elif"):
            self.pos = self.pos + 1
            push(nd.kids, self.expr())
            push(nd.kids, self.suite())
        if self.isKw("else"):
            self.pos = self.pos + 1
            push(nd.kids, self.suite())
        return nd

    func funcDecl(self):
        self.pos = self.pos + 1
        var f = FuncDef(self.next().val)
        self.eatOp("(")
        if !self.isOp(")"):
            push(f.params, self.next().val)
            if self.isOp(":"):
                self.pos = self.pos + 1
                self.skipType()
            while self.isOp(","):
                self.pos = self.pos + 1
                push(f.params, self.next().val)
                if self.isOp(":"):
                    self.pos = self.pos + 1
                    self.skipType()
        self.eatOp(")")
        if self.isOp("->"):
            self.pos = self.pos + 1
            self.skipType()
        f.body = self.suite()
        push(self.funcs, f)

    func parse(self):
        self.skipNL()
        if self.isKw("module"):
            self.pos = self.pos + 1
            self.pos = self.pos + 1        # module name
        self.skipNL()
        while !self.isKind("END"):
            self.skipNL()
            if self.isKind("END"):
                return
            self.parseVis()
            if self.isKw("func"):
                self.funcDecl()
            else:
                self.pos = self.pos + 1
            self.skipNL()

# ------------------------------- Compiler (AST -> bytecode) -------------------------------
class Ins:
    var op: String
    var a: Int
    var b: Int
    init(o: String, x: Int, y: Int):
        self.op = o
        self.a = x
        self.b = y

class Loop:
    var breaks: List<Int>
    var continues: List<Int>
    init():
        self.breaks = []
        self.continues = []

class Compiler:
    var funcs: List<FuncDef>
    var binames: List<String>
    var funcIdx          # Map name -> func index
    var nconst: Int
    var out: String
    var code: List<Ins>
    var locals           # Map name -> slot
    var nextSlot: Int
    var loops: List<Loop>
    init(fs: List<FuncDef>):
        self.funcs = fs
        self.binames = split(BINAMES, " ")
        self.funcIdx = map()
        self.nconst = 0
        self.out = ""
        self.code = []
        self.locals = map()
        self.nextSlot = 0
        self.loops = []

    func builtinId(self, name: String) -> Int:
        var i = 0
        while i < len(self.binames):
            if self.binames[i] == name:
                return i
            i = i + 1
        return -1
    func emit(self, op: String, a: Int, b: Int) -> Int:
        push(self.code, Ins(op, a, b))
        return len(self.code) - 1
    func addConst(self) -> Int:
        var c = self.nconst
        self.nconst = self.nconst + 1
        return c
    func here(self) -> Int:
        return len(self.code)
    func patch(self, at: Int, target: Int):
        self.code[at].a = target
    func declare(self, name: String) -> Int:
        var s = self.nextSlot
        set(self.locals, name, s)
        self.nextSlot = self.nextSlot + 1
        return s
    func opName(self, op: String) -> String:
        if op == "+":
            return "ADD"
        if op == "-":
            return "SUB"
        if op == "*":
            return "MUL"
        if op == "/":
            return "DIV"
        if op == "%":
            return "MOD"
        if op == "<":
            return "LT"
        if op == "<=":
            return "LE"
        if op == ">":
            return "GT"
        if op == ">=":
            return "GE"
        if op == "==":
            return "EQ"
        return "NE"

    func compileArgs(self, e: Node):
        var i = 1
        while i < len(e.kids):
            self.compileExpr(e.kids[i])
            i = i + 1

    func compileExpr(self, e: Node):
        var k = e.kind
        if k == "int" || k == "flt" || k == "str" || k == "bool":
            var idx = self.emit("CONST", self.addConst(), 0)
        elif k == "name":
            if has(self.locals, e.sval):
                var s = self.emit("LOAD", get(self.locals, e.sval), 0)
            elif has(self.funcIdx, e.sval):
                var m = self.emit("MKCLOSURE", get(self.funcIdx, e.sval), 0)
        elif k == "unary":
            self.compileExpr(e.kids[0])
            if e.sval == "!":
                var n1 = self.emit("NOT", 0, 0)
            else:
                var n2 = self.emit("NEG", 0, 0)
        elif k == "binop":
            self.compileBinary(e)
        elif k == "call":
            self.compileCall(e)
        elif k == "index":
            self.compileExpr(e.kids[0])
            self.compileExpr(e.kids[1])
            var ig = self.emit("IGET", 0, 0)
        elif k == "list":
            var i = 0
            while i < len(e.kids):
                self.compileExpr(e.kids[i])
                i = i + 1
            var ml = self.emit("MAKELIST", len(e.kids), 0)

    func compileBinary(self, e: Node):
        var op = e.sval
        if op == "&&":
            self.compileExpr(e.kids[0])
            var jf = self.emit("JFALSE", 0, 0)
            self.compileExpr(e.kids[1])
            var je = self.emit("JUMP", 0, 0)
            self.patch(jf, self.here())
            var c1 = self.emit("CONST", self.addConst(), 0)
            self.patch(je, self.here())
        elif op == "||":
            self.compileExpr(e.kids[0])
            var jt = self.emit("JTRUE", 0, 0)
            self.compileExpr(e.kids[1])
            var je2 = self.emit("JUMP", 0, 0)
            self.patch(jt, self.here())
            var c2 = self.emit("CONST", self.addConst(), 0)
            self.patch(je2, self.here())
        else:
            self.compileExpr(e.kids[0])
            self.compileExpr(e.kids[1])
            var b = self.emit(self.opName(op), 0, 0)

    func compileCall(self, e: Node):
        var callee = e.kids[0]
        var argc = len(e.kids) - 1
        if callee.kind == "name":
            var name = callee.sval
            if has(self.locals, name):
                var l = self.emit("LOAD", get(self.locals, name), 0)
                self.compileArgs(e)
                var cv = self.emit("CALLV", argc, 0)
            elif self.builtinId(name) >= 0:
                self.compileArgs(e)
                var bi = self.emit("BUILTIN", self.builtinId(name), argc)
            elif has(self.funcIdx, name):
                self.compileArgs(e)
                var cl = self.emit("CALL", get(self.funcIdx, name), argc)
            else:
                self.compileArgs(e)
        else:
            self.compileExpr(callee)
            self.compileArgs(e)
            var cv2 = self.emit("CALLV", argc, 0)

    func compileBlock(self, block: Node):
        var i = 0
        while i < len(block.kids):
            self.compileStmt(block.kids[i])
            i = i + 1

    func compileStmt(self, s: Node):
        var k = s.kind
        if k == "var":
            if len(s.kids) > 0:
                self.compileExpr(s.kids[0])
            else:
                var pn = self.emit("PUSHNIL", 0, 0)
            var st = self.emit("STORE", self.declare(s.sval), 0)
        elif k == "assign":
            var target = s.kids[0]
            if target.kind == "name":
                self.compileExpr(s.kids[1])
                if has(self.locals, target.sval):
                    var st2 = self.emit("STORE", get(self.locals, target.sval), 0)
            elif target.kind == "index":
                self.compileExpr(target.kids[0])
                self.compileExpr(target.kids[1])
                self.compileExpr(s.kids[1])
                var iset = self.emit("ISET", 0, 0)
        elif k == "expr":
            self.compileExpr(s.kids[0])
            var pop = self.emit("POP", 0, 0)
        elif k == "return":
            if len(s.kids) > 0:
                self.compileExpr(s.kids[0])
            else:
                var pn2 = self.emit("PUSHNIL", 0, 0)
            var r = self.emit("RET", 0, 0)
        elif k == "if":
            self.compileIf(s)
        elif k == "while":
            self.compileWhile(s)
        elif k == "break":
            var jb = self.emit("JUMP", 0, 0)
            push(self.loops[len(self.loops) - 1].breaks, jb)
        elif k == "continue":
            var jc = self.emit("JUMP", 0, 0)
            push(self.loops[len(self.loops) - 1].continues, jc)

    func compileIf(self, s: Node):
        self.compileExpr(s.kids[0])
        var jf = self.emit("JFALSE", 0, 0)
        self.compileBlock(s.kids[1])
        var ends = []
        push(ends, self.emit("JUMP", 0, 0))
        self.patch(jf, self.here())
        var i = 2
        while i + 1 < len(s.kids):
            self.compileExpr(s.kids[i])
            var j = self.emit("JFALSE", 0, 0)
            self.compileBlock(s.kids[i + 1])
            push(ends, self.emit("JUMP", 0, 0))
            self.patch(j, self.here())
            i = i + 2
        if i < len(s.kids):
            self.compileBlock(s.kids[i])
        var e = 0
        while e < len(ends):
            self.patch(ends[e], self.here())
            e = e + 1

    func compileWhile(self, s: Node):
        var condPos = self.here()
        self.compileExpr(s.kids[0])
        var jf = self.emit("JFALSE", 0, 0)
        push(self.loops, Loop())
        self.compileBlock(s.kids[1])
        var jb = self.emit("JUMP", condPos, 0)
        var endPos = self.here()
        self.patch(jf, endPos)
        var lp = self.loops[len(self.loops) - 1]
        var b = 0
        while b < len(lp.breaks):
            self.patch(lp.breaks[b], endPos)
            b = b + 1
        var c = 0
        while c < len(lp.continues):
            self.patch(lp.continues[c], condPos)
            c = c + 1
        pop(self.loops)

    func compileFunc(self, f: FuncDef):
        self.code = []
        self.locals = map()
        self.nextSlot = 0
        self.loops = []
        var i = 0
        while i < len(f.params):
            self.declare(f.params[i])
            i = i + 1
        self.compileBlock(f.body)
        var pn = self.emit("PUSHNIL", 0, 0)
        var r = self.emit("RET", 0, 0)
        self.out = self.out + "func " + f.name + " nlocals=" + str(self.nextSlot) + "\n"
        var j = 0
        while j < len(self.code):
            var ins = self.code[j]
            self.out = self.out + "  " + str(j) + ": " + ins.op + " " + str(ins.a) + " " + str(ins.b) + "\n"
            j = j + 1

    func compile(self):
        var i = 0
        while i < len(self.funcs):
            set(self.funcIdx, self.funcs[i].name, i)
            i = i + 1
        var j = 0
        while j < len(self.funcs):
            self.compileFunc(self.funcs[j])
            j = j + 1

func main():
    var src = readFile("examples/selfhost/compile_sample.rg")
    var lx = Lexer(src)
    lx.run()
    var ps = Parser(lx.toks)
    ps.parse()
    var cc = Compiler(ps.funcs)
    cc.compile()
    print(substr(cc.out, 0, len(cc.out) - 1))   # drop the trailing newline (print re-adds one)
