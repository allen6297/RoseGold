module rgchecker

#/
  A RoseGold TYPE CHECKER written in RoseGold — self-hosting, rung 3 (after the
  lexer and parser). It lexes (offside rule, with line tracking) and parses a
  core subset into an AST, then runs a two-pass check — collect function
  signatures, then check each body — reporting the SAME type errors as
  `rosegoldc --check`, in the same order and wording:
    • undefined name 'X'
    • expected N argument(s), got M          (call arity)
    • argument K: 'T' is not assignable to 'U'  (call argument types)
  The harness diffs this against `--check` on examples/check_sample.rg. The type
  checker stage of RoseGold, self-hosted (name resolution + call checking).
/#

const KEYWORDS = "module import as pub internal private static func var const return pass if elif else while for in break continue try catch raise yield class trait enum init match extends extend extern uses signal true false"
const BUILTINS = "print len range push pop str ord chr substr split int readFile writeFile map set get has keys remove"

class Tok:
    var kind: String
    var val: String
    var line: Int
    init(k: String, v: String, ln: Int):
        self.kind = k
        self.val = v
        self.line = ln

# ------------------------------- Lexer (offside rule + line tracking) -------------------------------
class Lexer:
    var src: String
    var pos: Int
    var n: Int
    var line: Int
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
        self.line = 1
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
        push(self.toks, Tok(kind, "", self.line))
        self.last = kind
    func emitVal(self, kind: String, val: String):
        push(self.toks, Tok(kind, val, self.line))
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
                    self.line = self.line + 1
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
                self.line = self.line + 1
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
    var kind: String     # int/flt/str/bool/name/binop/unary/call/index/list/member  |  var/assign/return/expr/if/while/for/block/break/continue/pass
    var sval: String     # name / op / field / var-name / loop-var
    var ty: String       # var type annotation ("" if none)
    var line: Int
    var kids: List<Node>
    init(k: String):
        self.kind = k
        self.sval = ""
        self.ty = ""
        self.line = 0
        self.kids = []

class FuncDef:
    var name: String
    var params: List<String>
    var ptypes: List<String>
    var ret: String
    var body: Node          # a "block" node
    init(nm: String):
        self.name = nm
        self.params = []
        self.ptypes = []
        self.ret = "Void"
        self.body = Node("block")

