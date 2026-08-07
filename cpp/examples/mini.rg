module mini

# ---------------------------------------------------------------------
#  A small IMPERATIVE language interpreter, written in RoseGold.
#
#  Next self-hosting step past calc.rg: adds statements and variables, so
#  it needs an ENVIRONMENT -- built on the Map<String, Int> type. It scans
#  a multi-line program, parses it into statement + expression enum ASTs,
#  and executes it against a mutable variable map.
#
#    var a = 3 + 4
#    var b = a * 2
#    print b - 1
#    print (a + b) * 2
# ---------------------------------------------------------------------

enum Expr:
    Num(v: Int)
    Var(name: String)
    Bin(op: String, l: Expr, r: Expr)

enum Stmt:
    Assign(name: String, e: Expr)
    Print(e: Expr)

func isDigit(c: String) -> Bool:
    return c >= "0" && c <= "9"

func isAlpha(c: String) -> Bool:
    return (c >= "a" && c <= "z") || (c >= "A" && c <= "Z") || c == "_"

# ---- tokenizer: source -> List<String>, using "\n" as a statement break ----
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
        else:
            push(toks, c)
            i = i + 1
    return toks

# ---- parser: List<String> -> List<Stmt> ----
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
    func parseProgram(self) -> List<Stmt>:
        var stmts: List<Stmt> = []
        self.skipBreaks()
        while self.pos < len(self.toks):
            push(stmts, self.parseStmt())
            self.skipBreaks()
        return stmts
    func parseStmt(self) -> Stmt:
        if self.peek() == "var":
            self.advance()
            var name = self.advance()
            self.advance()                 # consume '='
            return Assign(name, self.parseExpr())
        self.advance()                     # consume 'print'
        return Print(self.parseExpr())
    func parseExpr(self) -> Expr:
        var left = self.parseTerm()
        while self.peek() == "+" || self.peek() == "-":
            var op = self.advance()
            left = Bin(op, left, self.parseTerm())
        return left
    func parseTerm(self) -> Expr:
        var left = self.parseFactor()
        while self.peek() == "*" || self.peek() == "/":
            var op = self.advance()
            left = Bin(op, left, self.parseFactor())
        return left
    func parseFactor(self) -> Expr:
        var t = self.advance()
        if t == "(":
            var e = self.parseExpr()
            self.advance()                 # consume ')'
            return e
        if isDigit(t):
            return Num(int(t))
        return Var(t)

# ---- interpreter: walk the AST against a variable environment ----
func apply(op: String, a: Int, b: Int) -> Int:
    if op == "+":
        return a + b
    if op == "-":
        return a - b
    if op == "*":
        return a * b
    return a / b

func eval(e: Expr, env: Map<String, Int>) -> Int:
    return match e:
        Num(v):        v
        Var(name):     get(env, name)
        Bin(op, l, r): apply(op, eval(l, env), eval(r, env))

func exec(s: Stmt, env: Map<String, Int>):
    match s:
        Assign(name, e): set(env, name, eval(e, env))
        Print(e):        print(eval(e, env))

func main():
    var src = "var a = 3 + 4\nvar b = a * 2\nprint b - 1\nprint (a + b) * 2\n"
    var prog = Parser(tokenize(src)).parseProgram()
    var env: Map<String, Int> = map()
    for s in prog:
        exec(s, env)
