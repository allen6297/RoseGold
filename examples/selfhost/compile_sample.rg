module compile_sample

# A functions-only program within the self-hosted compiler's subset: params,
# var/assign, arithmetic + comparison, if, while, return, and calls (user
# functions + the print builtin). `rgcompiler.rg` compiles this to the same
# bytecode `rosegoldc --bytecode` prints. It also runs, so it's golden-tested.

fn bigger(a: Int, b: Int) -> Int:
    if a < b:
        return b
    return a

fn sumTo(n: Int) -> Int:
    var total = 0
    var i = 1
    while i <= n:
        total = total + i
        i = i + 1
    return total

fn main():
    print(bigger(3, 7))
    print(sumTo(5))
