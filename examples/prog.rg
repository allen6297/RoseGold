module prog

import util

enum Shape:
    Circle(radius: Float)
    Rect(width: Float, height: Float)

fn area(s: Shape) -> Float:
    return match s:
        Circle(r):   3.14159 * r * r
        Rect(w, h):  w * h

fn fib(n: Int) -> Int:
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

class Counter:
    var value: Int
    init(start: Int):
        self.value = start
    fn bump(self):
        self.value = self.value + 1

fn main():
    # closures + higher-order
    var square = fn(x: Int) -> Int => x * x
    print("square(9)      =", square(9))

    # cross-module call
    print("util.twice(21) =", util.twice(21))

    # loop over a list
    var total = 0
    for n in [1, 2, 3, 4, 5]:
        total = total + n
    print("sum 1..5       =", total)

    # recursion
    print("fib(10)        =", fib(10))

    # enum construction + match
    var shapes = [Circle(2.0), Rect(3.0, 4.0)]
    for s in shapes:
        print("area           =", area(s))

    # class with init + method + field access
    var c = Counter(10)
    c.bump()
    c.bump()
    print("counter        =", c.value)
