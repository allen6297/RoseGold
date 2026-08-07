module trait_errors

trait Drawable:
    func draw(self) -> String

# (1) Conformance failure: Bad `uses Drawable` but never implements draw().
class Bad uses Drawable:
    var x: Int
    init(x: Int):
        self.x = x

# (2) Conformance failure: draw() has the wrong signature (Int, trait wants String).
class Wrong uses Drawable:
    func draw(self) -> Int:
        return 1

# A perfectly good class that simply does NOT implement Drawable.
class Plain:
    var v: Int
    init(v: Int):
        self.v = v

func render<T: Drawable>(x: T) -> String:
    return x.draw()

func show(d: Drawable) -> String:
    return d.draw()

func main():
    var p = Plain(1)
    print(render(p))     # (3) bound: Plain does not satisfy Drawable
    print(show(p))       # (4) argument: Plain not assignable to Drawable
