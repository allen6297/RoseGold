module traits

# A trait is an abstract capability: a set of method signatures with no bodies.
# Any class that `uses` a trait must implement every method it declares.
trait Drawable:
    fn draw(self) -> String
    fn area(self) -> Float

class Circle uses Drawable:
    var r: Float
    init(r: Float):
        self.r = r
    fn draw(self) -> String:
        return "circle"
    fn area(self) -> Float:
        return 3.14159 * self.r * self.r

class Square uses Drawable:
    var side: Float
    init(side: Float):
        self.side = side
    fn draw(self) -> String:
        return "square"
    fn area(self) -> Float:
        return self.side * self.side

# Trait-typed parameter: accepts any Drawable, dispatched dynamically at runtime.
fn describe(d: Drawable) -> String:
    return d.draw()

# Trait-bounded generic: T must implement Drawable, so the body may call trait
# methods on values of type T -- and the checker verifies the bound at call sites.
fn total_area<T: Drawable>(items: List<T>) -> Float:
    var sum = 0.0
    for it in items:
        sum = sum + it.area()
    return sum

fn main():
    var c = Circle(2.0)
    var s = Square(3.0)
    print("describe c  =", describe(c))
    print("describe s  =", describe(s))

    # A mixed list is typed at the common trait Drawable, so it iterates uniformly.
    var shapes: List<Drawable> = [c, s]
    for d in shapes:
        print("draw        =", d.draw())

    print("area circle =", total_area([Circle(1.0), Circle(2.0)]))
    print("area mixed  =", total_area(shapes))
