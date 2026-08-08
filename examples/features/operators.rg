module operators

trait Ordered:
    func compareTo(self, other: Self) -> Int

# Operator overloading is convention-based: `+ - * / %` dispatch to add/sub/mul/
# div/mod, `< <= > >=` to compareTo, and `== !=` to equals -- on any user type
# (a class method, or a method added via `extend`).
class Vec2 uses Ordered:
    var x: Int
    var y: Int
    init(x: Int, y: Int):
        self.x = x
        self.y = y
    func mag(self) -> Int:
        return self.x + self.y
    func add(self, other: Vec2) -> Vec2:
        return Vec2(self.x + other.x, self.y + other.y)
    func compareTo(self, other: Vec2) -> Int:
        return self.mag() - other.mag()
    func equals(self, other: Vec2) -> Bool:
        return self.x == other.x && self.y == other.y

# `a < b` on a bounded generic now works too: it desugars to a.compareTo(b) < 0.
func smaller<T: Ordered>(a: T, b: T) -> T:
    if a < b:
        return a
    return b

func main():
    var u = Vec2(1, 2)
    var v = Vec2(3, 4)
    var s = u + v                        # -> u.add(v)
    print("u + v     =", s.x, s.y)
    print("u < v     =", u < v)          # -> u.compareTo(v) < 0
    print("u == copy =", u == Vec2(1, 2))# -> u.equals(...)
    print("smaller   =", smaller(u, v).mag())
