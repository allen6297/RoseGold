module ext_demo

trait Ordered:
    func compareTo(self, other: Self) -> Int
    func lessThan(self, other: Self) -> Bool:      # default, in terms of compareTo
        return self.compareTo(other) < 0

# Retroactive conformance: make the BUILT-IN types satisfy Ordered after the fact.
extend Int uses Ordered:
    func compareTo(self, other: Int) -> Int:
        return self - other

extend String uses Ordered:
    func compareTo(self, other: String) -> Int:
        if self < other:
            return -1
        if self > other:
            return 1
        return 0

# A generic that now works for Int and String, because they conform via extensions.
func maxOf<T: Ordered>(a: T, b: T) -> T:
    if a.compareTo(b) > 0:
        return a
    return b

func main():
    print("max int     =", maxOf(3, 9))
    print("max string  =", maxOf("apple", "pear"))
    print("3 lt 9      =", 3.lessThan(9))          # inherited default, dispatched on a primitive
    print("compare     =", 10.compareTo(4))
