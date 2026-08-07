module self_trait

# `Self` in a trait signature means "the conforming type". This makes a real
# Comparable-style trait expressible -- impossible before, when method params
# had to name a concrete type.
trait Ordered:
    func compareTo(self, other: Self) -> Int

class Money uses Ordered:
    var cents: Int
    init(cents: Int):
        self.cents = cents
    func compareTo(self, other: Money) -> Int:
        return self.cents - other.cents

class Version uses Ordered:
    var n: Int
    init(n: Int):
        self.n = n
    func compareTo(self, other: Version) -> Int:
        return self.n - other.n

# A generic bounded by Ordered: inside the body, `Self` on the bound resolves to
# T, so `a.compareTo(b)` type-checks (b must be a T) and the checker verifies it.
func maxOf<T: Ordered>(a: T, b: T) -> T:
    if a.compareTo(b) > 0:
        return a
    return b

func main():
    print("max money   =", maxOf(Money(500), Money(300)).cents)
    print("max version =", maxOf(Version(2), Version(7)).n)
