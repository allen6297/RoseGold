module trait_defaults

# A trait method may carry a default body (a "protocol extension"). Conformers
# must implement the abstract methods, but inherit the defaults for free -- and
# may override them. A default body can call the trait's other methods on self.
trait Greeter:
    fn name(self) -> String
    fn greet(self) -> String:
        return "Hello, " + self.name()

# Person implements only `name` and inherits the default `greet`.
class Person uses Greeter:
    var who: String
    init(who: String):
        self.who = who
    fn name(self) -> String:
        return self.who

# Robot implements `name` and OVERRIDES `greet`.
class Robot uses Greeter:
    fn name(self) -> String:
        return "robot"
    fn greet(self) -> String:
        return "BEEP BOOP " + self.name()

# The default is also visible through a trait bound.
fn announce<T: Greeter>(x: T) -> String:
    return x.greet()

fn main():
    print(announce(Person("Ada")))   # inherited default
    print(announce(Robot()))         # overridden