# ------------------------------- Parser (builds AST with line numbers) -------------------------------
class Parser:
    var toks: List<Tok>
    var pos: Int
    var modName: String
    var funcs: List<FuncDef>
    init(ts: List<Tok>):
        self.toks = ts
        self.pos = 0
        self.modName = "$entry"
        self.funcs = []

    func kind(self) -> String:
        return self.toks[self.pos].kind
    func curLine(self) -> Int:
        return self.toks[self.pos].line
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
            return "func(" + ps + ") -> " + self.parseType()
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

    # ---- expressions ----
    func expr(self) -> Node:
        return self.orE()
    func binLevel(self, left: Node, op: String, right: Node) -> Node:
        var nd = Node("binop")
        nd.sval = op
        nd.line = left.line
        push(nd.kids, left)
        push(nd.kids, right)
        return nd
    func orE(self) -> Node:
        var left = self.andE()
        while self.isOp("||"):
            var op = self.next().val
            left = self.binLevel(left, op, self.andE())
        return left
    func andE(self) -> Node:
        var left = self.eqE()
        while self.isOp("&&"):
            var op = self.next().val
            left = self.binLevel(left, op, self.eqE())
        return left
    func eqE(self) -> Node:
        var left = self.cmpE()
        while self.isOp("==") || self.isOp("!="):
            var op = self.next().val
            left = self.binLevel(left, op, self.cmpE())
        return left
    func cmpE(self) -> Node:
        var left = self.addE()
        while self.isOp("<") || self.isOp("<=") || self.isOp(">") || self.isOp(">="):
            var op = self.next().val
            left = self.binLevel(left, op, self.addE())
        return left
    func addE(self) -> Node:
        var left = self.mulE()
        while self.isOp("+") || self.isOp("-"):
            var op = self.next().val
            left = self.binLevel(left, op, self.mulE())
        return left
    func mulE(self) -> Node:
        var left = self.unaryE()
        while self.isOp("*") || self.isOp("/") || self.isOp("%"):
            var op = self.next().val
            left = self.binLevel(left, op, self.unaryE())
        return left
    func unaryE(self) -> Node:
        if self.isOp("!") || self.isOp("-"):
            var ln = self.curLine()
            var op = self.next().val
            var nd = Node("unary")
            nd.sval = op
            nd.line = ln
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
                call.line = e.line
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
                idx.line = e.line
                push(idx.kids, e)
                push(idx.kids, self.expr())
                self.eatOp("]")
                e = idx
            elif self.isOp("."):
                self.pos = self.pos + 1
                var m = Node("member")
                var ft = self.next()
                m.sval = ft.val
                m.line = ft.line
                push(m.kids, e)
                e = m
            else:
                go = false
        return e
    func primary(self) -> Node:
        var k = self.kind()
        if k == "INT":
            var t = self.next()
            var nd = Node("int")
            nd.line = t.line
            return nd
        if k == "FLT":
            var t = self.next()
            var nd = Node("flt")
            nd.line = t.line
            return nd
        if k == "STR":
            var t = self.next()
            var nd = Node("str")
            nd.line = t.line
            return nd
        if self.isKw("true") || self.isKw("false"):
            var t = self.next()
            var nd = Node("bool")
            nd.sval = t.val
            nd.line = t.line
            return nd
        if k == "IDENT":
            var t = self.next()
            var nd = Node("name")
            nd.sval = t.val
            nd.line = t.line
            return nd
        if self.isOp("("):
            self.pos = self.pos + 1
            var inner = self.expr()
            self.eatOp(")")
            return inner
        if self.isOp("["):
            var ln = self.curLine()
            self.pos = self.pos + 1
            var nd = Node("list")
            nd.line = ln
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
            var ln = self.curLine()
            self.pos = self.pos + 1
            var nd = Node("while")
            nd.line = ln
            push(nd.kids, self.expr())
            push(nd.kids, self.suite())
            return nd
        if self.isKw("for"):
            var ln = self.curLine()
            self.pos = self.pos + 1
            var nd = Node("for")
            nd.line = ln
            nd.sval = self.next().val
            self.pos = self.pos + 1        # 'in'
            push(nd.kids, self.expr())
            push(nd.kids, self.suite())
            return nd
        if self.isKw("var") || self.isKw("const"):
            return self.varStmt()
        if self.isKw("return"):
            var ln = self.curLine()
            self.pos = self.pos + 1
            var nd = Node("return")
            nd.line = ln
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
            nd.line = e.line
            push(nd.kids, e)
            push(nd.kids, self.expr())
            return nd
        var ex = Node("expr")
        ex.line = e.line
        push(ex.kids, e)
        return ex
    func varStmt(self) -> Node:
        self.pos = self.pos + 1
        var nd = Node("var")
        var t = self.next()
        nd.sval = t.val
        nd.line = t.line
        if self.isOp(":"):
            self.pos = self.pos + 1
            nd.ty = self.parseType()
        if self.isOp("="):
            self.pos = self.pos + 1
            push(nd.kids, self.expr())
        return nd
    func ifStmt(self) -> Node:
        var ln = self.curLine()
        self.pos = self.pos + 1
        var nd = Node("if")
        nd.line = ln
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
        self.pos = self.pos + 1                 # 'func'
        var f = FuncDef(self.next().val)        # name
        self.eatOp("(")
        if !self.isOp(")"):
            push(f.params, self.next().val)
            var ty = "Any"
            if self.isOp(":"):
                self.pos = self.pos + 1
                ty = self.parseType()
            push(f.ptypes, ty)
            while self.isOp(","):
                self.pos = self.pos + 1
                push(f.params, self.next().val)
                var t2 = "Any"
                if self.isOp(":"):
                    self.pos = self.pos + 1
                    t2 = self.parseType()
                push(f.ptypes, t2)
        self.eatOp(")")
        if self.isOp("->"):
            self.pos = self.pos + 1
            f.ret = self.parseType()
        f.body = self.suite()
        push(self.funcs, f)

    func parse(self):
        self.skipNL()
        if self.isKw("module"):
            self.pos = self.pos + 1
            self.modName = self.next().val
        self.skipNL()
        while !self.isKind("END"):
            self.skipNL()
            if self.isKind("END"):
                return
            self.parseVis()
            if self.isKw("func"):
                self.funcDecl()
            else:
                self.pos = self.pos + 1        # skip non-func top-level (out of subset)
            self.skipNL()

