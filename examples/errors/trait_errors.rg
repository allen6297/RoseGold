module trait_errors

trait Drawable:
    fn draw(self) -> String

# (1) Conformance failure: Bad `uses Drawable` but never implements draw().
class Bad uses Drawable:
    var x: Int
    init(x: Int):
        self.x = x

# (2) Conformance failure: draw() has the wrong signature (Int, trait wants String).
class Wrong uses Drawable:
    fn draw(self) -> Int:
        return 1

# A perfectly good class that simply does NOT implement Drawable.
class Plain:
    var v: Int
    init(v: Int):
        self.v = v

fn render<T: Drawable>(x: T) -> String:
    return x.draw()

fn show(d: Drawable) -> String:
    return d.draw()

fn main():
    var p = Plain(1)
    print(render(p))     # (3) bound: Plain does not satisfy Drawable
    print(show(p))       # (4) argument: Plain not assignable to Drawable
