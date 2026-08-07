module calc

# ---------------------------------------------------------------------
#  A tiny arithmetic language -- tokenizer, recursive-descent parser
#  building a RECURSIVE enum AST, and an evaluator -- all in RoseGold.
#
#  The "parser + AST + eval" self-hosting milestone: earlier fragments
#  only lexed. This exercises recursive enums, `match` with destructuring,
#  classes with mutable state, growable lists, and recursion -- exactly the
#  shape of RoseGold's own front-end, written in RoseGold.
# ---------------------------------------------------------------------

enum Node:
    Num(v: Int)
    Bin(op: String, l: Node, r: Node)

# ---- tokenizer: String -> List<String> ----
func isDigit(c: String) -> Bool:
    return c >= "0" && c <= "9"

func tokenize(src: String) -> List<String>:
    var toks: List<String> = []
    var i = 0
    var n = len(src)
    while i < n:
        var c = src[i]
        if c == " ":
            i = i + 1
        elif isDigit(c):
            var num = ""
            while i < n && isDigit(src[i]):
                num = num + src[i]
                i = i + 1
            push(toks, num)
        else:
            push(toks, c)
            i = i + 1
    return toks

# ---- parser: List<String> -> Node (precedence via the grammar) ----
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
    func parseExpr(self) -> Node:                 # expr := term (('+'|'-') term)*
        var left = self.parseTerm()
        while self.peek() == "+" || self.peek() == "-":
            var op = self.advance()
            left = Bin(op, left, self.parseTerm())
        return left
    func parseTerm(self) -> Node:                 # term := factor (('*'|'/') factor)*
        var left = self.parseFactor()
        while self.peek() == "*" || self.peek() == "/":
            var op = self.advance()
            left = Bin(op, left, self.parseFactor())
        return left
    func parseFactor(self) -> Node:               # factor := number | '(' expr ')'
        var t = self.advance()
        if t == "(":
            var e = self.parseExpr()
            self.advance()                        # consume ')'
            return e
        return Num(int(t))

# ---- evaluator: Node -> Int ----
func apply(op: String, a: Int, b: Int) -> Int:
    if op == "+":
        return a + b
    if op == "-":
        return a - b
    if op == "*":
        return a * b
    return a / b

func eval(n: Node) -> Int:
    return match n:
        Num(v):        v
        Bin(op, l, r): apply(op, eval(l), eval(r))

func run(src: String):
    var p = Parser(tokenize(src))
    print(src, "=", eval(p.parseExpr()))

func main():
    run("1 + 2 * 3")
    run("(1 + 2) * 3")
    run("10 - 4 - 3")
    run("2 * (3 + 4) * 5")