# ------------------------------- Checker (name resolution + call checking) -------------------------------
class Checker:
    var funcs: List<FuncDef>
    var modName: String
    var sigs           # Map name -> FuncDef
    var builtins: List<String>
    var errors: List<String>
    var scopes         # stack of Map name -> type
    init(fs: List<FuncDef>, mod: String):
        self.funcs = fs
        self.modName = mod
        self.sigs = map()
        self.builtins = split(BUILTINS, " ")
        self.errors = []
        self.scopes = []

    func isBuiltin(self, name: String) -> Bool:
        var i = 0
        while i < len(self.builtins):
            if self.builtins[i] == name:
                return true
            i = i + 1
        return false

    func err(self, line: Int, msg: String):
        push(self.errors, self.modName + ":" + str(line) + ": " + msg)

    func lookup(self, name: String) -> String:
        var i = len(self.scopes) - 1
        while i >= 0:
            if has(self.scopes[i], name):
                return get(self.scopes[i], name)
            i = i - 1
        return ""

    func assignable(self, from: String, to: String) -> Bool:
        return from == to || from == "Any" || to == "Any"

    func infer(self, e: Node) -> String:
        var k = e.kind
        if k == "int":
            return "Int"
        if k == "flt":
            return "Float"
        if k == "str":
            return "String"
        if k == "bool":
            return "Bool"
        if k == "name":
            var t = self.lookup(e.sval)
            if t != "":
                return t
            if has(self.sigs, e.sval) || self.isBuiltin(e.sval):
                return "Any"
            self.err(e.line, "undefined name '" + e.sval + "'")
            return "Any"
        if k == "unary":
            var ot = self.infer(e.kids[0])
            if e.sval == "!":
                return "Bool"
            return ot
        if k == "binop":
            var lt = self.infer(e.kids[0])
            var rt = self.infer(e.kids[1])
            var op = e.sval
            if op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=" || op == "&&" || op == "||":
                return "Bool"
            if lt == "Int" && rt == "Int":
                return "Int"
            if lt == "Float" && rt == "Float":
                return "Float"
            if lt == "String" && rt == "String" && op == "+":
                return "String"
            return "Any"
        if k == "index":
            self.infer(e.kids[0])
            self.infer(e.kids[1])
            return "Any"
        if k == "list":
            var i = 0
            while i < len(e.kids):
                self.infer(e.kids[i])
                i = i + 1
            return "Any"
        if k == "call":
            return self.inferCall(e)
        if k == "member":
            self.infer(e.kids[0])
            return "Any"
        return "Any"

    func inferCall(self, e: Node) -> String:
        var callee = e.kids[0]
        # arguments come first in traversal order
        var args = []
        var ai = 1
        while ai < len(e.kids):
            push(args, e.kids[ai])
            ai = ai + 1
        if callee.kind == "name":
            var name = callee.sval
            if has(self.sigs, name):
                var sig = get(self.sigs, name)
                var atypes = []
                var j = 0
                while j < len(args):
                    push(atypes, self.infer(args[j]))
                    j = j + 1
                if len(args) != len(sig.params):
                    self.err(e.line, "expected " + str(len(sig.params)) + " argument(s), got " + str(len(args)))
                    return sig.ret
                var k2 = 0
                while k2 < len(args):
                    if !self.assignable(atypes[k2], sig.ptypes[k2]):
                        self.err(args[k2].line, "argument " + str(k2 + 1) + ": '" + atypes[k2] + "' is not assignable to '" + sig.ptypes[k2] + "'")
                    k2 = k2 + 1
                return sig.ret
            # builtins + unknown callees: infer args (catch nested errors), no arity/type check
            if !self.isBuiltin(name) && self.lookup(name) == "":
                self.err(callee.line, "undefined name '" + name + "'")
            var b = 0
            while b < len(args):
                self.infer(args[b])
                b = b + 1
            return "Any"
        self.infer(callee)
        var c = 0
        while c < len(args):
            self.infer(args[c])
            c = c + 1
        return "Any"

    func checkBlock(self, block: Node):
        push(self.scopes, map())
        var i = 0
        while i < len(block.kids):
            self.checkStmt(block.kids[i])
            i = i + 1
        pop(self.scopes)

    func checkStmt(self, s: Node):
        var k = s.kind
        if k == "var":
            var t = "Any"
            if len(s.kids) > 0:
                t = self.infer(s.kids[0])
            if s.ty != "":
                t = s.ty
            set(self.scopes[len(self.scopes) - 1], s.sval, t)
        elif k == "assign":
            self.infer(s.kids[0])
            self.infer(s.kids[1])
        elif k == "expr":
            self.infer(s.kids[0])
        elif k == "return":
            if len(s.kids) > 0:
                self.infer(s.kids[0])
        elif k == "if":
            var i = 0
            # kids: (cond, block) pairs, with an optional trailing else block
            while i + 1 < len(s.kids):
                self.infer(s.kids[i])
                self.checkBlock(s.kids[i + 1])
                i = i + 2
            if i < len(s.kids):
                self.checkBlock(s.kids[i])
        elif k == "while":
            self.infer(s.kids[0])
            self.checkBlock(s.kids[1])
        elif k == "for":
            self.infer(s.kids[0])
            push(self.scopes, map())
            set(self.scopes[len(self.scopes) - 1], s.sval, "Any")
            self.checkBlock(s.kids[1])
            pop(self.scopes)

    func checkFunc(self, f: FuncDef):
        push(self.scopes, map())
        var i = 0
        while i < len(f.params):
            set(self.scopes[0], f.params[i], f.ptypes[i])
            i = i + 1
        self.checkBlock(f.body)
        pop(self.scopes)

    func check(self):
        var i = 0
        while i < len(self.funcs):
            set(self.sigs, self.funcs[i].name, self.funcs[i])
            i = i + 1
        var j = 0
        while j < len(self.funcs):
            self.checkFunc(self.funcs[j])
            j = j + 1

    func report(self):
        if len(self.errors) == 0:
            return
        var out = "type errors:"
        var i = 0
        while i < len(self.errors):
            out = out + "\n  " + self.errors[i]
            i = i + 1
        print(out)

func main():
    var src = readFile("examples/selfhost/check_sample.rg")
    var lx = Lexer(src)
    lx.run()
    var ps = Parser(lx.toks)
    ps.parse()
    var ck = Checker(ps.funcs, ps.modName)
    ck.check()
    ck.report()
