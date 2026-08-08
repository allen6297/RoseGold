module parse_sample

# A program within the self-hosted parser's grammar subset: module-level
# globals + functions, with statements (var/assign/return/if-elif-else/while/
# for/expr) and expressions (literals, names, calls, indexing, and binary ops
# across the full precedence ladder). `rgparser.rg` parses this to the same AST
# that `rosegoldc --ast` prints. It also runs, so it's golden-tested twice.

const LIMIT = 5

func add(a: Int, b: Int) -> Int:
    return a + b

func classify(n: Int) -> String:
    if n < 0:
        return "neg"
    elif n == 0:
        return "zero"
    else:
        return "pos"

func sumTo(n: Int) -> Int:
    var total = 0
    var i = 1
    while i <= n:
        total = total + i
        i = i + 1
    return total

func demo() -> Int:
    var xs = [10, 20, 30, 40]
    var acc = xs[0]
    for x in xs:
        acc = acc + x * 2 - 1
    return acc

func main():
    print(add(2, 3))
    print(classify(-1))
    print(sumTo(LIMIT))
    print(demo())
