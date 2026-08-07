module interp

# ---------------------------------------------------------------------
#  A Turing-complete little imperative language, interpreted in RoseGold.
#
#  Builds on mini.rg by adding control flow (if / while), comparison
#  operators, and reassignment -- enough to run real algorithms. Blocks
#  are `end`-delimited to keep the parser tiny. Statements are AST enums
#  executed against a Map<String, Int> environment, with nested blocks
#  handled by recursion.
#
#    var fact = 1
#    var i = 1
#    while i <= 5:
#        fact = fact * i
#        i = i + 1
#    end
#    print fact          # 120
# ---------------------------------------------------------------------

enum Expr:
    Num(v: Int)
    Var(name: String)
    Bin(op: String, l: Expr, r: Expr)

enum Stmt:
    Assign(name: String, e: Expr)
    Print(e: Expr)
    While(cond: Expr, body: List<Stmt>)
    If(cond: Expr, thenB: List<Stmt>, elseB: List<Stmt>)

func isDigit(c: String) -> Bool:
    return c >= "0" && c <= "9"

func isAlpha(c: String) -> Bool:
    return (c >= "a" && c <= "z") || (c >= "A" && c <= "Z") || c == "_"

# ---- tokenizer (handles two-char comparison operators) ----
func tokenize(src: String) -> List<String>:
    var toks: List<String> = []
    var i = 0
    var n = len(src)
    while i < n:
        var c = src[i]
        if c == "\n":
            push(toks, "\n")
            i = i + 1
        elif c == " ":
            i = i + 1
        elif isDigit(c):
            var num = ""
            while i < n && isDigit(src[i]):
                num = num + src[i]
                i = i + 1
            push(toks, num)
        elif isAlpha(c):
            var word = ""
            while i < n && isAlpha(src[i]):
                word = word + src[i]
                i = i + 1
            push(toks, word)
        elif c == "<" || c == ">" || c == "=" || c == "!":
            if i + 1 < n && src[i + 1] == "=":
                push(toks, c + "=")
                i = i + 2
            else:
                push(toks, c)
                i = i + 1
        else:
            push(toks, c)
            i = i + 1
    return toks

# ---- parser ----
class Parser:
    var toks: List<String>
    var pos: Int
    init(toks: List<String>):
        self.toks = toks
        self.pos = 0
    func peek(self) -> String:
        if self.pos < len(self.toks):
            return self.toks[self.pos]
        return ""
    func advance(self) -> String:
        var t = self.peek()
        self.pos = self.pos + 1
        return t
    func skipBreaks(self):
        while self.peek() == "\n":
            self.pos = self.pos + 1
    func parseBlock(self) -> List<Stmt>:
        var stmts: List<Stmt> = []
        self.skipBreaks()
        while self.peek() != "end" && self.peek() != "else" && self.pos < len(self.toks):
            push(stmts, self.parseStmt())
            self.skipBreaks()
        return stmts
    func parseStmt(self) -> Stmt:
        var t = self.peek()
        if t == "var":
            self.advance()
            var name = self.advance()
            self.advance()                       # '='
            return Assign(name, self.parseExpr())
        if t == "print":
            self.advance()
            return Print(self.parseExpr())
        if t == "while":
            self.advance()
            var cond = self.parseExpr()
            self.advance()                       # ':'
            var body = self.parseBlock()
            self.advance()                       # 'end'
            return While(cond, body)
        if t == "if":
            self.advance()
            var cond = self.parseExpr()
            self.advance()                       # ':'
            var thenB = self.parseBlock()
            var elseB: List<Stmt> = []
            if self.peek() == "else":
                self.advance()                   # 'else'
                self.advance()                   # ':'
                elseB = self.parseBlock()
            self.advance()                       # 'end'
            return If(cond, thenB, elseB)
        var name = self.advance()                # reassignment: IDENT '=' expr
        self.advance()                           # '='
        return Assign(name, self.parseExpr())
    func parseExpr(self) -> Expr:                 # comparison (lowest precedence)
        var left = self.parseAdd()
        if self.isCmp(self.peek()):
            var op = self.advance()
            left = Bin(op, left, self.parseAdd())
        return left
    func isCmp(self, t: String) -> Bool:
        return t == "<" || t == ">" || t == "<=" || t == ">=" || t == "==" || t == "!="
    func parseAdd(self) -> Expr:
        var left = self.parseMul()
        while self.peek() == "+" || self.peek() == "-":
            var op = self.advance()
            left = Bin(op, left, self.parseMul())
        return left
    func parseMul(self) -> Expr:
        var left = self.parseFactor()
        while self.peek() == "*" || self.peek() == "/":
            var op = self.advance()
            left = Bin(op, left, self.parseFactor())
        return left
    func parseFactor(self) -> Expr:
        var t = self.advance()
        if t == "(":
            var e = self.parseExpr()
            self.advance()                       # ')'
            return e
        if isDigit(t):
            return Num(int(t))
        return Var(t)

# ---- interpreter ----
func apply(op: String, a: Int, b: Int) -> Int:
    if op == "+":
        return a + b
    if op == "-":
        return a - b
    if op == "*":
        return a * b
    if op == "/":
        return a / b
    var r = false
    if op == "<":
        r = a < b
    if op == ">":
        r = a > b
    if op == "<=":
        r = a <= b
    if op == ">=":
        r = a >= b
    if op == "==":
        r = a == b
    if op == "!=":
        r = a != b
    if r:
        return 1
    return 0

func eval(e: Expr, env: Map<String, Int>) -> Int:
    return match e:
        Num(v):        v
        Var(name):     get(env, name)
        Bin(op, l, r): apply(op, eval(l, env), eval(r, env))

func runBlock(body: List<Stmt>, env: Map<String, Int>):
    for s in body:
        exec(s, env)

func runWhile(cond: Expr, body: List<Stmt>, env: Map<String, Int>):
    while eval(cond, env) != 0:
        runBlock(body, env)

func runIf(cond: Expr, thenB: List<Stmt>, elseB: List<Stmt>, env: Map<String, Int>):
    if eval(cond, env) != 0:
        runBlock(thenB, env)
    else:
        runBlock(elseB, env)

func exec(s: Stmt, env: Map<String, Int>):
    match s:
        Assign(name, e):        set(env, name, eval(e, env))
        Print(e):               print(eval(e, env))
        While(cond, body):      runWhile(cond, body, env)
        If(cond, thenB, elseB): runIf(cond, thenB, elseB, env)

func run(src: String):
    var prog = Parser(tokenize(src)).parseBlock()
    var env: Map<String, Int> = map()
    runBlock(prog, env)

func main():
    var src = "var fact = 1\nvar i = 1\nwhile i <= 5:\nfact = fact * i\ni = i + 1\nend\nprint fact\nvar sum = 0\nvar j = 1\nwhile j <= 10:\nsum = sum + j\nj = j + 1\nend\nprint sum\nvar x = 7\nif x < 5:\nprint 100\nelse:\nprint 200\nend\n"
    run(src)
