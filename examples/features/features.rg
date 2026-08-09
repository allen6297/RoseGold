module features

fn sum(xs: List<Int>) -> Int:
    var total = 0
    for x in xs:
        total = total + x
    return total

fn checked(n: Int) -> Int:
    if n < 0:
        raise "negative input!"
    return n * 2

var greeting = "RoseGold"

fn main():
    var xs = [1, 2, 3, 4, 5]
    print("sum       =", sum(xs))
    xs[0] = 100
    print("after set =", xs[0], sum(xs))
    print("len       =", len(xs))
    print("range(3)  =", range(3))

    var acc = 0
    for n in range(10):
        if n == 3:
            continue
        if n > 6:
            break
        acc = acc + n
    print("acc       =", acc)

    try:
        print("checked 5 =", checked(5))
        print("checked -1=", checked(-1))
        print("unreached")
    catch e:
        print("caught    :", e)

    print("global    =", greeting)
