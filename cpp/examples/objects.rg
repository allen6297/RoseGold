module objects

enum Shape:
    Circle(radius: Float)
    Rect(width: Float, height: Float)

func area(s: Shape) -> Float:
    return match s:
        Circle(r):   3.14159 * r * r
        Rect(w, h):  w * h

class Counter:
    var value: Int
    init(start: Int):
        self.value = start
    func bump(self):
        self.value = self.value + 1
    func get(self) -> Int:
        return self.value

enum Option:
    Some(value: Int)
    None

func main():
    var shapes = [Circle(2.0), Rect(3.0, 4.0)]
    for s in shapes:
        print("area    =", area(s))

    var c = Counter(10)
    c.bump()
    c.bump()
    print("counter =", c.get())
    print("field   =", c.value)

    var o = Some(42)
    var v = match o:
        Some(n): n
        None: -1
    print("option  =", v)

    var none = None
    var w = match none:
        Some(n): n
        None: -1
    print("none    =", w)
