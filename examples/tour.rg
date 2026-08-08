module tour

#/
  A guided TOUR of RoseGold in one file. Every section prints its results, so
  running this program is a quick tour of the language:
    enums + match, generics, classes, traits + default methods, operator
    overloading, closures, collections (list + map), error handling,
    coroutines, signals, and value-type vectors.
  Run:  ./cpp/rosegoldc examples/tour.rg
/#

# ---------- enums + pattern matching ----------
enum Shape:
    Circle(r: Float)
    Rect(w: Float, h: Float)

## The area of a shape, by matching on its variant.
## @param s the shape to measure
## @return the area
func area(s: Shape) -> Float:
    return match s:
        Circle(r):   3.14159 * r * r
        Rect(w, h):  w * h

# ---------- generics ----------
## A one-slot container generic over its element type.
class Box<T>:
    var item: T
    init(x: T):
        self.item = x
    func get(self) -> T:
        return self.item

# ---------- traits with a default method ----------
trait Greeter:
    func name(self) -> String
    ## Default greeting, reused by every conformer (overridable).
    func greet(self) -> String:
        return "Hello, " + self.name()

class Person uses Greeter:
    var who: String
    init(who: String):
        self.who = who
    func name(self) -> String:
        return self.who

# ---------- operator overloading ----------
class Vec2:
    var x: Int
    var y: Int
    init(x: Int, y: Int):
        self.x = x
        self.y = y
    func add(self, o: Vec2) -> Vec2:
        return Vec2(self.x + o.x, self.y + o.y)
    func show(self) -> String:
        return "(" + str(self.x) + ", " + str(self.y) + ")"

# ---------- signals ----------
class Clock:
    signal tick(n: Int)
    func run(self, times: Int):
        var i = 0
        while i < times:
            i = i + 1
            self.tick.emit(i)

# ---------- coroutines ----------
## A generator that yields 0, 1, ..., n-1 across resumes.
func counter(n: Int):
    var i = 0
    while i < n:
        yield i
        i = i + 1

func main():
    print("== enums + match ==")
    print("circle:", area(Circle(2.0)))
    print("rect:  ", area(Rect(3.0, 4.0)))

    print("== generics ==")
    var b = Box(41)
    print("box.get() + 1 =", b.get() + 1)

    print("== traits (inherited default) ==")
    print(Person("Ada").greet())

    print("== operator overloading ==")
    var sum = Vec2(1, 2) + Vec2(3, 4)
    print("v + w =", sum.show())

    print("== closures ==")
    var sq = func(x: Int) -> Int => x * x
    print("sq(9) =", sq(9))

    print("== collections ==")
    var nums = [1, 2, 3]
    push(nums, 4)
    var total = 0
    for n in nums:
        total = total + n
    print("sum 1..4 =", total)
    var ages = map()
    set(ages, "ada", 36)
    set(ages, "alan", 41)
    print("keys:", keys(ages), "ada =", get(ages, "ada"))

    print("== error handling ==")
    try:
        raise "boom"
    catch e:
        print("caught:", e)

    print("== coroutines ==")
    var c = coroutine(counter, 4)
    var gen = []
    var v = resume(c)
    while !done(c):
        push(gen, v)
        v = resume(c)
    print("generated:", gen)

    print("== signals ==")
    var clk = Clock()
    clk.tick.connect(func(n: Int) => print("  tick", n))
    clk.run(3)

    print("== vectors ==")
    var p = vec2(3.0, 4.0)
    print("vlen(3,4) =", vlen(p))
