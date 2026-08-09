module structdemo

# `struct` is a VALUE type: unlike a class (a shared reference), a struct is
# COPIED when assigned, passed to a function, or stored — so each binding is an
# independent value. Structs can have fields, methods, and `uses` traits, but no
# `extends` (compose, don't inherit).

trait Summable:
    fn total(self) -> Int

struct Point uses Summable:
    var x: Int
    var y: Int
    init(x: Int, y: Int):
        self.x = x
        self.y = y
    fn total(self) -> Int:
        return self.x + self.y
    fn moved(self, dx: Int, dy: Int) -> Point:
        return Point(self.x + dx, self.y + dy)

fn main():
    var a = Point(1, 2)
    var b = a               # a copy
    b.x = 100
    print("a =", a.x, a.y)          # 1 2 (unchanged)
    print("b =", b.x, b.y)          # 100 2
    print("a.total =", a.total())   # 3 (trait method)

    # methods return new values; the receiver is untouched
    var c = a.moved(10, 10)
    print("c =", c.x, c.y)          # 11 12
    print("a still =", a.x, a.y)    # 1 2

    # independent copies inside a list
    var ps = [a, a]
    ps[0].x = 7
    print("list =", ps[0].x, ps[1].x)   # 7 1
