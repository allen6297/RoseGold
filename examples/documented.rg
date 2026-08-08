module documented

# ---------------------------------------------------------------------
#  Documentation comments (Javadoc-style). A `##***` line or a `#/*** ... /#`
#  block attaches to the declaration on the next line. `rosegoldc --doc` renders
#  a Markdown page from them; the language server shows them on hover.
#  (Ordinary `#` and `#/ ... /#` comments are NOT docs.)
# ---------------------------------------------------------------------

##*** Adds two numbers together.
##*** @param a the first addend
##*** @param b the second addend
##*** @return their sum
func add(a: Int, b: Int) -> Int:
    return a + b

#/***
  A counter that clamps at a maximum value.
  @param start the initial count
  @param cap the highest value it will reach
/#
class Counter:
    ##*** the current count
    var count: Int
    var cap: Int

    init(start: Int, cap: Int):
        self.count = start
        self.cap = cap

    ##*** Increment the counter, clamping at `cap`.
    ##*** @return the new count
    func bump(self) -> Int:
        if self.count < self.cap:
            self.count = self.count + 1
        return self.count

func main():
    print(add(2, 3))
    var c = Counter(0, 2)
    print(c.bump())
    print(c.bump())
    print(c.bump())
